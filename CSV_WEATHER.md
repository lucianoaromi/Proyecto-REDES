# Historial local y meteorologico

## Formato y frecuencia

La descarga `/csv` utiliza seis columnas, separador coma, decimal con punto y
lineas CRLF (importar como CSV delimitado por coma en Excel):

```csv
FechaHora,TemperaturaLocal_C,HumedadLocal_pct,TemperaturaExterior_C,HumedadExterior_pct,FechaHoraWeatherAPI
01/11/2026 12:00:00,25.1,51,,,
01/11/2026 12:05:00,25.4,52,32.4,64,01/11/2026 12:01:00
```

Los datos locales conservan sus promedios e intervalos originales: DHT22 cada
2500 ms, promedio historico por minuto y CSV cada cinco minutos. Los exteriores
son la ultima muestra valida, no un promedio de cinco minutos. Se reutilizan
`tempApi`, `humApi` y `weatherLastSuccessTime`: no se consultan servicios para
escribir el CSV ni se cambia el intervalo meteorologico de diez minutos.
La fecha de WeatherAPI usa la misma hora local que `/data`.

Antes del primer exito, o si no hay valores finitos y fecha valida, los tres
campos exteriores quedan vacios. Cero es un dato valido. Si la siguiente consulta
falla, el codigo de WeatherAPI conserva los valores y fecha anteriores, que
pueden repetirse en varias filas; la fecha no se renueva al guardar el CSV.

## Historial anterior: alternativa sin migracion destructiva

- `/historial.csv` conserva sus bytes originales de tres columnas. No se copia,
  reescribe, elimina ni renombra durante la introduccion del formato nuevo.
- Las nuevas muestras se guardan en `/historial_clima.csv`, con seis columnas.
- `/csv` transmite una unica cabecera, las filas anteriores con tres campos
  vacios adicionales, y las filas nuevas. Esta conversion solo ocurre al descargar.
- La cabecera anterior reconocida es la que generaba el firmware:
  `FechaHora,Temperatura,Hum.edad`. Un formato desconocido o una fila incompleta
  no se reescriben automaticamente: se conserva el archivo y se informa error.
- Los archivos vacios sin registros se contemplan. Un archivo nuevo de cero bytes
  puede recibir su cabecera sin perder registros.
- `/csvinfo` conserva exactamente `first` y `last`, obtenidos de la cache combinada.
  El arranque recorre cada archivo una vez; el nombre de descarga usa esa cache.
  No se recorre el historial en las consultas periodicas de la pagina.

No se realiza una migracion en flash: no puede garantizarse una reescritura de
un archivo grande ante falta de espacio o corte de alimentacion. Esta alternativa
mantiene ambos formatos validos sin necesitar una segunda copia del historial.
Solo el borrado solicitado explicitamente desde Clear elimina tambien el legado.

## Limite, espacio y fallos

El limite original de 8640 registros se aplica al archivo **nuevo**. Al alcanzarlo
se reinicia ese archivo, como la politica anterior, conservando el archivo legado.
Por tanto la descarga puede incluir mas de 8640 filas sumando el legado; este se
conserva hasta el borrado explicito. No se prometen treinta dias si no hay espacio.
Una fila habitual con clima y fecha ocupa unos 57 bytes (492480 bytes para 8640
filas, mas cabecera y sobrecarga SPIFFS). Se suma el espacio del archivo anterior.
El tamano real depende de los valores y del sistema de archivos.

Antes de escribir se consulta `SPIFFS.info()` y se reserva margen de un bloque
ademas de la fila. No se eliminan datos para obtener espacio: si no hay margen,
se omite ese guardado y se vuelve a intentar en el siguiente intervalo. Las
mediciones, graficas, comunicaciones y alertas siguen su logica anterior.

La rotacion prepara y verifica `/historial_clima.tmp`, renombra el archivo lleno
a `/historial_clima.bak`, instala el temporal y luego retira el respaldo. El arranque
restaura el respaldo si falta el archivo activo o completa la limpieza si el
reemplazo tiene cabecera valida. Ante un estado ambiguo conserva ambos archivos
y suspende el CSV. El legado queda fuera de esas operaciones.
Los archivos auxiliares son internos de SPIFFS; no se generan en el repositorio.

Se comprueban el numero exacto de bytes escritos y los errores de escritura/flush.
Una escritura parcial suspende nuevos append para no concatenar filas rotas.
El arranque tambien detecta filas truncadas. En ese estado `/csv` devuelve HTTP 500
en lugar de entregar el archivo como integro; se conservan los archivos para
recuperacion. Un error de red o lectura posterior a enviar las cabeceras HTTP
puede cortar una descarga; se registra un diagnostico y hay que repetir/verificar
la descarga. SPIFFS no ofrece una garantia absoluta ante corrupcion fisica o
cortes arbitrarios: las recuperaciones anteriores se verifican por simulacion,
no por pruebas electricas de corte en esta etapa.

