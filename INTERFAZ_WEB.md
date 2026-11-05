# Sensor local y clima exterior

La interfaz conserva fondo, tipografia, colores de temperatura/humedad, iconos
Material Symbols, estilos CSV y graficos Chart.js. En escritorio muestra dos
tarjetas equilibradas y una franja lunar inferior; hasta 680 px apila las
tarjetas. Los graficos tienen un contenedor responsive propio, sin modificar
sus datos ni algoritmos. El HTML sigue almacenado en flash mediante `F()`.

## Datos meteorologicos

Se agrega `current.humidity` a los filtros de ArduinoJson **6.21.6**, con
validacion estricta de entero entre 0 y 100. Permanecen los cinco campos
anteriores, umbrales, TLS, parametros e intervalos de consulta. Todos los datos
se publican juntos despues de una respuesta completa y valida.

Solo se amplía `GET /data`: conserva todos sus campos previos y agrega el objeto
`weather`:

```json
{"valid":true,"temp":32.4,"hum":64,"updated":"18/10/2026 05:10:00","stale":false}
```

`updated` corresponde al reloj del ESP8266 en UTC-3 al finalizar la ultima
consulta exitosa, no al refresco de pantalla ni al instante de observacion del
proveedor. No se modifica en consultas fallidas ni al solicitar `/data`.

Antes de la primera consulta valida, `valid` es `false`, las mediciones y fecha
son `null` y se muestra `--` / `Sin datos`. Los ceros validos se muestran como
ceros. Ante un fallo posterior se conserva el conjunto anterior y `stale` pasa
a `true`; tambien se marca anterior cuando su antiguedad supera el intervalo
meteorologico existente de diez minutos. La fecha sigue siendo la original.
Si falla la consulta local desde el navegador, se mantienen las cifras y se
indica `Sin conexion local`.

JavaScript solo consulta el servidor HTTP local. No se agregan solicitudes a
WeatherAPI, credenciales ni dependencias web. Los graficos siguen mostrando
exclusivamente las mediciones del DHT22; la luna se calcula con la logica local
existente. Telegram, TLS, CSV, borrado, lecturas e intervalos se conservan.

## Verificacion reproducible

- `tests/weather_json_test.cpp`: parser real con seis campos, humedad ausente,
  tipos y rangos invalidos, ceros, truncamientos y respuestas grandes; conserva
  la salida anterior ante errores.
- `tests/weather_endpoint_test.py`: extrae y compila el bloque `/data` y el
  serializador reales usando estado/Print simulados. Comprueba compatibilidad
  de campos, JSON valido, `null`, ceros, fecha fija, fallos y antiguedad.
- `tests/web_interface_test.cjs`: carga el HTML real en Edge headless contra un
  servidor de fixtures en localhost. Verifica estados, valores, luna, cuatro
  graficos, descarga/borrado CSV simulado y anchos 1280, 768, 680, 390 y 320 px.
  Comprueba ausencia de overflow horizontal, tarjetas equilibradas y graficos
  que aprovechen el ancho. Requiere Playwright solo en la PC de pruebas.

Ejemplos para las pruebas del endpoint y navegador:

```text
python tests/weather_endpoint_test.py --compiler c++ --output <carpeta-temporal>
python tests/weather_endpoint_test.py --compiler <zig-0.14.1> --output <carpeta-temporal> --wasi
node tests/web_interface_test.cjs
```

Para Playwright instalado fuera del repositorio, definir `PLAYWRIGHT_MODULE_PATH`
con la ruta del modulo y `WEB_TEST_OUTPUT` con una carpeta temporal de capturas.
La variante WASI ejecuta C++ en Node sin permisos de red ni directorios
compartidos. Ninguna prueba utiliza credenciales reales o modifica el CSV del
ESP8266. Las pruebas de navegador usan las fuentes/CDN existentes de la pagina.

## Resultados locales del 18/10/2026

- Compilacion final correcta con Arduino CLI 1.3.1, NodeMCU
  `esp8266:esp8266:nodemcuv2`, ESP8266 Core 3.1.2 y ArduinoJson 6.21.6. Se uso
  una copia temporal con credenciales de ejemplo, sin cargar firmware. Se
  mantienen las 17 advertencias preexistentes de obsolescencia de SPIFFS.
- **234 casos de parser aprobados**, ejecutando el mismo C++ en WASI de 32 bits
  con Zig 0.14.1 y Node 22.23.1. Incluyen los seis campos, humedad ausente,
  tipos incorrectos, -1/101/fracciones, limites 0/100, errores y truncamientos.
- **8 casos del endpoint real aprobados**, con su serializador C++ y estado
  simulado. Los campos anteriores no cambian; los nuevos producen JSON valido
  y mantienen valores/fecha tras un fallo o entre refrescos de pantalla.
- Prueba de navegador aprobada en Edge headless a los cinco anchos indicados,
  incluyendo estados sin datos/validos/anteriores/sin conexion, luna, cuatro
  graficos, botones CSV, ausencia de overflow y graficos con ancho util.
- Comparacion estatica aprobada para Telegram/TLS, DHT22, luna, almacenamiento,
  rutas CSV, planificador, intervalos y funciones de graficos. La huella de
  `secrets.h` no cambio y no se detectaron valores reales nuevos en los archivos
  publicables. No se hicieron commits ni push.

| Recurso | Antes del rediseño | Rediseño final |
| --- | ---: | ---: |
| RAM estatica | 32776 / 80192 bytes (40 %) | 33520 / 80192 bytes (41 %) |
| IRAM, incluida cache reservada | 61819 / 65536 bytes (94 %) | 61819 / 65536 bytes (94 %) |
| Codigo en flash | 461968 bytes (44 %) | 473108 bytes (45 %) |

El aumento de RAM estatica es **744 bytes**. El filtro y el documento JSON tienen
capacidades de 176 y 432 bytes; el compilador Xtensa midio sus objetos completos
en 216 y 472 bytes: **688 bytes automaticos**, 32 mas que antes. `WeatherReadings`
ocupa 24 bytes. No se reserva un cuerpo JSON completo en RAM. Estas cifras no
incluyen todos los temporales ni miden el pico de heap/stack durante TLS real.

Pendiente en hardware: contrastar temperatura/humedad exteriores y fecha de
exito con el monitor serie, provocar un fallo meteorologico para verificar
conservacion y estado anterior, medir heap/stack bajo TLS, y comprobar
interaccion desde telefonos reales y acceso al CSV del dispositivo.
