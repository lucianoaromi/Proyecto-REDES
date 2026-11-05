# Control local conjunto de D6 y D7

Implementado en `experiment/wifimanager`. No requiere bibliotecas nuevas.

- El switch **LEDs D6/D7**, en **Sensor local**, pasa a modo manual y enciende o
  apaga ambos LEDs en la misma petición HTTP.
- **Automático** restaura el control independiente según el último clima válido:
  D6 encendido desde 35 °C; D7 fijo con lluvia actual, con ráfagas ante pronóstico
  y apagado si no hay lluvia ni pronóstico.
- Las consultas meteorológicas continúan en manual y actualizan los datos, pero
  no escriben las salidas. Los tres Ticker se desarman y los callbacks y funciones
  automáticas comprueban el modo antes de actuar.
- Al volver a automático, el pronóstico inicia una nueva ráfaga de 60 segundos,
  con alternancia cada 500 ms y repetición cada cinco minutos, como al activar
  originalmente ese patrón. No se retoma una ráfaga a mitad de su fase.
  Repetir una orden de automático estando ya en automático no reinicia los Ticker.
- El modo reside en RAM. Al reiniciar vuelve a automático, con ambos apagados hasta
  recibir datos meteorológicos. No se escribe flash ni SPIFFS para este control.

## PWM y temporizadores

El Core ESP8266 3.1.2 instalado usa por defecto el rango PWM 0..255. El brillo
automático original de D7 permanece en **15/255** (~5,9%). En manual se usa
**64/255** (~25,1%) para aumentar la visibilidad sin llevarlo a conducción continua.
No se cambia el rango, la frecuencia PWM ni el nivel de tensión del GPIO.
D6 conserva HIGH/LOW.

`Ticker::detach()` desarma el temporizador SDK y elimina su callback en este core.
El modo manual se establece antes de desarmar los Ticker y aplicar las salidas.
Se protegen también las entradas de los callbacks, incluido uno pendiente al
comenzar la transición. Al salir de manual se limpian los indicadores del patrón
y se evalúan inmediatamente los datos de lluvia más recientes.

La luminosidad y la corriente admisible no pueden certificarse por software:
el repositorio no especifica las resistencias ni el circuito de cada LED.
La prueba física debe verificar resistencias limitadoras adecuadas y la corriente
de ambos LEDs encendidos. PWM reduce la corriente media, no la corriente de pico.

## HTTP

| Solicitud | Resultado |
| --- | --- |
| `GET /led` | Consulta el estado sin modificarlo |
| `PUT /led?mode=manual&on=1` | D6 y D7 encendidos manualmente |
| `PUT /led?mode=manual&on=0` | D6 y D7 apagados manualmente |
| `PUT /led?mode=auto` | Ambos recuperan su control automático independiente |

Solicitudes sin cuerpo. Respuesta correcta: HTTP 200 y
`{"mode":"manual","on":true,"d6":true,"d7":true}`.
En automático las salidas pueden diferir, por ejemplo:
`{"mode":"auto","on":false,"d6":true,"d7":false}`.
`on` significa que **ambos** están encendidos; `d6` y `d7` indican cada salida.
En automático la etiqueta muestra **Automático** y el título del switch permite
consultar el estado individual. La consulta cada dos segundos es una instantánea;
no representa todas las alternancias de 500 ms del parpadeo.
Parámetros inválidos: HTTP 400 sin modificar las salidas ni Ticker. Métodos no admitidos: HTTP 405.
No se admiten parámetros adicionales, duplicados ni otra ordenación.
Las respuestas de `/led` incluyen `Cache-Control: no-store`.

`/data` conserva sus campos y agrega `led` con el mismo objeto de estado.
La actualización existente cada dos segundos sincroniza los navegadores.
El switch solo cambia al recibir un estado válido; permanece deshabilitado
durante la orden. Se descartan respuestas periódicas iniciadas antes o durante
esa orden. Un error HTTP, de red o de formato muestra **Sin confirmación** y
conserva la última indicación confirmada, hasta recuperar comunicación.
La orden tiene un timeout de ocho segundos en el navegador.

