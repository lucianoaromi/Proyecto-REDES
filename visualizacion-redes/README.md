# Proyecto-REDES · Arquitectura en movimiento

Aplicación didáctica independiente para la exposición de Redes de Datos. Muestra
la arquitectura y el recorrido lógico de las comunicaciones del proyecto ESP8266.
**Los paquetes, tiempos, estados y valores ilustrados no son tráfico capturado ni
mediciones del dispositivo. La aplicación no se conecta a una placa real.**

## Ejecutar

1. Abrir **`index.html`** con Edge, Chrome o Firefox. Alcanza con hacer doble clic.
2. Seleccionar un escenario y pulsar **Iniciar recorrido**.
3. Usar **Red** / **Hardware** para alternar los diagramas. Seleccionar los
   dispositivos para leer su función en el inspector.
4. Para exponer, pulsar **Pantalla completa** en la cabecera. En pantallas de
   escritorio amplias concentra la interfaz en el diagrama, los controles y la
   ficha técnica. Volver a pulsarlo o usar Escape para salir.

No requiere instalación, compilación, cuenta, claves, servidor ni Internet.
Mantener `index.html`, `styles.css` y `script.js` en la misma carpeta. Se puede
copiar esta carpeta a otra computadora o a una memoria USB.

En celulares el diagrama conserva una escala legible y permite desplazamiento
horizontal dentro de su panel; los controles y el texto se adaptan al ancho.

Si se prefiere servir la carpeta por HTTP, ejecutar **dentro de esta carpeta**:

```powershell
python -m http.server 8000 --bind 127.0.0.1
```

Luego abrir `http://127.0.0.1:8000`. Esa dirección corresponde exclusivamente al
servidor de desarrollo de la computadora, no al ESP8266. Cerrar con Ctrl+C.

## Archivos

```text
visualizacion-redes/
  index.html                    Interfaz y diagramas SVG locales
  styles.css                    Diseño, adaptación y modo de proyección
  script.js                     Escenarios y motor de animación
  README.md                     Uso, análisis y limitaciones
  tests/
    visualization_test.cjs      Regresión de interfaz con Playwright/Edge
    packet_timing_test.cjs       Velocidad visible, secuencia y tiempo de lectura
```

HTML, CSS, JavaScript y SVG propios. Se usan fuentes del sistema. No hay paquetes,
CDN, imágenes externas, analítica ni librerías de ejecución. La política CSP
incluye `connect-src 'none'` para impedir conexiones desde la aplicación.
Playwright solo se usa en las pruebas opcionales, no en la aplicación.

## Interacción

- **Iniciar / pausar / continuar / repetir**, **Paso siguiente** y **Reiniciar**.
- Barra de etapas: seleccionar una etapa pausa y reconstruye su estado.
- Velocidad entre **0,5× y 2×**, sin alterar el orden lógico de las etapas.
- Computadora o teléfono como cliente de HTTP, LEDs y portal.
- LEDs: **Encender**, **Apagar** y **Auto**, con sus endpoints reales.
- Inspector de dispositivos mediante clic, Enter o Espacio.
- Atajos fuera de los controles: **Espacio** reproducir/pausar, **→** siguiente,
  **R** reiniciar. Las pestañas admiten flechas, Home y End.
- La reproducción se pausa si la página deja de estar visible.
- `prefers-reduced-motion` mantiene marcas estáticas y conserva los pasos.

Los pasos actualizan emisor, receptor, protocolo, capa TCP/IP, puerto de destino,
operación, mensaje conceptual y explicación. Se distinguen etapas verificadas
en código y fases de protocolo desarrolladas de forma didáctica.

## Velocidad de las animaciones

El motor centraliza sus parámetros en `TIMING` dentro de `script.js`.
La referencia a 1× es **40 píxeles CSS por segundo**, próxima al recorrido DHT
más lento anterior (aproximadamente 38 px/s en la vista de escritorio analizada).
Antes, una ventana fija de cada etapa producía velocidades muy diferentes según
la distancia: un trayecto TCP externo podía superar diez veces esa referencia.

Ahora cada paquete calcula:

