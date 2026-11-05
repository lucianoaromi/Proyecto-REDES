"""Ejecuta las funciones DHT/filtro/historial y el bloque de alertas reales, sin placa ni red."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "ESP8266_DHT22/ESP8266_DHT22.ino").read_text(encoding="utf-8")

def function(name):
    start = re.search(r"^(?:bool|void) " + name + r"\([^\n]*\) \{", source, re.M).start()
    return source[start:source.index("\n}", start) + 2]

globals_cpp = source[source.index("float TEMPERATURA ="):source.index("// LED + WeatherAPI")]
for name in ["OFFSET_TEMP", "OFFSET_HUM", "ALERTA_TEMP", "ALERT_INTERVAL"]:
    globals_cpp += re.search(r"^const [^\n]*\b" + name + r"\s*=[^;]*;", source, re.M).group() + "\n"
read_loop = source.split("// 2) Lectura DHT + alerta\n", 1)[1].split("//-------------------------------------------------------------------------------------------------", 1)[0]
prefix = r'''
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using std::isnan;
struct String : std::string {
  using std::string::string;
  using std::string::operator=;
  String(float v, int = 0) : std::string(std::to_string(v)) {}
};
unsigned long ticks = 0, lastAlert = 0;
unsigned long millis() { return ticks; }
String alertaPendiente;
String obtenerFechaHora() { return "fecha simulada"; }
struct { template<class... T> void printf(const char*, T...) {} } Serial;
#define DIAG_EVENTO(...) ((void)0)
#define PERF_DHT_ATTEMPT() ((void)0)
struct FakeDHT {
  float t = 20, h = 50;
  int temperatureReads = 0, humidityReads = 0;
  float readTemperature() { ++temperatureReads; return t; }
  float readHumidity() { ++humidityReads; return h; }
} dht;
struct Row { float t, h; };
std::vector<Row> csv;
void agregarRegistroLog(const String&, float t, float h) { csv.push_back({t,h}); }
int checks = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a-b) < 0.001f; }
'''
suffix = r'''
void reset() {
  ticks = lastRead = lastHistMinute = lastCsvSave = lastAlert = 0;
  TEMPERATURA = HUMEDAD = 0;
  tempFiltrada = humFiltrada = NAN;
  tempMin = 200; tempMax = -100; humMin = 100; humMax = 0;
  minTempSum = minHumSum = csvTempSum = csvHumSum = 0;
  minCount = csvCount = 0; histCount = histIndex = 0;
  alertaPendiente.clear(); csv.clear(); dht = FakeDHT{};
}
void reading(float t, float h) {
  dht.t = t - OFFSET_TEMP; dht.h = h - OFFSET_HUM;
  ticks += READ_INTERVAL_MS + 1; sample();
}
int main() {
  check(READ_INTERVAL_MS == 2500 && READ_INTERVAL_MS >= 2000, "intervalo DHT");
  check(near(ALPHA_FILTRO, 0.5f), "alpha");
  check(HIST_MINUTE_INTERVAL == 60000 && CSV_SAVE_INTERVAL_MS == 300000, "periodos historicos");
  reset(); ticks = READ_INTERVAL_MS; sample();
  check(dht.temperatureReads == 0, "no lectura prematura");
  reset(); reading(20, 50);
  check(dht.temperatureReads == 1 && dht.humidityReads == 1, "una pareja de lecturas por ciclo");
  check(near(tempFiltrada,20) && near(humFiltrada,50), "primera muestra y offsets");
  reading(30,70);
  check(near(tempFiltrada,25) && near(humFiltrada,60), "escalon filtrado");
  const auto count = minCount;
  reading(NAN,10); reading(10,NAN); reading(NAN,NAN);
  check(near(TEMPERATURA,30) && near(HUMEDAD,70), "NaN conserva crudos");
  check(near(tempFiltrada,25) && near(humFiltrada,60), "NaN conserva filtro");
  check(minCount == count && near(tempMax,25) && near(humMax,60), "NaN no afecta historial/extremos");
  reset(); reading(NAN,50);
  check(isnan(tempFiltrada) && minCount == 0 && alertaPendiente.empty(), "primer fallo sin datos ni alerta");
  reading(20,150); check(near(HUMEDAD,100), "limite humedad alto");
  reading(20,-10); check(near(HUMEDAD,0), "limite humedad bajo");

  // Cinco minutos nominales: 24 muestras por minuto, dos invalidas por ventana.
  reset(); float ft = NAN, fh = NAN, totalT = 0, totalH = 0;
  for (int minute = 0; minute < 5; ++minute) {
    float sumT = 0, sumH = 0; int valid = 0;
    for (int i = 0; i < 24; ++i) {
      float t = 20.0f + minute + static_cast<float>(i) / 10.0f;
      float h = 40.0f + minute + static_cast<float>(i) / 5.0f;
      if (i == 5 || i == 10) { reading(NAN,h); continue; }
      ft = isnan(ft) ? t : (t+ft)/2; fh = isnan(fh) ? h : (h+fh)/2;
      sumT += ft; sumH += fh; ++valid; reading(t,h);
    }
    check(histCount == minute+1 && minCount == 0, "un punto por minuto y reinicio acumulador");
    check(near(histTemp[minute],sumT/valid) && near(histHum[minute],sumH/valid), "promedio usa cantidad valida");
    totalT += sumT/valid; totalH += sumH/valid;
    check(csv.size() == (minute == 4 ? 1U : 0U), "un CSV a cinco minutos");
  }
  check(near(csv[0].t,totalT/5) && near(csv[0].h,totalH/5), "CSV promedio de cinco promedios");
  check(csvCount == 0 && near(csvTempSum,0), "CSV reinicia acumuladores");
  while (histCount < HOUR_POINTS) reading(25,50);
  for (int i=0;i<24;++i) reading(25,50);
  check(histCount == 60 && histIndex == 1, "historial circular conserva 60 puntos");

  // Una operacion bloqueante retrasa la muestra; no hay rafaga de recuperacion.
  reset(); reading(20,50); ticks += 15000; sample(); sample();
  check(dht.temperatureReads == 2 && minCount == 2, "pausa HTTPS no duplica muestras");
  ticks = 65000; sample();
  check(histCount == 1 && near(histTemp[0],20), "ventana retrasada conserva promedio");

  reset(); reading(30,50); reading(40,50);
  check(near(tempFiltrada,35) && lastAlert == ticks && !alertaPendiente.empty(), "alerta a 35 tras primer escalon");
  const auto firstAlert = lastAlert;
  for (int i=0;i<119;++i) reading(40,50);
  check(lastAlert == firstAlert, "sin adelantar intervalo de alertas");
  reading(40,50);
  check(lastAlert > firstAlert && lastAlert-firstAlert > ALERT_INTERVAL, "alerta posterior a cinco minutos");
  std::cout << "PASS: " << checks << " verificaciones DHT, NaN, offsets, filtro, historial, CSV y alertas; C++ real, sin hardware.\n";
}
'''
args.output.mkdir(parents=True, exist_ok=True)
cpp = args.output / "dht_under_test.cpp"
cpp.write_text(prefix + globals_cpp + "\n" + "\n".join(function(n) for n in
               ["actualizarFiltro", "leerDHT", "actualizarHistorial"]) +
               "\nvoid sample() { unsigned long ahora = millis();\n" + read_loop + "}\n" + suffix,
               encoding="utf-8")
binary = args.output / "dht_test.exe"
if Path(args.compiler).stem.lower() == "cl":
    # El firmware usa literales double en operaciones float; tolerar esos avisos MSVC.
    cmd = [args.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
           "/wd4244", "/wd4305",
           str(cpp), "/Fo:" + str(args.output / "dht_test.obj"), "/Fe:" + str(binary)]
else:
    cmd = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)]
subprocess.run(cmd, check=True)
subprocess.run([str(binary)], check=True)
