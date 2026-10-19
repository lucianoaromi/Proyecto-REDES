#pragma once
#if DIAGNOSTICO_CONEXIONES
enum class ServicioDiag : uint8_t { Telegram, WeatherAPI };
enum class EstadoDiag : uint8_t {
  Pendiente, Wifi, Ntp, Reintento, Prioridad, Lluvia, HttpLocal, Memoria,
  Habilitada, Intento, Exito, ErrorConexion, ErrorRespuesta
};

uint8_t ultimoEstadoTelegramDiag = 255;
uint8_t ultimoEstadoWeatherDiag = 255;
bool wifiObservadoDiag = false;
bool wifiConectadoDiag = false;
bool wifiConectoAntesDiag = false;
bool wifiInicioFallidoDiag = false;
volatile bool ntpEventoDiag = false;
volatile unsigned long ntpMomentoDiag = 0;
bool huboIntentoTelegramDiag = false;
bool huboIntentoWeatherDiag = false;
bool weatherPeriodicoPendienteDiag = false;

void prefijoDiag(const __FlashStringHelper *servicio, unsigned long instante) {
  Serial.print(F("[DIAG +"));
  Serial.print(instante);
  Serial.print(F(" ms]["));
  Serial.print(servicio);
  Serial.print(F("] "));
}

const __FlashStringHelper *nombreServicioDiag(ServicioDiag servicio) {
  return servicio == ServicioDiag::Telegram ? F("Telegram") : F("WeatherAPI");
}

void eventoDiag(ServicioDiag servicio, const __FlashStringHelper *evento,
                const __FlashStringHelper *detalle) {
  prefijoDiag(nombreServicioDiag(servicio), millis());
  Serial.print(evento);
  Serial.print(' ');
  Serial.println(detalle);
}

void estadoDiag(ServicioDiag servicio, EstadoDiag estado,
                const __FlashStringHelper *evento,
                const __FlashStringHelper *detalle) {
  uint8_t &anterior = servicio == ServicioDiag::Telegram ?
      ultimoEstadoTelegramDiag : ultimoEstadoWeatherDiag;
  if (anterior == static_cast<uint8_t>(estado)) return;
  anterior = static_cast<uint8_t>(estado);
  eventoDiag(servicio, evento, detalle);
}

void memoriaDiag(ServicioDiag servicio) {
  prefijoDiag(nombreServicioDiag(servicio), millis());
  Serial.print(F("MEMORIA antes_TLS libre="));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(" bloque_max="));
  Serial.println(ESP.getMaxFreeBlockSize());
}

void finDiag(ServicioDiag servicio, unsigned long inicio, bool exito,
             bool huboIntento) {
  prefijoDiag(nombreServicioDiag(servicio), millis());
  Serial.print(F("FIN resultado="));
  Serial.print(exito ? F("exito") : (huboIntento ? F("error") : F("sin_intento")));
  Serial.print(F(" duracion_ms="));
  Serial.println(millis() - inicio);
}

void conexionTlsDiag(ServicioDiag servicio, unsigned long inicio) {
  prefijoDiag(nombreServicioDiag(servicio), millis());
  Serial.print(F("CONEXION_TLS exitosa duracion_conexion_ms="));
  Serial.println(millis() - inicio);
}

void cabecerasWeatherDiag(int codigo, unsigned long inicio) {
  prefijoDiag(F("WeatherAPI"), millis());
  Serial.print(F("FIN_GET codigo="));
  Serial.print(codigo);
  Serial.print(F(" duracion_hasta_cabeceras_ms="));
  Serial.println(millis() - inicio);
}

void reconexionWifiDiag() {
  prefijoDiag(F("WiFi"), millis());
  Serial.println(F("INTENTO reconexion explicita"));
}

void inicioWifiDiag() {
  prefijoDiag(F("WiFi"), millis());
  Serial.println(F("INICIO conexion WiFi.begin"));
}

void inicioNtpDiag() {
  prefijoDiag(F("NTP"), millis());
  Serial.println(F("INICIO servicio SNTP configurado"));
}

void clienteHttpDiag(unsigned long ahora) {
  if (WiFi.status() != WL_CONNECTED || !horaValidaParaTLS()) return;
  bool telegramPendiente = avisoInicioPendiente || alertaPendiente.length() > 0;
  if (telegramPendiente &&
      (!intentoTelegramRealizado || ahora - ultimoIntentoTelegram >= EXTERNAL_RETRY_MS)) {
    estadoDiag(ServicioDiag::Telegram, EstadoDiag::HttpLocal,
               F("APLAZADA"), F("cliente HTTP local en atencion"));
  } else if (!rainWindowActive &&
             (climaInicialPendiente || ahora - lastWeatherCheck >= WEATHER_INTERVAL) &&
             (!intentoWeatherRealizado || ahora - ultimoIntentoWeather >= EXTERNAL_RETRY_MS)) {
    estadoDiag(ServicioDiag::WeatherAPI, EstadoDiag::HttpLocal,
               F("APLAZADA"), F("cliente HTTP local en atencion"));
  }
}

void observarWifiDiag() {
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (!wifiObservadoDiag || conectado != wifiConectadoDiag) {
    prefijoDiag(F("WiFi"), millis());
    if (conectado) {
      Serial.println((wifiConectoAntesDiag || wifiInicioFallidoDiag) ? F("RECONEXION exitosa") :
                     F("CONEXION establecida"));
      wifiConectoAntesDiag = true;
      prefijoDiag(F("TLS"), millis());
      Serial.println(puedeIniciarTLS() ? F("DISPONIBLE") : F("PENDIENTE hora o memoria"));
    } else {
      Serial.println(wifiConectoAntesDiag ? F("PERDIDA de conexion") :
                     F("PENDIENTE conexion inicial"));
    }
    wifiConectadoDiag = conectado;
    wifiObservadoDiag = true;
  }
}

