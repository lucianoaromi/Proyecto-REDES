# Clientes HTTP activos

La cabecera muestra las IPv4 distintas que enviaron un `GET /data HTTP/1.0`
o `HTTP/1.1` valido durante los ultimos 30 segundos. No cuenta conexiones TCP,
clientes asociados al router ni identifica dispositivos fisicos de forma absoluta.
Varias pestanas o clientes detras de una misma IP comparten registro. Solo `/data`
renueva la actividad; otras rutas no lo hacen. La solicitud actual cuenta antes
de generar el JSON. Al reiniciar se pierde la tabla.

`http_active_clients.h` mantiene ocho pares IPv4/millis en un arreglo fijo,
sin Strings, sockets permanentes, asignaciones dinamicas ni datos en SPIFFS.
La caducidad usa resta sin signo de 32 bits, valida al cruzar el desbordamiento
de millis() (los registros se revisan regularmente desde loop()). No hay nuevas
bibliotecas ni peticiones periodicas: se reutiliza `/data` cada dos segundos.

Nuevo campo aditivo: `"clients":{"active":2,"limited":false}`. Los otros campos
se conservan. `active` es el numero de registros vigentes. Si la tabla esta llena,
no se expulsa a clientes activos: se recuerda la ultima actividad no registrable
durante 30 s y `limited` pasa a true. La interfaz muestra `8+` (o el numero de
registros que siga vigente seguido de +). Durante ese intervalo es un limite
inferior conservador; puede seguir marcado aunque una IPv4 antes excluida entre
en un hueco libre. No se puede conocer el total exacto sin registrar esas IP.
Un error HTTP, de red o datos invalidos muestra "Clientes activos: sin conexion";
la proxima respuesta valida restaura el contador.

La estructura ocupa 72 bytes en una arquitectura con alineacion de 4 bytes
(64 de registros, 4 de tiempo de saturacion y booleano con padding). El codigo
hace recorridos acotados de ocho elementos; las nuevas operaciones no esperan
red ni acceden al historial. El campo agrega aproximadamente 39 bytes a cada
respuesta (~20 bytes/s por navegador a intervalos de 2 s, sin cabeceras TCP/IP).
El heap durante TLS solo puede medirse en hardware; no hay asignaciones dinamicas
propias del registro. La expiracion y la pantalla pueden demorarse mientras
operaciones sincronas existentes mantienen ocupado el loop().

## Verificacion fisica pendiente

1. Cargar manualmente el firmware, abrir la pagina y comprobar 1 cliente.
2. Abrir varias pestanas del mismo equipo: debe continuar en 1.
3. Abrir la pagina desde otro equipo con distinta IPv4: debe mostrar 2.
4. Cerrar todas las pestanas de un equipo; despues de 30 s de su ultima consulta,
   mas el tiempo hasta la siguiente respuesta, debe desaparecer del contador.
5. Interrumpir y recuperar la conexion: comprobar el mensaje y su recuperacion.
6. Con mas de ocho IPv4 activas comprobar el signo +, sin expulsar las anteriores.
7. Verificar LEDs manual/automatico, sensores, graficas, CSV, WiFiManager y HTTPS;
   comparar los diagnosticos de heap y mayor bloque antes de TLS con la base.

Las pruebas automatizadas usan IPv4, reloj, GPIO y red simulados: no verifican
asociacion Wi-Fi, consumo de heap TLS ni funcionamiento fisico del ESP8266.

## Resultados de compilacion y pruebas offline

Compiladas la base 2199a45 y la version actual para `esp8266:esp8266:nodemcuv2`,
ESP8266 Core 3.1.2, usando copias temporales con `secrets.example.h` como
`secrets.h`. Sin cargar firmware ni solicitar servicios con claves reales.

| Recurso | Antes | Despues | Diferencia |
| --- | ---: | ---: | ---: |
| RAM estatica | 35872 | 36008 | +136 bytes |
| Flash | 559912 | 561732 | +1820 bytes |
| IRAM y cache | 61883 | 61883 | 0 bytes |

El incremento de RAM incluye 72 bytes de BSS y 64 bytes de constantes; no es
una medicion del heap libre durante el funcionamiento.

- `http_active_clients_test.cpp`: IPv4 unicas, renovacion, caducidad a 30 s,
  saturacion sin expulsion, limite inferior, solicitudes validas y rollover;
  confirma `sizeof(HttpActiveClients) == 72` en el host.
- `weather_endpoint_test.py`: 12 respuestas del bloque C++ real de `/data`,
  con campos anteriores, dos IPv4, deduplicacion y caducidad.
- `web_interface_test.cjs`: estados 1, 2 y 8+, error HTTP y recuperacion,
  cinco anchos (320 a 1280 px), LEDs, graficas, CSV y polling original.
  La comprobacion de sincronizacion activa la pagina evaluada para evitar
  throttling del navegador en segundo plano.
- Regresiones: LEDs (114), lluvia/PWM/Ticker (55), DHT/historial/alertas (36),
  CSV/cache/descarga, diagnosticos HTTP activos e inactivos, WiFiManager (72),
  parser meteorologico (234) y preservacion de 21 funciones y certificados.
  Con Zig/Clang se omitio localmente la advertencia `missing-field-initializers`
  del fixture CSV preexistente; no se modificaron ese fixture ni el firmware.

Para ejecutar el nuevo test con un compilador C++ nativo (por ejemplo g++):

```sh
g++ -std=c++11 -Wall -Wextra -Werror -I ESP8266_DHT22 tests/http_active_clients_test.cpp -o <salida-temporal>
<salida-temporal>
```

El resto de scripts Python admite `--compiler` y `--output`; el test de navegador
usa Playwright/Edge ya disponible, con `PLAYWRIGHT_MODULE_PATH` y
`WEB_TEST_OUTPUT` si se instalo fuera del repositorio. No requiere bibliotecas
nuevas en Arduino IDE. Las pruebas no modifican el historial real de SPIFFS.

Nota del entorno de pruebas: las aserciones de interfaz de la ejecucion final
pasaron y se generaron las capturas. Edge quedo esperando durante el cierre del
browser en `finally`, por lo que se terminaron los procesos de prueba de esta
tarea. Una ejecucion previa tambien detecto un timeout en la pagina en segundo
plano; el test final incorpora `bringToFront()` antes de comprobarla. No son
pruebas del servidor ESP8266 ni evidencias de un fallo fisico del firmware.
