#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace WeatherCsv {
static const char HEADER[] = "FechaHora,TemperaturaLocal_C,HumedadLocal_pct,TemperaturaExterior_C,HumedadExterior_pct,FechaHoraWeatherAPI";
static const size_t ROW_SIZE = 160;
inline bool legacyHeader(const char *s) {
  return strcmp(s, "FechaHora,Temperatura,Hum.edad") == 0;
}
inline size_t row(char *out, size_t capacity, const char *date, float t, float h,
                  float exteriorT, float exteriorH, time_t updated) {
  char stamp[20] = "";
  if (updated > 0 && isfinite(exteriorT) && isfinite(exteriorH)) {
    struct tm *local = localtime(&updated);
    if (local) strftime(stamp, sizeof(stamp), "%d/%m/%Y %H:%M:%S", local);
  }
  const int n = stamp[0] ?
      snprintf(out, capacity, "%s,%.1f,%.0f,%.1f,%.0f,%s\r\n", date, t, h, exteriorT, exteriorH, stamp) :
      snprintf(out, capacity, "%s,%.1f,%.0f,,,\r\n", date, t, h);
  return n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0;
}
// Lee una linea acotada; no devuelve una fila incompleta como registro valido.
template <typename Input>
bool line(Input &input, char *out, size_t capacity, bool &complete) {
  size_t n = 0;
  bool overflow = false;
  complete = false;
  while (input.available()) {
    const int ch = input.read();
    if (ch < 0) break;
    if (ch == '\n') { complete = true; break; }
    if (ch == '\r') continue;
    if (n + 1 < capacity) out[n++] = static_cast<char>(ch);
    else overflow = true;
  }
  out[n] = '\0';
  return !overflow && complete;
}
inline bool columns(const char *line, bool legacy) {
  unsigned commas = 0;
  for (const char *p = line; *p; ++p) if (*p == ',') ++commas;
  return strlen(line) >= 19 && commas == (legacy ? 2U : 5U);
}
// Exporta con un buffer acotado; rellena solo los tres campos nuevos del legado.
template <typename Input, typename Output>
bool exportRows(Input &input, Output &output, bool legacy) {
  char lineBuffer[ROW_SIZE];
  uint8_t buffer[512];
  size_t used = 0;
  auto flush = [&]() {
    size_t sent = 0;
    while (sent < used) {
      const size_t n = output.write(buffer + sent, used - sent);
      if (!n) return false;
      sent += n;
    }
    used = 0;
    return true;
  };
  auto append = [&](const char *text) {
    while (*text) {
      buffer[used++] = static_cast<uint8_t>(*text++);
      if (used == sizeof(buffer) && !flush()) return false;
    }
    return true;
  };
  while (input.available()) {
    bool complete;
    if (!line(input, lineBuffer, sizeof(lineBuffer), complete) || !columns(lineBuffer, legacy))
      return false;
    if (!append(lineBuffer) || (legacy && !append(",,,")) || !append("\r\n")) return false;
  }
  return flush();
}
} // namespace WeatherCsv