```text
longitud visible = longitud del recorrido SVG × escala actual del diagrama
duración de desplazamiento = longitud visible / 40 px/s
velocidad efectiva = 40 px/s × selector global
```

Se incluyen los pequeños tránsitos internos entre los bordes de un dispositivo,
para evitar saltos cuando dos enlaces SVG no comparten un extremo. No se dibujan
conexiones nuevas ni se modifica la topología. Al redimensionar, cambiar de vista
o entrar en pantalla completa, se conserva la posición lógica y se actualiza
la duración para mantener la misma velocidad visible.

Los paquetes del mismo lote salen juntos (D6 y D7). Los lotes consecutivos,
como solicitud/respuesta o SYN/SYN-ACK/ACK, se separan 650 ms de tiempo de animación.
Hay 900 ms iniciales y 1200 ms finales por etapa con movimiento; estos márgenes
siguen el selector de velocidad. Los trayectos **no** tienen una duración fija.

El texto permanece hasta que terminan los recorridos y se cumple el tiempo de
lectura: **al menos 10 segundos reales**, ampliados según la longitud del título
y la explicación principal, a una referencia de 180 palabras/minuto. El selector
modifica el movimiento proporcionalmente (20, 40, 60 u 80 px/s) y no acorta ese
mínimo de lectura. Pausar congela ambos relojes. El avance manual permite pasar
inmediatamente a la etapa elegida y elimina sus partículas anteriores.

## Escenarios

| Recorrido | Contenido |
| --- | --- |
| Acceso al servidor | GET /, procesamiento, HTTP 200 HTML, polling GET /data y JSON |
| Control de LEDs | Orden PUT, GPIO D6 y D7, confirmación HTTP 200, manual ON/OFF y auto |
| WeatherAPI | Condiciones, DNS si hace falta, TCP, TLS 1.2, GET HTTP/1.0, JSON y lógica de LEDs |
| Telegram | Aviso pendiente, DNS, TCP, TLS 1.2, GET sendMessage y confirmación HTTP 200 |
| Conexión Wi-Fi | Red guardada, asociación, DHCP DORA ilustrativo e inicio de HTTP/SNTP |
| Portal WiFiManager | AP temporal, DNS cautivo, /wifi, POST /wifisave, 202, prueba y cierre |
| NTP | Servidores configurados, DNS, solicitud/respuesta SNTP y habilitación del reloj para TLS |
| Recorrido general | Síntesis del arranque, NTP, Telegram, WeatherAPI, DHT22, HTTP y orden manual ON |

La síntesis general omite fases repetidas y comprime las esperas; no es una
reproducción cronológica del loop. Conserva la prioridad inicial de Telegram
antes de WeatherAPI cuando ambos están pendientes. El DHT22 se lee durante el
loop, no únicamente después de WeatherAPI.

## Análisis del proyecto y trazabilidad

Código revisado en `C:\GitHub\Proyecto-REDES-WiFiManager`, rama
`experiment/wifimanager`, HEAD **`2199a45e9cae70ec16ba9b6ddf6f07f420097903`**.
La aplicación se añadió exclusivamente a `visualizacion-redes/`; no se necesitan
modificaciones del firmware, su HTML embebido ni WiFiManager.
No se utilizaron credenciales de `secrets.h`.

### Hardware verificado

| Elemento | Evidencia | Representación |
| --- | --- | --- |
| DHT22 | `ESP8266_DHT22.ino`: `SENSOR D5`, `DHTTYPE DHT22`, `leerDHT()` | Datos por D5/GPIO14; señal digital propia del DHT, sin IP |
| LED temperatura exterior | Sketch: `LED_API = D6`; `led_api_control.h`: `weather()` y `write()` | D6/GPIO12; HIGH/LOW; automático desde 35 °C de WeatherAPI |
| LED lluvia exterior | Sketch: `LED_RAIN = D7`, `cambiarControlLluvia()`, `actualizarPatronLluvia()` | D7/GPIO13; PWM manual 64/255, automático 15/255 |
| Lectura del sensor | `READ_INTERVAL_MS = 2500UL` | Intervalo nominal 2,5 s; HTTPS puede demorar la ejecución |
| Alimentación y resistencias | `Conexiones.jpg` y advertencias de `LED_CONTROL.md` | Se declara el trazado incompleto; no se dibuja un circuito supuesto |

