// =============================
//  LINEAS IMPORTANTES:
// =============================
// const float UMBRAL_LED = 38.0;  // (umbral temperatura API)
// const unsigned long WEATHER_INTERVAL = 30000UL; // (30 segundos) °°°°°°°°°°° 10UL * 60UL * 1000UL // (10 min)

// const float UMBRAL_PRECIP_ACTUAL = 0.5f; // Umbral de precipitación más estricto (mm)

// const unsigned long RAIN_BURST_DURATION  = 60UL * 1000UL;  // Duración de la ráfaga: 60 s

// °°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <DHT.h>
#include <Ticker.h>
#include <time.h>
#include <coredecls.h>
#include <FS.h>
#include <math.h>
#include "secrets.h"
#include "tls_certificates.h"
#include "weather_json.h"
#include "wifi_provisioning.h"
#include "http_timing_diagnostics.h"
#include "led_api_control.h"

// Diagnostico temporal: cambiar a 0 para compilar sin trazas de conexion.
#define DIAGNOSTICO_CONEXIONES 1

// =============================
// Brillo del LED de lluvia (Core 3.1.2: 0 = apagado, 255 = máximo)
// =============================
const uint16_t BRILLO_LLUVIA = 15; 
const uint16_t BRILLO_LLUVIA_MANUAL = 64; // Core 3.1.2: PWM 0..255 (~25%).


// =============================
// CONSTANTES PARA REGULAR TEMPERATURA Y HUMEDAD
// =============================
const float OFFSET_TEMP    = 0;  // en °C, ajustar para igualar al otro sensor
const float OFFSET_HUM     = -12.8;  // en %, ajustar para igualar al otro sensor


// =============================
// CONTANTES PARA PROBAR LED DE LLUVIA
// =============================
const bool MODO_PRUEBA_LLUVIA = false;   // <-- true para forzar lluvia


// =============================
// PATRÓN LED LLUVIA:
// - Si hay PRONÓSTICO de lluvia (lluviaPronosticoApi == true):
//     * Cada 5 minutos se abre una ventana de 60 s
//     * Dentro de esos 60 s parpadea cada 500 ms (ON/OFF)
// =============================
const unsigned long RAIN_BLINK_INTERVAL  = 500UL;              // Parpadeo interno: 500 ms
const unsigned long RAIN_BURST_DURATION  = 60UL * 1000UL;      // Duración de la ráfaga: 60 s
const unsigned long RAIN_BURST_PERIOD    = 5UL * 60UL * 1000UL;// Cada 5 min se inicia una ráfaga


// =============================
// CONFIG WIFI
// =============================
// Credenciales cargadas desde secrets.h (archivo local ignorado por Git)

// =============================
// CONFIG DHT22
// =============================
#define SENSOR D5
#define DHTTYPE DHT22
DHT dht(SENSOR, DHTTYPE);

// Declaraciones adelantadas de funciones de control de LED de lluvia
void actualizarLedRain(unsigned long ahora);
void actualizarPatronLluvia();

// Variables globales (ultima lectura valida con offsets)
float TEMPERATURA = 0;
float HUMEDAD     = 0;

// Variables filtradas (suavizadas)
float tempFiltrada = NAN;
float humFiltrada  = NAN;
const float ALPHA_FILTRO = 0.5;   // 0.1 = más suave, 0.5 = más rápido

float tempMax = -100, tempMin = 200;
float humMax  = 0,    humMin  = 100;

unsigned long lastRead = 0;

// =============================
// INTERVALOS DE TIEMPO
// =============================
const unsigned long READ_INTERVAL_MS      = 2500UL;   // 2,5 segundos (DHT: minimo 2 s)
const unsigned long HIST_MINUTE_INTERVAL  = 60000UL;  // 1 minuto
const unsigned long CSV_SAVE_INTERVAL_MS  = 300000UL; // 5 minutos

// =============================
// HISTORIAL 1 HORA (60 puntos)
// =============================
const int HOUR_POINTS = 60;
float histTemp[HOUR_POINTS];
float histHum[HOUR_POINTS];
int   histCount = 0;  // cantidad de muestras válidas
int   histIndex = 0;  // índice circular

unsigned long lastHistMinute = 0;   // para el gráfico (1 minuto)
unsigned long lastCsvSave    = 0;   // para el archivo CSV (5 minutos)

// =============================
// ACUMULADORES ESTILO ESTACIÓN METEO
// =============================

// Para promedio de 1 minuto (gráfico 1h)
float minTempSum = 0;
float minHumSum  = 0;
uint16_t minCount = 0;

// Para promedio de 5 minutos (CSV)
float csvTempSum = 0;
float csvHumSum  = 0;
uint16_t csvCount = 0;

// =============================
// LED + WeatherAPI
// =============================
const uint8_t LED_API  = D6;  // LED temp API
const uint8_t LED_RAIN = D7;  // LED lluvia API
void cambiarControlLluvia(bool manual, bool on);
bool estadoLedLluvia();
LedApiControl ledApi(LED_API, cambiarControlLluvia, estadoLedLluvia);

float tempApi   = NAN;   // última temperatura desde la API
float humApi    = NAN;
time_t weatherLastSuccessTime = 0;
unsigned long weatherLastSuccessMillis = 0;
bool weatherLastAttemptOk = false;
bool  lluviaApi = false; // lluvia AHORA (estado actual)
bool  lluviaPronosticoApi = false; // lluvia PRONOSTICADA (próximas horas)
bool  rainWindowActive = false;     // indica si estamos dentro de la ráfaga de parpadeo
bool  rainPatternEnabled = false;    // indica si el patrón de pronóstico está habilitado

// Tickers para patrón de lluvia
Ticker rainBlinkTicker;      // parpadeo cada 500 ms durante la ventana
Ticker rainWindowEndTicker;  // fin de ventana (60 s)
Ticker rainBurstTicker;      // programa la próxima ventana (cada 5 min)

// Estado para el patrón de ráfagas de lluvia
unsigned long lastRainBurst   = 0;   // Cuándo empezó la última ráfaga de 30 s
unsigned long lastBlinkToggle = 0;   // Último cambio ON/OFF dentro de la ráfaga
bool          rainLedState    = false;

unsigned long lastWeatherCheck = 0;
const unsigned long WEATHER_INTERVAL = 10UL * 60UL * 1000UL; // cada 10 minutos

// WEATHER_API_KEY se carga desde secrets.h
const char* WEATHER_CITY    = "Corrientes,Argentina";


// =============================
// TELEGRAM CONFIG
// =============================
// TELEGRAM_TOKEN y CHAT_ID se cargan desde secrets.h

const float ALERTA_TEMP = 35.0;              // UMBRAL DE ALERTA
unsigned long lastAlert = 0;                 // tiempo del último aviso
const unsigned long ALERT_INTERVAL = 300000; // 5 MINUTOS

WiFiServer server(80);
WifiProvisioning wifiProvisioning;
bool serviciosRedIniciados = false;
// Diagnostico temporal: eventos STA sin SSID, claves ni otros datos sensibles.
WiFiEventHandler wifiAsociadaDiag;
WiFiEventHandler wifiDesconectadaDiag;
WiFiEventHandler wifiIpv4Diag;
WiFiEventHandler wifiDhcpDiag;

// TLS se ejecuta de a una conexion y solo cuando no hay un cliente HTTP pendiente.
const unsigned long EXTERNAL_RETRY_MS = 30000UL;
const uint16_t EXTERNAL_IO_TIMEOUT_MS = 5000;
const time_t MIN_TLS_TIME = 1704067200; // 01/01/2024 UTC: control de plausibilidad
bool horaSincronizada = false;
bool avisoInicioPendiente = true;
bool climaInicialPendiente = true;
String alertaPendiente;
bool intentoTelegramRealizado = false;
bool intentoWeatherRealizado = false;
unsigned long ultimoIntentoTelegram = 0;
unsigned long ultimoIntentoWeather = 0;
unsigned long ultimoIntentoWifi = 0;

bool horaValidaParaTLS() {
  return horaSincronizada && time(nullptr) >= MIN_TLS_TIME;
}

bool puedeIniciarTLS() {
  if (!serviciosRedIniciados || wifiProvisioning.portalActive()) return false;
  if (WiFi.status() != WL_CONNECTED || !horaValidaParaTLS()) return false;
  // RX de 16 KiB evita depender de MFLN. Reservar margen para TLS y las estructuras JSON.
  return ESP.getFreeHeap() >= 35000 && ESP.getMaxFreeBlockSize() >= 17000;
}

void configurarTLS(WiFiClientSecure &client, const BearSSL::X509List &ca) {
  client.setTrustAnchors(&ca);
  client.setSSLVersion(BR_TLS12, BR_TLS12);
  client.setX509Time(time(nullptr));
  client.setTimeout(EXTERNAL_IO_TIMEOUT_MS);
  client.setBufferSizes(16384, 512);
}

#include "connection_diagnostics.h"

// =============================
// LOG EN SPIFFS (HASTA ~1 MES)
// =============================
const char* LOG_FILE = "/historial.csv";
const unsigned long MAX_LOG_LINES = 8640UL; // 24 × 60 / 5 * 30
bool spiffsOk = false;
unsigned long logLines = 0;
char primeraFechaCSV[11] = "";
char ultimaFechaCSV[11] = "";

void reiniciarFechasCSV() {
  primeraFechaCSV[0] = '\0';
  ultimaFechaCSV[0] = '\0';
}

void registrarFechaCSV(const String &linea) {
  if (linea.length() <= 5) return;
  if (primeraFechaCSV[0] == '\0') {
    linea.toCharArray(primeraFechaCSV, sizeof(primeraFechaCSV));
  }
  linea.toCharArray(ultimaFechaCSV, sizeof(ultimaFechaCSV));
}

// =============================
// HORA NTP (ARGENTINA GMT-3)
// =============================
String obtenerFechaHora() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  if (!timeinfo) return "Sin hora";

  char buffer[32];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          timeinfo->tm_mday,
          timeinfo->tm_mon + 1,
          timeinfo->tm_year + 1900,
          timeinfo->tm_hour,
          timeinfo->tm_min,
          timeinfo->tm_sec);
  return String(buffer);
}

