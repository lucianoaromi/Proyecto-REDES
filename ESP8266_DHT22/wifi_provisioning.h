#pragma once
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <memory>
#include <new>
extern "C" {
#include <user_interface.h>
}

// Experimento aislado: el objeto WiFiManager solo existe durante el portal.
class WifiProvisioning {
 public:
  static constexpr uint32_t START_WAIT_MS = 120000;
  static constexpr uint32_t PORTAL_LIMIT_MS = 300000;
  static constexpr uint32_t CANDIDATE_WAIT_MS = 120000;
  static constexpr uint32_t RETRY_MS = 30000;

  void begin(const char *apPassword, const char *fallbackSsid, const char *fallbackPassword) {
    password_ = apPassword;
    fallbackSsid_ = fallbackSsid;
    fallbackPassword_ = fallbackPassword;
    started_ = millis();
    phase_ = Phase::Waiting;
    initialNetwork_ = InitialNetwork::None;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    stationState(F("ARRANQUE_STA"));
    if (restoreSavedNetwork()) {
      event(F("INICIO red guardada; espera inicial maxima 120000 ms"));
    } else if (startFallbackNetwork()) {
      event(F("INICIO red local configurada; espera inicial maxima 120000 ms"));
    } else {
      event(F("SIN_RED_GUARDADA ni credenciales locales utilizables"));
    }
  }

  bool portalActive() const { return phase_ == Phase::Portal; }
  bool ready() const {
    return phase_ == Phase::Online && connectedIPv4();
  }

  void process() {
    if (phase_ == Phase::Portal) {
      // Resta unsigned: plazo independiente de clientes y seguro ante rollover.
      if (elapsed(portalStarted_, PORTAL_LIMIT_MS)) {
        closePortal(false, F("TIEMPO_MAXIMO"));
        return;
      }
      wm_->process();
      if (closeRequested_) {
        closePortal(false, F("SALIDA_USUARIO"));
        return;
      }
      if (queued_) startCandidate();
      if (trying_ && connectedIPv4()) {
        // El SDK escribe la configuracion actual solo despues de obtener IPv4.
        station_config config{};
        bool saved = wifi_station_get_config(&config) && wifi_station_set_config(&config);
        if (saved) {
          station_config flash{};
          const bool read = wifi_station_get_config_default(&flash);
          Serial.print(F("[WM +")); Serial.print(millis());
          Serial.print(F(" ms] FLASH_TRAS_GUARDAR lectura=")); Serial.print(read);
          Serial.print(F(" ssid_igual="));
          Serial.print(read && memcmp(config.ssid, flash.ssid, sizeof(config.ssid)) == 0);
          Serial.print(F(" clave_igual="));
          Serial.println(read && memcmp(config.password, flash.password, sizeof(config.password)) == 0);
          clearSecret(&flash, sizeof(flash));
        }
        clearSecret(&config, sizeof(config));
        if (saved) {
          duration(F("RED_NUEVA_GUARDADA"), candidateStarted_);
          closePortal(true, F("CONEXION_CONFIRMADA"));
          return;
        }
        event(F("ERROR guardando en flash; se conserva la red anterior si la escritura fallo"));
        stopCandidate();
      } else if (trying_ && elapsed(candidateStarted_, CANDIDATE_WAIT_MS)) {
        duration(F("CONEXION_NUEVA_AGOTADA; red anterior conservada"), candidateStarted_);
        stopCandidate();
      }
      if (elapsed(lastSample_, 15000)) {
        lastSample_ = millis();
        memory(F("PORTAL_ACTIVO"));
      }
      return;
    }
    // Tras la primera conexion, la reconexion pertenece al firmware original.
    if (phase_ == Phase::Online) return;
    if (connectedIPv4()) {
      phase_ = Phase::Online;
      if (initialNetwork_ == InitialNetwork::Fallback) {
        persistFallbackNetwork();
        duration(F("RED_LOCAL_CONECTADA"), started_);
      } else {
        duration(F("RED_GUARDADA_CONECTADA"), started_);
      }
      return;
    }
    if (phase_ == Phase::Waiting && elapsed(started_, START_WAIT_MS)) {
      stationState(F("ESPERA_INICIAL_AGOTADA"));
      openPortal();
    } else if (phase_ == Phase::Retry && elapsed(lastRetry_, RETRY_MS)) {
      lastRetry_ = millis();
      stationState(F("ANTES_REINTENTO"));
      const bool accepted = WiFi.reconnect();
      Serial.print(F("[WM +")); Serial.print(millis());
      Serial.print(F(" ms] RECONNECT_ACEPTADO=")); Serial.println(accepted);
      stationState(F("DESPUES_REINTENTO"));
      event(F("REINTENTO red guardada; portal cerrado"));
    }
  }

