#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <stdint.h>

#if ARDUINOJSON_VERSION_MAJOR != 6 || ARDUINOJSON_VERSION_MINOR < 21 || \
    (ARDUINOJSON_VERSION_MINOR == 21 && ARDUINOJSON_VERSION_REVISION < 6)
#error "Instalar ArduinoJson 6.21.6 (version probada con ESP8266 Core 3.1.2)."
#endif

struct WeatherReadings {
  float temperature;
  float currentPrecip;
  int willRain;
  int rainChance;
  float dailyPrecip;
  int humidity;
};

// Capacidad fija: no depende del largo del pronostico horario descartado.
constexpr size_t WEATHER_FILTER_CAPACITY = JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(3) +
    JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(3);
constexpr size_t WEATHER_JSON_CAPACITY = WEATHER_FILTER_CAPACITY + 256;

enum class WeatherJsonStatus { Ok, InvalidJson, InvalidFields, IncompleteBody, Timeout };

template <typename Input>
WeatherJsonStatus parseWeatherJson(Input &input, WeatherReadings &output) {
  StaticJsonDocument<WEATHER_FILTER_CAPACITY> filter;
  filter["current"]["temp_c"] = true;
  filter["current"]["precip_mm"] = true;
  filter["current"]["humidity"] = true;
  filter["forecast"]["forecastday"][0]["day"]["daily_will_it_rain"] = true;
  filter["forecast"]["forecastday"][0]["day"]["daily_chance_of_rain"] = true;
  filter["forecast"]["forecastday"][0]["day"]["totalprecip_mm"] = true;

  StaticJsonDocument<WEATHER_JSON_CAPACITY> document;
  DeserializationError error = deserializeJson(document, input,
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(10));
  if (error || filter.overflowed() || document.overflowed()) {
    return WeatherJsonStatus::InvalidJson;
  }

  JsonObjectConst current = document["current"];
  JsonObjectConst day = document["forecast"]["forecastday"][0]["day"];
  // is<T>() rechaza tambien campos ausentes, null, booleanos y cadenas numericas.
  if (!current["temp_c"].is<float>() || !current["precip_mm"].is<float>() ||
      !day["daily_will_it_rain"].is<int>() || !day["daily_chance_of_rain"].is<int>() ||
      !day["totalprecip_mm"].is<float>() || !current["humidity"].is<int>()) {
    return WeatherJsonStatus::InvalidFields;
  }

  WeatherReadings candidate = {
    current["temp_c"].as<float>(), current["precip_mm"].as<float>(),
    day["daily_will_it_rain"].as<int>(), day["daily_chance_of_rain"].as<int>(),
    day["totalprecip_mm"].as<float>(), current["humidity"].as<int>()
  };
  if (!isfinite(candidate.temperature) || !isfinite(candidate.currentPrecip) ||
      !isfinite(candidate.dailyPrecip) || candidate.currentPrecip < 0 || candidate.dailyPrecip < 0 ||
      (candidate.willRain != 0 && candidate.willRain != 1) ||
      candidate.rainChance < 0 || candidate.rainChance > 100 ||
      candidate.humidity < 0 || candidate.humidity > 100) {
    return WeatherJsonStatus::InvalidFields;
  }
  output = candidate; // Ninguna asignacion parcial ante datos invalidos.
  return WeatherJsonStatus::Ok;
}

// Lector para ArduinoJson: plazo TOTAL, incluso si el servidor envia bytes lentamente.
// Client y Clock se parametrizan para probar exactamente este lector sin red ni hardware.
template <typename Client, typename Clock>
class WeatherJsonReader {
 public:
  WeatherJsonReader(Client &client, uint32_t timeoutMs, int contentLength)
      : client_(client), started_(Clock::now()), timeoutMs_(timeoutMs),
        contentLength_(contentLength) {}

  int read() {
    if (contentLength_ >= 0 && bytesRead_ >= static_cast<size_t>(contentLength_)) return -1;
    while (static_cast<uint32_t>(Clock::now() - started_) < timeoutMs_) {
      if (client_.available()) {
        int c = client_.read();
        if (c >= 0) {
          ++bytesRead_;
          if ((bytesRead_ & 255U) == 0) Clock::idle(); // Ceder CPU aun con datos continuos.
          return c;
        }
      } else if (!client_.connected()) {
        return -1;
      }
      Clock::idle();
    }
    timedOut_ = true;
    return -1;
  }

  size_t readBytes(char *buffer, size_t length) {
    size_t count = 0;
    while (count < length) {
      int c = read();
      if (c < 0) break;
      buffer[count++] = static_cast<char>(c);
    }
    return count;
  }

  // Tras el objeto raiz solo se admite espacio JSON. Verificar tambien el framing HTTP.
  bool finish() {
    int c;
    while ((c = read()) >= 0) {
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return !timedOut_ && (contentLength_ < 0 || bytesRead_ == static_cast<size_t>(contentLength_));
  }

  bool timedOut() const { return timedOut_; }
  size_t bytesRead() const { return bytesRead_; }

 private:
  Client &client_;
  uint32_t started_;
  uint32_t timeoutMs_;
  int contentLength_;
  size_t bytesRead_ = 0;
  bool timedOut_ = false;
};

template <typename Client, typename Clock>
WeatherJsonStatus readWeatherResponse(Client &client, int contentLength, uint32_t timeoutMs,
                                     WeatherReadings &output, size_t &bytesRead) {
  WeatherJsonReader<Client, Clock> reader(client, timeoutMs, contentLength);
  WeatherReadings candidate;
  WeatherJsonStatus status = parseWeatherJson(reader, candidate);
  if (status == WeatherJsonStatus::Ok && !reader.finish()) {
    status = WeatherJsonStatus::IncompleteBody;
  }
  bytesRead = reader.bytesRead();
  if (reader.timedOut()) return WeatherJsonStatus::Timeout;
  if (status == WeatherJsonStatus::Ok) output = candidate;
  return status;
}
