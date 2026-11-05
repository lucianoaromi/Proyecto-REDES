# Prototipo experimental WiFiManager

Ubicacion: `C:\GitHub\Proyecto-REDES-WiFiManager`. Rama: `experiment/wifimanager`.
Base: `53d8c0f`, que contiene las mejoras TLS, JSON, interfaz y diagnosticos anteriores.
La carpeta original `C:\GitHub\Proyecto-REDES` no se modifica. El prototipo ya
se cargo manualmente en un ESP8266 para las primeras pruebas fisicas. No se han
realizado commits ni publicado cambios.

## Dependencias y compilacion

- NodeMCU 1.0 (ESP-12E Module), FQBN `esp8266:esp8266:nodemcuv2`.
- ESP8266 Core **3.1.2**.
- ArduinoJson **6.21.6**.
- WiFiManager de tzapu **2.0.17**.
- Bibliotecas existentes: DHT sensor library 1.4.6 y Adafruit Unified Sensor 1.1.15.
- ESP8266WebServer y DNSServer vienen con el core; no instalar variantes async.

Las bibliotecas instaladas no se modificaron. Se utilizaron los mismos valores
predeterminados del FQBN en las dos compilaciones, con Arduino CLI 1.3.1.

```text
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 ESP8266_DHT22
```

En Arduino IDE seleccionar las versiones indicadas en los gestores de placas y
bibliotecas, abrir `ESP8266_DHT22/ESP8266_DHT22.ino` y compilar. Mantener desactivada
la depuracion interna del core que pudiera imprimir solicitudes/credenciales.

## Archivos del experimento

- `ESP8266_DHT22/wifi_provisioning.h`: estados, portal, persistencia y registros.
- `ESP8266_DHT22/ESP8266_DHT22.ino`: inicio Wi-Fi no bloqueante, arranque diferido
  de HTTP/NTP y exclusiones de TLS/HTTP mientras se configura.
- `ESP8266_DHT22/connection_diagnostics.h`: memoria durante y despues de HTTPS.
- `ESP8266_DHT22/secrets.example.h`: campo de clave del AP, vacio por defecto.
- `tests/wifi_provisioning_test.cpp` y `tests/wifi_stubs/`: pruebas simuladas.
- `tests/wifi_preservation_test.py`: comparacion con la base.
- Este documento.

`ESP8266_DHT22/secrets.h` se creo localmente con valores ficticios para compilar;
esta ignorado por Git. No se copiaron credenciales del proyecto original.

## Secuencia y plazos

1. Al arrancar se registra memoria, se activa STA y se carga la configuracion
   Wi-Fi guardada en flash por el SDK. Si no existe una configuracion guardada,
   intenta una unica vez `ssid` y `password` de `secrets.h`, sin registrarlos.
   Tras obtener IPv4, esa red se guarda en flash para los siguientes reinicios.
   Una red guardada desde el portal siempre tiene prioridad y nunca se reemplaza
   automaticamente por los valores de `secrets.h`.
2. Se esperan hasta **120 segundos** sin un bucle bloqueante. Una conexion con
   IPv4 habilita inmediatamente HTTP y NTP; no se espera el resto del plazo.
   Este margen contempla el arranque de 93 segundos observado anteriormente.
3. Si no hay conexion, se crea WiFiManager y se abre `MonitorAmbiental-Config`
   protegido por WPA2-Personal. Acceso directo: `http://192.168.4.1`.
4. El portal dura como maximo **300 segundos** desde el comienzo de su apertura.
   El plazo no se reinicia por visitas, escaneos ni clientes conectados. Se usa
   resta unsigned de `millis()` para tolerar el desbordamiento del contador.
5. La seleccion de red y escaneo usan WiFiManager. Su ruta `/wifisave` se intercepta
   antes del manejador original: recibe solo POST, inicia `WiFi.begin` con
   persistencia desactivada y vuelve al loop. Cada candidata tiene hasta **120
   segundos**, siempre dentro del limite global del portal. Mientras se prueba
   una red se rechazan nuevos envios con 409.
6. Solo tras `WL_CONNECTED` e IPv4 distinta de cero se llama a
   `wifi_station_set_config`, la misma API SDK empleada por el core. Una red que
   no conecta no reemplaza la configuracion persistida. No se utiliza el guardado
   normal de WiFiManager, porque escribe antes de confirmar y espera dentro de
   `connectWifi`, incluso con su portal no bloqueante.