//-------------------------------------------------------------------------------------------------

// =============================
// ENCODER URL PARA TELEGRAM
// =============================
String urlEncode(const String &text) {
  String encoded = "";
  char c;
  char buf[4];

  for (int i = 0; i < text.length(); i++) {
    c = text.charAt(i);
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9')) {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

//-------------------------------------------------------------------------------------------------

// =============================
// ENVIAR MENSAJE TELEGRAM
// =============================
bool enviarTelegram(const String &mensaje) {
  DIAG_MEMORIA(ServicioDiag::Telegram);
  if (!puedeIniciarTLS()) {
    DIAG_APLAZAMIENTO_TLS(ServicioDiag::Telegram);
    return false;
  }
  DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Habilitada,
              F("HABILITADA"), F("WiFi, hora y memoria aptos"));
  // La CA debe vivir tanto como el cliente que la utiliza.
  BearSSL::X509List ca(TELEGRAM_ROOT_CA);
  WiFiClientSecure client;
  configurarTLS(client, ca);

  Serial.println("📡 Conectando a Telegram...");

  unsigned long inicioConexion = millis();
  DIAG_MARCAR_INTENTO_TG();
  DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Intento,
              F("INTENTO"), F("DNS/TCP/TLS iniciado"));
  if (!client.connect("api.telegram.org", 443)) {
    DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::ErrorConexion,
                F("ERROR_CONEXION"), F("DNS/TCP/TLS fallido"));
    Serial.printf("[Telegram] Conexion TLS rechazada/fallida (codigo %d).\n",
                  client.getLastSSLError());
    return false;
  }
  DIAG_TLS_CONECTADA(ServicioDiag::Telegram, inicioConexion);

  String url = "/bot" + TELEGRAM_TOKEN +
               "/sendMessage?chat_id=" + CHAT_ID +
               "&text=" + urlEncode(mensaje);

  client.print(
    String("GET ") + url + " HTTP/1.1\r\n" +
    "Host: api.telegram.org\r\n" +
    "User-Agent: ESP8266\r\n" +
    "Connection: close\r\n\r\n"
  );

  Serial.println("📨 Enviando mensaje a Telegram...");

  // Limitar longitud y tiempo total; no registrar respuestas remotas con posibles secretos.
  String line;
  unsigned long inicioRespuesta = millis();
  while (millis() - inicioRespuesta < EXTERNAL_IO_TIMEOUT_MS && line.length() < 96) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') break;
      line += c;
    } else if (!client.connected()) {
      break;
    }
    delay(1);
  }
  bool enviado = line.startsWith("HTTP/1.1 200");
  if (enviado) {
    DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Exito,
                F("EXITO"), F("respuesta HTTP 200"));
    Serial.println("[Telegram] Respuesta HTTP 200 recibida.");
  } else {
    DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::ErrorRespuesta,
                F("ERROR_RESPUESTA"), F("sin confirmacion HTTP 200"));
    Serial.println("[Telegram] Sin confirmacion HTTP 200; se reintentara.");
  }

  client.stop();
  return enviado;
}

// =============================
// FILTRO EXPONENCIAL SUAVIZADO
// =============================
void actualizarFiltro() {
  if (isnan(tempFiltrada) || isnan(humFiltrada)) {
    tempFiltrada = TEMPERATURA;
    humFiltrada  = HUMEDAD;
  } else {
    tempFiltrada = ALPHA_FILTRO * TEMPERATURA + (1.0 - ALPHA_FILTRO) * tempFiltrada;
    humFiltrada  = ALPHA_FILTRO * HUMEDAD     + (1.0 - ALPHA_FILTRO) * humFiltrada;
  }
}

//-------------------------------------------------------------------------------------------------

// =============================
// LECTURA DEL DHT22
// =============================
bool leerDHT() {
  // DHT conserva la trama durante 2 s: ambos valores pertenecen a una sola lectura.
  const float t = dht.readTemperature();
  const float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return false;

  TEMPERATURA = t;
  HUMEDAD     = h;

  TEMPERATURA += OFFSET_TEMP;
  HUMEDAD     += OFFSET_HUM;

  if (HUMEDAD < 0)   HUMEDAD = 0;
  if (HUMEDAD > 100) HUMEDAD = 100;

  actualizarFiltro();

  float tUso = isnan(tempFiltrada) ? TEMPERATURA : tempFiltrada;
  float hUso = isnan(humFiltrada)  ? HUMEDAD     : humFiltrada;

  if (tUso > tempMax) tempMax = tUso;
  if (tUso < tempMin) tempMin = tUso;

  if (hUso > humMax) humMax = hUso;
  if (hUso < humMin) humMin = hUso;

  return true;
}

//-------------------------------------------------------------------------------------------------

// =============================
// INICIALIZAR LOG EN SPIFFS
// =============================
void iniciarLog() {
  reiniciarFechasCSV();
  if (!SPIFFS.exists(LOG_FILE)) {
    File f = SPIFFS.open(LOG_FILE, "w");
    if (f) {
      f.println("FechaHora,Temperatura,Hum.edad");
      f.close();
    }
    logLines = 0;
    Serial.println("Log creado nuevo.");
    return;
  }

  File f = SPIFFS.open(LOG_FILE, "r");
  if (!f) {
    logLines = 0;
    Serial.println("No se pudo abrir log existente.");
    return;
  }

  bool first = true;
  logLines = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (first) {
      first = false;
      continue;
    }
    if (line.length() > 1) logLines++;
    registrarFechaCSV(line);
  }

  f.close();
  Serial.printf("Log existente con %lu lineas.\n", logLines);
}

//-------------------------------------------------------------------------------------------------

// =============================
// AGREGAR REGISTRO AL LOG
// =============================
void agregarRegistroLog(const String &fechaHora, float t, float h) {
  if (!spiffsOk) return;

  File f = SPIFFS.open(LOG_FILE, "a");
  if (!f) {
    Serial.println("No se pudo abrir log para escribir.");
    return;
  }

  const size_t bytesEscritos = f.printf("%s,%.1f,%.0f\r\n", fechaHora.c_str(), t, h);
  const bool escrito = bytesEscritos > 0 && f.getWriteError() == 0;
  f.close();
  if (!escrito) return;

  logLines++;
  registrarFechaCSV(fechaHora);

  if (logLines >= MAX_LOG_LINES) {
    Serial.println("Limite de 1 mes alcanzado, reiniciando log...");
    bool reiniciado = SPIFFS.remove(LOG_FILE);
    File nf = SPIFFS.open(LOG_FILE, "w");
    if (nf) {
      nf.println("FechaHora,Temperatura,Hum.edad");
      nf.close();
      reiniciado = true;
    }
    if (reiniciado) {
      logLines = 0;
      reiniciarFechasCSV();
    }
  }
}

//-------------------------------------------------------------------------------------------------

