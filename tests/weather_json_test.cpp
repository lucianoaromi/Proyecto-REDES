// Pruebas nativas sin red: ejecutan el mismo parser y lector que el firmware.
#include "weather_json.h"
#include <cstdlib>
#include <iostream>
#include <string>

struct FakeClock {
  static uint32_t ticks;
  static uint32_t now() { return ticks; }
  static void idle() { ++ticks; }
};
uint32_t FakeClock::ticks = 0;

struct FakeClient {
  std::string body;
  size_t position = 0;
  bool keepOpen = false;
  uint32_t nextByte = 0;
  uint32_t byteDelay = 0;

  int available() const {
    return position < body.size() && static_cast<int32_t>(FakeClock::now() - nextByte) >= 0;
  }
  int read() {
    if (!available()) return -1;
    nextByte = FakeClock::now() + byteDelay;
    return static_cast<unsigned char>(body[position++]);
  }
  bool connected() const { return keepOpen || position < body.size(); }
};

static int cases = 0;

static void require(bool value, const std::string &message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static bool same(const WeatherReadings &a, const WeatherReadings &b) {
  return a.temperature == b.temperature && a.currentPrecip == b.currentPrecip &&
      a.willRain == b.willRain && a.rainChance == b.rainChance && a.dailyPrecip == b.dailyPrecip && a.humidity == b.humidity;
}

static std::string fixture(const std::string &temp = "35", const std::string &precip = "0.85",
                           const std::string &will = "1", const std::string &chance = "75",
                           const std::string &daily = "0.4", int omit = -1,
                           const std::string &humidity = "64") {
  const char *keys[] = {"temp_c", "precip_mm", "daily_will_it_rain",
                        "daily_chance_of_rain", "totalprecip_mm", "humidity"};
  std::string values[] = {temp, precip, will, chance, daily, humidity};
  std::string current, day;
  for (int i = 0; i < 6; ++i) {
    if (i == omit) continue;
    std::string &object = (i < 2 || i == 5) ? current : day;
    if (!object.empty()) object += ',';
    object += std::string("\"") + keys[i] + "\":" + values[i];
  }
  return "{\"current\":{" + current + "},\"forecast\":{\"forecastday\":[{\"day\":{" + day + "}}]}}";
}

static WeatherReadings check(const std::string &label, FakeClient client, int length,
                             WeatherJsonStatus expected, uint32_t start = 0) {
  ++cases;
  FakeClock::ticks = start;
  if (start) client.nextByte = start;
  const WeatherReadings previous = {-20, 9, 1, 99, 10, 31};
  WeatherReadings output = previous;
  size_t bytesRead = 0;
  WeatherJsonStatus status = readWeatherResponse<FakeClient, FakeClock>(
      client, length, 3000, output, bytesRead);
  require(status == expected, label + " / estado " + std::to_string(static_cast<int>(status)));
  if (expected != WeatherJsonStatus::Ok) require(same(output, previous), label + " / estado anterior");
  require(bytesRead <= client.body.size(), label + " / longitud");
  if (expected == WeatherJsonStatus::Timeout) {
    require(static_cast<uint32_t>(FakeClock::ticks - start) == 3000, label + " / plazo total");
  }
  return output;
}

static WeatherReadings check(const std::string &label, const std::string &body,
                             WeatherJsonStatus expected) {
  FakeClient client;
  client.body = body;
  return check(label, client, static_cast<int>(body.size()), expected);
}

int main() {
  const std::string valid = fixture();
  const WeatherReadings expected = {35, 0.85f, 1, 75, 0.4f, 64};
  require(same(check("seis campos", valid, WeatherJsonStatus::Ok), expected), "valores exactos");
  const WeatherReadings zero = {0, 0, 0, 0, 0, 0};
  require(same(check("ceros validos", fixture("0", "0", "0", "0", "0", -1, "0"), WeatherJsonStatus::Ok), zero),
          "ceros no confundidos con ausencia");
  require(check("temperatura negativa", fixture("-5.25"), WeatherJsonStatus::Ok).temperature == -5.25f,
          "temperatura negativa");

  for (int i = 0; i < 6; ++i) {
    check("campo ausente " + std::to_string(i), fixture("35", "0.85", "1", "75", "0.4", i),
          WeatherJsonStatus::InvalidFields);
    for (const auto &bad : {"null", "\"35\"", "true", "{}", "[]"}) {
      std::string values[] = {"35", "0.85", "1", "75", "0.4", "64"};
      values[i] = bad;
      check("tipo invalido " + std::to_string(i) + " " + bad,
            fixture(values[0], values[1], values[2], values[3], values[4], -1, values[5]), WeatherJsonStatus::InvalidFields);
    }
  }
  check("flag fuera de rango", fixture("35", "0", "2"), WeatherJsonStatus::InvalidFields);
  check("flag fraccionario", fixture("35", "0", "1.5"), WeatherJsonStatus::InvalidFields);
  check("probabilidad negativa", fixture("35", "0", "1", "-1"), WeatherJsonStatus::InvalidFields);
  check("probabilidad mayor 100", fixture("35", "0", "1", "101"), WeatherJsonStatus::InvalidFields);
  check("probabilidad fraccionaria", fixture("35", "0", "1", "74.5"), WeatherJsonStatus::InvalidFields);
  check("precipitacion negativa", fixture("35", "-0.1"), WeatherJsonStatus::InvalidFields);
  check("precipitacion diaria negativa", fixture("35", "0", "1", "75", "-0.1"), WeatherJsonStatus::InvalidFields);
  check("temperatura no finita", fixture("1e100"), WeatherJsonStatus::InvalidFields);
  check("precipitacion no finita", fixture("35", "1e100"), WeatherJsonStatus::InvalidFields);
  check("precipitacion diaria no finita", fixture("35", "0", "1", "75", "1e100"), WeatherJsonStatus::InvalidFields);

  check("humedad negativa", fixture("35", "0", "1", "75", "0.4", -1, "-1"), WeatherJsonStatus::InvalidFields);
  check("humedad mayor 100", fixture("35", "0", "1", "75", "0.4", -1, "101"), WeatherJsonStatus::InvalidFields);
  check("humedad fraccionaria", fixture("35", "0", "1", "75", "0.4", -1, "64.5"), WeatherJsonStatus::InvalidFields);
  require(check("humedad 100 valida", fixture("35", "0", "1", "75", "0.4", -1, "100"), WeatherJsonStatus::Ok).humidity == 100,
          "limite superior de humedad");

  for (size_t size = 0; size < valid.size(); ++size) {
    check("truncamiento " + std::to_string(size), valid.substr(0, size), WeatherJsonStatus::InvalidJson);
  }
  check("JSON invalido", "{not-json}", WeatherJsonStatus::InvalidJson);
  check("objeto vacio", "{}", WeatherJsonStatus::InvalidFields);
  check("basura posterior", valid + "garbage", WeatherJsonStatus::IncompleteBody);
  check("dos documentos", valid + valid, WeatherJsonStatus::IncompleteBody);
  check("espacios posteriores", valid + " \r\n\t", WeatherJsonStatus::Ok);
  std::string spaced;
  for (char c : valid) {
    spaced += c;
    if (c == ':') spaced += " \n ";
  }
  check("espacios JSON", spaced, WeatherJsonStatus::Ok);

  // Las claves homonimas horarias preceden a current y los campos utiles estan despues de 8 KiB.
  const std::string large = "{\"forecast\":{\"forecastday\":[{\"hour\":[{\"temp_c\":99,\"precip_mm\":99,"
      "\"unused\":\"" + std::string(24000, 'x') + "\"}],\"day\":{\"daily_will_it_rain\":1,"
      "\"daily_chance_of_rain\":75,\"totalprecip_mm\":0.4}}]},"
      "\"current\":{\"humidity\":64,\"precip_mm\":0.85,\"temp_c\":35}}";
  require(same(check("mas de 24 KiB y orden distinto", large, WeatherJsonStatus::Ok), expected),
          "seleccion por estructura, no por primera coincidencia");
  check("corte en campo descartado", large.substr(0, 8192), WeatherJsonStatus::InvalidJson);

  FakeClient client;
  client.body = valid;
  client.keepOpen = true;
  check("longitud conocida sin esperar cierre", client, static_cast<int>(valid.size()), WeatherJsonStatus::Ok);
  check("servidor no cierra sin longitud", client, -1, WeatherJsonStatus::Timeout);
  client.keepOpen = false;
  check("longitud desconocida con cierre", client, -1, WeatherJsonStatus::Ok);
  check("cuerpo menor al anunciado", client, static_cast<int>(valid.size() + 10), WeatherJsonStatus::IncompleteBody);
  check("longitud anunciada insuficiente", client, static_cast<int>(valid.size() - 1), WeatherJsonStatus::InvalidJson);
  client.nextByte = 4000;
  check("sin primer byte", client, -1, WeatherJsonStatus::Timeout);
  client.nextByte = 0;
  client.byteDelay = 40;
  check("goteo de bytes", client, -1, WeatherJsonStatus::Timeout);
  check("overflow de millis", client, -1, WeatherJsonStatus::Timeout, UINT32_MAX - 100);

  std::cout << "PASS: " << cases << " casos; seis campos, tipos, truncamientos, framing y timeout.\n";
  std::cout << "Capacidades del host: filtro=" << WEATHER_FILTER_CAPACITY
            << ", documento=" << WEATHER_JSON_CAPACITY
            << "; no son mediciones de heap del ESP8266.\n";
}