Clear elimina los dos historiales y auxiliares y crea la cabecera nueva. Si alguna
operacion falla responde HTTP 500 y la interfaz muestra error, sin modificar el
diseno. El borrado de varios archivos no es una transaccion atomica: un error
puede dejar parte de los archivos sin borrar; la cache se reconstruye con lo que
exista. No se ha ejecutado Clear contra un dispositivo real durante este trabajo.

## Recursos y pruebas

No hay bibliotecas nuevas. Se usan buffers acotados: fila de 160 bytes y salida
de descarga de 512 bytes, sin cargar el archivo entero en RAM. Se conservan los
diagnosticos de memoria existentes; el heap y el peor caso de pila con Wi-Fi/TLS
requieren mediciones fisicas. El recorrido de arranque y la descarga siguen siendo
sincronos; el primero cede al SDK por fila, pero puede aumentar el tiempo de arranque
si ambos archivos son grandes. No hay recorridos nuevos en `/data` ni `/csvinfo`.

Las pruebas de `csv_date_cache_test.py` ejecutan las funciones y rutas C++ reales
con SPIFFS simulado: preservacion byte a byte de 5292 lineas antiguas, nuevas
columnas, vacios, ceros, clima repetido con fecha fija, cache, descarga con escrituras
parciales de red, errores, espacio, rotacion y estados interrumpidos. La suite del
navegador comprueba la descarga de seis columnas y el error de Clear con datos
ficticios; no contacta WeatherAPI ni Telegram.

## Verificacion en ESP8266 pendiente

1. Descargar una copia del CSV actual y registrar sus fechas y cantidad de filas.
2. Cargar solo el sketch manualmente conservando SPIFFS y su particion; no usar
   borrado completo de flash ni cargar una imagen de datos. No se carga firmware
   automaticamente desde esta tarea.
3. Verificar el mensaje `[CSV]` de arranque, `/csvinfo` y descarga: las filas previas
   deben mantener fecha y valores locales y mostrar vacios en las columnas nuevas.
4. Esperar registros de cinco minutos y comparar los valores exteriores y su fecha
   con `/data`; entre consultas de diez minutos la fecha puede repetirse.
5. En una prueba controlada, verificar campos vacios antes del primer exito y valores
   retenidos ante un fallo posterior, sin cambiar credenciales ni imprimir secretos.
6. Reiniciar y comprobar continuidad, fechas, cabeceras y conteos. Medir duracion
   de arranque/descarga y heap/mayor bloque antes de TLS con ambos archivos grandes.
7. Verificar LEDs, contador, DHT22, graficas, WiFiManager, NTP, Telegram y WeatherAPI.
8. Solo despues de respaldar los datos y autorizarlo explicitamente en la placa,
   probar Clear: debe vaciar ambos historiales y crear el de seis columnas. Las
   pruebas de espacio lleno, fallos y rotacion se han hecho en simulacion para
   evitar destruir el historial real.

## Resultado final de esta etapa

Compilacion correcta para `esp8266:esp8266:nodemcuv2`, ESP8266 Core 3.1.2 y
ArduinoJson 6.21.6, en una copia temporal con credenciales ficticias.

| Recurso | Antes | Despues | Incremento |
| --- | ---: | ---: | ---: |
| RAM estatica | 36008 B | 36536 B | 528 B |
| Flash | 561732 B | 563932 B | 2200 B |
| IRAM y cache | 61883 B | 61883 B | 0 B |

Pruebas satisfactorias: 40 de CSV/clima/SPIFFS, diagnosticos HTTP activos e
inactivos, 12 respuestas reales del bloque `/data`, 36 de DHT/historial/alertas,
55 de lluvia/PWM/Ticker, 114 de LEDs/HTTP, contador IPv4, 72 de WiFiManager,
234 del parser JSON y comparacion de 21 funciones/certificados preservados.
La interfaz paso las comprobaciones en cinco anchos, descarga de seis columnas,
filas antiguas y nuevas y manejo del error HTTP de Clear.
Como en la etapa anterior, Edge quedo detenido en la limpieza posterior al mensaje
PASS; sus procesos de prueba se cerraron al finalizar. Las aserciones pasaron,
pero no se presenta ese cierre forzado como una salida normal del runner.
No se probo hardware, corte electrico real, heap en ejecucion ni servicios con
credenciales reales. No se modifico `visualizacion-redes/`, ni se crearon commits,
push o cargas de firmware.