// =============================
// GUARDAR MUESTRA EN HISTORIAL
// =============================
void actualizarHistorial(float t, float h) {
  unsigned long ahora = millis();

  minTempSum += t;
  minHumSum  += h;
  minCount++;

  if (ahora - lastHistMinute >= HIST_MINUTE_INTERVAL) {
    lastHistMinute = ahora;

    if (minCount > 0) {
      float tMin = minTempSum / minCount;
      float hMin = minHumSum  / minCount;

      histTemp[histIndex] = tMin;
      histHum[histIndex]  = hMin;

      histIndex = (histIndex + 1) % HOUR_POINTS;
      if (histCount < HOUR_POINTS) histCount++;

      csvTempSum += tMin;
      csvHumSum  += hMin;
      csvCount++;

      minTempSum = 0;
      minHumSum  = 0;
      minCount   = 0;
    }
  }

  if (ahora - lastCsvSave >= CSV_SAVE_INTERVAL_MS) {
    lastCsvSave = ahora;

    if (csvCount > 0) {
      float tCsv = csvTempSum / csvCount;
      float hCsv = csvHumSum  / csvCount;

      String fecha = obtenerFechaHora();
      agregarRegistroLog(fecha, tCsv, hCsv);

      csvTempSum = 0;
      csvHumSum  = 0;
      csvCount   = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------

// ==========================================================
//                    CÁLCULO DE FASE LUNAR
// ==========================================================
double edadLunar() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  int d = t->tm_mday;
  int m = t->tm_mon + 1;
  int y = t->tm_year + 1900;

  if (m < 3) {
    y--;
    m += 12;
  }

  long a = y / 100;
  long b = a / 4;
  long c = 2 - a + b;
  long e = (long)(365.25 * (y + 4716));
  long f = (long)(30.6001 * (m + 1));

  long jd = c + d + e + f - 1524.5;
  double daysSinceNew = jd - 2451549.5;

  double age = fmod(daysSinceNew, 29.53058867);
  if (age < 0) age += 29.53058867;

  return age;
}

String fechaFutura(double dias) {
  time_t now = time(nullptr);
  now += (long)(dias * 86400);

  struct tm* t = localtime(&now);

  char buf[20];
  sprintf(buf, "%02d/%02d/%04d",
          t->tm_mday,
          t->tm_mon + 1,
          t->tm_year + 1900);

  return String(buf);
}

void calcularLunas(String &proximaNueva, String &proximaLlena) {
  double edad  = edadLunar();
  double ciclo = 29.53058867;

  double dn = ciclo - edad;
  double dl = 14.765 - edad;

  if (dl < 0) dl += ciclo;

  proximaNueva = fechaFutura(dn);
  proximaLlena = fechaFutura(dl);
}

int faseLunar() {
  double age  = edadLunar();
  double frac = age / 29.53058867;

  if (frac < 0.0625) return 0;
  if (frac < 0.1875) return 1;
  if (frac < 0.3125) return 2;
  if (frac < 0.4375) return 3;
  if (frac < 0.5625) return 4;
  if (frac < 0.6875) return 5;
  if (frac < 0.8125) return 6;
  if (frac < 0.9375) return 7;
  return 0;
}

String faseNombre(int f) {
  switch (f) {
    case 0: return "Luna nueva";
    case 1: return "Creciente cóncava";
    case 2: return "Cuarto creciente";
    case 3: return "Gibosa creciente";
    case 4: return "Luna llena";
    case 5: return "Gibosa menguante";
    case 6: return "Cuarto menguante";
    case 7: return "Menguante cóncava";
  }
  return "Desconocida";
}

String faseIcono(int f) {
  switch (f) {
    case 0: return "🌑";
    case 1: return "🌘";
    case 2: return "🌗";
    case 3: return "🌖";
    case 4: return "🌕";
    case 5: return "🌔";
    case 6: return "🌓";
    case 7: return "🌒";
  }
  return "🌑";
}

//-------------------------------------------------------------------------------------------------

// =============================
// -----------------------------
// Ticker callbacks para patrón de lluvia
// -----------------------------
void IRAM_ATTR toggleRainLed() {
  if (ledApi.manual() || !rainWindowActive) return;
  rainLedState = !rainLedState;
  analogWrite(LED_RAIN, rainLedState ? BRILLO_LLUVIA : 0);
}

void stopRainWindow() {
  if (ledApi.manual()) return;
  rainWindowActive = false;
  rainLedState = false;
  rainBlinkTicker.detach();
  analogWrite(LED_RAIN, 0);
}

void startRainWindow() {
  if (ledApi.manual() || !rainPatternEnabled) return;
  rainWindowActive = true;
  rainLedState = false;
  analogWrite(LED_RAIN, 0);
  lastRainBurst = millis();

  rainBlinkTicker.detach();
  rainBlinkTicker.attach_ms(RAIN_BLINK_INTERVAL, toggleRainLed);

  rainWindowEndTicker.detach();
  rainWindowEndTicker.once_ms(RAIN_BURST_DURATION, stopRainWindow);
}

void disableRainPattern() {
  if (ledApi.manual()) return;
  rainPatternEnabled = false;
  rainBurstTicker.detach();
  rainWindowEndTicker.detach();
  rainBlinkTicker.detach();
  stopRainWindow();
}

void enableRainPattern() {
  if (ledApi.manual()) return;
  rainPatternEnabled = true;
  startRainWindow();            // arranca inmediatamente la primera ráfaga
  rainBurstTicker.detach();
  rainBurstTicker.attach_ms(RAIN_BURST_PERIOD, startRainWindow);
}

// Controla el patrón según lluvia actual / pronóstico / modo prueba
void actualizarPatronLluvia() {
  if (ledApi.manual()) return;
  if (MODO_PRUEBA_LLUVIA) {
    disableRainPattern();
    rainLedState = true;
    analogWrite(LED_RAIN, BRILLO_LLUVIA);
    return;
  }

  // Lluvia actual: LED fijo encendido, sin parpadeo
  if (lluviaApi) {
    disableRainPattern();
    rainLedState = true;
    analogWrite(LED_RAIN, BRILLO_LLUVIA);
    return;
  }

  // Pronóstico de lluvia: patrón con ráfagas de 60 s cada 5 min
  if (lluviaPronosticoApi) {
    if (!rainPatternEnabled) {
      enableRainPattern();
    }
    return;
  }

  // Sin lluvia ni pronóstico: todo apagado
  disableRainPattern();
}

// Mantener compatibilidad con llamadas existentes
void actualizarLedRain(unsigned long /*ahora*/) {
  actualizarPatronLluvia();
}

// Transiciones llamadas desde loop(), sin espera ni escritura en flash.
void cambiarControlLluvia(bool manual, bool on) {
  rainBurstTicker.detach();
  rainWindowEndTicker.detach();
  rainBlinkTicker.detach();
  rainPatternEnabled = false;
  rainWindowActive = false;
  rainLedState = false;
  if (manual) {
    rainLedState = on;
    analogWrite(LED_RAIN, on ? BRILLO_LLUVIA_MANUAL : 0);
  } else {
    actualizarPatronLluvia();
  }
}

bool estadoLedLluvia() {
  return rainLedState;
}

//-------------------------------------------------------------------------------------------------

// =============================
// WeatherAPI → temperatura + lluvia → LEDs
// =============================
struct WeatherJsonClock {
  static uint32_t now() { return millis(); }
  static void idle() { delay(1); }
};

bool actualizarClimaApi() {
  weatherLastAttemptOk = false;
  DIAG_MEMORIA(ServicioDiag::WeatherAPI);
  if (!puedeIniciarTLS()) {
    DIAG_APLAZAMIENTO_TLS(ServicioDiag::WeatherAPI);
    return false;
  }
  DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Habilitada,
              F("HABILITADA"), F("WiFi, hora y memoria aptos"));

  BearSSL::X509List ca(WEATHER_ROOT_CA);
  WiFiClientSecure client;
  configurarTLS(client, ca);
  HTTPClient http;

  String url = "https://api.weatherapi.com/v1/forecast.json?key=";
  url += WEATHER_API_KEY;
  url += "&q=";
  url += WEATHER_CITY;
  url += "&lang=es&aqi=no&days=1";  // 1 día de pronóstico

  Serial.println("[API] Consultando WeatherAPI (forecast + current)...");

  // IMPORTANTE: evitar chunked y problemas raros
  http.useHTTP10(true);
  http.setTimeout(EXTERNAL_IO_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::ErrorRespuesta,
                F("ERROR_PREPARACION"), F("http.begin fallido; sin conexion"));
    Serial.println("[API] Error en http.begin()");
    return false;
  }

  unsigned long inicioConexion = millis();
  DIAG_MARCAR_INTENTO_WEATHER();
  DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Intento,
              F("INTENTO"), F("HTTP GET HTTPS iniciado; DNS/TCP/TLS"));
  int httpCode = http.GET();
  DIAG_CABECERAS_WEATHER(httpCode, inicioConexion);
  Serial.printf("[API] httpCode = %d\n", httpCode);

  if (httpCode != HTTP_CODE_OK) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI,
                httpCode < 0 ? EstadoDiag::ErrorConexion : EstadoDiag::ErrorRespuesta,
                httpCode < 0 ? F("ERROR_CONEXION") : F("ERROR_RESPUESTA"),
                F("HTTP GET sin codigo 200"));
    Serial.printf("[API] Error HTTP: %d\n", httpCode);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::ErrorRespuesta,
                F("ERROR_RESPUESTA"), F("flujo HTTP no disponible"));
    http.end();
    return false;
  }

  WeatherReadings readings;
  size_t bytesRead = 0;
  WeatherJsonStatus status = readWeatherResponse<WiFiClient, WeatherJsonClock>(
      *stream, http.getSize(), 3000, readings, bytesRead);
  http.end();
  Serial.printf("[API] bytes procesados = %u\n", static_cast<unsigned>(bytesRead));
  if (status != WeatherJsonStatus::Ok) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::ErrorRespuesta,
                F("ERROR_RESPUESTA"), F("JSON incompleto o invalido"));
    Serial.printf("[API] Respuesta descartada (estado JSON %u); se conserva el clima anterior.\n",
                  static_cast<unsigned>(status));
    return false;
  }

  // Publicar el conjunto solo cuando el cuerpo completo y los cinco campos son validos.
  const float UMBRAL_PRECIP_ACTUAL = 0.85f;
  const int   UMBRAL_CHANCE_PRONOSTICO = 75;
  const float UMBRAL_PRECIP_PRONOSTICO = 0.4f;
  tempApi = readings.temperature;
  humApi = readings.humidity;
  weatherLastSuccessTime = time(nullptr);
  weatherLastSuccessMillis = millis();
  weatherLastAttemptOk = true;
  lluviaApi = readings.currentPrecip > UMBRAL_PRECIP_ACTUAL;
  lluviaPronosticoApi = readings.willRain == 1 &&
      (readings.rainChance >= UMBRAL_CHANCE_PRONOSTICO ||
       readings.dailyPrecip >= UMBRAL_PRECIP_PRONOSTICO);

  Serial.printf("[API] Temp API = %.1f C\n", tempApi);
  Serial.printf("[API] Precip_mm (actual) = %.2f\n", readings.currentPrecip);
  Serial.printf("[API] daily_will_it_rain = %d\n", readings.willRain);
  Serial.printf("[API] daily_chance_of_rain = %d%%\n", readings.rainChance);
  Serial.printf("[API] totalprecip_mm (dia) = %.2f\n", readings.dailyPrecip);
  Serial.printf("[API] lluvia actual = %s | lluvia pronosticada = %s\n",
                lluviaApi ? "SI" : "NO", lluviaPronosticoApi ? "SI" : "NO");
  actualizarPatronLluvia();

  // --------- LED DE TEMPERATURA ----------
  ledApi.weather(tempApi);
  DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Exito,
              F("EXITO"), F("HTTP 200 y JSON valido"));
  return true;
}

// ============================= Fin de actualizarClimaApi() ======================================