El estado confirma la salida ordenada por el firmware; no mide eléctricamente
los LEDs. Si se pierde una respuesta, la orden podría haberse ejecutado: la próxima
consulta reconciliará el estado. Un timeout del navegador no cancela una orden
que el ESP8266 llegue a recibir después.

## Verificación realizada

- Compilación NodeMCU (`esp8266:esp8266:nodemcuv2`), Core 3.1.2, con una copia
  temporal del sketch y `secrets.example.h` como credenciales ficticias.
- RAM estática: **35.872/80.192 bytes**; flash: **559.912/1.048.576 bytes**;
  IRAM y caché: **61.883/65.536 bytes**. No son mediciones del heap durante TLS.
- El controlador utiliza un pin, tres booleanos y dos punteros a funciones,
  sin asignaciones dinámicas propias ni bibliotecas adicionales.
- `led_control_test.cpp`: 114 comprobaciones con el controlador HTTP real y
  GPIO simulado: encendido, apagado, umbral, manual durante cambios del clima,
  retorno a automático, reinicio, métodos y parámetros inválidos.
- `rain_led_control_test.py`: 55 comprobaciones con funciones del sketch real,
  GPIO y Ticker simulados; PWM manual y automático, las dos salidas, callbacks
  pendientes, inmunidad al clima, restauración de lluvia actual, pronóstico,
  ventanas y repetición, prioridad de lluvia actual y reinicio.
- `weather_endpoint_test.py`: ocho respuestas del bloque `/data` real;
  conserva campos anteriores y comprueba el estado inicial de ambas salidas.
- `web_interface_test.cjs`: Edge/Playwright con servidor local simulado;
  manual/automático, confirmación demorada, error HTTP, fallo de red, JSON
  inválido, sincronización entre dos páginas y rechazo de respuestas anteriores a la orden.
  Cinco anchos entre 320 y 1280 px, sin desbordes ni solapamiento del control.
  Se conservan gráficas, clima, luna, CSV, tarjetas estáticas al hover y actualización cada dos segundos.
- También pasan las pruebas existentes de JSON (234 casos), WiFiManager
  (72 comprobaciones), DHT/historial/alertas (36), caché CSV, tiempos HTTP
  con diagnósticos activos e inactivos, y preservación del código ajeno a los LEDs.
- El compilador continúa informando advertencias en áreas existentes
  (SPIFFS, formatos y posibles valores meteorológicos sin inicializar).
  No se cargó firmware ni se probaron servicios con credenciales reales.

## Prueba pendiente en ESP8266

1. Cargar manualmente el firmware y abrir su IP desde la red local.
2. Confirmar **Automático** al arrancar. Verificar D6 según la temperatura
   exterior y 35 °C, y D7 según la lluvia actual/pronosticada.
3. Conmutar ON y OFF: comprobar que ambos LEDs resultan visibles y luego apagados.
   Verificar el circuito y la corriente antes de prolongar el encendido manual.
4. Mantener cada estado manual durante una consulta de WeatherAPI y por más de
   cinco minutos: ninguna ráfaga ni callback debe alterar las salidas.
   Volver a automático: comprobar el patrón correspondiente al último clima,
   incluidos los 60 segundos de ráfaga y su repetición a los cinco minutos
   cuando exista pronóstico. No modificar los datos meteorológicos reales para
   forzar la prueba: los escenarios ausentes quedan cubiertos solo por simulación.
5. Abrir otro navegador: debe reflejar el estado en la siguiente consulta
   de `/data`. Probar el botón **Automático** y reiniciar estando en manual.
6. Interrumpir la comunicación del navegador y comprobar el aviso de error;
   al recuperarla, verificar la reconciliación con el estado real.
7. Comprobar mediciones, gráficas, historial, portal WiFiManager y HTTPS.

Se conserva el servidor HTTP local sin autenticación: los equipos con acceso
al ESP8266 pueden controlar ambos LEDs. No se habilita CORS y las órdenes usan PUT,
evitando mutaciones por GET y formularios simples de otros sitios.
Las operaciones HTTPS síncronas existentes pueden demorar la atención HTTP;
este cambio no modifica sus tiempos ni prioridades. Dos navegadores que envíen
órdenes distintas se resuelven en el orden en que el dispositivo las procese.
