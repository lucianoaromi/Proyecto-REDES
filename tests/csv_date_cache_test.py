"""Ejecuta funciones reales del CSV y rutas HTTP con SPIFFS simulado; no usa la placa."""
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
    match = re.search(r"^(?:String|void) " + name + r"\([^\n]*\) \{", source, re.M)
    assert match, name
    return source[match.start():source.index("\n}", match.start()) + 2]

def route(path, following):
    start = f'  if (requestLine.startsWith("GET /{path} ")) {{\n'
    block = source.split(start, 1)[1].split(f"\n  // /{following}", 1)[0]
    return block.rsplit("\n  }", 1)[0]

prefix = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using __FlashStringHelper = char;
#define F(s) s
#define PERF_CSV_BEGIN(...) ((void)0)
#define PERF_CSV_OPEN(...) ((void)0)
#define PERF_CSV_LINE() ((void)0)
#define PERF_HTTP_ROUTE(...) ((void)0)
#define PERF_HTTP_PHASE(...) ((void)0)
struct String : std::string {
  using std::string::string;
  String(const std::string &s) : std::string(s) {}
  String substring(size_t a, size_t b) const { return substr(a, b-a); }
  void toCharArray(char *out, size_t count) const {
    if (!count) return;
    const size_t n = std::min(size(), count-1);
    std::memcpy(out, data(), n); out[n] = '\0';
  }
};
struct Logger {
  template<class T> void println(const T&) {}
  template<class... T> void printf(const char*, T...) {}
} Serial;
const char *LOG_FILE = "fixture.csv";
const unsigned long MAX_LOG_LINES = 8640UL;
bool spiffsOk = true;
unsigned long logLines = 0;
char primeraFechaCSV[11] = "";
char ultimaFechaCSV[11] = "";
struct FakeFs {
  bool present = true, readable = true, appendable = true, truncatable = true;
  bool removable = true, writable = true;
  uint32_t reads = 0, byteReads = 0;
  std::vector<String> lines;
  bool exists(const char*) { return present; }
  bool remove(const char*) {
    if (!present || !removable) return false;
    present = false; lines.clear(); return true;
  }
  struct File {
    FakeFs *fs; char mode; bool ok; size_t row = 0, pos = 0;
    std::string bytes;
    explicit operator bool() const { return ok; }
    bool available() const {
      return ok && (mode == 'r') &&
        ((pos > 0) ? pos < bytes.size() : row < fs->lines.size());
    }
    String readStringUntil(char) {
      ++fs->reads;
      return fs->lines.at(row++);
    }
    size_t read(uint8_t *out, size_t count) {
      const size_t n = std::min(count, bytes.size()-pos);
      std::memcpy(out, bytes.data()+pos, n);
      pos += n; fs->byteReads += static_cast<uint32_t>(n); return n;
    }
    uint32_t size() const { return static_cast<uint32_t>(bytes.size()); }
    size_t printf(const char *format, const char *date, float t, float h) {
      if (!fs->writable) return 0;
      char line[96];
      const int n = std::snprintf(line, sizeof(line), format, date, t, h);
      if (n <= 0 || n >= static_cast<int>(sizeof(line))) return 0;
      std::string value(line, static_cast<size_t>(n));
      if (value.size() >= 2 && value.substr(value.size()-2) == "\r\n")
        value.resize(value.size()-2);
      fs->lines.push_back(String(value)); fs->present = true;
      return static_cast<size_t>(n);
    }
    int getWriteError() const { return fs->writable ? 0 : 1; }
    void println(const char *line) { fs->lines.push_back(line); fs->present = true; }
    void close() { ok = false; }
  };
  File open(const char*, const char *mode) {
    const char operation = mode[0];
    const bool ok = operation == 'r' ? present && readable :
                    operation == 'a' ? appendable : truncatable;
    if (ok && operation == 'w') { present = true; lines.clear(); }
    if (ok && operation == 'a') present = true;
    File file{this, operation, ok};
    if (ok && operation == 'r')
      for (const auto &line : lines) file.bytes += line + "\r\n";
    return file;
  }
} SPIFFS;
using File = FakeFs::File;
struct Client {
  std::string response;
  int stops = 0;
  void print(const std::string &value) { response += value; }
  void println(const std::string &value = "") { response += value + "\r\n"; }
  size_t write(const uint8_t *data, size_t n) {
    response.append(reinterpret_cast<const char*>(data), n); return n;
  }
  void stop() { ++stops; }
};
void check(bool ok, const char *label) {
  if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
'''

suffix = r'''
int main() {
  SPIFFS.lines.push_back("FechaHora,Temperatura,Hum.edad");
  for (int i=0; i<5291; ++i)
    SPIFFS.lines.push_back(i==5290 ? "18/10/2026 12:00:00,24.0,50" :
                                      "01/10/2026 12:00:00,20.0,40");
  const auto original = SPIFFS.lines;
  iniciarLog();
  check(SPIFFS.reads == 5292 && logLines == 5291, "arranque: recorrido unico y recuento");
  check(SPIFFS.lines == original, "arranque conserva las 5292 lineas");
  String first, last;
  for (int i=0; i<10; ++i) obtenerFechasCSV(first,last);
  check(first=="01/10/2026" && last=="18/10/2026", "fechas de registros existentes");
  check(SPIFFS.reads==5292, "csvinfo usa cache y no lee el archivo");
  const auto oldCount = logLines;
  SPIFFS.appendable=false;
  agregarRegistroLog("19/10/2026 12:00:00", 25, 51);
  obtenerFechasCSV(first,last);
  check(SPIFFS.lines==original && logLines==oldCount && last=="18/10/2026",
        "fallo de apertura no cambia datos ni fechas");
  SPIFFS.appendable=true; SPIFFS.writable=false;
  agregarRegistroLog("19/10/2026 12:00:00", 25, 51);
  obtenerFechasCSV(first,last);
  check(SPIFFS.lines==original && logLines==oldCount && last=="18/10/2026",
        "fallo de escritura no cambia cache");
  SPIFFS.writable=true;
  agregarRegistroLog("19/10/2026 12:00:00", 25, 51);
  obtenerFechasCSV(first,last);
  check(SPIFFS.lines.size()==5293 && logLines==5292 && first=="01/10/2026" &&
        last=="19/10/2026", "escritura exitosa actualiza ultima fecha");
  Client download;
  csvDownload(download);
  check(download.stops==1 && SPIFFS.lines.size()==5293, "descarga no cambia historial");
  check(download.response.find("filename=\"historial_20261001_20261019.csv\"")!=std::string::npos,
        "nombre de descarga");
  check(download.response.find("19/10/2026 12:00:00,25.0,51\r\n")!=std::string::npos &&
        SPIFFS.byteReads>0, "contenido de descarga");
  check(SPIFFS.reads==10585, "solo descarga recorre el archivo nuevamente");
  logLines=MAX_LOG_LINES-1;
  agregarRegistroLog("20/10/2026 12:00:00", 26, 52);
  obtenerFechasCSV(first,last);
  check(SPIFFS.lines.size()==1 && logLines==0 && first.empty() && last.empty(),
        "reinicio por limite limpia archivo y cache");
  agregarRegistroLog("21/10/2026 12:00:00", 27, 53);
  obtenerFechasCSV(first,last);
  check(first=="21/10/2026" && last==first, "primer registro tras reinicio");
  SPIFFS.removable=false; SPIFFS.truncatable=false;
  logLines=MAX_LOG_LINES-1;
  agregarRegistroLog("22/10/2026 12:00:00", 28, 54);
  obtenerFechasCSV(first,last);
  check(SPIFFS.lines.size()==3 && logLines==MAX_LOG_LINES &&
        first=="21/10/2026" && last=="22/10/2026",
        "fallo de reinicio conserva historial y fechas");
  SPIFFS.removable=true; SPIFFS.truncatable=true;
  Client clear;
  csvClear(clear);
  obtenerFechasCSV(first,last);
  check(clear.stops==1 && SPIFFS.lines.size()==1 && first.empty() && last.empty() &&
        logLines==0, "borrado manual limpia fechas");
  SPIFFS.reads=0;
  iniciarLog(); obtenerFechasCSV(first,last);
  check(SPIFFS.reads==1 && first.empty() && last.empty(), "archivo solo cabecera");
  SPIFFS.readable=false;
  iniciarLog(); obtenerFechasCSV(first,last);
  check(first.empty() && last.empty() && logLines==0, "error al abrir archivo existente");
  SPIFFS.readable=true;
  SPIFFS.present=false; SPIFFS.lines.clear();
  iniciarLog(); obtenerFechasCSV(first,last);
  check(SPIFFS.lines.size()==1 && first.empty() && last.empty(), "archivo nuevo");
  SPIFFS.lines.push_back("xx");
  iniciarLog(); obtenerFechasCSV(first,last);
  check(logLines==1 && first.empty() && last.empty(), "registro corto no crea fecha");
  SPIFFS.present=false; SPIFFS.lines.clear(); SPIFFS.truncatable=false;
  iniciarLog(); obtenerFechasCSV(first,last);
  check(first.empty() && last.empty() && !SPIFFS.present, "error al crear archivo");
  SPIFFS.truncatable=true;
  std::cout << "PASS: cache CSV, 5292 lineas, /csvinfo sin lectura, append/fallos, "
               "rotacion, borrado y descarga intacta.\n";
}
'''

parts = [prefix]
for name in ("reiniciarFechasCSV", "registrarFechaCSV", "iniciarLog",
             "agregarRegistroLog", "obtenerNombreCSV", "obtenerFechasCSV"):
    parts.append(function(name))
parts.extend([
    "void csvDownload(Client &client) {\n" + route("csv", "clearcsv") + "\n}\n",
    "void csvClear(Client &client) {\n" + route("clearcsv", "csvinfo") + "\n}\n",
    suffix,
])
args.output.mkdir(parents=True, exist_ok=True)
cpp = args.output / "csv_date_cache_under_test.cpp"
cpp.write_text("\n".join(parts), encoding="utf-8")
binary = cpp.with_suffix(".exe")
if Path(args.compiler).stem.lower() == "cl":
    command = [args.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
               "/utf-8", str(cpp), "/Fo:" + str(cpp.with_suffix(".obj")),
               "/Fe:" + str(binary)]
else:
    command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
               str(cpp), "-o", str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