En automático D7 queda fijo con lluvia actual. Con pronóstico realiza ráfagas de
60 s, alternando cada 500 ms y repitiendo cada cinco minutos. La visualización
explica ese patrón en el inspector, sin reproducirlo como señal eléctrica.
Los pines GPIO indicados corresponden al mapa NodeMCU, no a números de patas físicas.

`Conexiones.jpg` muestra el sensor y una indicación ambigua de resistencia
4,7/10 kΩ, sin detallar los dos LEDs. No permite afirmar el trazado completo,
el tipo de módulo sensor ni dimensionar las resistencias de los LEDs.
La vista Hardware es un **mapa funcional**, no una guía de montaje.

### HTTP habitual y control de salidas

El sketch usa **`WiFiServer server(80)`**, no un servidor HTTPS ni un WebSocket.
Lee la línea de solicitud, responde en HTTP/1.1 y cierra la conexión.
El HTML principal está embebido en flash. La interfaz usa polling `/data` cada
2 s y `/csvinfo` cada 5 s; también tiene `/csv` y `/clearcsv`.
Las rutas CSV no se ejecutan desde este explorador.

El controlador `LedApiControl::handleRequest()` y la interfaz real definen:

| Solicitud sin cuerpo | Resultado |
| --- | --- |
| `GET /led` | Consulta sin efectos |
| `PUT /led?mode=manual&on=1` | Ambas salidas encendidas |
| `PUT /led?mode=manual&on=0` | Ambas salidas apagadas |
| `PUT /led?mode=auto` | Restituye el automático independiente según último clima válido |

Respuesta correcta: 200 JSON y `Cache-Control: no-store`. El campo `on` significa
`d6 && d7`; no garantiza que las dos salidas sean iguales en automático.
Los ejemplos de auto usan un clima **ilustrativo** de 36 °C sin lluvia ni
pronóstico: D6 ON y D7 OFF. El escenario de apagado parte de manual ON como
estado inicial didáctico. No se realiza una lectura real de los LEDs.

La orden manual desarma los Ticker de lluvia antes de aplicar las salidas. Las
consultas meteorológicas continúan en manual, pero no modifican los GPIO.
El modo reside en RAM y vuelve a automático al reiniciar. El navegador original
solo confirma el switch tras una respuesta válida; una respuesta perdida puede
ocultar una orden que sí se ejecutó. La visualización muestra el caso exitoso.

### Comunicaciones externas

| Servicio | Fuente en el sketch | Protocolo y destino |
| --- | --- | --- |
| WeatherAPI | `actualizarClimaApi()`, `http.useHTTP10(true)`, `http.GET()` | GET HTTP/1.0 sobre TLS 1.2/TCP 443; `api.weatherapi.com/v1/forecast.json` |
| Telegram | `enviarTelegram()`, `client.connect("api.telegram.org", 443)` | GET HTTP/1.1 sendMessage sobre TLS 1.2/TCP 443 |
| Hora | `configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov")` | NTP/SNTP sobre UDP 123 del core; sin TCP ni TLS |
| Resolución | Conexiones por nombres de host del core | DNS si no hay caché; se ilustra el caso convencional UDP 53 |

WeatherAPI solicita Corrientes, Argentina, `lang=es`, `aqi=no` y `days=1`.
La URL real contiene una clave que se omite. El intervalo nominal tras el primer
éxito es de diez minutos y el parser valida datos antes de actualizar variables.
El ejemplo de 31 °C y lluvia actual es ficticio y está identificado en pantalla.

Telegram usa un GET, no un POST. El sketch genera el aviso de inicio y alertas
de temperatura local desde 35 °C, con una separación mínima de cinco minutos
entre generaciones. La confirmación se basa en HTTP 200; no interpreta JSON ni
verifica entrega/lectura. No implementa comandos del bot, webhook o getUpdates.
El teléfono de la topología se representa exclusivamente como navegador local.

