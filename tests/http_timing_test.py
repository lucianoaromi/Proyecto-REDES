"""Sondas reales y /csvinfo con SPIFFS/Serial/reloj simulados; sin red ni placa."""
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
    start = re.search(r"^(?:String|void) " + name + r"\([^\n]*\) \{", source, re.M).start()
    return source[start:source.index("\n}", start)+2]
body = source.split('  if (requestLine.startsWith("GET /csvinfo ")) {\n', 1)[1]
body = body.split("\n  // Página principal", 1)[0].rsplit("\n  }", 1)[0]
prefix = r'''
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using __FlashStringHelper = char;
#define F(s) s
uint32_t ticks = 0;
uint32_t millis() { return ticks; }
struct Logger {
  std::ostringstream log;
  template<class T> void print(const T &v) { log << v; }
  void println() { log << '\n'; }
} Serial;
struct String : std::string {
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  String substring(size_t a, size_t b) const { return substr(a,b-a); }
  void toCharArray(char *out, size_t count) const {
    if (!count) return;
    size_t n = size() < count-1 ? size() : count-1;
    std::memcpy(out, data(), n); out[n] = '\0';
  }
};
const char* LOG_FILE = "fixture.csv";
std::vector<String> lines;
uint32_t reads = 0;
struct File {
  bool ok; size_t index = 0;
  explicit operator bool() const { return ok; }
  bool available() const { return ok && index < lines.size(); }
  uint32_t size() const {
    uint32_t n=0; for (const auto &line:lines) n += static_cast<uint32_t>(line.size()+1); return n;
  }
  String readStringUntil(char) { ++ticks; ++reads; return lines[index++]; }
  void close() { ok = false; ++ticks; }
};
struct {
  bool present = true, readable = true;
  bool exists(const char*) { ++ticks; return present; }
  File open(const char*, const char*) { ++ticks; return {readable}; }
} SPIFFS;
struct Client {
  std::string response; int stops = 0;
  void print(const std::string &s) { response += s; ++ticks; }
  void println(const std::string &s = "") { print(s+"\r\n"); }
  void stop() { ++stops; ticks+=2; }
};
void check(bool ok, const char *name) {
  if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
'''
suffix = r'''
void clearLog() { Serial.log.str(""); Serial.log.clear(); }
void request(Client &client) {
  PERF_HTTP_REQUEST();
  ticks += 10;
  PERF_HTTP_PHASE(Recepcion);
  ticks += 20;
  PERF_HTTP_PHASE(Preparacion);
  csvInfo(client);
}
int main() {
  // Mas de 5000 registros y cabecera: ejecutar exactamente el recorrido real.
  lines.push_back("FechaHora,Temperatura,Hum.edad");
  for (int i=0;i<5001;++i)
    lines.push_back(i==5000 ? "19/10/2026 12:00:00,25.0,50" : "01/10/2026 12:00:00,20.0,40");
  for (size_t i=1;i<lines.size();++i) registrarFechaCSV(lines[i]);
  Client client;
  request(client);
  check(reads == 0, "csvinfo no lee SPIFFS");
  check(client.stops == 1, "cierre unico");
  check(client.response == "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"first\":\"01/10/2026\",\"last\":\"19/10/2026\"}", "respuesta intacta");
#if DIAGNOSTICO_HTTP_TIEMPOS
  auto log = Serial.log.str();
  check(log.find("CSV_FECHAS inicio_ms=30 duracion_ms=0 abierto=0 bytes_archivo=0 lineas=0") != std::string::npos, "lectura desde memoria");
  check(log.find("ruta=/csvinfo inicio_ms=0 total_ms=38 espera_ms=10 recepcion_ms=20 preparacion_ms=0 respuesta_ms=6 cierre_ms=2") != std::string::npos, "desglose solicitud");
  check(log.find("19/10/2026") == std::string::npos, "no se registra contenido CSV");
#else
  check(Serial.log.str().empty(), "sin trazas al desactivar");
#endif
  reads = 0; clearLog();
  check(obtenerNombreCSV() == "historial_20261001_20261019.csv" && reads == 5002, "nombre intacto y recorrido");
  lines = {"FechaHora,Temperatura,Hum.edad"};
  reiniciarFechasCSV();
  String first, last;
  obtenerFechasCSV(first,last); check(first.empty() && last.empty(), "solo cabecera");
  SPIFFS.present = false;
  reads=0; obtenerFechasCSV(first,last); check(reads==0 && first.empty() && last.empty(), "archivo ausente");
  SPIFFS.present=true; SPIFFS.readable=false;
  obtenerFechasCSV(first,last); check(reads==0 && first.empty() && last.empty(), "error apertura");
  clearLog(); ticks=100; PERF_DHT_ATTEMPT(); ticks+=6500; PERF_DHT_ATTEMPT();
#if DIAGNOSTICO_HTTP_TIEMPOS
  check(Serial.log.str().find("primero=1 intervalo_ms=0")!=std::string::npos, "primer intento");
  check(Serial.log.str().find("primero=0 intervalo_ms=6500")!=std::string::npos, "intervalo real");
  clearLog(); ticks=UINT32_MAX-3;
  { PERF_HTTP_REQUEST(); ticks=6; }
  check(Serial.log.str().find("total_ms=10 espera_ms=10")!=std::string::npos, "rollover de millis");
#endif
  std::cout << "PASS: HTTP/CSV/DHT con diagnostico=" << DIAGNOSTICO_HTTP_TIEMPOS
            << "; 5001 registros, fases, respuesta, errores, intervalo y rollover.\n";
}
'''
# csvInfo conserva el cuerpo real, incluida la salida anticipada del endpoint.
# El contexto se pasa por referencia para mantener el temporizador de la solicitud.
enabled_signature = "void csvInfo(Client &client, HttpTiming::Request &httpTiming)"
disabled_signature = "void csvInfo(Client &client)"
args.output.mkdir(parents=True, exist_ok=True)
for enabled in [1, 0]:
    signature = enabled_signature if enabled else disabled_signature
    test_suffix = suffix.replace("csvInfo(client);", "csvInfo(client, httpTiming);" if enabled else "csvInfo(client);")
    cpp = args.output / f"http_timing_{enabled}.cpp"
    cpp.write_text(prefix + f'\n#define DIAGNOSTICO_HTTP_TIEMPOS {enabled}\n#include "{(root / "ESP8266_DHT22/http_timing_diagnostics.h").as_posix()}"\n' +
                   '\nchar primeraFechaCSV[11] = "";\nchar ultimaFechaCSV[11] = "";\n' +
                   function("reiniciarFechasCSV") + "\n" + function("registrarFechaCSV") +
                   "\n" + function("obtenerFechasCSV") + "\n" + function("obtenerNombreCSV") +
                   "\n" + signature + " {\n" + body + "\n}\n" + test_suffix, encoding="utf-8")
    binary = cpp.with_suffix(".exe")
    if Path(args.compiler).stem.lower() == "cl":
        cmd = [args.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
               str(cpp), "/Fo:" + str(cpp.with_suffix(".obj")), "/Fe:" + str(binary)]
    else:
        cmd = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(binary)], check=True)