7. Se cierra HTTP/DNS de WiFiManager, se destruye el objeto y se vuelve a STA.
   Solo despues se inicia el servidor habitual en TCP 80 y se configura NTP.
   TLS conserva sus requisitos originales de Wi-Fi, hora, CA y memoria.
8. Al vencer el portal o elegir Exit se restaura la red guardada. Si no existe,
   vuelve a intentar la red local de `secrets.h`. Se reintenta cada **30 segundos**,
   sin abrir el portal repetidamente. Si vuelve la red, se habilitan los servicios
   normales.
9. Tras haber funcionado normalmente, una desconexion queda a cargo de la
   reconexion original (automatica y reintento de 30 s). No abre otro portal.

Para cambiar nuevamente de red, reiniciar el dispositivo mientras la red guardada
este fuera de alcance o con su AP temporalmente apagado. Tras 120 s aparece el
portal. Si el portal ya vencio, reiniciar para volver a disponer de esa ventana.
No se agregan botones, comandos remotos ni endpoints al servidor habitual.

El modo no bloqueante mantiene las lecturas DHT22 y las tareas locales del loop.
La biblioteca todavia puede introducir pausas en escaneos y atencion HTTP; su
apertura contiene una espera de 500 ms y su cierre una de 1000 ms. El limite
absoluto se comprueba entre llamadas al servicio de la biblioteca: no es una
cota de tiempo real estricto ante una llamada interna que tarde en retornar.
Sin hora valida, las tareas que dependen de NTP conservan sus restricciones.

## Seguridad y convivencia de servicios

En `secrets.h`, configurar `WIFI_CONFIG_AP_PASSWORD` con una clave propia y fuerte
(de 12 a 63 caracteres ASCII imprimibles). La plantilla la deja vacia: si falta
una clave valida, el portal se rechaza y se continua intentando la red guardada.
La clave ficticia creada para compilar no es una clave recomendada para uso real.

El menu del portal ofrece configuracion Wi-Fi y salida. Un `RequestHandler`
registrado mediante `setWebServerCallback` ANTES de las rutas de la biblioteca
rechaza `/update`, `/u`, `/erase`, `/restart`, `/info`, `/param`, `/paramsave`,
`/status` y `/close`. Esto tambien impide que un upload multipart invoque OTA:
el core 3.1.2 selecciona primero un unico handler para la solicitud y su upload.
Ocultar el menu por si solo no seria suficiente. El guard tambien rechaza
solicitudes dirigidas a la IPv4 de STA: el portal atiende en la direccion del AP.
El codigo OTA de la biblioteca puede quedar enlazado, pero sus manejadores no
son accesibles mediante estas rutas.

`setShowPassword(false)` y `setDebugOutput(false)` evitan mostrar claves del
portal en paginas y trazas de WiFiManager. Los registros propios no imprimen
SSID, claves ni cuerpos de formularios. Las credenciales candidatas se conservan
transitoriamente en RAM y en la configuracion actual del SDK; los buffers propios
se limpian tras iniciar la conexion y al cerrar el portal. El portal usa HTTP
sobre la red WPA2 del AP; no es un servidor HTTPS. La persistencia del SDK no
proporciona almacenamiento cifrado frente a acceso fisico a la flash.

No se habilita HTTP normal hasta cerrar y destruir el portal; este solo se abre
antes de la primera puesta en servicio. Los guardas de `puedeIniciarTLS`, del
planificador externo y del loop impiden HTTPS y la atencion HTTP habitual durante
el portal. No cambian los certificados, `configurarTLS`, Telegram, WeatherAPI,
ArduinoJson ni sus intervalos. Se conserva el HTML principal en flash.

La configuracion SSID/clave sirve para redes personales compatibles con ESP8266
(2,4 GHz). No implementa WPA2-Enterprise ni el login de portales cautivos externos.
La validacion TLS conserva **35.000 bytes libres** y **17.000 de bloque contiguo**
como requisitos previos; si no se alcanzan, los servicios se aplazan.

## Memoria y resultados de compilacion anteriores al ajuste DHT

Ambas compilaciones finalizaron correctamente con credenciales ficticias. La
comparacion usa el commit de partida en una carpeta temporal y el prototipo real,
con los mismos parametros y versiones.

