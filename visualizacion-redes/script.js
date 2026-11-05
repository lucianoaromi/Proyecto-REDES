/* Proyecto-REDES. Modelo didáctico aislado: sin fetch, sockets ni datos del equipo. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const NS = 'http://www.w3.org/2000/svg';
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  // Distancia en píxeles CSS: referencia próxima al recorrido DHT más lento anterior.
  // Ningún escenario define velocidades ni duraciones particulares para sus paquetes.
  const TIMING = Object.freeze({
    pixelsPerSecond: 40,
    leadMs: 900,
    gapMs: 650,
    tailMs: 1200,
    minReadingMs: 10000,
    readingWordsPerMinute: 180
  });
  const state = { scenario: 'http', index: -1, status: 'ready', elapsed: 0, readingElapsed: 0, readingMs: 0, phase: 'lead', batchIndex: 0, batches: [], scale: 1, speed: 1, lastTime: null, raf: null, steps: [], packets: [], model: null };
  let ledCommand = 'on';

  const devices = {
    pc: ['Computadora cliente', 'El navegador inicia solicitudes HTTP al ESP8266. Se supone conexión Wi-Fi al mismo AP y pertenencia a la misma subred; no se conoce su IP ni su puerto efímero.', 'ESP8266_DHT22.ino · HTML y fetch /data, /led'],
    phone: ['Teléfono móvil cliente', 'Cumple el mismo papel HTTP que la computadora. El teléfono del dibujo es un navegador local; no representa un cliente de Telegram ni recibe mensajes directamente del ESP8266.', 'ESP8266_DHT22.ino · interfaz web responsive'],
    ap: ['Router / punto de acceso', 'En la misma subred puentea tramas entre estaciones Wi-Fi. Para destinos externos se usa el gateway de la red. Se agrupan AP y gateway para explicar la topología; no se ha verificado el equipo, el servidor DHCP, el DNS ni la existencia de NAT.', 'WIFIMANAGER_EXPERIMENT.md · conectividad STA'],
    esp: ['ESP8266 NodeMCU', 'Atiende HTTP en TCP 80 y actúa como cliente de servicios externos. Los datos locales provienen del DHT22; el clima exterior proviene de WeatherAPI. Las conexiones HTTPS son síncronas y se atienden de una en una, sin un cliente HTTP pendiente.', 'ESP8266_DHT22.ino · WiFiServer server(80)'],
    internet: ['Internet · tránsito lógico', 'Agrupa redes intermedias cuyo recorrido no está documentado. No representa un servidor, una IP concreta ni un único salto. NAT puede existir en el despliegue, pero el firmware no lo configura ni lo demuestra.', 'ESP8266_DHT22.ino · clientes HTTPS y configTime'],
    weather: ['WeatherAPI · clima exterior', 'El ESP8266 consulta api.weatherapi.com/v1/forecast.json mediante GET HTTP/1.0 sobre TLS 1.2, TCP 443. Solicita Corrientes, Argentina. Tras el primer éxito, el intervalo nominal es de diez minutos, sujeto al planificador.', 'ESP8266_DHT22.ino · actualizarClimaApi()'],
    telegram: ['Telegram · Bot API', 'El ESP8266 envía el aviso de inicio y alertas de temperatura local desde 35 °C. Usa GET HTTP/1.1 sendMessage por TLS 1.2, TCP 443. Confirma solo el estado HTTP 200, sin interpretar el JSON ni recibir comandos del bot.', 'ESP8266_DHT22.ino · enviarTelegram()'],
    ntp: ['Hora de red · SNTP', 'configTime configura pool.ntp.org y time.nist.gov. El core sincroniza por NTP/SNTP sobre UDP 123. Se requiere una actualización SNTP y una fecha plausible antes de TLS. Se usa UTC−3 para la presentación local; NTP no está autenticado en este proyecto.', 'ESP8266_DHT22.ino · configTime y settimeofday_cb'],
    dht: ['DHT22 · sensor local', 'El pin de datos es D5 / GPIO14. Intercambia una señal digital propia del DHT, sin puertos TCP/UDP. La lectura nominal es cada 2,5 s; las operaciones HTTPS pueden demorar el loop. El esquema no permite certificar el trazado completo de alimentación.', 'ESP8266_DHT22.ino · SENSOR D5 · leerDHT()'],
    d6: ['LED D6 · temperatura exterior', 'D6 / GPIO12 usa digitalWrite HIGH/LOW. En automático se enciende desde 35 °C de WeatherAPI, no del DHT22. En manual obedece la misma orden que D7. El repositorio no especifica el circuito ni las resistencias del LED.', 'led_api_control.h · weather() y write()'],
    d7: ['LED D7 · lluvia exterior', 'D7 / GPIO13 usa PWM: 64/255 en manual, 15/255 en automático. Lluvia actual: encendido; pronóstico: ráfagas de 60 s cada cinco minutos con alternancia de 500 ms. El dibujo resume su estado lógico, no el PWM ni la luminosidad real.', 'ESP8266_DHT22.ino · cambiarControlLluvia()']
  };

  const scenarioInfo = {
    http: ['El ESP8266 también es un servidor.', 'Seguí el GET de un navegador, la respuesta HTML y la consulta periódica de datos. La comunicación local no requiere Internet.'],
    led: ['Una solicitud HTTP se convierte en una acción física.', 'El control manual actúa sobre D6 y D7 juntos. Podés observar las salidas desde la pestaña Hardware.'],
    weather: ['El microcontrolador ahora es el cliente.', 'Recorrido de una consulta saliente a WeatherAPI. Se supone Wi-Fi, hora válida y memoria suficiente para TLS.'],
    telegram: ['Una notificación sale desde la estación.', 'El firmware envía un aviso de inicio o una alerta local. El servicio responde al ESP8266 por la conexión iniciada por este.'],
    wifi: ['Conectarse es más que asociarse al AP.', 'Asociación Wi-Fi, configuración IP mediante DHCP y puesta en servicio. Los valores de red reales no están capturados.'],
    portal: ['Configurar la red es una etapa separada.', 'Tras 120 s sin conexión inicial, y con una clave AP válida, WiFiManager puede abrir el portal temporal durante un máximo de 300 s.'],
    ntp: ['La hora también llega por la red.', 'El cliente SNTP del core obtiene la hora antes de habilitar las comunicaciones TLS. No se transmite la zona horaria por NTP.'],
    general: ['De la conexión al control físico.', 'Una síntesis ordenada del arranque, la hora, los servicios externos y el acceso local. Los plazos se comprimen para la exposición.']
  };

  // Convención de recorridos: “-” invierte un enlace SVG existente.
  // Un mismo lote comparte inicio (p. ej. D6 y D7); lotes consecutivos no se solapan.
  const packet = (route, label, response = false, batch = 0) => ({ route, label, response, batch });
  const reverse = route => [...route].reverse().map(id => id.startsWith('-') ? id.slice(1) : `-${id}`);
  const localRoute = () => [`link-${$('client').value}-ap`, 'link-ap-esp'];
  const wanRoute = service => ['-link-ap-esp', 'link-ap-net', `link-net-${service}`];
  const clientName = () => $('client').value === 'pc' ? 'Computadora' : 'Teléfono móvil';
  const stage = (title, description, data = {}) => ({
    title, description, from: 'ESP8266', to: 'Proceso local', protocol: 'Operación local', layer: 'Aplicación / lógica del equipo', port: 'No corresponde', operation: 'Procesamiento', message: 'Sin mensaje de red en esta etapa.',
    note: 'Animación conceptual. No corresponde a una captura ni a una duración medida.', verified: true, nodes: ['esp'], packets: [], mode: 'lan', ...data
  });

  function dnsStages(host) {
    return [stage('Resolver el nombre, si no está en caché.', `El cliente necesita la dirección asociada a ${host}. La resolución queda a cargo del core; no conocemos la ubicación del resolver entregado por la red.`, {
      from: 'ESP8266', to: 'Resolver DNS no identificado', protocol: 'DNS sobre UDP/IP (habitual)', layer: 'Aplicación → transporte UDP', port: 'UDP 53 · consulta habitual', operation: `Consulta de nombre: ${host}`, message: `DNS: ${host}\nDirección resuelta: no capturada`, verified: false, nodes: ['esp', 'ap'],
      packets: [packet(['-link-ap-esp'], 'Hacia DNS', false), packet(['link-ap-esp'], 'Desde DNS', true, 1)],
      note: 'Solo se anima el tramo local hasta el AP y de regreso. El AP no se identifica como servidor DNS. Caché, consultas exactas y transporte no fueron capturados; UDP 53 es el flujo convencional resumido.'
    })];
  }

  function tcpStage(service, host) {
    const route = wanRoute(service);
    return stage('Abrir la conexión TCP.', `El ESP8266 inicia la conexión hacia ${host}:443 a través del gateway. Se agrupan los tres intercambios de establecimiento para explicar el transporte.`, {
      from: 'ESP8266 ↔ servicio externo', to: host, protocol: 'TCP sobre IP', layer: 'Transporte', port: 'TCP 443 al servidor', operation: 'SYN → SYN-ACK → ACK', message: 'SYN →\n← SYN-ACK\nACK →', verified: false, nodes: ['esp', 'ap', 'internet', service], mode: 'wan',
      packets: [packet(route, 'SYN →', false), packet(reverse(route), '← SYN-ACK', true, 1), packet(route, 'ACK →', false, 2)],
      note: 'TCP se muestra de forma didáctica: sin números de secuencia, puertos efímeros ni segmentos reales. El destino TCP 443 sí corresponde al cliente HTTPS del firmware.'
    });
  }

  function tlsStage(service, host) {
    const route = wanRoute(service);
    return stage('Negociar TLS 1.2 y validar el certificado.', `BearSSL verifica la cadena de confianza, el nombre ${host} y la vigencia usando la hora sincronizada. Después, HTTP viaja cifrado sobre TCP.`, {
      from: 'ESP8266 ↔ servicio externo', to: host, protocol: 'TLS 1.2 sobre TCP/IP', layer: 'Aplicación (entre HTTP y TCP)', port: 'TCP 443 al servidor', operation: 'Negociación TLS · fases agrupadas', message: 'TLS 1.2\nCA + nombre + vigencia\nHTTP cifrado tras la negociación', nodes: ['esp', 'ap', 'internet', service], mode: 'wan',
      packets: [packet(route, 'TLS →', false), packet(reverse(route), '← TLS', true, 1)],
      note: 'TLS 1.2 y la validación están configurados en el sketch. Las flechas agrupan una negociación con más mensajes; no representan dos paquetes TLS reales. Sin hora válida o memoria suficiente, se aplaza la conexión.'
    });
  }

  function httpStages() {
    const route = localRoute(), client = clientName(), clientNode = $('client').value;
    return [
      stage('El navegador solicita la página.', `${client} abre la dirección local del ESP8266. En esta misma subred el AP puentea las tramas; no se usa el gateway ni Internet. Se presupone TCP establecido y resolución de enlace disponible.`, {
        from: client, to: 'ESP8266 · servidor HTTP', protocol: 'HTTP/1.1 sobre TCP/IP', layer: 'Aplicación → transporte TCP', port: 'TCP 80', operation: 'GET /', message: 'GET / HTTP/1.1\nHost: dirección local del ESP (no capturada)', nodes: [clientNode, 'ap', 'esp'], packets: [packet(route, 'GET / →')],
        note: 'La IP habitual se obtiene por DHCP y no se inventa. Los detalles previos de TCP, ARP y encapsulación Wi-Fi se omiten en este escenario.'
      }),
      stage('El firmware prepara el HTML.', 'WiFiServer acepta al cliente y el sketch lee la línea de solicitud. La página principal se escribe desde flash, sin convertir al router en servidor HTTP.', {
        to: 'Manejador HTTP del ESP8266', protocol: 'HTTP · procesamiento local', operation: 'Preparar text/html', message: 'WiFiServer server(80)\nHTML servido desde flash'
      }),
      stage('La respuesta regresa por la LAN.', 'El ESP8266 devuelve HTTP 200 y el HTML al navegador. La respuesta usa la conexión TCP aceptada y el sketch la cierra al terminar.', {
        from: 'ESP8266', to: client, protocol: 'HTTP/1.1 sobre TCP/IP', layer: 'Aplicación → transporte TCP', port: 'Puerto efímero del cliente (no capturado)', operation: 'HTTP 200 · text/html', message: 'HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: close', nodes: ['esp', 'ap', clientNode], packets: [packet(reverse(route), '← 200 · HTML', true)],
        note: '80 es el puerto del servidor, no el puerto de destino de esta respuesta. Las descargas CDN del navegador original quedan fuera del recorrido animado.'
      }),
      stage('JavaScript pide las mediciones.', 'La interfaz original consulta /data cada 2 s. No hay WebSocket ni envío espontáneo de datos desde el ESP al navegador: es polling HTTP.', {
        from: client, to: 'ESP8266 · /data', protocol: 'HTTP sobre TCP/IP', layer: 'Aplicación', port: 'TCP 80', operation: 'GET /data · polling cada 2 s', message: 'GET /data HTTP/1.1', nodes: [clientNode, 'ap', 'esp'], packets: [packet(route, 'GET /data →')],
        note: 'Se representa una sola consulta periódica. El intervalo de 2 s pertenece a la interfaz real; la duración de esta animación es independiente.'
      }),
      stage('Los datos vuelven como JSON.', 'La respuesta contiene lecturas locales, historial, clima exterior y el objeto led. El navegador actualiza la interfaz; la lectura nominal del DHT22 es cada 2,5 s y dos consultas pueden recibir la misma muestra.', {
        from: 'ESP8266', to: client, protocol: 'HTTP sobre TCP/IP', layer: 'Aplicación', port: 'Puerto efímero del cliente (no capturado)', operation: 'HTTP 200 · application/json', message: 'HTTP/1.1 200 OK\nContent-Type: application/json\nCampos: temp, hum, histT, histH, weather, led, …', nodes: ['esp', 'ap', clientNode], packets: [packet(reverse(route), '← 200 · JSON', true)]
      })
    ];
  }

  function ledStages() {
    const route = localRoute(), client = clientName(), clientNode = $('client').value;
    const query = ledCommand === 'auto' ? 'mode=auto' : `mode=manual&on=${ledCommand === 'on' ? 1 : 0}`;
    const auto = ledCommand === 'auto', on = ledCommand === 'on';
    const body = auto ? '{"mode":"auto","on":false,"d6":true,"d7":false}' : JSON.stringify({ mode: 'manual', on, d6: on, d7: on });
    return [
      stage('Un control de la interfaz genera la orden.', auto ? 'Se parte de ambos LEDs en manual ON. Para demostrar el retorno independiente, se supone un último clima válido de ejemplo: 36 °C, sin lluvia ni pronóstico.' : on ? 'Se parte de ambos LEDs apagados. El switch pide encenderlos juntos; no existe un endpoint independiente para cada LED.' : 'Se parte de ambos LEDs en manual ON para observar el apagado conjunto. El estado inicial también es una suposición didáctica.', {
        from: client, to: 'Interfaz del navegador', operation: auto ? 'Seleccionar Automático' : `Switch conjunto: ${on ? 'ON' : 'OFF'}`, message: `Orden preparada (todavía no enviada)\nPUT /led?${query}`, nodes: [clientNode], note: 'Los estados iniciales son ejemplos locales. No se consulta ni se controla una placa real.'
      }),
      stage('La orden viaja por HTTP.', `${client} envía un PUT sin cuerpo a /led. El AP puentea la solicitud hacia el ESP8266 dentro de la LAN.`, {
        from: client, to: 'ESP8266 · controlador LED', protocol: 'HTTP sobre TCP/IP', layer: 'Aplicación', port: 'TCP 80', operation: `PUT /led?${query}`, message: `PUT /led?${query} HTTP/1.1\nSin cuerpo`, nodes: [clientNode, 'ap', 'esp'], packets: [packet(route, 'PUT /led →')],
        note: 'Método y parámetros tomados de led_api_control.h. GET /led solo consulta; los métodos inválidos reciben 405 y los parámetros inválidos, 400.'
      }),
      stage(auto ? 'Los GPIO recuperan el control automático.' : `La orden ${on ? 'enciende' : 'apaga'} ambas salidas.`, auto ? 'Con el clima de ejemplo (36 °C y sin lluvia ni pronóstico), D6 queda encendido y D7 apagado. Los estados pueden diferir. El modo automático no depende de la temperatura local del DHT22.' : `El controlador bloquea los temporizadores automáticos y aplica el modo manual. D6 usa ${on ? 'HIGH' : 'LOW'}; D7 usa PWM ${on ? '64/255' : '0/255'}. El resultado corresponde a la salida ordenada, sin medición eléctrica.`, {
        from: 'ESP8266 · lógica de control', to: 'D6 / GPIO12 + D7 / GPIO13', protocol: 'Señales GPIO · D7 con PWM', layer: 'Hardware local · fuera de TCP/IP', port: 'No hay puertos de red', operation: auto ? 'Restaurar lógica según el clima' : 'digitalWrite(D6) + analogWrite(D7)', message: auto ? 'Clima de ejemplo: 36 °C; sin lluvia\nD6 = HIGH · D7 = 0/255' : `D6 = ${on ? 'HIGH' : 'LOW'}\nD7 = ${on ? '64' : '0'}/255 (PWM)`, nodes: ['esp', 'd6', 'd7'], packets: [packet(['link-d6'], 'D6 →'), packet(['link-d7'], 'D7 →')], action: auto ? 'led-auto' : on ? 'led-on' : 'led-off',
        note: 'Mirá Hardware para seguir las señales físicas. Las resistencias, la alimentación y la intensidad luminosa reales no están documentadas ni se simulan.'
      }),
      stage('El ESP confirma el estado ordenado.', 'El firmware responde HTTP 200 con el objeto de estado y Cache-Control: no-store. El switch original espera una respuesta válida antes de confirmar. Una respuesta perdida no demuestra que la orden no se haya ejecutado.', {
        from: 'ESP8266', to: client, protocol: 'HTTP/1.1 sobre TCP/IP', layer: 'Aplicación', port: 'Puerto efímero del cliente (no capturado)', operation: 'HTTP 200 · estado JSON', message: `HTTP/1.1 200 OK\nCache-Control: no-store\n${body}`, nodes: ['esp', 'ap', clientNode], packets: [packet(reverse(route), '← 200 · estado', true)],
        note: 'El campo on es la conjunción de d6 y d7. En automático, la respuesta mostrada corresponde únicamente al clima de ejemplo indicado.'
      })
    ];
  }

  function weatherStages() {
    const route = wanRoute('weather');
    return [
      stage('El planificador habilita la consulta.', 'Se supone Wi-Fi, hora SNTP válida y memoria apta. El firmware inicia una operación externa si no hay cliente HTTP pendiente; no mantiene dos conexiones TLS a la vez.', { operation: 'Preparar WeatherAPI', message: 'Condiciones: Wi-Fi + hora válida + memoria\nPrimera consulta / intervalo nominal: 10 min', note: 'El envío puede aplazarse por la prioridad de Telegram, la ventana de lluvia o memoria insuficiente. No hay lectura HTTP en paralelo durante una operación HTTPS síncrona.' }),
      ...dnsStages('api.weatherapi.com'), tcpStage('weather', 'api.weatherapi.com'), tlsStage('weather', 'api.weatherapi.com'),
      stage('Enviar la consulta HTTPS.', 'El ESP8266 solicita clima actual y pronóstico de un día para Corrientes, Argentina. Usa HTTP/1.0 y no sigue redirecciones. El router actúa como salida hacia Internet.', {
        from: 'ESP8266 · cliente HTTPS', to: 'api.weatherapi.com', protocol: 'HTTPS = HTTP/1.0 + TLS 1.2 + TCP/IP', layer: 'Aplicación', port: 'TCP 443', operation: 'GET /v1/forecast.json', message: 'GET /v1/forecast.json HTTP/1.0\nq=Corrientes,Argentina; lang=es; aqi=no; days=1\nCredencial omitida', nodes: ['esp', 'ap', 'internet', 'weather'], mode: 'wan', packets: [packet(route, 'HTTPS · GET →')],
        note: 'El texto muestra el significado de la solicitud antes de cifrarse. En la red, TLS protege el contenido HTTP. No se incluye ninguna API key.'
      }),
      stage('Recibir y validar el JSON meteorológico.', 'Tras HTTP 200, el parser procesa current y forecast con documentos de capacidad fija. Un cuerpo inválido se descarta y se conserva el último clima válido.', {
        from: 'WeatherAPI', to: 'ESP8266', protocol: 'HTTPS · respuesta HTTP cifrada', layer: 'Aplicación', port: 'Puerto efímero del ESP (no capturado)', operation: 'HTTP 200 · JSON meteorológico', message: 'Respuesta ilustrativa, no real:\ncurrent: temp_c = 31, precip_mm > umbral\nforecast: datos de un día', nodes: ['weather', 'internet', 'ap', 'esp'], mode: 'wan', packets: [packet(reverse(route), '← HTTPS · JSON', true)],
        note: 'Los valores son ejemplos didácticos; no son mediciones, clima actual ni contenido de una captura. El sketch solo actualiza sus variables tras validar la respuesta.', action: 'weather-example'
      }),
      stage('El clima exterior actualiza la lógica de LEDs.', 'Para este ejemplo en automático, 31 °C deja D6 apagado y la lluvia actual enciende D7. Si el modo fuera manual, WeatherAPI actualizaría los datos pero no las salidas.', {
        from: 'ESP8266 · clima exterior validado', to: 'D6 + D7 · lógica automática', protocol: 'GPIO / PWM · no es red', layer: 'Hardware local · fuera de TCP/IP', operation: 'Evaluar temperatura y lluvia', message: 'Ejemplo: 31 °C + lluvia actual\nD6: OFF · D7: ON (PWM automático 15/255)', nodes: ['esp', 'd6', 'd7'], packets: [packet(['link-d6'], 'D6 →'), packet(['link-d7'], 'D7 →')], action: 'weather-leds', note: 'La cadencia PWM y las ráfagas de pronóstico no se reproducen eléctricamente. Los brillos del dibujo indican estados lógicos.'
      })
    ];
  }

  function telegramStages() {
    const route = wanRoute('telegram');
    return [
      stage('Se genera un aviso pendiente.', 'Puede ser el aviso de arranque o una alerta cuando la temperatura local filtrada del DHT22 alcanza 35 °C. Las alertas se generan con una separación mínima de cinco minutos. No se reciben comandos de Telegram.', { operation: 'Aviso de inicio / alerta local', message: 'Mensaje pendiente en RAM\nSin token, chat_id ni datos privados', note: 'El escenario representa un envío exitoso con Wi-Fi, hora y memoria aptos. Un fallo conserva el aviso para reintentar, con un intervalo mínimo de 30 s.' }),
      ...dnsStages('api.telegram.org'), tcpStage('telegram', 'api.telegram.org'), tlsStage('telegram', 'api.telegram.org'),
      stage('El ESP envía sendMessage mediante GET.', 'El sketch construye un GET HTTP/1.1 por la conexión TLS hacia api.telegram.org. El bot se usa para notificar; el teléfono del diagrama sigue siendo un cliente web local.', {
        from: 'ESP8266 · cliente HTTPS', to: 'api.telegram.org · Bot API', protocol: 'HTTPS = HTTP/1.1 + TLS 1.2 + TCP/IP', layer: 'Aplicación', port: 'TCP 443', operation: 'GET · método sendMessage', message: 'GET …/sendMessage HTTP/1.1\nHost: api.telegram.org\nToken y parámetros privados omitidos', nodes: ['esp', 'ap', 'internet', 'telegram'], mode: 'wan', packets: [packet(route, 'HTTPS · GET →')], note: 'Se omite la URL completa que contiene credenciales. El mensaje dibujado es una explicación del HTTP cifrado, no el contenido visible de un paquete en Internet.'
      }),
      stage('El servicio responde por la misma conexión.', 'El firmware considera confirmado el envío al leer HTTP/1.1 200. No interpreta el JSON de Telegram ni verifica la entrega o lectura en un teléfono.', {
        from: 'Telegram Bot API', to: 'ESP8266', protocol: 'HTTPS · respuesta HTTP/1.1', layer: 'Aplicación', port: 'Puerto efímero del ESP (no capturado)', operation: 'HTTP 200 · confirmación del servicio', message: 'HTTP/1.1 200 OK\nEl sketch comprueba solo el estado HTTP.', nodes: ['telegram', 'internet', 'ap', 'esp'], mode: 'wan', packets: [packet(reverse(route), '← HTTPS · 200', true)], note: 'Es tráfico de respuesta a una conexión saliente. No se representa apertura de puertos, webhook, consulta getUpdates ni conexión entrante al ESP8266.'
      })
    ];
  }

  function wifiStages() {
    const dhcp = (title, description, response, operation) => stage(title, description, {
      from: response ? 'Servicio DHCP de la red' : 'ESP8266 · cliente DHCP', to: response ? 'ESP8266 · cliente DHCP' : 'Servicio DHCP no identificado', protocol: 'DHCP sobre UDP/IP', layer: 'Aplicación → transporte UDP', port: response ? 'UDP 68 · cliente' : 'UDP 67 · servidor', operation, message: `${operation}\nServidor DHCP e IP: no identificados`, verified: false, nodes: ['esp', 'ap'], packets: [packet([response ? 'link-ap-esp' : '-link-ap-esp'], operation, response)],
      note: 'DORA es una explicación del arranque DHCP convencional, no una captura del core. Se anima solo el tramo Wi-Fi; no se afirma que el router sea el servidor DHCP. Una renovación puede seguir otra secuencia.'
    });
    return [
      stage('El equipo intenta la red configurada.', 'WifiProvisioning activa STA y prioriza la configuración guardada. Solo si no existe utiliza las credenciales locales de respaldo. La espera inicial máxima es de 120 s, sin bloquear el loop.', { operation: 'WiFi.begin · modo STA', message: 'Red guardada → prioridad\nSin red guardada → respaldo local\nEspera inicial máxima: 120 s', action: 'wifi-start' }),
      stage('Asociarse al punto de acceso.', 'El ESP8266 establece el enlace Wi-Fi en 2,4 GHz. El evento STA_ASOCIADA no equivale a tener una IPv4 utilizable; los intercambios de autenticación y asociación se agrupan.', {
        from: 'ESP8266 · estación', to: 'Punto de acceso Wi-Fi', protocol: 'Wi-Fi · IEEE 802.11', layer: 'Acceso a la red / enlace', port: 'Sin puerto TCP/UDP', operation: 'Autenticación y asociación (resumen)', message: 'STA_ASOCIADA\nEnlace disponible ≠ IPv4 obtenida', verified: false, nodes: ['esp', 'ap'], packets: [packet(['-link-ap-esp'], 'Asociación →', false), packet(['link-ap-esp'], '← AP', true, 1)]
      }),
      dhcp('DHCP Discover: buscar configuración.', 'El cliente descubre servicios DHCP de la red. El SDK gestiona esta fase; el sketch no fija una IPv4 estática en esta rama.', false, 'DHCP Discover'),
      dhcp('DHCP Offer: recibir una oferta.', 'Un servicio DHCP propone una configuración. Su ubicación física y los valores ofrecidos no se conocen.', true, 'DHCP Offer'),
      dhcp('DHCP Request: solicitar la oferta.', 'El cliente solicita la configuración elegida. Se omiten direcciones de broadcast, identificadores y opciones concretas.', false, 'DHCP Request'),
      dhcp('DHCP ACK: confirmar la configuración.', 'La red confirma la concesión. IP, máscara, gateway y DNS dependen del despliegue; no se muestran valores supuestos.', true, 'DHCP ACK'),
      stage('IPv4 válida: habilitar los servicios.', 'Al obtener WL_CONNECTED y una IPv4 distinta de cero, el firmware marca la red lista. Con el portal cerrado, inicia HTTP en TCP 80 y configura SNTP. HTTPS todavía exige una hora válida y memoria suficiente.', {
        to: 'Servidor HTTP y cliente SNTP', operation: 'server.begin() + configTime()', message: 'IP / máscara / gateway / DNS: según red\nValores reales no capturados\nHTTP: TCP 80 disponible', action: 'wifi-ready', note: 'Se ilustró una conexión exitosa. Si vence la espera inicial, el portal solo se abre con clave AP válida. Tras la puesta en servicio, una desconexión no reabre automáticamente el portal.'
      })
    ];
  }

  function portalStages() {
    const client = clientName(), clientNode = $('client').value, route = [`link-${clientNode}-esp`];
    return [
      stage('Sin conexión inicial: abrir el AP de configuración.', 'Tras 120 s sin IPv4, y solo si hay una clave AP válida de 12 a 63 caracteres, se abre MonitorAmbiental-Config con WPA2-Personal. Dura como máximo 300 s; los tiempos se comprimen aquí.', { operation: 'WiFiManager · AP temporal', message: 'MonitorAmbiental-Config\nWPA2-Personal · clave no mostrada\nPortal máximo: 300 s', mode: 'portal', portal: true, action: 'portal-open', note: 'No se muestra una clave ni una red real del usuario. El servidor habitual y HTTPS aún no están habilitados.' }),
      stage('El cliente se une directamente al ESP8266.', `${client} cambia al AP temporal del ESP. No pasa por el router doméstico. El softAP proporciona configuración de red a sus clientes; no debe confundirse con el DHCP de la red de destino.`, {
        from: client, to: 'ESP8266 · softAP', protocol: 'Wi-Fi + DHCP (resumen)', layer: 'Enlace + aplicación sobre UDP', port: 'DHCP: UDP 67 servidor / 68 cliente', operation: 'Asociación y configuración del cliente', message: 'Cliente ↔ AP temporal del ESP8266\nHTTP habitual: detenido', nodes: [clientNode, 'esp'], packets: [packet(route, 'Wi-Fi / DHCP →', false), packet(reverse(route), '← Configuración', true, 1)], mode: 'portal', portal: true, verified: false, note: 'El intercambio de asociación y DHCP se agrupa didácticamente. Solo la IP del portal, 192.168.4.1, está documentada; no se asigna una IP al cliente.'
      }),
      stage('DNS cautivo ayuda a encontrar el portal.', 'WiFiManager atiende DNS en UDP 53 para el portal. La apertura automática depende del sistema del cliente; también puede abrirse directamente http://192.168.4.1 sin este paso DNS.', {
        from: client, to: 'DNS del portal en el ESP8266', protocol: 'DNS sobre UDP/IP', layer: 'Aplicación', port: 'UDP 53', operation: 'DNS cautivo (opcional)', message: 'DNS del portal → 192.168.4.1\nAlternativa: acceso HTTP directo', nodes: [clientNode, 'esp'], packets: [packet(route, 'DNS →', false), packet(reverse(route), '← Portal', true, 1)], mode: 'portal', portal: true, note: 'DNS cautivo del AP temporal, distinto del resolver usado al conectarse a Internet. No se inventa un dominio consultado por el cliente.'
      }),
      stage('Abrir el formulario de WiFiManager.', 'El navegador solicita /wifi en 192.168.4.1. Este servidor de configuración usa HTTP sobre el AP WPA2, no HTTPS ni la página de monitoreo habitual.', {
        from: client, to: 'ESP8266 · portal WiFiManager', protocol: 'HTTP sobre TCP/IP', layer: 'Aplicación', port: 'TCP 80', operation: 'GET /wifi → HTML del portal', message: 'GET /wifi HTTP/1.1\nHost: 192.168.4.1\n← Formulario de configuración', nodes: [clientNode, 'esp'], packets: [packet(route, 'GET /wifi →', false), packet(reverse(route), '← HTML', true, 1)], mode: 'portal', portal: true
      }),
      stage('POST /wifisave inicia un intento asíncrono.', 'El guard acepta solo POST válido y responde 202. La red candidata se prueba hasta 120 s dentro del límite global del portal. Todavía no se guardan credenciales en flash.', {
        from: client, to: 'Portal · /wifisave', protocol: 'HTTP sobre TCP/IP', layer: 'Aplicación', port: 'TCP 80 solicitud / efímero respuesta', operation: 'POST /wifisave → HTTP 202', message: 'POST /wifisave HTTP/1.1\nContenido del formulario omitido\n← 202: probando red (aún no confirmada)', nodes: [clientNode, 'esp'], packets: [packet(route, 'POST →', false), packet(reverse(route), '← 202', true, 1)], mode: 'portal', portal: true, note: '202 no significa conexión exitosa. Durante un intento se rechazan nuevos envíos con 409. No se introducen SSID ni contraseñas en esta aplicación.'
      }),
      stage('La candidata conecta y obtiene IPv4.', 'En AP+STA, el ESP se asocia a la red elegida y obtiene configuración IP. Solo tras confirmar la conexión se persiste en flash. La asociación y DHCP de la candidata se agrupan en esta etapa.', {
        from: 'ESP8266 · interfaz STA', to: 'AP y servicio DHCP de la red elegida', protocol: 'Wi-Fi + DHCP (resumen)', layer: 'Enlace + aplicación sobre UDP', port: 'DHCP UDP 67 / 68', operation: 'Confirmar IPv4 → guardar en flash', message: 'WL_CONNECTED + IPv4 válida\nSolo entonces: wifi_station_set_config()', nodes: ['esp', 'ap'], packets: [packet(['-link-ap-esp'], 'STA →', false), packet(['link-ap-esp'], '← IPv4', true, 1)], mode: 'portal', portal: true, note: 'Se representa el caso exitoso. Un fallo de candidata conserva la configuración persistida anterior; timeout o Exit cierran el portal y restauran los intentos de red.'
      }),
      stage('Cerrar el portal antes de servir el monitor.', 'Se detiene HTTP/DNS de WiFiManager, se destruye su objeto y se vuelve a STA. Después se inicia el servidor habitual en TCP 80 y se configura SNTP. El cliente debe volver a la LAN; la IP de monitoreo puede diferir de la del portal.', {
        operation: 'stopConfigPortal → destruir → server.begin', message: 'Portal HTTP/DNS: cerrado\nHTTP habitual: TCP 80\nIP LAN: obtenida de la red, no capturada', action: 'wifi-ready', portal: false, note: 'No conviven dos servidores en TCP 80. No se redirige el navegador automáticamente a una IP LAN supuesta.'
      })
    ];
  }

  function ntpStages() {
    const route = wanRoute('ntp');
    return [
      stage('Configurar los servidores de hora.', 'Con Wi-Fi e IPv4 listos y el portal cerrado, configTime configura pool.ntp.org y time.nist.gov. No se bloquea el loop esperando la hora.', { operation: 'configTime · UTC−3', message: 'pool.ntp.org\ntime.nist.gov\nDesplazamiento local: UTC−3', note: 'Los dos nombres son fuentes configuradas, no dos consultas simultáneas demostradas. El core selecciona y reintenta según su funcionamiento.' }),
      ...dnsStages('pool.ntp.org / time.nist.gov'),
      stage('Solicitar la hora mediante SNTP.', 'El cliente del core envía una consulta NTP a uno de los servidores configurados. Usa UDP, sin establecimiento TCP ni negociación TLS.', {
        from: 'ESP8266 · cliente SNTP', to: 'Servidor NTP configurado', protocol: 'NTP/SNTP sobre UDP/IP', layer: 'Aplicación → transporte UDP', port: 'UDP 123', operation: 'Solicitud de hora', message: 'Consulta NTP / SNTP\nDestino: servidor configurado · UDP 123', nodes: ['esp', 'ap', 'internet', 'ntp'], packets: [packet(route, 'SNTP →')], mode: 'wan', note: 'El servidor elegido y el puerto de origen no fueron capturados. No se representa NTS ni autenticación NTP.'
      }),
      stage('Recibir una actualización de hora.', 'La respuesta vuelve al cliente UDP. El callback settimeofday_cb marca horaSincronizada si la actualización proviene de SNTP; el firmware exige además una fecha plausible desde 01/01/2024 para TLS.', {
        from: 'Servidor NTP', to: 'ESP8266 · cliente SNTP', protocol: 'NTP/SNTP sobre UDP/IP', layer: 'Aplicación', port: 'Puerto UDP del cliente (no capturado)', operation: 'Actualización SNTP → hora válida', message: 'Hora SNTP recibida\nCallback: desdeSntp = true\nTLS: comprobar fecha + Wi-Fi + memoria', nodes: ['ntp', 'internet', 'ap', 'esp'], packets: [packet(reverse(route), '← Hora', true)], mode: 'wan', action: 'time-ready', note: 'La respuesta usa el puerto UDP del cliente; no se le atribuye un puerto efímero sin evidencia. La hora se usa para verificar vigencia de certificados, pero NTP no queda autenticado.'
      })
    ];
  }

  function makeSteps() {
    const builders = { http: httpStages, led: ledStages, weather: weatherStages, telegram: telegramStages, wifi: wifiStages, portal: portalStages, ntp: ntpStages };
    if (state.scenario !== 'general') return builders[state.scenario]();
    // Resumen pedagógico: conserva el orden de prioridad del planificador al arrancar.
    const wifi = wifiStages(), ntp = ntpStages(), telegram = telegramStages(), weather = weatherStages(), http = httpStages();
    const previousCommand = ledCommand;
    ledCommand = 'on'; const led = ledStages(); ledCommand = previousCommand;
    const sensor = stage('Leer el DHT22 por D5.', 'La estación adquiere temperatura y humedad locales mediante una interfaz digital del sensor. El intervalo nominal es de 2,5 s; la atención HTTPS síncrona puede demorar una lectura. Estos valores locales son distintos del clima de WeatherAPI.', {
      from: 'DHT22', to: 'ESP8266 · D5 / GPIO14', protocol: 'Interfaz digital propia del DHT', layer: 'Hardware local · fuera de TCP/IP', port: 'Sin puertos de red', operation: 'Lectura local de temperatura y humedad', message: 'DHT22 → D5 / GPIO14\nFiltrado e historial en el ESP8266', nodes: ['dht', 'esp'], packets: [packet(['-link-dht'], 'Datos DHT →', true)], note: 'La marca representa el dato adquirido, sin reproducir la temporización de la señal DHT. Esta lectura ocurre durante el loop, no solo después de una consulta meteorológica.'
    });
    return [wifi[0], wifi[1], wifi[5], wifi[6], ntp[0], ntp[2], ntp[3], telegram[0], telegram[3], telegram[4], telegram[5], weather[0], weather[4], weather[5], weather[6], sensor, http[0], http[2], http[3], http[4], led[1], led[2], led[3]];
  }

  function initialModel() {
    if (state.scenario === 'led' && ledCommand !== 'on') return { d6: true, d7: true, mode: 'manual', climate: ledCommand === 'auto' ? 'example-auto' : null, wifi: true, time: true, portal: false };
    return { d6: false, d7: false, mode: 'auto', climate: null, wifi: !['wifi', 'portal', 'general'].includes(state.scenario), time: !['wifi', 'portal', 'ntp', 'general'].includes(state.scenario), portal: state.scenario === 'portal' };
  }

  function updateModel(index) {
    const m = initialModel();
    for (let i = 0; i <= index; i++) {
      const action = state.steps[i].action;
      if (action === 'led-on' || action === 'led-off') { m.mode = 'manual'; m.d6 = m.d7 = action === 'led-on'; }
      if (action === 'led-auto') { m.mode = 'auto'; m.d6 = true; m.d7 = false; }
      if (action === 'weather-example') m.climate = 'example-weather';
      if (action === 'weather-leds' && m.mode === 'auto') { m.d6 = false; m.d7 = true; }
      if (action === 'wifi-start') { m.wifi = false; m.time = false; }
      if (action === 'portal-open') { m.portal = true; m.wifi = false; m.time = false; }
      if (action === 'wifi-ready') { m.wifi = true; m.portal = false; }
      if (action === 'time-ready') m.time = true;
    }
    state.model = m;
    document.querySelectorAll('[data-led-node]').forEach(node => {
      const key = node.dataset.ledNode, on = m[key];
      node.classList.toggle('on', on);
      node.querySelector('.led-text').textContent = on ? 'ENCENDIDO' : 'APAGADO';
      node.setAttribute('aria-label', `LED ${key.toUpperCase()}, ${on ? 'encendido' : 'apagado'}, ver detalles`);
    });
    $('led-readout').querySelectorAll('i').forEach((dot, i) => dot.classList.toggle('on', i === 0 ? m.d6 : m.d7));
    $('led-mode').textContent = m.mode === 'manual' ? 'MANUAL · EJEMPLO' : m.climate ? 'AUTO · CLIMA DE EJEMPLO' : 'AUTO · SIN CLIMA';
    $('network-svg').classList.toggle('portal-active', m.portal);
    $('portal-label').toggleAttribute('hidden', !m.portal);
    $('esp-service').textContent = m.portal ? 'Portal WiFiManager · TCP 80' : m.wifi ? 'Servidor HTTP · TCP 80' : 'STA · esperando configuración';
  }

  function setView(view) {
    for (const key of ['network', 'hardware']) {
      const selected = key === view;
      $(`tab-${key}`).setAttribute('aria-selected', String(selected));
      $(`tab-${key}`).tabIndex = selected ? 0 : -1;
      $(`view-${key}`).hidden = !selected;
    }
    retimePackets(); drawPackets();
  }

  function inspectDevice(key) {
    const [title, description, source] = devices[key];
    $('node-title').textContent = title;
    $('node-description').textContent = description;
    $('node-source').textContent = source;
    document.querySelectorAll('[data-node]').forEach(node => node.classList.toggle('is-selected', node.dataset.node === key));
  }

  function renderInfo(step) {
    for (const field of ['from', 'to', 'protocol', 'layer', 'port', 'operation']) $(`info-${field}`).textContent = step[field];
    $('message').textContent = step.message;
    $('stage-note').textContent = step.note;
    $('stage-kind').textContent = step.verified ? 'VERIFICADO EN CÓDIGO · RECORRIDO CONCEPTUAL' : 'ETAPA DIDÁCTICA · SIN CAPTURA';
    $('stage-kind').classList.toggle('concept', !step.verified);
    const modeText = step.mode === 'wan' ? 'Internet · salida por gateway' : step.mode === 'portal' ? 'Portal · enlace directo al softAP' : 'LAN · puente / capa de enlace';
    $('network-mode').textContent = modeText;
    $('ap-role').textContent = step.mode === 'wan' ? 'Gateway hacia Internet' : 'Puente en la LAN';
    $('message-label').textContent = step.protocol.includes('HTTPS') ? 'SIGNIFICADO DEL HTTP CIFRADO' : 'MENSAJE / OPERACIÓN REPRESENTADA';
  }

  function clearPackets() {
    $('network-packets').replaceChildren();
    $('hardware-packets').replaceChildren();
    state.packets = []; state.batches = [];
  }

  function buildPackets(step) {
    clearPackets();
    for (const spec of step.packets) {
      const segments = [];
      let previousEnd = null;
      for (const key of spec.route) {
        const reversed = key.startsWith('-'), id = reversed ? key.slice(1) : key, path = $(id);
        const length = path.getTotalLength();
        const start = path.getPointAtLength(reversed ? length : 0);
        const end = path.getPointAtLength(reversed ? 0 : length);
        if (previousEnd) {
          // El SVG corta los enlaces en el borde de cada dispositivo. Incluir
          // su tránsito interno evita saltos entre dos tramos, sin dibujar enlaces nuevos.
          const bridgeLength = Math.hypot(start.x - previousEnd.x, start.y - previousEnd.y);
          if (bridgeLength > 0) segments.push({ length: bridgeLength, start: previousEnd, end: start });
        }
        segments.push({ path, reversed, length }); previousEnd = end;
      }
      const hardware = $(spec.route[0].replace(/^-/, '')).closest('svg').id === 'hardware-svg';
      const group = document.createElementNS(NS, 'g');
      group.setAttribute('class', 'packet');
      group.style.color = spec.response ? 'var(--response)' : 'var(--request)';
      for (const [className, radius] of [['packet-ring', 11], ['packet-core', 5]]) {
        const circle = document.createElementNS(NS, 'circle');
        circle.setAttribute('r', radius); circle.setAttribute('class', className); group.append(circle);
      }
      const label = document.createElementNS(NS, 'text');
      label.setAttribute('class', 'packet-label'); label.textContent = spec.label;
      label.setAttribute('y', '-16'); label.setAttribute('text-anchor', 'middle'); group.append(label);
      $(hardware ? 'hardware-packets' : 'network-packets').append(group);
      state.packets.push({ ...spec, group, segments, length: segments.reduce((sum, s) => sum + s.length, 0) });
    }
    state.batches = [...new Set(state.packets.map(p => p.batch))].sort((a, b) => a - b)
      .map(batch => state.packets.filter(p => p.batch === batch));
    retimePackets();
  }

  function retimePackets() {
    // Ambos SVG comparten viewBox y estilos. Se mide el visible, incluso si el
    // paquete pertenece a la otra pestaña; display:none no da una escala fiable.
    const svg = $($('view-network').hidden ? 'hardware-svg' : 'network-svg');
    const matrix = svg.getScreenCTM();
    const scale = matrix ? Math.hypot(matrix.a, matrix.b) : state.scale;
    if (!(scale > 0)) return;
    // Conservar la posición lógica al cambiar tamaño o entrar en pantalla completa.
    if (state.phase === 'moving') state.elapsed *= scale / state.scale;
    state.scale = scale;
    for (const p of state.packets) {
      p.durationMs = p.length * scale / TIMING.pixelsPerSecond * 1000;
    }
  }

  function drawPackets() {
    if (state.index < 0) return;
    const batch = state.batches[state.batchIndex] || [];
    for (const p of state.packets) {
      const local = p.durationMs > 0 ? state.elapsed / p.durationMs : 1;
      // En movimiento reducido se conserva una marca quieta del lote actual.
      const visible = state.phase === 'moving' && batch.includes(p) && (reducedMotion || local <= 1);
      p.group.style.display = visible ? '' : 'none';
      if (!visible) continue;
      let distance = p.length * (reducedMotion ? .5 : Math.max(0, Math.min(1, local)));
      let segment = p.segments[p.segments.length - 1];
      for (const item of p.segments) {
        segment = item;
        if (distance <= item.length) break;
        distance -= item.length;
      }
      const point = segment.path
        ? segment.path.getPointAtLength(segment.reversed ? segment.length - distance : distance)
        : { x: segment.start.x + (segment.end.x - segment.start.x) * distance / segment.length,
          y: segment.start.y + (segment.end.y - segment.start.y) * distance / segment.length };
      p.group.setAttribute('transform', `translate(${point.x.toFixed(2)} ${point.y.toFixed(2)})`);
    }
  }

  function updateControls() {
    const playing = state.status === 'playing', complete = state.status === 'complete';
    $('play-label').textContent = playing ? 'Pausar recorrido' : complete ? 'Repetir recorrido' : state.status === 'paused' ? 'Continuar recorrido' : 'Iniciar recorrido';
    $('play-symbol').textContent = playing ? 'Ⅱ' : '▶';
    $('play-state').textContent = playing ? 'REPRODUCIENDO' : complete ? 'COMPLETADO' : state.status === 'paused' ? 'EN PAUSA' : 'EN ESPERA';
    $('next').disabled = complete;
    document.body.dataset.playback = state.status;
    $('progress-label').textContent = `${Math.max(0, state.index + 1)} de ${state.steps.length} etapas`;
  }

  function enterStage(index) {
    state.index = index; state.elapsed = 0; state.readingElapsed = 0;
    state.phase = 'lead'; state.batchIndex = 0;
    const step = state.steps[index];
    const words = `${step.title} ${step.description}`.trim().split(/\s+/).length;
    // Reloj de lectura real, independiente del selector: también protege los textos a 2×.
    state.readingMs = Math.max(TIMING.minReadingMs, words * 60000 / TIMING.readingWordsPerMinute);
    updateModel(index); renderInfo(step);
    $('step-counter').textContent = String(index + 1).padStart(2, '0');
    $('step-label').textContent = `ETAPA ${index + 1} / ${state.steps.length}`;
    $('step-title').textContent = step.title;
    $('step-description').textContent = step.description;
    document.querySelectorAll('[data-node]').forEach(node => node.classList.toggle('is-active', step.nodes.includes(node.dataset.node)));
    document.querySelectorAll('.link').forEach(link => link.classList.toggle('is-active', step.packets.some(p => p.route.some(key => key.replace(/^-/, '') === link.id))));
    $('timeline').querySelectorAll('button').forEach((button, i) => {
      button.classList.toggle('done', i < index); button.classList.toggle('current', i === index);
      if (i === index) button.setAttribute('aria-current', 'step'); else button.removeAttribute('aria-current');
    });
    buildPackets(step); drawPackets(); updateControls();
  }

  function cancelFrame() {
    if (state.raf !== null) cancelAnimationFrame(state.raf);
    state.raf = null; state.lastTime = null;
  }

  function finish() {
    state.status = 'complete'; cancelFrame(); clearPackets();
    $('timeline').querySelectorAll('button').forEach(b => b.classList.add('done'));
    updateControls();
  }

  function phaseDuration() {
    if (state.phase === 'moving') return Math.max(...state.batches[state.batchIndex].map(p => p.durationMs));
    return { lead: TIMING.leadMs, gap: TIMING.gapMs, tail: TIMING.tailMs }[state.phase];
  }

  function nextPhase() {
    if (state.phase === 'lead') state.phase = state.batches.length ? 'moving' : 'reading';
    else if (state.phase === 'moving') state.phase = state.batchIndex + 1 < state.batches.length ? 'gap' : 'tail';
    else if (state.phase === 'gap') { state.batchIndex++; state.phase = 'moving'; }
    else if (state.phase === 'tail') state.phase = 'reading';
    state.elapsed = 0;
  }

  function advanceTime(delta) {
    // Consumir tiempo real conserva el remanente entre fases y etapas, incluso
    // con un frame demorado. La explicación cambia solo después del último lote
    // y del tiempo mínimo de lectura. Pausar detiene ambos relojes.
    let remaining = Math.max(0, delta);
    while (remaining > 0 && state.status === 'playing') {
      const reading = state.phase === 'reading';
      const limit = reading ? Math.max(0, state.readingMs - state.readingElapsed)
        : Math.max(0, (phaseDuration() - state.elapsed) / state.speed);
      const consumed = Math.min(remaining, limit);
      state.readingElapsed += consumed;
      if (!reading) state.elapsed += consumed * state.speed;
      remaining -= consumed;
      if (consumed < limit) break;
      if (!reading) nextPhase();
      else if (state.index + 1 < state.steps.length) enterStage(state.index + 1);
      else { finish(); return; }
    }
  }

  function updateAnimation(now) {
    retimePackets();
    const delta = state.lastTime === null ? 0 : Math.max(0, now - state.lastTime);
    state.lastTime = now;
    advanceTime(delta); drawPackets();
  }

  function tick(now) {
    if (state.status !== 'playing') return;
    updateAnimation(now);
    if (state.status === 'playing') state.raf = requestAnimationFrame(tick);
  }

  function pause() {
    if (state.status === 'playing') updateAnimation(performance.now());
    if (state.status !== 'complete') state.status = 'paused';
    cancelFrame(); drawPackets(); updateControls();
  }

  function reset() {
    cancelFrame(); clearPackets(); state.index = -1; state.elapsed = 0; state.readingElapsed = 0; state.phase = 'lead'; state.batchIndex = 0; state.status = 'ready';
    state.steps = makeSteps(); updateModel(-1);
    $('step-counter').textContent = '00'; $('step-label').textContent = 'LISTO PARA EXPLORAR';
    $('step-title').textContent = scenarioInfo[state.scenario][0];
    $('step-description').textContent = scenarioInfo[state.scenario][1] + (reducedMotion ? ' Movimiento reducido activo: las marcas de recorrido permanecen quietas.' : '');
    document.querySelectorAll('.is-active').forEach(node => node.classList.remove('is-active'));
    $('timeline').replaceChildren(...state.steps.map((step, index) => {
      const button = document.createElement('button'); button.type = 'button';
      button.setAttribute('aria-label', `Etapa ${index + 1}: ${step.title}`); button.title = `Etapa ${index + 1}: ${step.title}`;
      button.addEventListener('click', () => { pause(); enterStage(index); }); return button;
    }));
    renderInfo(state.steps[0]); updateControls();
  }

  function play() {
    if (state.status === 'playing') { pause(); return; }
    if (state.status === 'complete') reset();
    if (state.index < 0) enterStage(0);
    state.status = 'playing'; state.lastTime = performance.now(); updateControls(); state.raf = requestAnimationFrame(tick);
  }

  function next() {
    if (state.status === 'complete') return;
    pause();
    if (state.index + 1 >= state.steps.length) { finish(); return; }
    enterStage(state.index + 1);
  }

  document.querySelectorAll('[data-scenario]').forEach(button => button.addEventListener('click', () => {
    state.scenario = button.dataset.scenario;
    document.querySelectorAll('[data-scenario]').forEach(item => {
      const active = item === button; item.classList.toggle('active', active); item.setAttribute('aria-pressed', String(active));
    });
    $('led-options').hidden = state.scenario !== 'led';
    $('client').disabled = !['http', 'led', 'portal', 'general'].includes(state.scenario);
    reset();
  }));
  document.querySelectorAll('[data-led]').forEach(button => button.addEventListener('click', () => {
    ledCommand = button.dataset.led;
    document.querySelectorAll('[data-led]').forEach(item => { const selected = item === button; item.classList.toggle('selected', selected); item.setAttribute('aria-pressed', String(selected)); });
    reset();
  }));
  for (const view of ['network', 'hardware']) {
    $(`tab-${view}`).addEventListener('click', () => setView(view));
    $(`tab-${view}`).addEventListener('keydown', event => {
      if (['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
        event.preventDefault(); const other = event.key === 'Home' ? 'network' : event.key === 'End' ? 'hardware' : view === 'network' ? 'hardware' : 'network';
        setView(other); $(`tab-${other}`).focus();
      }
    });
  }
  document.querySelectorAll('[data-node]').forEach(node => {
    node.addEventListener('click', () => inspectDevice(node.dataset.node));
    node.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); inspectDevice(node.dataset.node); } });
  });
  $('play').addEventListener('click', play); $('next').addEventListener('click', next); $('reset').addEventListener('click', reset);
  $('client').addEventListener('change', reset);
  $('speed').addEventListener('input', () => {
    // Contabilizar hasta este instante con el factor anterior evita saltos de posición.
    if (state.status === 'playing') updateAnimation(performance.now());
    state.speed = Number($('speed').value); $('speed-value').textContent = `${state.speed}×`;
  });
  window.addEventListener('resize', () => { retimePackets(); drawPackets(); });
  document.addEventListener('keydown', event => {
    if (event.altKey || event.ctrlKey || event.metaKey || event.target.closest('input,select,textarea,button,a,summary,[role="button"]')) return;
    if (event.key === ' ') { event.preventDefault(); play(); }
    if (event.key === 'ArrowRight') { event.preventDefault(); next(); }
    if (event.key.toLowerCase() === 'r') reset();
  });
  document.addEventListener('visibilitychange', () => { if (document.hidden && state.status === 'playing') pause(); });
  $('fullscreen').addEventListener('click', async () => {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else if (document.documentElement.requestFullscreen) await document.documentElement.requestFullscreen();
      else $('fullscreen').querySelector('span').textContent = 'Usá F11 para pantalla completa';
    } catch { $('fullscreen').querySelector('span').textContent = 'Usá F11 para pantalla completa'; }
  });
  document.addEventListener('fullscreenchange', () => { $('fullscreen').querySelector('span').textContent = document.fullscreenElement ? 'Salir de pantalla completa' : 'Pantalla completa'; retimePackets(); drawPackets(); });
  reset();
})();
