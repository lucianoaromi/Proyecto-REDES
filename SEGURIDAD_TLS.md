# Comunicaciones externas con TLS

## Alcance

El firmware conserva las credenciales y los nombres de variables de `secrets.h`.
Ese archivo no se incluye en Git. Los certificados de `tls_certificates.h` son
publicos y no contienen claves privadas ni credenciales.

- WeatherAPI usa HTTPS, TLS 1.2 y validacion de cadena, nombre del servidor y
  vigencia del certificado. Se mantienen ciudad, parametros y umbrales
  meteorologicos. El procesamiento estructurado se describe mas abajo.
  No se siguen redirecciones ni existe retroceso a HTTP.
- Telegram usa TLS 1.2 con validacion de certificado; se elimina `setInsecure()`.
  Se conservan el aviso de inicio y las alertas de temperatura. La confirmacion
  sigue basandose en el estado HTTP 200, sin interpretar el JSON de Telegram.
- No se registran las URL con credenciales, el cuerpo meteorologico completo ni
  la linea de respuesta remota de Telegram. Compilar con la depuracion del core
  desactivada: los diagnosticos internos de bibliotecas pueden incluir URL.
- La pagina HTML/CSS/JavaScript conserva exactamente su contenido. Se envia
  desde flash con `F()` para liberar RAM. Las rutas locales, el borrado del CSV,
  el sensor y los algoritmos de almacenamiento no se modifican.

## Reloj y disponibilidad

El callback `settimeofday_cb` del core marca la recepcion de una actualizacion
SNTP. Antes de cada conexion se exige esa actualizacion, una fecha a partir del
01/01/2024 y conectividad Wi-Fi. Es un control de plausibilidad, no autenticacion
de NTP. Se mantienen los servidores horarios y UTC-3 existentes.

La conexion Wi-Fi inicial espera como maximo 15 segundos. La sincronizacion NTP
no bloquea `setup()` ni `loop()`: mientras falte hora valida, las comunicaciones
externas se posponen y continúan las mediciones y el CSV. Se habilita la
reconexion Wi-Fi y se reintenta cada 30 segundos si sigue desconectado.

El aviso de inicio y la ultima alerta pendiente se conservan en RAM para
reintentar el envio con un intervalo minimo de 30 segundos. No es una cola
persistente: un reinicio pierde el aviso pendiente y una alerta posterior puede
reemplazar a la anterior. Se conserva el intervalo de cinco minutos entre
generaciones de alertas. La primera consulta meteorologica se reintenta cada
30 segundos hasta obtener datos; despues se conserva el intervalo de diez
minutos y la condicion de no consultar durante la ventana de lluvia.

Las conexiones externas se inician solo cuando el servidor no tiene un cliente
pendiente, y se ejecuta como maximo una por iteracion. Son sincronicas: un cliente
que llegue durante una conexion TLS puede esperar. Los limites de E/S de la
aplicacion son de 5 segundos y la lectura meteorologica mantiene un limite total
de 3 segundos, sin un corte por longitud del JSON. DNS, TCP y el handshake tienen los limites internos
del core; en 3.1.2 el contexto TLS utiliza un timeout interno de 15 segundos.
Los limites por operacion no equivalen a un plazo global de cinco segundos.

## Memoria y certificados

Las CA se guardan en `PROGMEM` y se analiza solo la correspondiente al servicio
utilizado. Su objeto `X509List` vive hasta despues del cierre y destruccion del
cliente. No se mantienen dos conexiones TLS simultaneas.

Se usan buffers RX de 16384 y TX de 512 bytes, evitando asumir soporte MFLN del
servidor. Antes de crear TLS se exigen al menos 35000 bytes de heap libre y un
bloque contiguo de 17000 bytes. Estos umbrales son preventivos, no una garantia
de memoria disponible durante toda la negociacion. El JSON se procesa con
documentos de capacidad fija, sin reservar el cuerpo completo en el heap.

Fuentes oficiales y SHA-256 de los certificados DER:

| Servicio | CA raiz | Fuente | SHA-256 |
| --- | --- | --- | --- |
| Telegram | Go Daddy Root Certificate Authority - G2 | https://certs.godaddy.com/repository/gdroot-g2.crt.pem | `45140b3247eb9cc8c5b4f0d7b53091f73292089e6e5a63e2749dd3aca9198eda` |
| WeatherAPI | ISRG Root X1 | https://letsencrypt.org/certs/isrgrootx1.pem | `96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6` |