| Recurso | Base 53d8c0f | Prototipo | Diferencia |
|---|---:|---:|---:|
| Codigo en flash (IROM) | 477.056 B | 551.748 B | +74.692 B |
| RAM estatica | 33.536 B | 35.644 B | +2.108 B |
| Codigo en IRAM | 29.051 B | 29.091 B | +40 B |
| IRAM + cache reservada | 61.819 B | 61.859 B | +40 B |

Quedan 44.548 B respecto del limite de RAM estatica y 3.677 B de IRAM para codigo.
**La resta de RAM estatica no es el heap disponible en ejecucion.** El consumo
dinamico del portal, el mayor bloque recuperado y los picos de TLS permanecen
sin medir. Las cifras anteriores no justifican reducir los requisitos de TLS.

## Pruebas automaticas realizadas

- Compilacion NodeMCU del prototipo y de la base: correctas, sin conflictos de
  bibliotecas. Core 3.1.2, ArduinoJson 6.21.6 y WiFiManager 2.0.17 confirmados.
- **72 verificaciones** del controlador real `wifi_provisioning.h` con primitivas
  Wi-Fi/SDK/servidor/reloj simuladas. Cubren arranque de 93 s, IPv4, persistencia
  solo tras exito, prioridad de la red guardada, respaldo con `secrets.h`, clave
  incorrecta, error de escritura, salida, timeout absoluto, reconexion,
  desbordamiento de millis(), bloqueo de rutas OTA/upload y registros sin claves.
  El modelo reproduce la prioridad de handlers revisada en el core;
  no sustituye pruebas de HTTP, radio, flash ni heap en la placa.

- **24 funciones** originales comparadas sin diferencias, ademas de certificados,
  parser JSON y umbrales. El servidor y la interfaz solo admiten los cambios DHT
  descritos abajo; se conservan el diseno, las rutas y el formato de `/data`.
- Suite meteorologica existente: **234 casos aprobados**.
- Suite existente del endpoint: **8 respuestas aprobadas**.
- `secrets.h` no esta versionado y coincide con la exclusion de `.gitignore`.

## Diagnostico temporal del primer arranque y la reconexion

Las nuevas trazas `[WM +... ms]` registran `ARRANQUE_STA` (modo y estado del SDK),
`RESTAURAR_FLASH` (lectura y coincidencia de SSID/clave entre flash y RAM),
`BEGIN_GUARDADA_ESTADO_INMEDIATO`, eventos `STA_ASOCIADA`, `STA_DESCONECTADA
motivo=...`, `STA_IPV4_OBTENIDA` y `STA_DHCP_TIMEOUT`. Al vencer los 120 s se
registra `ESPERA_INICIAL_AGOTADA`. Tras cerrar el portal se registra
`DESPUES_CIERRE_PORTAL`; a los 30 s, `ANTES_REINTENTO`, `RECONNECT_ACEPTADO`
y `DESPUES_REINTENTO`. El valor inmediato de `begin()` y el booleano de
`reconnect()` indican que se inicio un intento, no que se conecto.

Al configurar una red, `FLASH_TRAS_GUARDAR` compara una lectura de flash con
la configuracion STA actual. Solo imprime resultados booleanos; no imprime
SSID, claves, BSSID, tokens ni cuerpos HTTP. La igualdad en el momento del
guardado no reemplaza la observacion tras reiniciar el dispositivo.

Para analizar el fallo, capturar el Monitor Serie desde el reset hasta la
reconexion posterior al portal. Si `RESTAURAR_FLASH` marca lectura/restauracion
o igualdad en cero, investigar ese paso. Si la restauracion es correcta pero no
aparece `STA_ASOCIADA`, comparar `STA_DESCONECTADA motivo` y el estado del SDK
entre el primer intento y el reintento. En Core 3.1.2, `sdk_estado` 2 indica
clave rechazada, 3 AP no encontrado, 4 fallo de conexion y 5 IPv4 obtenida;
los motivos 201, 202, 203 y 204 indican, respectivamente, AP no encontrado,
fallo de autenticacion, fallo de asociacion y timeout de negociacion. Si
aparece `STA_ASOCIADA` sin `STA_IPV4_OBTENIDA`, observar si aparece
`STA_DHCP_TIMEOUT`. Contrastar `modo` al inicio y antes del reintento permite
detectar una diferencia en la inicializacion del adaptador. Ninguno de estos
registros cambia los plazos o reintentos.