`configurarTLS()` fija TLS 1.2 y CA, nombre y vigencia del certificado.
`puedeIniciarTLS()` exige Wi-Fi, servicios habilitados, portal cerrado, hora SNTP
recibida y plausible, al menos 35.000 bytes libres y un bloque de 17.000 bytes.
Las comunicaciones se ejecutan de una en una y son síncronas. El servidor HTTP
puede demorarse mientras una conexión HTTPS ocupa el loop.

El control de plausibilidad de hora no autentica NTP. No se representa NTS.

### WiFiManager

Fuente principal: `wifi_provisioning.h`, la integración en el sketch y
`WIFIMANAGER_EXPERIMENT.md`. La versión documentada es WiFiManager 2.0.17,
ESP8266 Core 3.1.2.

1. STA intenta la red guardada; solo sin ella usa el respaldo local.
2. Espera hasta 120 s sin bloquear el loop. Si obtiene IPv4, continúa antes.
3. Si falla y hay clave AP válida, abre `MonitorAmbiental-Config`, WPA2-Personal.
   El portal documentado está en **`http://192.168.4.1`** y atiende HTTP/DNS.
4. El portal está limitado a 300 s desde su apertura, sin reiniciar ese límite
   por actividad. La apertura automática depende del cliente; la URL directa
   permite prescindir del paso DNS cautivo.
5. `POST /wifisave` acepta la candidata y devuelve **202**, sin confirmar conexión.
   El intento tiene hasta 120 s dentro del límite global del portal.
6. Solo tras `WL_CONNECTED` e IPv4 válida se persiste la candidata en flash.
7. Se cierra HTTP/DNS del portal y se destruye WiFiManager. Después se habilitan
   el HTTP habitual en TCP 80 y SNTP. El cliente debe regresar a la LAN.

Timeout/Exit restauran los intentos de la red guardada. Tras el primer servicio
normal, las desconexiones no reabren el portal automáticamente. Estos errores
se describen, pero no tienen animaciones separadas.

La descripción antigua de espera inicial de 15 s en `SEGURIDAD_TLS.md` corresponde
a una base anterior: para esta rama se priorizó el código actual y el documento
WiFiManager, que establecen 120 s. Ningún servidor habitual se representa activo
al mismo tiempo que el portal de configuración.

## Convenciones y simplificaciones

- **Wi-Fi azul discontinuo**: enlaces locales asumidos para el escenario; el
  repositorio no demuestra el medio de conexión real de cada navegador.
- **GPIO ámbar**: conexiones funcionales de hardware, no tramas de red.
- **Violeta continuo**: recorrido lógico por Internet, sin número real de saltos.
- **Marca azul con etiqueta**: solicitud/envío; **verde con etiqueta**: respuesta.
- En la misma subred el AP **puentea** tramas; no se representa enrutamiento ni
  NAT entre cliente y ESP. En conexiones externas se usa un gateway. AP y gateway
  se agrupan en un nodo por claridad; NAT y ubicación física no están demostrados.
- No se inventan IP, máscara, gateway, DNS, puertos de origen, SSID privados,
  tokens ni claves API. La única IP del sistema mostrada es la documentada del portal.
- La ubicación del servidor DHCP y del resolver DNS de la red normal no está
  identificada. Por eso solo se anima su **tramo local hasta el AP y de regreso**;
  no se les asigna un nodo, una IP ni el papel de servidor al router.
- DHCP DORA es la secuencia convencional de ejemplo. No es una observación del
  intercambio real del SDK; renovaciones y estados previos pueden cambiarla.
- Las fases de TCP y TLS se agrupan. No se representan segmentos, fragmentación,
  ACK de aplicación, retransmisiones, ARP, tamaños, latencia ni negociación WPA
  completas. Para HTTP local se presupone la conexión TCP establecida.
- El destino 80/443/123 es el del **servidor**. Las respuestas vuelven al puerto
  del cliente, cuyo valor no se inventa. No se asume que el cliente SNTP use un
  puerto efímero particular.