// Sin espera de NTP en loop(): el callback del core habilita TLS cuando llega la hora.
// Cada llamada ejecuta como maximo una operacion externa, sin conexiones TLS simultaneas.
void atenderServiciosExternos() {
  if (!serviciosRedIniciados || wifiProvisioning.portalActive()) return;
  unsigned long ahora = millis();
  bool telegramPendiente = avisoInicioPendiente || alertaPendiente.length() > 0;
  bool weatherPendiente = climaInicialPendiente || ahora - lastWeatherCheck >= WEATHER_INTERVAL;
  DIAG_WEATHER_PERIODICO(ahora);
  if (WiFi.status() != WL_CONNECTED) {
    if (telegramPendiente) {
      DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Wifi,
                  F("APLAZADA"), F("WiFi desconectado"));
    }
    if (weatherPendiente) {
      DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Wifi,
                  F("APLAZADA"), F("WiFi desconectado"));
    }
    if (ahora - ultimoIntentoWifi >= EXTERNAL_RETRY_MS) {
      ultimoIntentoWifi = ahora;
      DIAG_RECONEXION_WIFI();
      WiFi.reconnect();
    }
    return;
  }
  if (!horaValidaParaTLS()) {
    if (telegramPendiente) {
      DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Ntp,
                  F("APLAZADA"), F("hora NTP no valida"));
    }
    if (weatherPendiente) {
      DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Ntp,
                  F("APLAZADA"), F("hora NTP no valida"));
    }
    return;
  }

  if (telegramPendiente && intentoTelegramRealizado &&
      ahora - ultimoIntentoTelegram < EXTERNAL_RETRY_MS) {
    DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Reintento,
                F("APLAZADA"), F("esperando intervalo de reintento 30000 ms"));
  }

  if (telegramPendiente &&
      (!intentoTelegramRealizado || ahora - ultimoIntentoTelegram >= EXTERNAL_RETRY_MS)) {
    if (weatherPendiente && !rainWindowActive &&
        (!intentoWeatherRealizado || ahora - ultimoIntentoWeather >= EXTERNAL_RETRY_MS)) {
      DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Prioridad,
                  F("APLAZADA"), F("turno de Telegram"));
    }
    intentoTelegramRealizado = true;
    ultimoIntentoTelegram = ahora;
    DIAG_REINICIAR_INTENTO_TG();
    unsigned long inicioOperacion = millis();
    bool enviado = false;
    if (avisoInicioPendiente) {
      enviado = enviarTelegram("🤖 Sistema iniciado correctamente.\nIP: " + WiFi.localIP().toString());
      if (enviado) {
        avisoInicioPendiente = false;
      }
    } else {
      enviado = enviarTelegram(alertaPendiente);
      if (enviado) alertaPendiente = "";
    }
    DIAG_FIN(ServicioDiag::Telegram, inicioOperacion, enviado, DIAG_HUBO_INTENTO_TG());
    if (!enviado) {
      DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Pendiente,
                  F("PENDIENTE"), F("reintento de Telegram"));
    }
    return;
  }

  if (weatherPendiente && rainWindowActive) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Lluvia,
                F("APLAZADA"), F("ventana del LED de lluvia activa"));
  } else if (weatherPendiente && intentoWeatherRealizado &&
             ahora - ultimoIntentoWeather < EXTERNAL_RETRY_MS) {
    DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Reintento,
                F("APLAZADA"), F("esperando intervalo de reintento 30000 ms"));
  }
  if (!rainWindowActive &&
      (climaInicialPendiente || ahora - lastWeatherCheck >= WEATHER_INTERVAL) &&
      (!intentoWeatherRealizado || ahora - ultimoIntentoWeather >= EXTERNAL_RETRY_MS)) {
    intentoWeatherRealizado = true;
    ultimoIntentoWeather = ahora;
    lastWeatherCheck = ahora;
    DIAG_REARMAR_WEATHER();
    DIAG_REINICIAR_INTENTO_WEATHER();
    unsigned long inicioOperacion = millis();
    bool actualizado = actualizarClimaApi();
    if (actualizado) climaInicialPendiente = false;
    DIAG_FIN(ServicioDiag::WeatherAPI, inicioOperacion, actualizado,
             DIAG_HUBO_INTENTO_WEATHER());
    if (!actualizado) {
      DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Pendiente,
                  F("PENDIENTE"), climaInicialPendiente ?
                  F("reintento inicial") : F("siguiente consulta periodica"));
    }
  }
}

// =============================
// CSV nombre e info
// =============================
String obtenerNombreCSV() {
  PERF_CSV_BEGIN(F("CSV_NOMBRE"));
  if (!SPIFFS.exists(LOG_FILE)) return "historial_vacio.csv";

  File f = SPIFFS.open(LOG_FILE, "r");
  if (!f) return "historial_error.csv";
  PERF_CSV_OPEN(f.size());

  String linea;
  String primeraFecha = "";
  String ultimaFecha  = "";

  bool esCabecera = true;

  while (f.available()) {
    linea = f.readStringUntil('\n');
    PERF_CSV_LINE();

    if (esCabecera) {
      esCabecera = false;
      continue;
    }

    if (linea.length() > 5) {
      if (primeraFecha == "") {
        primeraFecha = linea.substring(0, 10);
      }
      ultimaFecha = linea.substring(0, 10);
    }
  }

  f.close();

  if (primeraFecha == "" || ultimaFecha == "")
    return "historial_sin_datos.csv";

  auto normalizar = [](String f) {
    String d = f.substring(0, 2);
    String m = f.substring(3, 5);
    String y = f.substring(6, 10);
    return y + m + d;
  };

  String f1 = normalizar(primeraFecha);
  String f2 = normalizar(ultimaFecha);

  return "historial_" + f1 + "_" + f2 + ".csv";
}

void obtenerFechasCSV(String &primera, String &ultima) {
  PERF_CSV_BEGIN(F("CSV_FECHAS"));
  primera = primeraFechaCSV;
  ultima = ultimaFechaCSV;
}

//-------------------------------------------------------------------------------------------------

// =============================
// SETUP
// =============================
void setup() {
  Serial.begin(115200);
  WifiProvisioning::memory(F("ARRANQUE"));
  dht.begin();

  pinMode(LED_API, OUTPUT);
  pinMode(LED_RAIN, OUTPUT);   // NUEVO
  ledApi.begin();

  DIAG_ESTADO(ServicioDiag::Telegram, EstadoDiag::Pendiente,
              F("PENDIENTE"), F("aviso de inicio"));
  DIAG_ESTADO(ServicioDiag::WeatherAPI, EstadoDiag::Pendiente,
              F("PENDIENTE"), F("primera consulta"));
  wifiAsociadaDiag = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.println(F(" ms] STA_ASOCIADA"));
  });
  wifiDesconectadaDiag = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    Serial.print(F("[WM +")); Serial.print(millis());
    Serial.print(F(" ms] STA_DESCONECTADA motivo="));
    Serial.println(static_cast<int>(event.reason));
  });
  wifiIpv4Diag = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.println(F(" ms] STA_IPV4_OBTENIDA"));
  });
  wifiDhcpDiag = WiFi.onStationModeDHCPTimeout([]() {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.println(F(" ms] STA_DHCP_TIMEOUT"));
  });
  DIAG_INICIO_WIFI();
  wifiProvisioning.begin(WIFI_CONFIG_AP_PASSWORD, ssid, password);
  ultimoIntentoWifi = millis();

  settimeofday_cb([](bool desdeSntp) {
    if (desdeSntp) {
      horaSincronizada = true;
      DIAG_MARCAR_NTP();
    }
  });
  for (int i = 0; i < HOUR_POINTS; i++) {
    histTemp[i] = NAN;
    histHum[i]  = NAN;
  }

  if (SPIFFS.begin()) {
    spiffsOk = true;
    Serial.println("SPIFFS montado correctamente.");
    iniciarLog();
  } else {
    spiffsOk = false;
    Serial.println("❌ Error montando SPIFFS.");
  }

  // HTTP y NTP se inician desde loop() al obtener IPv4 y cerrar el portal.

  // La notificacion de inicio y la primera consulta se atienden desde loop().

  // Inicializa el patrón de lluvia (apagado por defecto)
  actualizarPatronLluvia();
}

//-------------------------------------------------------------------------------------------------

// =============================
// LOOP PRINCIPAL
// =============================
void imprimirClimaExterior(Print &output) {
  bool valid = weatherLastSuccessTime > 0 && isfinite(tempApi) && isfinite(humApi);
  bool stale = valid && (!weatherLastAttemptOk ||
      millis() - weatherLastSuccessMillis > WEATHER_INTERVAL);
  char updated[20] = {};
  if (valid) {
    struct tm *date = localtime(&weatherLastSuccessTime);
    if (date) strftime(updated, sizeof(updated), "%d/%m/%Y %H:%M:%S", date);
  }
  output.print(",\"weather\":{\"valid\":");
  output.print(valid ? "true" : "false");
  output.print(",\"temp\":");
  if (valid) output.print(tempApi, 1); else output.print("null");
  output.print(",\"hum\":");
  if (valid) output.print(humApi, 0); else output.print("null");
  output.print(",\"updated\":");
  if (updated[0]) {
    output.print('"');
    output.print(updated);
    output.print('"');
  } else {
    output.print("null");
  }
  output.print(",\"stale\":");
  output.print(stale ? "true}" : "false}");
}