Las suites C++ se ejecutaron sin red con un compilador C++17 de escritorio. La
interfaz se verifico en un navegador local sin contactar servicios externos. Para
repetir las pruebas nuevas dejando la salida fuera del repositorio:

```text
c++ -std=c++17 -Wall -Wextra -Werror -I tests/wifi_stubs -I ESP8266_DHT22 tests/wifi_provisioning_test.cpp -o <temporal>/wifi_test
<temporal>/wifi_test
python tests/wifi_preservation_test.py
```

## Resultados de las primeras pruebas fisicas

En el ESP8266 real quedaron confirmados:

- apertura de `MonitorAmbiental-Config` y seleccion de otra red desde el portal;
- asociacion, obtencion de IPv4 y cierre correcto del portal;
- recuperacion de memoria observada despues de destruir WiFiManager;
- funcionamiento posterior del servidor HTTP local;
- comunicaciones de Telegram y WeatherAPI mediante HTTPS; y
- persistencia de la red elegida tras reiniciar, sin volver a ingresar credenciales.

Estas pruebas demuestran que el flujo principal y la persistencia funcionan en el
hardware utilizado. No prueban todavia todos los escenarios de error ni permiten
generalizar el resultado a otros puntos de acceso.

## Limitacion pendiente de investigacion

En algunos arranques se observa una demora considerable y variable entre la
asociacion Wi-Fi y la obtencion de IPv4. La causa no esta demostrada. Puede estar
en la negociacion DHCP, el estado del adaptador durante el arranque o una condicion
externa del punto de acceso, pero los resultados disponibles no permiten elegir
entre esas hipotesis. La persistencia no aparece como causa: la misma configuracion
guardada consiguio conectar posteriormente sin volver a usar el portal.

Para investigarlo se deben conservar capturas completas desde el reset y comparar
`STA_ASOCIADA`, `STA_IPV4_OBTENIDA`, `STA_DHCP_TIMEOUT`, `STA_DESCONECTADA motivo`,
`sdk_estado` y `modo` entre un arranque rapido y uno lento. Los diagnosticos no
modifican los plazos ni la logica de conexion.

Siguen pendientes las pruebas de clave incorrecta y error de escritura, cierre por
Exit, bloqueo fisico de rutas OTA, perdida breve de red, pausas de escaneo y cierre,
y estabilidad prolongada del heap y del mayor bloque contiguo. Tambien debe
repetirse la demora intermitente de IPv4 con capturas cronologicas comparables.

Guardar una captura cronologica del Monitor Serie que incluya:

| Momento | Registro |
|---|---|
| Inicio | `ARRANQUE` |
| Antes de crear WiFiManager | `ANTES_ABRIR` |
| Portal abierto y cada 15 s | `PORTAL_ACTIVO` |
| Tras destruir el portal | `DESPUES_CERRAR` |
| Antes de cada TLS | `MEMORIA antes_TLS` |
| Telegram con TLS activo | `MEMORIA conexion_TLS_activa` |
| WeatherAPI tras GET | `MEMORIA despues_GET` |
| Tras cada operacion y liberar sus objetos locales | `MEMORIA despues_operacion` |

Todos incluyen memoria libre/bloque mayor; los eventos de conexion y cierre
registran tiempos con millis(). Las muestras no capturan necesariamente el pico
instantaneo de una asignacion interna de WiFiManager o BearSSL.

La aprobacion definitiva queda pendiente de resolver o caracterizar la demora
intermitente de IPv4 y de completar los escenarios indicados. El traspaso de TCP
80, la persistencia tras reinicio y HTTPS posterior ya fueron observados en las
primeras pruebas. El prototipo permanece separado de la version estable.

## Respuesta del DHT22 y graficas (19/10/2026)

El intervalo nominal cambia de 10 s a 2,5 s y el filtro exponencial del firmware
de 0,2 a 0,5. La biblioteca Adafruit DHT 1.4.6 instalada conserva el resultado
durante 2000 ms (`DHT.cpp`, `MIN_INTERVAL` y `DHT::read`). Las diez iteraciones
con pausas de 10 ms reutilizaban la misma trama; ahora se solicita una pareja
temperatura/humedad por ciclo, sin forzar la lectura. Se eliminan esas pausas
(100 ms por ciclo). Si cualquiera de los valores es NaN, se conserva la ultima
pareja valida y no se actualizan filtro, extremos, historial ni alertas.