  static void memory(const __FlashStringHelper *stage) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.print(F(" ms] "));
    Serial.print(stage); Serial.print(F(" heap=")); Serial.print(ESP.getFreeHeap());
    Serial.print(F(" bloque_max=")); Serial.println(ESP.getMaxFreeBlockSize());
  }

 private:
  enum class Phase : uint8_t { Waiting, Portal, Retry, Online };
  enum class InitialNetwork : uint8_t { None, Saved, Fallback };
  Phase phase_ = Phase::Waiting;
  InitialNetwork initialNetwork_ = InitialNetwork::None;
  std::unique_ptr<WiFiManager> wm_;
  const char *password_ = nullptr;
  const char *fallbackSsid_ = nullptr;
  const char *fallbackPassword_ = nullptr;
  uint32_t started_ = 0, portalStarted_ = 0, candidateStarted_ = 0;
  uint32_t lastSample_ = 0, lastRetry_ = 0;
  bool queued_ = false, trying_ = false, closeRequested_ = false, closing_ = false;
  char candidateSsid_[33] = {};
  char candidatePassword_[65] = {};

  // Registrado ANTES de las rutas de WiFiManager: el core 3.1.2 usa el primer
  // handler coincidente, tambien para upload. No se invoca ningun callback OTA.
  class PortalGuard : public RequestHandler {
   public:
    explicit PortalGuard(ESP8266WebServer &web) : web_(web) {}
    bool canHandle(HTTPMethod, const String &uri) override {
      return web_.client().localIP() != WiFi.softAPIP() ||
          uri == "/update" || uri == "/u" || uri == "/erase" ||
          uri == "/restart" || uri == "/info" || uri == "/param" ||
          uri == "/paramsave" || uri == "/status" || uri == "/close";
    }
    bool handle(ESP8266WebServer &web, HTTPMethod, const String &) override {
      web.send(404, "text/plain", "Ruta no disponible en este prototipo.");
      return true;
    }
    // canUpload() heredado devuelve false: ningun upload escribe en flash.
   private:
    ESP8266WebServer &web_;
  };

  static bool elapsed(uint32_t since, uint32_t interval) {
    return static_cast<uint32_t>(millis() - since) >= interval;
  }
  static bool connectedIPv4() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }
  static void clearSecret(void *data, size_t length) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (length--) *p++ = 0;
  }
  static void event(const __FlashStringHelper *text) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.print(F(" ms] "));
    Serial.println(text);
  }
  static void duration(const __FlashStringHelper *text, uint32_t since) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.print(F(" ms] "));
    Serial.print(text); Serial.print(F(" duracion_ms="));
    Serial.println(static_cast<uint32_t>(millis() - since));
  }
  static void stationState(const __FlashStringHelper *stage) {
    Serial.print(F("[WM +")); Serial.print(millis()); Serial.print(F(" ms] "));
    Serial.print(stage); Serial.print(F(" modo=")); Serial.print(static_cast<int>(WiFi.getMode()));
    Serial.print(F(" sdk_estado=")); Serial.print(static_cast<int>(wifi_station_get_connect_status()));
    Serial.print(F(" ipv4="));
    Serial.println(connectedIPv4());
  }
  bool validApPassword() const {
    if (!password_) return false;
    const size_t length = strlen(password_);
    if (length < 12 || length > 63) return false;
    for (size_t i = 0; i < length; ++i) {
      if (password_[i] < 32 || password_[i] > 126) return false;
    }
    return true;
  }
  static bool hasLengthInRange(const char *value, size_t minimum, size_t maximum) {
    if (!value) return false;
    size_t length = 0;
    while (length <= maximum && value[length] != '\0') ++length;
    return length >= minimum && length <= maximum;
  }
  bool hasUsableFallbackCredentials() const {
    // Una clave vacia es valida para una red abierta; no se registra su contenido.
    return hasLengthInRange(fallbackSsid_, 1, 32) &&
        hasLengthInRange(fallbackPassword_, 0, 64);
  }
  bool restoreSavedNetwork() {
    // begin() sin argumentos usa la configuracion actual; restaurar la de flash
    // es necesario despues de probar una candidata fallida solo en RAM.
    station_config saved{};
    WiFi.persistent(false);
    WiFi.disconnect(false);
    bool loaded = wifi_station_get_config_default(&saved);
    bool restored = loaded && wifi_station_set_config_current(&saved);
    const bool configured = restored && saved.ssid[0] != 0;
    station_config current{};
    const bool currentLoaded = configured && wifi_station_get_config(&current);
    Serial.print(F("[WM +")); Serial.print(millis());
    Serial.print(F(" ms] RESTAURAR_FLASH lectura=")); Serial.print(loaded);
    Serial.print(F(" ssid_presente=")); Serial.print(loaded && saved.ssid[0] != 0);
    Serial.print(F(" restauracion=")); Serial.print(restored);
    Serial.print(F(" ram_ssid_igual="));
    Serial.print(currentLoaded && memcmp(saved.ssid, current.ssid, sizeof(saved.ssid)) == 0);
    Serial.print(F(" ram_clave_igual="));
    Serial.println(currentLoaded && memcmp(saved.password, current.password, sizeof(saved.password)) == 0);
    clearSecret(&current, sizeof(current));
    clearSecret(&saved, sizeof(saved));
    initialNetwork_ = configured ? InitialNetwork::Saved : InitialNetwork::None;
    if (configured) {
      const wl_status_t immediate = WiFi.begin();
      Serial.print(F("[WM +")); Serial.print(millis());
      Serial.print(F(" ms] BEGIN_GUARDADA_ESTADO_INMEDIATO="));
      Serial.println(static_cast<int>(immediate));
      stationState(F("DESPUES_BEGIN_GUARDADA"));
    }
    return configured;
  }
  bool startFallbackNetwork() {
    initialNetwork_ = InitialNetwork::None;
    if (!hasUsableFallbackCredentials()) return false;
    // Solo se llega aqui sin una configuracion persistente: no reemplaza una
    // red elegida anteriormente en el portal.
    WiFi.persistent(false);
    WiFi.begin(fallbackSsid_, fallbackPassword_);
    initialNetwork_ = InitialNetwork::Fallback;
    return true;
  }
  void persistFallbackNetwork() {
    station_config config{};
    const bool saved = wifi_station_get_config(&config) && wifi_station_set_config(&config);
    clearSecret(&config, sizeof(config));
    if (saved) duration(F("RED_LOCAL_GUARDADA"), started_);
    else event(F("ERROR guardando red local en flash; se usara solo durante este arranque"));
  }
  void openPortal() {
    if (!validApPassword()) {
      event(F("PORTAL_RECHAZADO: configurar clave AP local de 12 a 63 caracteres ASCII"));
      phase_ = Phase::Retry;
      lastRetry_ = millis();
      return;
    }
    memory(F("ANTES_ABRIR"));
    const uint32_t start = millis();
    wm_.reset(new (std::nothrow) WiFiManager());
    if (!wm_) {
      event(F("ERROR sin memoria para WiFiManager"));
      phase_ = Phase::Retry;
      lastRetry_ = millis();
      return;
    }
    phase_ = Phase::Portal;
    portalStarted_ = millis();
    closing_ = closeRequested_ = queued_ = trying_ = false;
    WiFi.setAutoReconnect(false);
    wm_->setDebugOutput(false);
    wm_->setRestorePersistent(false);
    wm_->setShowPassword(false);
    wm_->setConfigPortalBlocking(false);
    // El plazo absoluto lo controla process(), no se extiende por actividad.
    wm_->setConfigPortalTimeout(0);
    const char *menu[] = {"wifi", "exit"};
    wm_->setMenu(menu, 2);
    wm_->setWebServerCallback([this]() { installRoutes(); });
    // En modo no bloqueante el retorno de startConfigPortal no indica exito.
    wm_->startConfigPortal("MonitorAmbiental-Config", password_);
    if (!wm_->getConfigPortalActive() ||
        WiFi.softAPIP() == IPAddress(0, 0, 0, 0) || !(WiFi.getMode() & WIFI_AP)) {
      closePortal(false, F("ERROR_ABRIENDO_PORTAL"));
      return;
    }
    lastSample_ = millis();
    duration(F("PORTAL_ABIERTO; acceder a http://192.168.4.1"), start);
    memory(F("PORTAL_ACTIVO"));
  }
  void installRoutes() {
    wm_->server->addHandler(new PortalGuard(*wm_->server));
    wm_->server->on("/wifisave", HTTP_ANY, [this]() { queueCandidate(); });
    wm_->server->on("/exit", HTTP_ANY, [this]() {
      closeRequested_ = true;
      wm_->server->send(200, "text/plain", "Cerrando portal. Se retomara la red guardada.");
    });
  }
  void queueCandidate() {
    auto &web = *wm_->server;
    if (closing_ || queued_ || trying_) {
      web.send(409, "text/plain", "Espere a que termine el intento actual.");
      return;
    }
    if (web.method() != HTTP_POST) {
      web.send(405, "text/plain", "Utilice el formulario mediante POST.");
      return;
    }
    const String name = web.arg("s");
    const String pass = web.arg("p");
    bool valid = name.length() > 0 && name.length() <= 32 &&
        name.length() == strlen(name.c_str()) && pass.length() == strlen(pass.c_str());
    valid = valid && (pass.length() == 0 || (pass.length() >= 8 && pass.length() <= 64));
    if (pass.length() == 64) {
      for (size_t i = 0; i < pass.length(); ++i) valid = valid && isxdigit(static_cast<unsigned char>(pass[i]));
    }
    if (!valid) {
      web.send(400, "text/plain", "SSID o longitud/formato de clave no valido.");
      return;
    }
    name.toCharArray(candidateSsid_, sizeof(candidateSsid_));
    pass.toCharArray(candidatePassword_, sizeof(candidatePassword_));
    queued_ = true;
    web.send(202, "text/plain", "Probando red (hasta 120 s). Se guardara solo al obtener IPv4. "
        "El AP se cerrara al conectar; si falla, vuelva a /wifi. IP final: Monitor Serie.");
  }
  void startCandidate() {
    queued_ = false;
    trying_ = true;
    candidateStarted_ = millis();
    WiFi.persistent(false);
    WiFi.disconnect(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(candidateSsid_, candidatePassword_);
    clearSecret(candidateSsid_, sizeof(candidateSsid_));
    clearSecret(candidatePassword_, sizeof(candidatePassword_));
    event(F("INTENTO_RED_NUEVA asincrono; sin escribir credenciales en flash"));
  }
  void stopCandidate() {
    trying_ = false;
    WiFi.disconnect(false);
    WiFi.enableSTA(false);
  }
  void closePortal(bool success, const __FlashStringHelper *reason) {
    const uint32_t closingStarted = millis();
    closing_ = true;
    duration(reason, portalStarted_);
    // Nunca destruir el servidor desde uno de sus callbacks.
    if (wm_ && wm_->getConfigPortalActive()) wm_->stopConfigPortal();
    wm_.reset();
    clearSecret(candidateSsid_, sizeof(candidateSsid_));
    clearSecret(candidatePassword_, sizeof(candidatePassword_));
    queued_ = trying_ = closeRequested_ = false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    phase_ = success && connectedIPv4() ? Phase::Online : Phase::Retry;
    lastRetry_ = millis();
    initialNetwork_ = InitialNetwork::None;
    if (!success && !restoreSavedNetwork()) startFallbackNetwork();
    stationState(F("DESPUES_CIERRE_PORTAL"));
    duration(F("PORTAL_CERRADO"), closingStarted);
    memory(F("DESPUES_CERRAR"));
  }
};