void loop() {
  wifiProvisioning.process();
  if (!serviciosRedIniciados && wifiProvisioning.ready()) {
    // WiFiManager ya fue destruido: unico servidor en TCP 80.
    server.begin();
    serviciosRedIniciados = true;
    Serial.print(F("[WM] HTTP listo; IPv4="));
    Serial.println(WiFi.localIP());
    DIAG_INICIO_NTP();
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println(F("[NTP] TLS pendiente hasta recibir una hora valida; sin espera bloqueante."));
  }
  unsigned long ahora = millis();
  DIAG_OBSERVAR_NTP();
  DIAG_OBSERVAR_WIFI();

// 1) SIEMPRE: actualizar LED de lluvia primero (rápido, sin bloqueos)
  actualizarLedRain(ahora);

// 2) Lectura DHT + alerta
  if (ahora - lastRead > READ_INTERVAL_MS) {
    lastRead = ahora;
    PERF_DHT_ATTEMPT();

    if (leerDHT()) {
      float tUso = isnan(tempFiltrada) ? TEMPERATURA : tempFiltrada;
      float hUso = isnan(humFiltrada)  ? HUMEDAD     : humFiltrada;

      Serial.printf("Temp (filtrada): %.1f C | Hum (filtrada): %.0f %%\n", tUso, hUso);

      actualizarHistorial(tUso, hUso);

      if (tUso >= ALERTA_TEMP &&
          (lastAlert == 0 || (ahora - lastAlert > ALERT_INTERVAL))) {

        lastAlert = ahora;

        String alerta = "ALERTA: Temperatura alta!\n";
        alerta += "🌡 Temp: " + String(tUso) + " °C\n";
        alerta += "💧 Hum: " + String(hUso, 0) + " %\n";
        alerta += "🕒 " + obtenerFechaHora();

        // Conservar el aviso hasta poder enviarlo con WiFi, hora y TLS validos.
        if (alertaPendiente.length() == 0) {
          DIAG_EVENTO(ServicioDiag::Telegram, F("PENDIENTE"),
                      F("alerta de temperatura"));
        }
        alertaPendiente = alerta;
      }
    }
  }

//-------------------------------------------------------------------------------------------------
  // =============================
  // SERVIDOR WEB
  // =============================
  if (!serviciosRedIniciados || wifiProvisioning.portalActive()) return;
  WiFiClient client = server.available();
  if (!client) {
    atenderServiciosExternos();
    return;
  }
  PERF_HTTP_REQUEST();
  DIAG_CLIENTE_HTTP(millis());

  unsigned long timeout = millis();
  while (!client.available()) {
    if (millis() - timeout > 500) {
      PERF_HTTP_PHASE(Cierre);
      client.stop();
      return;
    }
    // Mantener parpadeo responsivo mientras esperamos datos del cliente
    actualizarLedRain(millis());
    delay(1);
  }

  PERF_HTTP_PHASE(Recepcion);
  String requestLine = client.readStringUntil('\r');
  client.read(); // '\n'

  while (client.available()) {
    String header = client.readStringUntil('\r');
    client.read();
    if (header.length() == 1) break;
  }

  PERF_HTTP_PHASE(Preparacion);
  // LED_CONTROL_BEGIN
  if (ledApi.handleRequest(requestLine.c_str(), client)) {
    PERF_HTTP_ROUTE(F("/led"));
    PERF_HTTP_PHASE(Cierre);
    client.stop();
    return;
  }
  // LED_CONTROL_END
  // /data
  if (requestLine.indexOf("GET /data") >= 0) {
    PERF_HTTP_ROUTE(F("/data"));
    String fechaHora = obtenerFechaHora();

    float tOut = isnan(tempFiltrada) ? TEMPERATURA : tempFiltrada;
    float hOut = isnan(humFiltrada)  ? HUMEDAD     : humFiltrada;

    PERF_HTTP_PHASE(Respuesta);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    client.print("{\"temp\":");
    client.print(tOut, 1);
    client.print(",\"hum\":");
    client.print(hOut, 0);
    client.print(",\"tmin\":");
    client.print(tempMin, 1);
    client.print(",\"tmax\":");
    client.print(tempMax, 1);
    client.print(",\"hmin\":");
    client.print(humMin, 0);
    client.print(",\"hmax\":");
    client.print(humMax, 0);
    client.print(",\"time\":\"");
    client.print(fechaHora);
    client.print("\",\"hcount\":");
    client.print(histCount);

    int f = faseLunar();
    client.print(",\"moonIcon\":\"");
    client.print(faseIcono(f));
    client.print("\",\"moonName\":\"");
    client.print(faseNombre(f));

    String lunaNueva, lunaLlena;
    PERF_HTTP_MOON_BEGIN();
    calcularLunas(lunaNueva, lunaLlena);
    PERF_HTTP_MOON_END();

    client.print("\",\"nextNewMoon\":\"");
    client.print(lunaNueva);
    client.print("\",\"nextFullMoon\":\"");
    client.print(lunaLlena);
    client.print("\"");

    client.print(",\"histT\":[");
    for (int i = 0; i < histCount; i++) {
      int idx = (histIndex - histCount + i + HOUR_POINTS) % HOUR_POINTS;
      if (i > 0) client.print(",");
      client.print(histTemp[idx], 1);
    }

    client.print("],\"histH\":[");
    for (int i = 0; i < histCount; i++) {
      int idx = (histIndex - histCount + i + HOUR_POINTS) % HOUR_POINTS;
      if (i > 0) client.print(",");
      client.print(histHum[idx], 0);
    }
    client.print("]");
    imprimirClimaExterior(client);
    // LED_CONTROL_BEGIN
    client.print(",\"led\":");
    ledApi.printState(client);
    // LED_CONTROL_END
    client.println("}");

    PERF_HTTP_PHASE(Cierre);
    client.stop();
    return;
  }

  // /csv
  if (requestLine.startsWith("GET /csv ")) {
    PERF_HTTP_ROUTE(F("/csv"));
    if (!spiffsOk || !SPIFFS.exists(LOG_FILE)) {
      PERF_HTTP_PHASE(Respuesta);
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Log no disponible");
      PERF_HTTP_PHASE(Cierre);
      client.stop();
      return;
    }

    File f = SPIFFS.open(LOG_FILE, "r");
    if (!f) {
      PERF_HTTP_PHASE(Respuesta);
      client.println("HTTP/1.1 500 Internal Server Error");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("No se pudo abrir el log");
      PERF_HTTP_PHASE(Cierre);
      client.stop();
      return;
    }

    PERF_HTTP_PHASE(Respuesta);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/csv");
    client.println("Content-Disposition: attachment; filename=\"" + obtenerNombreCSV() + "\"");
    client.println("Connection: close");
    client.println();

    uint8_t buf[512];
    while (f.available()) {
      size_t len = f.read(buf, sizeof(buf));
      client.write(buf, len);
    }

    f.close();
    PERF_HTTP_PHASE(Cierre);
    client.stop();
    return;
  }

  // /clearcsv
  if (requestLine.startsWith("GET /clearcsv ")) {
    PERF_HTTP_ROUTE(F("/clearcsv"));
    bool reiniciado = false;
    if (spiffsOk && SPIFFS.exists(LOG_FILE)) {
      reiniciado = SPIFFS.remove(LOG_FILE);
    }

    File nf = SPIFFS.open(LOG_FILE, "w");
    if (nf) {
      nf.println("FechaHora,Temperatura,Hum.edad");
      nf.close();
      reiniciado = true;
    }

    if (reiniciado) {
      logLines = 0;
      reiniciarFechasCSV();
    }

    Serial.println("🗑️ Historial CSV borrado manualmente.");

    PERF_HTTP_PHASE(Respuesta);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("CSV borrado correctamente");
    PERF_HTTP_PHASE(Cierre);
    client.stop();
    return;
  }

  // /csvinfo
  if (requestLine.startsWith("GET /csvinfo ")) {
    PERF_HTTP_ROUTE(F("/csvinfo"));
    String f1, f2;
    obtenerFechasCSV(f1, f2);

    PERF_HTTP_PHASE(Respuesta);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    client.print("{\"first\":\"" + f1 + "\",");
    client.print("\"last\":\"" + f2 + "\"}");

    PERF_HTTP_PHASE(Cierre);
    client.stop();
    return;
  }

  // Página principal (HTML simple de prueba)
  PERF_HTTP_ROUTE(F("HTML"));
  PERF_HTTP_PHASE(Respuesta);
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();

  // El mismo HTML se envia desde flash para dejar RAM disponible para TLS.
  client.println(F(R"rawliteral(


<!-- ============================================================================================= -->



<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Monitor Ambiental ESP8266/DHT22</title>

    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Material+Symbols+Rounded" />
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

    <style>
        *, *::before, *::after { box-sizing: border-box; }
        body {
            background: #0d0f1a;
            color: #e0e0e0;
            font-family: Segoe UI, sans-serif;
            text-align: center;
            margin: 0;
            padding: 28px;
        }

        h1 {
            margin-bottom: 5px;
        }

        h2 {
            margin-top: 0;
            color: #aaa;
        }

        .panel {
            margin: 16px auto;
            display: inline-block;
            text-align: left;
            padding: 12px 20px;
            border-radius: 16px;
            background: #1b1d2b;
            min-width: 300px;
        }

        .header-panel {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 10px;
            padding: 11px 15px;
            width: 100%;
            max-width: 1000px;
        }

        .header-info {
            display: flex;
            align-items: center;
            gap: 14px;
        }

        .header-icon {
            font-size: 42px;
            color: #ff8b5d;
        }

        .header-text h1 {
            margin: 0;
            font-size: 22px;
            letter-spacing: -0.01em;
        }

        .header-subtitle {
            margin: 0;
            font-size: 10px;
            text-transform: uppercase;
            letter-spacing: 0.16em;
            color: rgba(224, 224, 224, 0.62);
        }

        .header-update {
            display: flex;
            flex-direction: column;
            align-items: flex-end;
            gap: 2px;
        }

        .update-label {
            font-size: 9px;
            letter-spacing: 0.16em;
            text-transform: uppercase;
            color: rgba(224, 224, 224, 0.52);
        }

        .update-value {
            font-size: 14px;
            font-weight: 600;
        }

        .section-title {
            font-size: 18.7px;
            margin-bottom: 6px;
            display: flex;
            align-items: center;
            gap: 6px;
            font-weight: bold;
        }

        .icon {
            font-family: 'Material Symbols Rounded';
            font-size: 28px;
            vertical-align: middle;
        }

        #temp {
            color: #FFD700;
            font-size: 26px;
            font-weight: 700;
        }

        #hum {
            color: #00B4FF;
            font-size: 26px;
            font-weight: 700;
        }

        .icon-temp {
            color: #FFC107;
        }

        .icon-hum {
            color: #03A9F4;
        }

        .icon-time {
            color: #4CAF50;
        }

        .label {
            margin: 4px 0;
            font-size: 15.4px;
        }

        .value {
            font-size: 17.6px;
            font-weight: bold;
        }

        .charts {
            max-width: 1000px;
            margin: 14px auto;
            display: flex;
            flex-wrap: wrap;
            justify-content: space-around;
            gap: 18px;
        }

        canvas {
            background: #141622;
            border-radius: 10px;
            padding: 8px;
        }

        .footer {
            margin-top: 25px;
            font-size: 16px;
            font-family: Georgia, serif;
            color: #DAA5;
            font-style: italic;
            text-shadow: 0 0 3px rgba(255, 215, 0, 0.25);
        }

        .chart-block {
            flex: 1;
            min-width: 280px;
        }

        .chart-canvas {
            position: relative;
            width: 100%;
            aspect-ratio: 2 / 1;
            min-height: 150px;
        }

        /* ======== Estilo iPhone Lunar Card ======== */
        .lunar-ios {
            background: rgba(255, 255, 255, 0.04);
            padding: 28px;
            border-radius: 22px;
            box-shadow:
                inset 0 0 12px rgba(255, 255, 255, 0.05),
                0 8px 22px rgba(0, 0, 0, 0.35);
            -webkit-backdrop-filter: blur(10px);
            backdrop-filter: blur(10px);
            transition: 0.25s;
            min-width: 320px;
        }

        .lunar-ios:hover {
            transform: scale(1.02);
            box-shadow:
                inset 0 0 16px rgba(255, 255, 255, 0.07),
                0 12px 30px rgba(0, 0, 0, 0.45);
        }

        .lunar-header {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 15px;
        }

        .lunar-icon-title {
            font-family: 'Material Symbols Rounded';
            font-size: 28px;
            color: #C8C9CC;
        }

        .lunar-title {
            font-size: 22px;
            font-weight: 600;
            color: #EEE;
        }

        .lunar-moon-icon {
            display: flex;
            justify-content: center;
            margin-bottom: 10px;
        }

        .moon-emoji {
            font-size: 60px;
            filter: drop-shadow(0 4px 6px rgba(0, 0, 0, 0.5));
        }

        .lunar-phase-name {
            text-align: center;
            font-size: 20px;
            font-weight: 600;
            color: #DCDCDC;
            margin-bottom: 15px;
        }

        .divider {
            width: 100%;
            height: 1px;
            background: rgba(255, 255, 255, 0.10);
            margin: 12px 0 18px 0;
        }

        .lunar-row {
            display: flex;
            justify-content: space-between;
            padding: 4px 0;
        }

        .label-ios {
            font-size: 14px;
            color: #B7B7B7;
        }

        .value-ios {
            font-size: 15px;
            font-weight: 600;
            color: #EEE;
        }

        /* Botones responsive */
        #btnCSV,
        #btnClear {
            width: clamp(44px, 5.5vw, 60px);
            height: clamp(44px, 5.5vw, 60px);
            border-radius: 50%;
        }

        #btnCSV span.material-symbols-rounded,
        #btnClear span.material-symbols-rounded {
            font-size: clamp(16px, 2.6vw, 24px);
        }

        #btnCSV .btn-label,
        #btnClear .btn-label {
            font-size: clamp(5.5px, 1.4vw, 8px);
        }

        @media (max-width: 480px) {
            #btnCSV,
            #btnClear {
                width: clamp(48px, 6.2vw, 64px);
                height: clamp(48px, 6.2vw, 64px);
            }
        }

        .heading-main {
            color: #A05C1F;
            font-size: 22px;
        }

        .monitoring-layout {
            width: 100%;
            max-width: 1000px;
            margin: 0 auto;
        }

        .environment-grid {
            display: grid;
            grid-template-columns: repeat(2, minmax(0, 1fr));
            gap: 18px;
            margin: 18px 0;
        }

        .environment-card {
            display: flex;
            flex-direction: column;
            min-width: 0;
            min-height: 300px;
            margin: 0;
            padding: 22px;
            border: 1px solid rgba(255,255,255,0.06);
        }

        .card-heading, .card-title-group {
            display: flex;
            align-items: center;
            gap: 12px;
        }

        /* LED_CONTROL_BEGIN */
        .card-heading.local-heading { align-items: flex-start; flex-wrap: nowrap; gap: 8px; }
        .led-control { margin-left: auto; text-align: right; font-size: 11px; color: #AAA; }
        .led-switch { display: flex; align-items: center; gap: 6px; margin-left: auto;
            background: none; border: 0; padding: 6px 0; min-height: 44px; color: #EEE; cursor: pointer; font: inherit; }
        .led-track { width: 34px; height: 20px; border-radius: 12px; background: #555; padding: 3px; }
        .led-track::after { content: ''; display: block; width: 14px; height: 14px;
            background: #FFF; border-radius: 50%; }
        .led-switch[aria-checked="true"] .led-track { background: #16A085; }
        .led-switch[aria-checked="true"] .led-track::after { transform: translateX(14px); }
        .led-switch:disabled { opacity: .55; cursor: wait; }
        .led-auto { display: block; margin: 3px 0 0 auto; background: none; border: 0;
            padding: 4px 0; color: #82CFC0; text-decoration: underline; font: inherit; cursor: pointer; }
        .led-auto:disabled { color: #999; text-decoration: none; cursor: default; }
        #ledError { display: block; max-width: 140px; color: #FFB4A9; }
        @media (max-width: 400px) {
            .local-heading .card-title-group { gap: 6px; }
            .local-heading h2 { font-size: 16px; }
            .local-heading .card-symbol { font-size: 24px; }
            .led-control { max-width: 100px; flex-shrink: 0; }
        }
        /* LED_CONTROL_END */
        .card-heading { justify-content: space-between; margin-bottom: 25px; }
        .card-title-group { min-width: 0; }
        .card-symbol { color: #ff8b5d; font-size: 30px; }
        .card-heading h2 { margin: 0 0 5px; color: #EEE; font-size: 20px; font-weight: 600; }
        .card-subtitle { margin: 0; color: #aaa; font-size: 11px; letter-spacing: 0.07em; }
        .metric-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 14px; }
        .metric-grid .section-title { color: #bdc2cc; font-size: 14px; font-weight: 500; }
        .metric-grid .icon { font-size: 22px; }
        .reading { display: flex; align-items: baseline; gap: 5px; margin-top: 8px; }
        .reading .value, .reading #temp, .reading #hum { font-size: 38px; font-weight: 600; line-height: 1.2; }
        .temp-reading { color: #FFD700; }
        .hum-reading { color: #00B4FF; }
        .reading-unit { color: #aaa; font-size: 18px; }
        .card-details { margin-top: auto; padding-top: 20px; }
        .card-details::before { content: ''; display: block; height: 1px; margin-bottom: 14px; background: rgba(255,255,255,0.08); }
        .extrema-row { display: flex; justify-content: space-between; gap: 10px; margin-top: 8px; font-size: 13px; color: #aaa; }
        .extrema-row .value { font-size: 15px; color: #e0e0e0; }
        .extrema-values { white-space: nowrap; color: #aaa; }
        .weather-status { flex-shrink: 0; padding: 5px 8px; border-radius: 20px; background: rgba(255,255,255,0.05); color: #aaa; font-size: 10px; }
        .weather-status[data-state="current"] { background: rgba(76,175,80,0.10); color: #98c69b; }
        .weather-status[data-state="stale"] { background: rgba(255,193,7,0.10); color: #d4b675; }
        .weather-location { display: flex; align-items: center; gap: 6px; color: #bdc2cc; font-size: 13px; }
        .weather-location .material-symbols-rounded { font-size: 17px; color: #ff8b5d; }
        .weather-updated { display: flex; justify-content: space-between; flex-wrap: wrap; gap: 6px 12px; margin-top: 10px; font-size: 11px; color: #9096a4; }
        .weather-updated time { color: #bdc2cc; font-size: 12px; }

        .lunar-card {
            display: grid;
            grid-template-columns: minmax(0, 1.2fr) repeat(2, minmax(0, 1fr));
            align-items: center;
            gap: 24px;
            width: 100%;
            min-width: 0;
            margin: 0 0 22px;
            padding: 22px;
            border: 1px solid rgba(255,255,255,0.06);
        }

        .lunar-overview { display: flex; align-items: center; gap: 18px; min-width: 0; }
        .lunar-card .lunar-header { gap: 7px; margin-bottom: 5px; }
        .lunar-card .lunar-title { font-size: 14px; color: #aaa; font-weight: 500; }
        .lunar-card .lunar-icon-title { font-size: 20px; }
        .lunar-card .lunar-phase-name { margin: 0; text-align: left; font-size: 18px; }
        .lunar-card .moon-emoji { font-size: 52px; }
        .lunar-event { display: flex; flex-direction: column; gap: 8px; padding-left: 22px; border-left: 1px solid rgba(255,255,255,0.08); }
        .lunar-event .label-ios { font-size: 12px; }
        .lunar-event .value-ios { font-size: 17px; font-weight: 600; }

        @media (max-width: 680px) {
            body { padding: 18px 16px; }
            .panel.header-panel { width: 100%; min-width: 0; padding: 16px; gap: 12px; }
            .header-info { gap: 9px; }
            .header-icon { font-size: 32px; }
            .header-text h1 { font-size: 19px; }
            .header-subtitle { font-size: 9px; letter-spacing: 0.09em; }
            .header-update { gap: 4px; }
            .update-label { font-size: 8px; }
            .update-value { font-size: 11px; }
            .environment-grid { grid-template-columns: minmax(0, 1fr); gap: 14px; margin: 14px 0; }
            .environment-card { min-height: 280px; padding: 20px; }
            .reading .value, .reading #temp, .reading #hum { font-size: 36px; }
            .lunar-card { grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 18px; padding: 20px; }
            .lunar-overview { grid-column: 1 / -1; }
            .lunar-event { border-left: 0; border-top: 1px solid rgba(255,255,255,0.08); padding: 15px 0 0; }
            .lunar-event .value-ios { font-size: 16px; }
            .chart-block { min-width: 0; width: 100%; flex-basis: 100%; }
            .charts { gap: 18px; }
            canvas { max-width: 100%; }
        }

        @media (max-width: 400px) {
            .panel.header-panel { flex-direction: column; align-items: flex-start; }
            .header-update { align-items: flex-start; margin-left: 41px; }
            .card-heading { flex-wrap: wrap; gap: 10px; }
            .weather-status { margin-left: 42px; }
            .environment-card { padding: 18px; }
            .reading .value, .reading #temp, .reading #hum { font-size: 34px; }
            .extrema-row { font-size: 12px; }
        }

        .panel-csv {
            max-width: 220px;
            width: clamp(180px, 52%, 220px);
            min-width: 0;
            margin: 14px auto;
            background: #11131d;
            text-align: center;
            padding: 8px 10px;
        }

        .csv-title {
            text-align: center;
            margin-bottom: 8px;
            font-size: 17px;
        }

        .csv-row {
            margin-bottom: 3px;
            font-size: 13px;
            color: #adb1bc;
        }

        .csv-row-large {
            margin-bottom: 6px;
        }

        .csv-row strong {
            font-weight: 400;
            color: #a1a5af;
        }

        .csv-row span {
            color: #9096a4;
        }

        .csv-row span.value-bold {
            font-weight: 600;
            color: #bdc2cc;
        }

        .csv-actions {
            display: flex;
            justify-content: center;
            gap: 12px;
            margin-top: 10px;
        }

        hr {
            border: none;
            height: 1px;
            background: rgba(255, 255, 255, 0.08);
            margin: 10px 0;
        }

        .circular-btn {
            width: 70px;
            height: 70px;
            border-radius: 50%;
            cursor: pointer;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 4px;
            border: 1px solid rgba(255, 255, 255, 0.06);
            background: #141620;
            transition: transform 0.18s ease, background-color 0.18s ease, border-color 0.18s ease;
        }

        .circular-btn:hover {
            transform: translateY(-2px);
            background: #1c1f2b;
            border-color: rgba(255, 255, 255, 0.12);
        }

        .btn-clear {
            background: #1f1419;
            border-color: rgba(200, 90, 110, 0.26);
        }

        .btn-clear:hover {
            background: #271a21;
            border-color: rgba(200, 90, 110, 0.4);
        }

        .btn-csv {
            background: #111d18;
            border-color: rgba(96, 164, 132, 0.26);
        }

        .btn-csv:hover {
            background: #16251f;
            border-color: rgba(96, 164, 132, 0.4);
        }

        .btn-icon {
            font-size: 24px;
            color: #a3a8b4;
        }

        .btn-label {
            font-size: 8px;
            letter-spacing: 0.36px;
            color: #9398a3;
        }

        #btnClear .btn-icon {
            color: #9a5764;
        }

        #btnClear .btn-label {
            color: #82616a;
        }

        #btnCSV .btn-icon {
            color: #46755d;
        }

        #btnCSV .btn-label {
            color: #5f7f6d;
        }
    </style>
</head>
<body>
    <div class="panel header-panel">
        <div class="header-info">
            <span class="material-symbols-rounded header-icon">sensors</span>
            <div class="header-text">
                <h1>Monitor Ambiental</h1>
                <p class="header-subtitle">ESP8266 · DHT22</p>
            </div>
        </div>
        <div class="header-update">
            <span class="update-label">Actualización de pantalla</span>
            <span id="time" class="update-value">--/--/---- --:--:--</span>
        </div>
    </div>

    <main class="monitoring-layout">
    <section class="environment-grid" aria-label="Monitoreo ambiental">
        <article class="panel environment-card" aria-labelledby="localTitle">
            <header class="card-heading local-heading">
                <div class="card-title-group">
                    <span class="material-symbols-rounded card-symbol" aria-hidden="true">sensors</span>
                    <div>
                        <h2 id="localTitle">Sensor local</h2>
                        <p class="card-subtitle">DHT22 · ESP8266</p>
                    </div>
                </div>
                <!-- LED_CONTROL_BEGIN -->
                <div class="led-control">
                    <button id="ledSwitch" class="led-switch" type="button" role="switch"
                            aria-checked="false" aria-label="LEDs D6/D7" disabled>
                        <span class="led-track" aria-hidden="true"></span><span>LEDs D6/D7</span>
                    </button>
                    <span id="ledStatus" role="status" aria-live="polite">Sin confirmar</span>
                    <button id="ledAuto" class="led-auto" type="button" disabled>Automático</button>
                    <span id="ledError" role="alert"></span>
                </div>
                <!-- LED_CONTROL_END -->
            </header>
            <div class="metric-grid">
                <div>
                    <div class="section-title"><span class="icon icon-temp" aria-hidden="true">wb_sunny</span>Temperatura</div>
                    <div class="reading"><span id="temp" class="value temp-reading">--.-</span><span class="reading-unit">°C</span></div>
                </div>
                <div>
                    <div class="section-title"><span class="icon icon-hum" aria-hidden="true">water_drop</span>Humedad</div>
                    <div class="reading"><span id="hum" class="value hum-reading">--.-</span><span class="reading-unit">%</span></div>
                </div>
            </div>
            <div class="card-details">
                <div class="extrema-row"><span>Temperatura mín. / máx.</span><span class="extrema-values"><span id="tmin" class="value">--.-</span> / <span id="tmax" class="value">--.-</span> °C</span></div>
                <div class="extrema-row"><span>Humedad mín. / máx.</span><span class="extrema-values"><span id="hmin" class="value">--.-</span> / <span id="hmax" class="value">--.-</span> %</span></div>
            </div>
        </article>

        <article class="panel environment-card" aria-labelledby="weatherTitle">
            <header class="card-heading">
                <div class="card-title-group">
                    <span class="material-symbols-rounded card-symbol" aria-hidden="true">partly_cloudy_day</span>
                    <div>
                        <h2 id="weatherTitle">Clima exterior</h2>
                        <p class="card-subtitle">Corrientes · WeatherAPI</p>
                    </div>
                </div>
                <span id="weatherStatus" class="weather-status" data-state="empty" role="status">Sin datos</span>
            </header>
            <div class="metric-grid">
                <div>
                    <div class="section-title"><span class="icon icon-temp" aria-hidden="true">wb_sunny</span>Temperatura</div>
                    <div class="reading"><span id="weatherTemp" class="value temp-reading">--</span><span class="reading-unit">°C</span></div>
                </div>
                <div>
                    <div class="section-title"><span class="icon icon-hum" aria-hidden="true">water_drop</span>Humedad</div>
                    <div class="reading"><span id="weatherHum" class="value hum-reading">--</span><span class="reading-unit">%</span></div>
                </div>
            </div>
            <div class="card-details">
                <div class="weather-location"><span class="material-symbols-rounded" aria-hidden="true">location_on</span>Corrientes, Argentina</div>
                <div class="weather-updated"><span>Última consulta exitosa</span><time id="weatherUpdated">Sin datos</time></div>
            </div>
        </article>
    </section>

    <section class="panel lunar-card" aria-label="Fases lunares">
        <div class="lunar-overview">
            <span id="moonIcon" class="moon-emoji" aria-hidden="true">🌑</span>
            <div>
                <div class="lunar-header"><span class="lunar-icon-title" aria-hidden="true">nightlight</span><span class="lunar-title">Fase Lunar</span></div>
                <div class="lunar-phase-name"><span id="moonName">--</span></div>
            </div>
        </div>
        <div class="lunar-event"><span class="label-ios">Próxima luna nueva</span><span id="nextNewMoon" class="value-ios">--/--/----</span></div>
        <div class="lunar-event"><span class="label-ios">Próxima luna llena</span><span id="nextFullMoon" class="value-ios">--/--/----</span></div>
    </section>

    <!-- ===========================
             GRÁFICOS
             =========================== -->
    <div class="charts">
        <div class="chart-block">
            <h3>Temperatura (últimos segundos)</h3>
            <div class="chart-canvas"><canvas id="tempChart"></canvas></div>
        </div>
        <div class="chart-block">
            <h3>Humedad (últimos segundos)</h3>
            <div class="chart-canvas"><canvas id="humChart"></canvas></div>
        </div>
    </div>

    <div class="charts">
        <div class="chart-block">
            <h3>Temperatura (última hora)</h3>
            <div class="chart-canvas"><canvas id="tempHourChart"></canvas></div>
        </div>
        <div class="chart-block">
            <h3>Humedad (última hora)</h3>
            <div class="chart-canvas"><canvas id="humHourChart"></canvas></div>
        </div>
    </div>

    <script>
        let tempChart, humChart;
        let tempHourChart, humHourChart;

        const HOUR_POINTS = 60;
        function crearGraficos() {
            const tctx = document.getElementById('tempChart').getContext('2d');
            const hctx = document.getElementById('humChart').getContext('2d');

            tempChart = new Chart(tctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Temperatura (°C)',
                        data: [],
                        borderColor: 'rgba(255,99,132,1)',
                        backgroundColor: 'rgba(255,99,132,0.25)',
                        tension: 0,
                        pointRadius: 0
                    }]
                },
                options: {
                    animation: false,
                    scales: {
                        x: {
                            display: false,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            }
                        },
                        y: {
                            beginAtZero: false,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            },
                            ticks: {
                                stepSize: 0.5,
                                callback: function (v) {
                                    return v.toFixed(1);
                                }
                            }
                        }
                    }
                }
            });

            humChart = new Chart(hctx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Humedad (%)',
                        data: [],
                        borderColor: 'rgba(54,162,235,1)',
                        backgroundColor: 'rgba(54,162,235,0.25)',
                        tension: 0.4,
                        pointRadius: 0
                    }]
                },
                options: {
                    animation: false,
                    scales: {
                        x: {
                            display: false,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            }
                        },
                        y: {
                            beginAtZero: false,
                            suggestedMax: 100,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            },
                            ticks: {
                                callback: function (v) {
                                    return v.toFixed(0);
                                }
                            }
                        }
                    }
                }
            });

            const thctx = document.getElementById('tempHourChart').getContext('2d');
            const hhctx = document.getElementById('humHourChart').getContext('2d');

            tempHourChart = new Chart(thctx, {
                type: 'scatter',
                data: {
                    datasets: [{
                        label: 'Temp 1h (°C)',
                        data: [],
                        borderColor: 'rgba(255,99,132,1)',
                        backgroundColor: 'rgba(255,99,132,1)',
                        showLine: false,
                        pointRadius: 3
                    }]
                },
                options: {
                    animation: false,
                    scales: {
                        x: {
                            type: 'linear',
                            min: 0,
                            max: HOUR_POINTS - 1,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            }
                        },
                        y: {
                            beginAtZero: false,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            },
                            ticks: {
                                callback: function (v) {
                                    return v.toFixed(1);
                                }
                            }
                        }
                    }
                }
            });

            humHourChart = new Chart(hhctx, {
                type: 'scatter',
                data: {
                    datasets: [{
                        label: 'Hum 1h (%)',
                        data: [],
                        borderColor: 'rgba(54,162,235,1)',
                        backgroundColor: 'rgba(54,162,235,1)',
                        showLine: false,
                        pointRadius: 3
                    }]
                },
                options: {
                    animation: false,
                    scales: {
                        x: {
                            type: 'linear',
                            min: 0,
                            max: HOUR_POINTS - 1,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            }
                        },
                        y: {
                            beginAtZero: false,
                            suggestedMax: 100,
                            grid: {
                                color: 'rgba(255,255,255,0.12)',
                                lineWidth: 1
                            },
                            ticks: {
                                callback: function (v) {
                                    return v.toFixed(0);
                                }
                            }
                        }
                    }
                }
            });
        }

        function agregarPunto(chart, label, value, maxPts) {
            chart.data.labels.push(label);
            chart.data.datasets[0].data.push(value);

            if (chart.data.labels.length > maxPts) {
                chart.data.labels.shift();
                chart.data.datasets[0].data.shift();
            }

            chart.update();
        }

        function actualizarEscalaTemperatura() {
            const valores = tempChart.data.datasets[0].data;
            if (!valores.length) return;

            const minimo = Math.min(...valores);
            const maximo = Math.max(...valores);
            const eje = tempChart.options.scales.y;

            if (eje.min === undefined || eje.max === undefined) {
                const centro = Math.round(((minimo + maximo) / 2) * 2) / 2;
                eje.min = centro - 1.5;
                eje.max = centro + 1.5;
            }

            if (minimo <= eje.min || maximo >= eje.max ||
                (eje.max - eje.min > 3 && maximo - minimo <= 1)) {
                if (maximo - minimo <= 1) {
                    const centro = Math.round(((minimo + maximo) / 2) * 2) / 2;
                    eje.min = centro - 1.5;
                    eje.max = centro + 1.5;
                } else {
                    eje.min = Math.floor((minimo - 0.5) * 2) / 2;
                    eje.max = Math.max(eje.min + 3, Math.ceil((maximo + 0.5) * 2) / 2);
                }
            }
        }

        function actualizarClimaExterior(weather, offline = false) {
            const status = document.getElementById('weatherStatus');
            if (offline) {
                status.textContent = 'Sin conexión local';
                status.dataset.state = 'stale';
                return;
            }
            const valid = weather && weather.valid === true &&
                Number.isFinite(weather.temp) && Number.isFinite(weather.hum);
            document.getElementById('weatherTemp').textContent = valid ? weather.temp.toFixed(1) : '--';
            document.getElementById('weatherHum').textContent = valid ? weather.hum.toFixed(0) : '--';
            document.getElementById('weatherUpdated').textContent = valid ? (weather.updated || 'Sin fecha') : 'Sin datos';
            status.textContent = !valid ? 'Sin datos' : (weather.stale ? 'Datos anteriores' : 'Datos disponibles');
            status.dataset.state = !valid ? 'empty' : (weather.stale ? 'stale' : 'current');
        }

        // LED_CONTROL_BEGIN
        let ledConfirmed = null;
        let ledPending = false;
        let ledGeneration = 0;

        function mostrarLed(state) {
            if (!state || !['auto', 'manual'].includes(state.mode) || typeof state.on !== 'boolean' ||
                typeof state.d6 !== 'boolean' || typeof state.d7 !== 'boolean' ||
                state.on !== (state.d6 && state.d7) ||
                (state.mode === 'manual' && state.d6 !== state.d7)) {
                throw new Error('Estado LED inválido');
            }
            ledConfirmed = state;
            document.getElementById('ledSwitch').setAttribute('aria-checked', String(state.on));
            document.getElementById('ledStatus').textContent =
                state.mode === 'auto' ? 'Automático' : `Manual · ${state.on ? 'ON' : 'OFF'}`;
            document.getElementById('ledSwitch').title =
                `D6 ${state.d6 ? 'ON' : 'OFF'} · D7 ${state.d7 ? 'ON' : 'OFF'}`;
            document.getElementById('ledSwitch').disabled = ledPending;
            document.getElementById('ledAuto').disabled = ledPending || state.mode === 'auto';
        }

        function errorLed() {
            document.getElementById('ledError').textContent = 'Sin confirmación. Reintentá.';
            document.getElementById('ledSwitch').disabled = true;
            document.getElementById('ledAuto').disabled = true;
        }

        async function cambiarLed(auto = false) {
            if (ledPending || !ledConfirmed) return;
            ledPending = true;
            ++ledGeneration;
            mostrarLed(ledConfirmed);
            document.getElementById('ledError').textContent = '';
            document.getElementById('ledStatus').textContent = 'Confirmando…';
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 8000);
            try {
                const query = auto ? 'mode=auto' : `mode=manual&on=${ledConfirmed.on ? 0 : 1}`;
                const response = await fetch(`/led?${query}`, {
                    method: 'PUT', cache: 'no-store', signal: controller.signal
                });
                if (!response.ok) throw new Error('Error HTTP');
                const state = await response.json();
                // Validar antes de habilitar controles o sustituir el ultimo estado confirmado.
                mostrarLed(state);
                ledPending = false;
                mostrarLed(state);
            } catch (error) {
                ledPending = false;
                mostrarLed(ledConfirmed);
                errorLed();
            } finally {
                clearTimeout(timeout);
                ++ledGeneration; // Descarta consultas /data iniciadas durante el comando.
            }
        }
        // LED_CONTROL_END

        function actualizarDatos() {
            // LED_CONTROL_BEGIN
            const ledVersion = ledGeneration;
            // LED_CONTROL_END
            fetch('/data')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('temp').textContent = d.temp.toFixed(1);
                    document.getElementById('hum').textContent = d.hum.toFixed(0);
                    document.getElementById('tmin').textContent = d.tmin.toFixed(1);
                    document.getElementById('tmax').textContent = d.tmax.toFixed(1);
                    document.getElementById('hmin').textContent = d.hmin.toFixed(0);
                    document.getElementById('hmax').textContent = d.hmax.toFixed(0);
                    document.getElementById('time').textContent = d.time;
                    actualizarClimaExterior(d.weather);
                    // LED_CONTROL_BEGIN
                    if (!ledPending && ledVersion === ledGeneration) {
                        try {
                            mostrarLed(d.led);
                            document.getElementById('ledError').textContent = '';
                        } catch (error) { errorLed(); }
                    }
                    // LED_CONTROL_END

                    document.getElementById('moonIcon').textContent = d.moonIcon;
                    document.getElementById('moonName').textContent = d.moonName;
                    document.getElementById('nextNewMoon').textContent = d.nextNewMoon;
                    document.getElementById('nextFullMoon').textContent = d.nextFullMoon;

                    agregarPunto(tempChart, d.time, d.temp, 40);
                    agregarPunto(humChart, d.time, d.hum, 40);

                    actualizarEscalaTemperatura();

                    humChart.options.scales.y.min = d.hum - 2;
                    humChart.options.scales.y.max = d.hum + 2;

                    tempChart.update();
                    humChart.update();

                    const hc = d.hcount;

                    tempHourChart.data.datasets[0].data = [];
                    humHourChart.data.datasets[0].data = [];

                    for (let i = 0; i < hc; i++) {
                        tempHourChart.data.datasets[0].data.push({ x: i, y: d.histT[i] });
                        humHourChart.data.datasets[0].data.push({ x: i, y: d.histH[i] });
                    }

                    if (hc > 2) {
                        const minT = Math.min(...d.histT);
                        const maxT = Math.max(...d.histT);
                        tempHourChart.options.scales.y.min = minT - 4;
                        tempHourChart.options.scales.y.max = maxT + 4;

                        const minH = Math.min(...d.histH);
                        const maxH = Math.max(...d.histH);
                        humHourChart.options.scales.y.min = minH - 20;
                        humHourChart.options.scales.y.max = maxH + 20;
                    }

                    tempHourChart.update();
                    humHourChart.update();
                })
                .catch(err => {
                    console.error('Error al obtener /data', err);
                    actualizarClimaExterior(null, true);
                    // LED_CONTROL_BEGIN
                    if (!ledPending && ledVersion === ledGeneration) errorLed();
                    // LED_CONTROL_END
                });
        }

        window.addEventListener('load', () => {
            // LED_CONTROL_BEGIN
            document.getElementById('ledSwitch').addEventListener('click', () => cambiarLed());
            document.getElementById('ledAuto').addEventListener('click', () => cambiarLed(true));
            // LED_CONTROL_END
            crearGraficos();
            actualizarDatos();
            setInterval(actualizarDatos, 2000);
            cargarInfoCSV();
            setInterval(cargarInfoCSV, 5000);
        });

        function cargarInfoCSV() {
            fetch('/csvinfo')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('csvFirst').textContent = d.first || '--/--/----';
                    document.getElementById('csvLast').textContent = d.last || '--/--/----';
                })
                .catch(err => console.error('Error al obtener /csvinfo', err));
        }
    </script>

    <!-- ===== TARJETA DE INFORME DEL CSV ===== -->
    <div class="panel panel-csv">
        <h2 class="csv-title">Historial CSV</h2>

        <div class="csv-row">
            <strong>Primer registro:</strong>
            <span id="csvFirst" class="value-bold">--/--/----</span>
        </div>

        <div class="csv-row csv-row-large">
            <strong>Último registro:</strong>
            <span id="csvLast" class="value-bold">--/--/----</span>
        </div>

        <div class="csv-actions">
            <!-- ========== BOTÓN CLEAR (ROJO) ========== -->
            <button id="btnClear" class="circular-btn btn-clear">
                <span class="material-symbols-rounded btn-icon">delete</span>
                <span class="btn-label">Clear</span>
            </button>

            <!-- ========== BOTÓN CSV (VERDE) ========== -->
            <button id="btnCSV" class="circular-btn btn-csv">
                <span class="material-symbols-rounded btn-icon">table</span>
                <span class="btn-label">.CSV</span>
            </button>
        </div>
    </div>

    </main>

    <div class="footer">by: Luciano Aromi</div>

    <script>
        document.getElementById('btnCSV').addEventListener('click', function () {
            window.location.href = '/csv';
        });

        document.getElementById('btnClear').addEventListener('click', function () {
            if (confirm('¿Seguro que deseas borrar TODO el historial del CSV?')) {
                fetch('/clearcsv')
                    .then(() => alert('Historial CSV borrado correctamente.'))
                    .catch(() => alert('Error al borrar el CSV.'));
            }
        });
    </script>
</body>
</html>



<!-- ============================================================================================= -->


)rawliteral"));

  PERF_HTTP_PHASE(Cierre);
  client.stop();
}