Las graficas de tiempo real usan directamente `d.temp` y `d.hum` de `/data`.
Se eliminan los buffers y el segundo EWMA de JavaScript; el polling continua
cada 2 s y se mantienen las escalas y el diseno. Es normal que dos consultas
consecutivas reciban la misma muestra porque el polling es mas frecuente que
la lectura del sensor.

El historial conserva el promedio de muestras validas por minuto (unas 24
en condiciones normales), los 60 puntos circulares y el CSV cada 5 minutos
calculado como promedio de los promedios por minuto. Se divide por el numero
real de muestras, sin asumir seis muestras por minuto ni multiplicar valores.
Las ventanas siguen cerrandose al llegar una lectura valida; un fallo del sensor
o una pausa del loop puede retrasarlas. Se conserva ese comportamiento previo.

La alerta conserva el umbral de 35 C y el intervalo de 5 minutos. Puede dispararse
antes: desde un valor filtrado de 30 C, una nueva lectura corregida de 40 C da
35 C con alpha 0,5 (antes daba 32 C). Tambien se atenúan menos las fluctuaciones
y pueden alcanzarse antes los extremos o el umbral por cambios breves.

Los 2500 ms superan el minimo de 2000 ms de la biblioteca, sin lecturas forzadas
ni rafagas para recuperar muestras atrasadas. El sensor mantiene su propia
inercia fisica y la biblioteca bloquea interrupciones brevemente al recibir la
trama. La estabilidad y el ruido deben verificarse en la placa.

HTTPS sigue siendo sincrono: `client.connect()`, `http.GET()` y la recepcion
ocupan el loop. Los timeouts de E/S de 5 s y las otras fases pueden superar el
intervalo del DHT; `delay(1)` cede CPU al sistema, pero no ejecuta de nuevo el
loop del sketch. Por tanto, esta mejora no garantiza muestras cada 2,5 s durante
una operacion externa. Se mantienen los diagnosticos de duracion existentes.

Verificaciones del ajuste:

- Compilacion correcta para NodeMCU con ESP8266 Core 3.1.2, ArduinoJson 6.21.6,
  DHT 1.4.6 y WiFiManager 2.0.17, en una copia temporal con credenciales de ejemplo.
  RAM estatica: 35.596 B; IRAM mas cache: 61.859 B; codigo en flash: 550.684 B.
  Persisten advertencias previas sobre SPIFFS obsoleto, formatos de fecha,
  comparaciones de signo y posibles valores meteorologicos sin inicializar.
- 36 comprobaciones nuevas de las funciones C++ reales de DHT, filtrado,
  historial y bloque de alertas, con sensor/reloj/CSV simulados: NaN, offsets,
  limites de humedad, escalones, promedio con muestras faltantes, cinco minutos
  de CSV, anillo de 60 puntos y una pausa simulada de 15 s.
- 72 comprobaciones Wi-Fi, 234 casos meteorologicos y las 8 respuestas originales
  del endpoint aprobadas. La prueba de preservacion admite solo los cambios
  DHT solicitados y mantiene sus comparaciones del resto del firmware.
- Prueba en navegador aprobada con escalones de temperatura y humedad que
  llegan directamente a ambas curvas, historicos intactos, polling de 2 s,
  estados meteorologicos, cuatro graficos, CSV y cinco anchos de pantalla.

Para repetir la prueba DHT con un compilador de escritorio (en Windows, `cl`
desde una consola de desarrollo MSVC; en otros entornos, `g++`):

```text
python tests/dht_history_test.py --compiler cl --output <directorio-temporal>
```

Antes de validar en hardware: cargar manualmente conservando SPIFFS y la
configuracion Wi-Fi, observar un cambio ambiental, contrastar tarjetas y curvas,
comprobar un promedio por minuto y un CSV a los cinco minutos, y comparar la
cadencia del DHT mientras Telegram/WeatherAPI realizan operaciones HTTPS.
Esta etapa no incluye carga automatica ni nuevas pruebas fisicas.

## Diagnostico temporal de HTTP y cadencia DHT (19/10/2026)