void observarNtpDiag() {
  if (!ntpEventoDiag) return;
  unsigned long instante = ntpMomentoDiag;
  ntpEventoDiag = false;
  prefijoDiag(F("NTP"), instante);
  Serial.print(F("SINCRONIZADA hora_valida="));
  Serial.print(horaValidaParaTLS() ? F("si") : F("no"));
  Serial.print(F(" epoch="));
  Serial.print(static_cast<unsigned long>(time(nullptr)));
  Serial.print(F(" TLS="));
  Serial.println(puedeIniciarTLS() ? F("disponible") : F("pendiente"));
}

void aplazamientoTlsDiag(ServicioDiag servicio) {
  if (WiFi.status() != WL_CONNECTED) {
    estadoDiag(servicio, EstadoDiag::Wifi, F("APLAZADA"), F("WiFi desconectado"));
  } else if (!horaValidaParaTLS()) {
    estadoDiag(servicio, EstadoDiag::Ntp, F("APLAZADA"), F("hora NTP no valida"));
  } else {
    estadoDiag(servicio, EstadoDiag::Memoria, F("APLAZADA"), F("memoria insuficiente para TLS"));
  }
}

#define DIAG_ESTADO(...) estadoDiag(__VA_ARGS__)
#define DIAG_EVENTO(...) eventoDiag(__VA_ARGS__)
#define DIAG_MEMORIA(...) memoriaDiag(__VA_ARGS__)
#define DIAG_FIN(...) finDiag(__VA_ARGS__)
#define DIAG_TLS_CONECTADA(...) conexionTlsDiag(__VA_ARGS__)
#define DIAG_CABECERAS_WEATHER(...) cabecerasWeatherDiag(__VA_ARGS__)
#define DIAG_RECONEXION_WIFI() reconexionWifiDiag()
#define DIAG_INICIO_WIFI() inicioWifiDiag()
#define DIAG_INICIO_NTP() inicioNtpDiag()
#define DIAG_CLIENTE_HTTP(...) clienteHttpDiag(__VA_ARGS__)
#define DIAG_APLAZAMIENTO_TLS(...) aplazamientoTlsDiag(__VA_ARGS__)
#define DIAG_MARCAR_NTP() do { ntpMomentoDiag = millis(); ntpEventoDiag = true; } while (0)
#define DIAG_OBSERVAR_NTP() observarNtpDiag()
#define DIAG_OBSERVAR_WIFI() observarWifiDiag()
#define DIAG_WIFI_INICIO_FALLIDO() do { wifiInicioFallidoDiag = true; } while (0)
#define DIAG_REINICIAR_INTENTO_TG() do { huboIntentoTelegramDiag = false; } while (0)
#define DIAG_REINICIAR_INTENTO_WEATHER() do { huboIntentoWeatherDiag = false; } while (0)
#define DIAG_MARCAR_INTENTO_TG() do { huboIntentoTelegramDiag = true; } while (0)
#define DIAG_MARCAR_INTENTO_WEATHER() do { huboIntentoWeatherDiag = true; } while (0)
#define DIAG_HUBO_INTENTO_TG() huboIntentoTelegramDiag
#define DIAG_HUBO_INTENTO_WEATHER() huboIntentoWeatherDiag
#define DIAG_REARMAR_WEATHER() do { weatherPeriodicoPendienteDiag = false; } while (0)
#define DIAG_WEATHER_PERIODICO(ahora) do { \
  if (!climaInicialPendiente && !weatherPeriodicoPendienteDiag && \
      (ahora) - lastWeatherCheck >= WEATHER_INTERVAL) { \
    eventoDiag(ServicioDiag::WeatherAPI, F("PENDIENTE"), F("consulta periodica")); \
    weatherPeriodicoPendienteDiag = true; \
  } \
} while (0)
#else
#define DIAG_ESTADO(...) ((void)0)
#define DIAG_EVENTO(...) ((void)0)
#define DIAG_MEMORIA(...) ((void)0)
#define DIAG_FIN(...) ((void)0)
#define DIAG_TLS_CONECTADA(...) ((void)0)
#define DIAG_CABECERAS_WEATHER(...) ((void)0)
#define DIAG_RECONEXION_WIFI() ((void)0)
#define DIAG_INICIO_WIFI() ((void)0)
#define DIAG_INICIO_NTP() ((void)0)
#define DIAG_CLIENTE_HTTP(...) ((void)0)
#define DIAG_APLAZAMIENTO_TLS(...) ((void)0)
#define DIAG_MARCAR_NTP() ((void)0)
#define DIAG_OBSERVAR_NTP() ((void)0)
#define DIAG_OBSERVAR_WIFI() ((void)0)
#define DIAG_WIFI_INICIO_FALLIDO() ((void)0)
#define DIAG_REINICIAR_INTENTO_TG() ((void)0)
#define DIAG_REINICIAR_INTENTO_WEATHER() ((void)0)
#define DIAG_MARCAR_INTENTO_TG() ((void)0)
#define DIAG_MARCAR_INTENTO_WEATHER() ((void)0)
#define DIAG_HUBO_INTENTO_TG() false
#define DIAG_HUBO_INTENTO_WEATHER() false
#define DIAG_REARMAR_WEATHER() ((void)0)
#define DIAG_WEATHER_PERIODICO(...) ((void)0)
#endif
