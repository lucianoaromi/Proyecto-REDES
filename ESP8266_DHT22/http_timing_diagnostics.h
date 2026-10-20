#pragma once

// Diagnostico temporal independiente de WiFiManager/TLS. Definir a 0 para retirarlo.
#ifndef DIAGNOSTICO_HTTP_TIEMPOS
#define DIAGNOSTICO_HTTP_TIEMPOS 1
#endif

#if DIAGNOSTICO_HTTP_TIEMPOS
namespace HttpTiming {
inline void prefix(const __FlashStringHelper *kind, uint32_t now) {
  Serial.print(F("[PERF +")); Serial.print(now); Serial.print(F(" ms] "));
  Serial.print(kind);
}
inline void field(const __FlashStringHelper *name, uint32_t value) {
  Serial.print(' '); Serial.print(name); Serial.print('='); Serial.print(value);
}

class Request {
 public:
  enum Phase : uint8_t { Espera, Recepcion, Preparacion, Respuesta, Cierre };
  Request() : start_(millis()), mark_(start_) { route_ = F("sin_ruta"); }
  void phase(Phase next) {
    const uint32_t now = millis();
    elapsed_[phase_] += now - mark_;
    mark_ = now;
    phase_ = next;
  }
  void route(const __FlashStringHelper *value) { route_ = value; }
  void moon(uint32_t start) { moonMs_ += static_cast<uint32_t>(millis()) - start; }
  ~Request() {
    const uint32_t end = millis();
    elapsed_[phase_] += end - mark_;
    // Capturar todos los tiempos antes de emitir la linea para excluir su impresion.
    prefix(F("HTTP"), end);
    Serial.print(F(" ruta=")); Serial.print(route_);
    field(F("inicio_ms"), start_);
    field(F("total_ms"), end - start_);
    field(F("espera_ms"), elapsed_[Espera]);
    field(F("recepcion_ms"), elapsed_[Recepcion]);
    field(F("preparacion_ms"), elapsed_[Preparacion]);
    field(F("respuesta_ms"), elapsed_[Respuesta]);
    field(F("cierre_ms"), elapsed_[Cierre]);
    field(F("lunas_ms"), moonMs_);
    Serial.println();
  }
 private:
  uint32_t start_, mark_, elapsed_[5] = {}, moonMs_ = 0;
  Phase phase_ = Espera;
  const __FlashStringHelper *route_;
};

class CsvScan {
 public:
  explicit CsvScan(const __FlashStringHelper *name) : name_(name), start_(millis()) {}
  void opened(uint32_t size) { opened_ = true; bytes_ = size; }
  void line() { ++lines_; }
  ~CsvScan() {
    const uint32_t end = millis();
    prefix(name_, end);
    field(F("inicio_ms"), start_);
    field(F("duracion_ms"), end - start_);
    field(F("abierto"), opened_);
    field(F("bytes_archivo"), bytes_);
    field(F("lineas"), lines_);
    Serial.println();
  }
 private:
  const __FlashStringHelper *name_;
  uint32_t start_, bytes_ = 0, lines_ = 0;
  bool opened_ = false;
};

inline void dhtAttempt() {
  static bool previousExists = false;
  static uint32_t previous = 0;
  const uint32_t now = millis();
  const uint32_t interval = now - previous;
  prefix(F("DHT_INTENTO"), now);
  field(F("primero"), !previousExists);
  field(F("intervalo_ms"), previousExists ? interval : 0);
  Serial.println();
  previous = now;
  previousExists = true;
}
}  // namespace HttpTiming

#define PERF_HTTP_REQUEST() HttpTiming::Request httpTiming
#define PERF_HTTP_PHASE(name) httpTiming.phase(HttpTiming::Request::name)
#define PERF_HTTP_ROUTE(name) httpTiming.route(name)
#define PERF_HTTP_MOON_BEGIN() const uint32_t httpMoonStart = millis()
#define PERF_HTTP_MOON_END() httpTiming.moon(httpMoonStart)
#define PERF_CSV_BEGIN(name) HttpTiming::CsvScan csvTiming(name)
#define PERF_CSV_OPEN(size) csvTiming.opened(size)
#define PERF_CSV_LINE() csvTiming.line()
#define PERF_DHT_ATTEMPT() HttpTiming::dhtAttempt()
#else
#define PERF_HTTP_REQUEST() ((void)0)
#define PERF_HTTP_PHASE(...) ((void)0)
#define PERF_HTTP_ROUTE(...) ((void)0)
#define PERF_HTTP_MOON_BEGIN() ((void)0)
#define PERF_HTTP_MOON_END() ((void)0)
#define PERF_CSV_BEGIN(...) ((void)0)
#define PERF_CSV_OPEN(...) ((void)0)
#define PERF_CSV_LINE() ((void)0)
#define PERF_DHT_ATTEMPT() ((void)0)
#endif