Observacion comunicada desde hardware: el DHT aparece cada 2-3 s sin la pagina
abierta y cada 6-7 s con ella abierta; navegador y Monitor Serie reflejan los
cambios casi simultaneamente. El archivo contiene mas de 5000 registros.
Estos datos son anteriores a las sondas de esta etapa; todavia no identifican
por si solos la operacion responsable.

La inspeccion confirma que `/csvinfo`, solicitado cada 5 s, llama siempre a
`obtenerFechasCSV()`. Esta funcion abre el CSV y recorre todas sus lineas para
obtener las fechas extremas. No carga el archivo entero a la vez, pero realiza
una lectura completa y crea/reutiliza Strings para las lineas y fechas.
Mientras dura esa solicitud, el sketch no vuelve al bloque de lectura DHT.
`/data` se solicita cada 2 s: no recorre SPIFFS, pero calcula las fechas lunares
y envia numerosos fragmentos mediante `WiFiClient::print`, incluidos hasta
60 puntos de temperatura y 60 de humedad. `/csv` recorre tambien el archivo
para construir su nombre y despues lo transmite en bloques de 512 bytes.

El servidor es sincrono. Espera hasta 500 ms por datos iniciales; posteriormente
`readStringUntil` puede esperar por el delimitador y las escrituras TCP pueden
esperar por espacio o por la red. El timeout inicial no limita la solicitud
completa. Los `yield`/`delay` internos atienden al sistema, no ejecutan otra vez
el bloque DHT del sketch. Una solicitud pendiente puede acumularse con las
siguientes porque el navegador conserva ambos intervalos de polling.
Las operaciones HTTPS siguen siendo otra fuente de pausas, ya instrumentada
por los diagnosticos previos. No se modifica ninguno de estos mecanismos.

### Registros nuevos

Las sondas estan aisladas en `ESP8266_DHT22/http_timing_diagnostics.h`.
`DIAGNOSTICO_HTTP_TIEMPOS` vale 1 por defecto; definirlo a 0 antes de incluir
el encabezado o como opcion de compilacion elimina las sondas. No cambia el
diagnostico Wi-Fi/TLS existente. Cada linea empieza con `[PERF +<millis> ms]`.

| Registro/campo | Significado |
|---|---|
| `DHT_INTENTO intervalo_ms` | Tiempo real entre entradas al bloque de lectura, incluidas las que resulten NaN. La primera indica `primero=1 intervalo_ms=0`. |
| `HTTP ruta=... inicio_ms total_ms` | Una linea al terminar cada solicitud, desde que `server.available()` devuelve un cliente hasta despues de `stop()`. |
| `espera_ms` | Espera inicial por datos; `sin_ruta` identifica una salida antes de reconocer la ruta. |
| `recepcion_ms` | Lectura de la linea de solicitud y las cabeceras. |
| `preparacion_ms` | Despacho y preparacion anteriores al primer envio; incluye el recorrido CSV en `/csvinfo`. |
| `respuesta_ms` | Generacion y escrituras de la respuesta; no es una medicion exclusiva del tiempo de red. En `/csv` incluye el recorrido para el nombre y la lectura para transmitir. |
| `cierre_ms` | Cierre del cliente y salida de la rama. |
| `lunas_ms` | Submedicion de `calcularLunas()` en `/data`, ya incluida en `respuesta_ms`; no sumarla nuevamente. |
| `CSV_FECHAS duracion_ms` | Duracion completa de `obtenerFechasCSV()`, incluyendo apertura y cierre. |
| `CSV_NOMBRE duracion_ms` | Duracion de `obtenerNombreCSV()` durante una descarga. |
| `abierto bytes_archivo lineas` | Apertura exitosa, tamano y numero de lineas recorridas, incluida la cabecera. |

Las cinco fases HTTP suman `total_ms`. Las mediciones CSV estan incluidas en
la fase HTTP correspondiente, no son tiempos adicionales. No se mide el tiempo
en cola anterior a `server.available()` ni la latencia completa del navegador.
Los contadores usan restas sin signo para tolerar el desbordamiento de millis().

No se registran cabeceras, parametros de URL, contenido CSV ni credenciales;
las etiquetas de ruta son constantes. Se emite una linea por solicitud, una
por recorrido y una por intento DHT, nunca una por iteracion del loop o por
registro CSV. El recorrido solo agrega un contador y una consulta de tamano.
Las sondas no crean Strings ni buffers proporcionales al historial.