Las cadenas publicas observadas el 18/10/2026 terminaban en esas CA. Una rotacion
de intermediarios bajo la misma raiz normalmente no requiere cambios; un cambio
de raiz requiere actualizar el firmware. Ante errores de certificados se debe
revisar la cadena y el reloj, sin desactivar la validacion. El vencimiento de
ISRG Root X1 es 04/06/2035 y el de Go Daddy G2 es 31/12/2037.

## Verificacion y pruebas pendientes

La referencia de compatibilidad es ESP8266 Arduino Core 3.1.2, placa NodeMCU
`esp8266:esp8266:nodemcuv2`. La compilacion de verificacion utiliza una copia
temporal de `secrets.example.h` como `secrets.h`, nunca credenciales reales, y
no carga firmware al dispositivo.

Compilacion local completada el 18/10/2026 con Arduino CLI 1.3.1, configuracion
predeterminada de NodeMCU y credenciales de ejemplo:

| Recurso | Original | Version TLS |
| --- | ---: | ---: |
| RAM estatica | 60980 / 80192 bytes (76 %) | 33036 / 80192 bytes (41 %) |
| IRAM, incluida cache reservada | 61691 / 65536 bytes (94 %) | 61691 / 65536 bytes (94 %) |
| Codigo en flash | 412060 bytes (39 %) | 456368 bytes (43 %) |

Ambas compilaciones terminaron sin errores. Se mantienen las advertencias
preexistentes de obsolescencia de SPIFFS; no se migro el almacenamiento.
La reduccion de RAM estatica es de 27944 bytes. Estas cifras no miden el pico
de heap en una conexion TLS real ni garantizan su estabilidad en hardware.

Para la etapa TLS se compararon contra el original el contenido completo del HTML,
las rutas HTTP, la lectura del DHT22, el almacenamiento y el procesamiento meteorologico.
Los handshakes publicos de OpenSSL desde la PC verifican las CA y los nombres,
incluyendo rechazo de CA equivocada y nombre incorrecto. No envian HTTP,
tokens ni claves, y no prueban el funcionamiento de BearSSL en el ESP8266.
Las comparaciones estaticas y los cuatro casos de OpenSSL finalizaron
correctamente; `git diff --check` no detecto errores de formato.

Antes de dar por validada la operacion real, probar en el dispositivo:

1. Arranque normal, sin Wi-Fi y con NTP bloqueado; el sensor y CSV deben continuar.
2. Recuperacion de Wi-Fi/NTP y envio del aviso pendiente; consulta meteorologica
   y alertas con las nuevas credenciales cuando el propietario las configure.
3. Rechazo de certificado no confiable, nombre incorrecto y reloj no valido.
4. Heap libre y bloque maximo antes, durante y despues de conexiones repetidas;
   respuesta web simultanea, ausencia de watchdog y recuperacion ante fallos.
5. Temperatura/humedad, ambos LEDs, descarga e informacion del CSV y apariencia
   de la pagina. El borrado conserva su comportamiento previo.

El propietario reporto HTTP 200 de Telegram y WeatherAPI y funcionamiento web
en hardware con la version TLS anterior a la correccion JSON. Las pruebas de
esta nueva recepcion JSON en el ESP8266 fisico siguen pendientes.

## Recepcion JSON de WeatherAPI

Dependencia nueva: **ArduinoJson 6.21.6**, de Benoit Blanchon, version de la rama
6 con memoria fija. En Arduino IDE: **Herramientas > Administrar bibliotecas**,
buscar **ArduinoJson**, seleccionar **6.21.6** e instalar. No instalar la version
7 para esta implementacion. El encabezado comprueba la version minima 6.21.6
y rechaza otras versiones mayores; versiones posteriores de la rama 6 no estan
probadas aqui. Referencias: https://arduinojson.org/v6/revisions/ y
https://arduinojson.org/v6/api/json/deserializejson/ .

`weather_json.h` filtra los siguientes campos directamente desde el flujo HTTPS:

- `current.temp_c`, `current.precip_mm` y `current.humidity` (entero de 0 a 100).
- `forecast.forecastday[0].day.daily_will_it_rain`.
- `forecast.forecastday[0].day.daily_chance_of_rain`.
- `forecast.forecastday[0].day.totalprecip_mm`.

La consulta conserva `days=1`. El filtro de arreglos de ArduinoJson se aplica a
todos sus elementos; se utiliza explicitamente el primer dia de la respuesta.
El pronostico horario y los demas campos se recorren pero no se almacenan.
Esto reduce RAM, no el volumen descargado. El filtro y documento son
`StaticJsonDocument`; una respuesta que exceda su capacidad se rechaza.