- El HTTP de las conexiones HTTPS solo se muestra semánticamente: sus textos
  viajan cifrados. Las dos flechas TLS no equivalen a dos paquetes de handshake.
- En el modelo TCP/IP se muestra TLS junto a la aplicación, entre HTTP y TCP;
  no se añaden capas de sesión/presentación del modelo OSI.
- La página **original** del firmware carga Chart.js desde jsDelivr y una fuente
  de Google por HTTPS. Esas conexiones salientes del navegador no se animan.
  Esta nueva página es independiente y todos sus recursos son locales.

## Verificación

Pruebas opcionales: `tests/visualization_test.cjs` y `tests/packet_timing_test.cjs`,
Node.js y una instalación existente
de Playwright con Edge. No hace falta instalar nada para usar la visualización.
Si Playwright está instalado fuera de la carpeta, indicar su ruta de módulo:

```powershell
$env:PLAYWRIGHT_MODULE_PATH = 'C:\ruta\a\node_modules\playwright'
node tests/visualization_test.cjs
node tests/packet_timing_test.cjs
```

`VISUALIZATION_TEST_OUTPUT` permite elegir una carpeta para las capturas; por
defecto se usa el temporal del sistema. `VISUALIZATION_BROWSER` permite indicar
otro canal Chromium instalado, por ejemplo `chrome`.

Cobertura automatizada:

- Apertura directa `file://` con el contexto de navegador **sin conexión**.
- Ocho escenarios, todos sus pasos, finalización, repetición y reconstrucción.
- Desplazamiento de partículas, pausa congelada y preferencia de movimiento reducido.
- Medición de distancia por tiempo en pantalla a 0,5×, 1×, 1,5× y 2×: trayectos
  cortos/largos, sentido inverso, lotes TCP/TLS y salidas simultáneas.
- Duración proporcional a la longitud, ausencia de saltos entre tramos, cambio de
  velocidad sin salto, conservación del mínimo de lectura y escala al redimensionar.
- Finalización automática de los ocho escenarios, sin partículas residuales.
- LEDs ON/OFF/auto, valores de GPIO, método PUT y parámetros correctos.
- Cambio de cliente y ruta, portal directo y cierre antes del HTTP habitual.
- HTTP local sin enlaces WAN, WeatherAPI HTTP/1.0, Telegram GET HTTP/1.1 y NTP UDP 123.
- Interacción por teclado, pestañas, velocidad y pantalla completa.
- Anchos 320, 390, 768, 1280, 1440 y 1920 px: sin desbordamiento horizontal de la
  página; los diagramas pequeños se desplazan dentro de su panel.
- Etiquetas dentro de los viewBox, sin posiciones NaN, errores de consola ni
  solicitudes externas. Capturas de red, hardware, móvil y proyección.

Además se revisaron visualmente las capturas y se comprobó la sintaxis con
`node --check`. Resultado de la verificación del 23/10/2026: **276 comprobaciones
aprobadas en Edge/Chromium**, contexto sin conexión y proyección 1920 × 1080 sin
scroll vertical. No se verificaron Safari ni dispositivos físicos.
Actualización del motor de animación: **664 comprobaciones adicionales**, con
**90 mediciones de desplazamiento** a 0,5×, 1×, 1,5× y 2×. La velocidad normalizada
medida quedó entre 39,80 y 40,04 px/s; la pequeña diferencia corresponde a medir
cuerdas sobre curvas y al redondeo de coordenadas. Las ocho reproducciones
completas, los lotes sin superposición, la pausa/continuación, el tiempo de
lectura y el cambio de escala quedaron verificados. Total: **940 comprobaciones**.

Se compararon además 48 combinaciones de escenarios, clientes y modos LED con
la versión anterior: dispositivos, textos técnicos, rutas, órdenes y acciones
idénticos. `index.html`, `styles.css` y los archivos existentes del firmware
permanecen sin modificaciones.

La verificación del firmware se realiza por lectura y trazabilidad;
no se compiló, modificó ni cargó firmware para esta herramienta. No se realizaron
commits ni push. Una eventual integración real deberá diseñarse y validarse por
separado; no se incluye un adaptador de red en esta versión.