La instrumentacion tiene un costo: Serial a 115200 baudios tarda aproximadamente
17 ms en transmitir 200 caracteres (8N1), aunque el bloqueo efectivo depende
del buffer. Cada temporizador captura su final antes de imprimir su propio
resumen; el total HTTP si incluye la impresion del resumen CSV interno.
El intervalo DHT incluye todas las pausas, tambien las de diagnostico. Por eso
no debe interpretarse esta version como una medicion sin perturbacion.

### Prueba fisica pendiente

1. Cargar manualmente el firmware conservando SPIFFS y la configuracion Wi-Fi.
   Capturar el Monitor Serie a 115200 baudios con la pagina cerrada durante
   al menos un minuto, una vez completadas las comunicaciones iniciales.
2. Abrir una sola pestana y registrar dos minutos sin descargar ni borrar CSV.
   Cerrar la pagina y registrar otro minuto. Mantener el mismo historial.
3. Buscar los `DHT_INTENTO` de 6000-7000 ms y las operaciones que ocurrieron
   entre ambos intentos. Comparar `HTTP /data` y `HTTP /csvinfo`, sus fases,
   `CSV_FECHAS` y los diagnosticos HTTPS existentes.
4. Si domina `CSV_FECHAS` y coincide con `preparacion_ms` de `/csvinfo`, queda
   localizado el costo del recorrido. Si domina `respuesta_ms` de `/data` con
   `lunas_ms` pequeno, investigar generacion/escrituras TCP; ese dato por si
   solo no distingue cada escritura. Si domina `recepcion_ms` o `cierre_ms`,
   investigar la recepcion de cabeceras o el cierre TCP respectivamente.
5. Opcionalmente descargar una vez el CSV para medir `CSV_NOMBRE` y `/csv`;
   no usar el boton de borrado. La descarga es un escenario distinto del
   polling normal y debe identificarse separadamente.

Optimizaciones posibles, NO implementadas: si el recorrido resulta dominante,
mantener en RAM las dos fechas extremas, inicializarlas una vez y actualizarlas
en los puntos existentes de escritura/rotacion/borrado. Si las escrituras TCP
dominan, evaluar agrupar fragmentos en un buffer pequeno y acotado. Si domina
la recepcion, revisar el reconocimiento del final de cabeceras y el limite
total de espera. Elegir solamente la correccion que justifiquen las mediciones;
no modificar intervalos, graficas ni mecanismos de conexion como parte de este
diagnostico.

### Verificacion automatizada de las sondas

`tests/http_timing_test.py` compila el encabezado real y los cuerpos reales de
`obtenerFechasCSV`, `obtenerNombreCSV` y `/csvinfo`, con SPIFFS/Serial/reloj/cliente
simulados. Se ejecuta con las sondas activadas y desactivadas. Verifica 5001
registros mas cabecera, respuesta HTTP exacta, un unico cierre, fechas, nombre,
archivo ausente, fallo de apertura, solo cabecera, intervalos DHT y rollover.
Los tiempos sinteticos de esa prueba verifican la contabilidad de las fases;
NO representan el rendimiento de SPIFFS ni TCP en el ESP8266.

```text
python tests/http_timing_test.py --compiler cl --output <directorio-temporal>
```

Se mantienen las 36 comprobaciones DHT/historial/alertas, 72 Wi-Fi, 234 JSON
meteorologico y 8 respuestas del endpoint. La prueba de navegador conserva
curvas directas, polling de 2 s, historicos, CSV y cinco anchos de pantalla.
La comparacion del firmware con la copia anterior al diagnostico es identica
al retirar solamente las sondas y su include. Los tests existentes de
preservacion y endpoint se adaptan para aceptar las sondas, sin cambiar sus
respuestas esperadas. No se ejecutaron solicitudes a servicios con secretos,
ni operaciones sobre el SPIFFS real.

