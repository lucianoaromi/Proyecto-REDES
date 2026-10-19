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

## Memoria y resultados de compilacion

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

- **25 funciones** originales comparadas sin diferencias, ademas de certificados,
  parser JSON, umbrales y servidor completo (HTML/CSS/JS y rutas CSV intactos).
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
