"""Compila el bloque /data y el serializador reales con estado y Print simulados."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--wasi", action="store_true", help="Usar Zig y ejecutar en el sandbox WASI de Node")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source = (Path(__file__).resolve().parents[1] / "ESP8266_DHT22/ESP8266_DHT22.ino").read_text(encoding="utf-8")
helper = source.split("void imprimirClimaExterior(Print &output) {", 1)[1].split("\nvoid loop()", 1)[0]
helper = "void imprimirClimaExterior(Print &output) {" + helper
endpoint = source.split('  if (requestLine.indexOf("GET /data") >= 0) {\n', 1)[1].split("\n  // /csv", 1)[0]
endpoint = endpoint.rsplit("\n  }", 1)[0]
prefix = r'''
#define PERF_HTTP_ROUTE(...) ((void)0)
#define PERF_HTTP_PHASE(...) ((void)0)
#define PERF_HTTP_MOON_BEGIN() ((void)0)
#define PERF_HTTP_MOON_END() ((void)0)
#include <cmath>
#include <math.h>
#include <ctime>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#define HIGH 1
#define LOW 0
void digitalWrite(unsigned char, int) {}
#include "led_api_control.h"
void rainMode(bool, bool) {}
bool rainState() { return false; }
LedApiControl ledApi(12, rainMode, rainState);
using String = std::string;
struct Print {
  std::ostringstream text;
  void print(const char *s) { text << s; }
  void print(const String &s) { text << s; }
  void print(char c) { text << c; }
  void print(int n) { text << n; }
  void print(float n, int digits) { text << std::fixed << std::setprecision(digits) << n; }
  void println(const char *s = "") { text << s << "\r\n"; }
  void stop() {}
};
float tempApi = NAN, humApi = NAN;
time_t weatherLastSuccessTime = 0;
uint32_t weatherLastSuccessMillis = 0, ticks = 0;
bool weatherLastAttemptOk = false;
const uint32_t WEATHER_INTERVAL = 600000;
uint32_t millis() { return ticks; }
float tempFiltrada = 26.8f, humFiltrada = 52, TEMPERATURA = 0, HUMEDAD = 0;
float tempMin = 24.1f, tempMax = 28.4f, humMin = 48, humMax = 60;
const int HOUR_POINTS = 60;
int histCount = 3, histIndex = 3;
float histTemp[HOUR_POINTS] = {26.2f, 26.6f, 26.8f}, histHum[HOUR_POINTS] = {51, 52, 52};
String interfaceTime = "18/10/2026 05:12:00";
String obtenerFechaHora() { return interfaceTime; }
int faseLunar() { return 2; }
String faseIcono(int) { return "🌓"; }
String faseNombre(int) { return "Cuarto creciente"; }
void calcularLunas(String &a, String &b) { a = "09/11/2026"; b = "26/10/2026"; }
'''
suffix = r'''
void emit() {
  Print client;
  getData(client);
  String response = client.text.str();
  std::cout << response.substr(response.find("\r\n\r\n") + 4) << '\n';
}
int main() {
  emit();
  tempApi = 32.4f; humApi = 64; weatherLastSuccessTime = 1792311000;
  ticks = 1000; weatherLastSuccessMillis = ticks; weatherLastAttemptOk = true; emit();
  ticks = 11000; interfaceTime = "18/10/2026 05:13:00"; emit();
  weatherLastAttemptOk = false; emit();
  weatherLastAttemptOk = true; ticks = 601001; emit();
  ticks = weatherLastSuccessMillis; tempApi = 0; humApi = 0; emit();
  tempApi = -5.2f; humApi = 100; emit();
  weatherLastSuccessMillis = UINT32_MAX - 1000; ticks = 999; emit();
}
'''
cpp = args.output / "endpoint_under_test.cpp"
cpp.write_text(prefix + helper + "\nvoid getData(Print &client) {\n" + endpoint + "\n}\n" + suffix, encoding="utf-8")
binary = args.output / ("endpoint_test.wasm" if args.wasi else "endpoint_test.exe")
command = [args.compiler]
if args.wasi:
    command += ["c++", "-target", "wasm32-wasi", "-fno-exceptions", "-fno-rtti"]
include = str(Path(__file__).resolve().parents[1] / "ESP8266_DHT22")
if Path(args.compiler).stem.lower() == "cl":
    command += ["/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
                "/D_CRT_SECURE_NO_WARNINGS", "/I" + include,
                "/Fo" + str(binary.with_suffix(".obj")), "/Fe" + str(binary), str(cpp)]
else:
    command += ["-I" + include, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", str(cpp), "-o", str(binary)]
subprocess.run(command, check=True)
if args.wasi:
    runner = args.output / "run_endpoint.cjs"
    runner.write_text("const {WASI}=require('node:wasi'); const fs=require('node:fs'); "
                      "(async()=>{const w=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});"
                      "const {instance}=await WebAssembly.instantiate(fs.readFileSync(process.argv[2]),"
                      "{wasi_snapshot_preview1:w.wasiImport});process.exitCode=w.start(instance);})();", encoding="utf-8")
    result = subprocess.run(["node", str(runner), str(binary)], capture_output=True, text=True, encoding="utf-8", check=True)
else:
    result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8", check=True)
rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
assert len(rows) == 8
legacy = {"temp", "hum", "tmin", "tmax", "hmin", "hmax", "time", "hcount", "moonIcon", "moonName", "nextNewMoon", "nextFullMoon", "histT", "histH"}
for row in rows:
    assert set(row) == legacy | {"weather", "led"}
    assert row["led"] == {"mode": "auto", "on": False, "d6": False, "d7": False}
    assert row["temp"] == 26.8 and row["hum"] == 52 and row["histT"] == [26.2, 26.6, 26.8]
    assert row["hcount"] == 3 and row["moonName"] == "Cuarto creciente"
assert rows[0]["weather"] == {"valid": False, "temp": None, "hum": None, "updated": None, "stale": False}
weather = rows[1]["weather"]
assert weather["valid"] and weather["temp"] == 32.4 and weather["hum"] == 64 and not weather["stale"]
assert weather["updated"] and rows[2]["weather"] == weather and rows[2]["time"] != rows[1]["time"]
for index in (3, 4):
    assert rows[index]["weather"] == dict(weather, stale=True)
assert rows[5]["weather"]["temp"] == 0 and rows[5]["weather"]["hum"] == 0
assert rows[6]["weather"]["temp"] == -5.2 and rows[6]["weather"]["hum"] == 100
assert not rows[7]["weather"]["stale"]
(args.output / "endpoint-results.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
print("PASS: 8 respuestas del endpoint C++ real: compatibilidad, null, ceros, humedad, fecha fija, fallo, antiguedad y millis().")