Compilacion NodeMCU con ESP8266 Core 3.1.2, ArduinoJson 6.21.6, DHT 1.4.6 y
WiFiManager 2.0.17, en copia temporal con `secrets.example.h` como credenciales
ficticias: correcta con diagnostico activo. RAM estatica 35.604 B, IRAM mas cache
61.859 B y codigo en flash 552.036 B. Respecto de la compilacion anterior al
diagnostico: +8 B de RAM estatica, +1352 B de flash y sin aumento de IRAM.
La compilacion completa con `DIAGNOSTICO_HTTP_TIEMPOS=0` tambien pasa:
35.596 B de RAM estatica, 61.859 B de IRAM mas cache y 550.684 B de flash,
los mismos valores que antes de agregar las sondas.
Estos numeros no miden el maximo de pila ni el heap durante una conexion real.
Las sondas usan pequenos objetos automaticos y no solicitan memoria dinamica.
Persisten las advertencias anteriores de SPIFFS obsoleto, formatos de fecha,
comparaciones de signo y posibles valores meteorologicos sin inicializar.

## Fechas de CSV en memoria (19/10/2026)

Una prueba fisica posterior confirmo la causa de la cadencia DHT degradada:
con el navegador cerrado, los intervalos fueron de 2501 ms; con el navegador
abierto, de 6300 a 8000 ms. /data tardo unos 100-120 ms, mientras /csvinfo
tardo unos 6300-6400 ms y obtenerFechasCSV() unos 6240-6312 ms. El historial
tenia 5292 lineas y 153471 bytes. Esas mediciones corresponden al firmware
anterior a la cache, no al resultado de esta correccion.

El recorrido de iniciarLog() durante el arranque ya cuenta los registros;
ahora captura tambien la primera y la ultima fecha en dos buffers de 11 bytes
(diez caracteres y terminador). No se añade otra lectura completa. Cada
escritura CSV que informa exito actualiza la ultima fecha y, si el historial
estaba vacio, tambien la primera. Un fallo de apertura o escritura deja la
cache y el contador sin cambios. Al reiniciar el archivo por limite o mediante
el boton manual se limpian las fechas cuando se consigue borrar o truncar el
archivo. Si ambas operaciones fallan, se conservan los datos y la cache.

obtenerFechasCSV() solo copia esos buffers a las cadenas que /csvinfo ya
utilizaba; la respuesta sigue teniendo exactamente first y last, incluidas
cadenas vacias si no hay registros o no pudo leerse el archivo al arrancar.
La descarga /csv mantiene su nombre, contenido y recorrido propios; no se
modifican los datos existentes, ni el intervalo de almacenamiento ni el
reinicio al alcanzar el limite. El resto del servidor y la pagina permanecen
sin cambios. El diagnostico CSV_FECHAS sigue activo, ahora para medir el
acceso a la cache; las fases HTTP y DHT_INTENTO siguen disponibles.

La nueva prueba nativa tests/csv_date_cache_test.py compila las funciones
reales del firmware con SPIFFS simulado: 5292 lineas preexistentes, un solo
recorrido inicial, diez consultas sin lecturas, escritura correcta, fallos de
apertura y escritura, limite, fallo de reinicio, borrado manual, archivo nuevo,
solo cabecera, registro corto, errores al abrir/crear y descarga con nombre y
contenido. tests/http_timing_test.py verifica ademas el JSON exacto de
/csvinfo y las fases con el diagnostico activo e inactivo. Son pruebas
simuladas; no sustituyen la comprobacion en hardware.

Compilacion correcta para NodeMCU con ESP8266 Core 3.1.2, ArduinoJson 6.21.6,
DHT 1.4.6 y WiFiManager 2.0.17, en copia temporal con credenciales ficticias.
Con los diagnosticos activados usa 35.628 B de RAM estatica, 61.859 B de
IRAM mas cache y 551.956 B de flash. Frente al firmware con diagnosticos y
sin cache: +24 B de RAM estatica, sin cambio de IRAM y -80 B de flash. No se
midio el pico de heap/pila ni el tiempo real de /csvinfo tras este cambio.
Las advertencias previas del compilador siguen presentes.

Antes de evaluar el resultado, cargar manualmente el firmware sin borrar
SPIFFS. Comparar, con el mismo historial y una sola pestana, los nuevos
HTTP ruta=/csvinfo preparacion_ms, CSV_FECHAS duracion_ms y los intervalos
DHT_INTENTO. Verificar tambien las fechas mostradas al abrir la pagina, tras
un nuevo registro de cinco minutos y tras un reinicio de la placa. Se espera
que /csvinfo deje de aportar unos 6,3 s de bloqueo por lectura del archivo;
la cadencia final del DHT aun requiere esa medicion fisica.