Se exige presencia y tipo numerico de todos los campos, tipo entero para
humedad/bandera/probabilidad, numeros finitos, humedad entre 0 y 100, precipitaciones no negativas, probabilidad
entre 0 y 100 y bandera 0 o 1. No se convierten cadenas o null a cero. Los ceros
numericos validos se aceptan. Los estados meteorologicos y los LEDs solo se
actualizan despues de validar el conjunto completo; cualquier error conserva
los valores anteriores.

El lector impone un plazo total de 3000 ms para el cuerpo, incluso ante goteo
de bytes, sin un limite artificial de 8192 bytes. Tras el objeto JSON solo se
acepta espacio JSON. Si existe `Content-Length`, se exige recibir esa cantidad;
si no existe, se exige el cierre de la conexion dentro del plazo. Se conserva
HTTP/1.0 y cierre de conexion: no se agrega soporte de compresion ni chunked;
una respuesta incompatible se descarta. El plazo del cuerpo no incluye DNS,
negociacion TLS ni recepcion de cabeceras.

Los diagnosticos muestran solo cantidad de bytes y un estado local:
0=correcto, 1=JSON invalido/incompleto o sin memoria, 2=campos invalidos,
3=cuerpo HTTP incompleto/datos posteriores, 4=plazo agotado. No muestran
credenciales ni el cuerpo remoto.

`tests/weather_json_test.cpp` ejecuta el mismo lector y parser sin red, con un
cliente y reloj simulados. Para compilarlo en una PC con un compilador C++11:

```text
c++ -std=c++11 -Wall -Wextra -Werror -I <ArduinoJson-6.21.6>/src -I ESP8266_DHT22 tests/weather_json_test.cpp -o weather_json_test
```

Las pruebas cubren cinco valores exactos, ceros, tipos/rangos invalidos,
campos ausentes, prefijos truncados, JSON mayor a 24 KiB, orden distinto y
claves horarias homonimas, longitudes HTTP, conexion sin cierre, goteo y
desbordamiento de `millis()`. En los errores se comprueba que la salida conserva
el ultimo conjunto valido. No requieren credenciales ni servicios externos.

Resultado local: **210 casos aprobados**, compilando C++11 con Zig 0.13.0
(Clang incluido) en Windows. La compilacion final del firmware tambien termino sin errores con
Arduino CLI 1.3.1, NodeMCU, ESP8266 Core 3.1.2 y ArduinoJson 6.21.6; mantuvo las
17 advertencias previas de SPIFFS. Solo se utilizaron credenciales de ejemplo
en una copia temporal, sin cargar firmware ni consultar servicios externos.

| Recurso | TLS anterior | TLS con JSON filtrado |
| --- | ---: | ---: |
| RAM estatica | 33036 bytes (41 %) | 32776 bytes (40 %) |
| IRAM, incluida cache reservada | 61691 bytes (94 %) | 61819 bytes (94 %) |
| Codigo en flash | 456368 bytes (43 %) | 461968 bytes (44 %) |

El compilador Xtensa midio 160 bytes de capacidad del filtro y 416 bytes del
documento; sus objetos `StaticJsonDocument` completos ocupan 200 y 456 bytes,
respectivamente: **656 bytes automaticos en total**. Se elimina la reserva de
8192 bytes mas terminador del `String` anterior. Estos datos no incluyen el
resto de variables locales, la pila del parser ni los recursos TLS, y no son
una medicion del pico de memoria en hardware. La lectura cede CPU cada 256
bytes y mientras espera datos, dentro del mismo plazo total.

La comparacion del codigo contra el inicio de esta correccion confirmo que
Telegram, validacion TLS, servidor y pagina, DHT22, CSV, planificador y
credenciales permanecen intactos. Solo cambia WeatherAPI, sus nuevos helpers
y un comentario de memoria. Los umbrales siguen siendo: temperatura >=35 C,
lluvia actual >0.85 mm, y pronostico con bandera 1 mas probabilidad >=75 % o
precipitacion diaria >=0.4 mm.

Pendiente en hardware: repetir consultas reales, comparar los cinco valores,
medir heap/stack y latencia web con respuestas grandes y fallos de red. No se
ha ejecutado esta nueva recepcion en un ESP8266 durante esta verificacion.

Actualizacion posterior de interfaz: se agrega humedad exterior como sexto
campo obligatorio. Los resultados de compilacion, memoria y pruebas de esa
ampliacion se documentan en `INTERFAZ_WEB.md`; las cifras anteriores corresponden
a la etapa de cinco campos. El propietario informo que esa etapa ya funciona
en hardware; esta ampliacion visual aun requiere su propia comprobacion fisica.
