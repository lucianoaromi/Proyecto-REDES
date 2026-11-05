"""Ejecuta los callbacks reales de lluvia y controlador HTTP con GPIO/Ticker simulados."""
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
names = ["toggleRainLed", "stopRainWindow", "startRainWindow", "disableRainPattern",
         "enableRainPattern", "actualizarPatronLluvia", "actualizarLedRain",
         "cambiarControlLluvia", "estadoLedLluvia"]

def function(name):
    match = re.search(r"^(?:void|bool) (?:IRAM_ATTR )?" + name + r"\([^\n]*\) \{", source, re.M)
    assert match, name
    return source[match.start():source.index("\n}", match.start()) + 2]

constants = "\n".join(re.search(r"^const [^\n]*\b" + name + r"\s*=[^;]*;", source, re.M).group()
                      for name in ["BRILLO_LLUVIA", "BRILLO_LLUVIA_MANUAL", "RAIN_BLINK_INTERVAL",
                                   "RAIN_BURST_DURATION", "RAIN_BURST_PERIOD", "MODO_PRUEBA_LLUVIA"])
globals_cpp = source[source.index("bool  lluviaApi"):source.index("unsigned long lastWeatherCheck")]
prefix = r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#define IRAM_ATTR
#define HIGH 1
#define LOW 0
const uint8_t LED_API = 12, LED_RAIN = 13;
struct Write { int pin, value; };
std::vector<Write> writes;
int d6 = 0, d7 = 0;
void digitalWrite(unsigned char pin, int value) {
  if (pin != LED_API) std::abort();
  d6 = value; writes.push_back({pin,value});
}
void analogWrite(unsigned char pin, int value) {
  if (pin != LED_RAIN || value < 0 || value > 255) std::abort();
  d7 = value; writes.push_back({pin,value});
}
unsigned long ticks = 0;
unsigned long millis() { return ticks; }
void (*duringDetach)() = nullptr;
struct Ticker {
  void (*callback)() = nullptr;
  unsigned long interval = 0;
  bool repeat = false;
  int attachments = 0;
  void detach() {
    callback = nullptr;
    if (duringDetach) { auto pending = duringDetach; duringDetach = nullptr; pending(); }
  }
  void attach_ms(unsigned long ms, void (*fn)()) {
    interval = ms; callback = fn; repeat = true; ++attachments;
  }
  void once_ms(unsigned long ms, void (*fn)()) {
    attach_ms(ms, fn); repeat = false;
  }
  void fire() {
    auto pending = callback;
    if (!repeat) callback = nullptr;
    if (pending) pending();
  }
};
#include "led_api_control.h"
void cambiarControlLluvia(bool, bool);
bool estadoLedLluvia();
LedApiControl ledApi(LED_API, cambiarControlLluvia, estadoLedLluvia);
struct Output {
  std::string text;
  void print(const char *s) { text += s; }
  void println(const char *s = "") { text += s; text += "\r\n"; }
};
int checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void command(const char *line) {
  Output out;
  check(ledApi.handleRequest(line,out), "ruta LED");
  check(out.text.find("HTTP/1.1 200 OK") == 0, "HTTP 200");
}
'''
suffix = r'''
bool stopped() {
  return !rainBlinkTicker.callback && !rainWindowEndTicker.callback && !rainBurstTicker.callback;
}
void automatic() { command("PUT /led?mode=auto HTTP/1.1"); }
void manual(bool on) {
  command(on ? "PUT /led?mode=manual&on=1 HTTP/1.1" : "PUT /led?mode=manual&on=0 HTTP/1.1");
  check(ledApi.manual(), "modo manual");
  check(d6 == (on ? HIGH : LOW) && d7 == (on ? BRILLO_LLUVIA_MANUAL : 0), "ambos GPIO");
  check(stopped() && !rainPatternEnabled && !rainWindowActive, "Ticker desarmados");
}
int main() {
  ledApi.begin();
  check(!ledApi.manual() && d6 == 0 && d7 == 0 && stopped(), "arranque automatico");
  check(BRILLO_LLUVIA == 15 && BRILLO_LLUVIA_MANUAL == 64, "PWM automatico y manual");
  check(RAIN_BLINK_INTERVAL == 500 && RAIN_BURST_DURATION == 60000 &&
        RAIN_BURST_PERIOD == 300000, "tiempos originales");
  lluviaPronosticoApi = true; ticks = 1000; actualizarPatronLluvia();
  check(rainPatternEnabled && rainWindowActive && lastRainBurst == ticks, "primera rafaga");
  check(rainBlinkTicker.interval == 500 && rainWindowEndTicker.interval == 60000 &&
        rainBurstTicker.interval == 300000, "Ticker configurados");
  rainBlinkTicker.fire();
  check(d7 == 15 && rainLedState, "parpadeo ON");
  rainBlinkTicker.fire();
  check(d7 == 0 && !rainLedState, "parpadeo OFF");
  auto oldBlink = rainBlinkTicker.callback;
  auto oldStop = rainWindowEndTicker.callback;
  auto oldStart = rainBurstTicker.callback;
  duringDetach = oldBlink; // Callback pendiente justo al pasar a manual.
  manual(true);
  size_t count = writes.size();
  oldBlink(); oldStop(); oldStart();
  disableRainPattern(); enableRainPattern();
  lluviaApi = true; lluviaPronosticoApi = false;
  actualizarPatronLluvia(); actualizarLedRain(2000); ledApi.weather(12);
  check(writes.size() == count && d6 == HIGH && d7 == 64, "manual inmune a callbacks y clima");
  manual(false);
  count = writes.size();
  oldBlink(); oldStop(); oldStart();
  ledApi.weather(40); actualizarPatronLluvia();
  check(writes.size() == count && d6 == LOW && d7 == 0, "OFF no reactivado por clima/Ticker");
  automatic();
  check(!ledApi.manual() && d6 == HIGH && d7 == 15 && stopped(), "lluvia actual fija restaurada");
  Output snapshot; ledApi.printState(snapshot);
  check(snapshot.text == "{\"mode\":\"auto\",\"on\":true,\"d6\":true,\"d7\":true}", "estado ambos ON");
  manual(true);
  lluviaApi = false; lluviaPronosticoApi = false; ledApi.weather(20);
  automatic();
  check(d6 == LOW && d7 == 0 && stopped(), "sin lluvia restaurado");
  manual(false);
  lluviaPronosticoApi = true; ledApi.weather(35); ticks = 450000;
  automatic();
  check(d6 == HIGH && d7 == 0 && rainWindowActive && lastRainBurst == ticks, "pronostico reinicia rafaga");
  const int attachments = rainBlinkTicker.attachments;
  automatic();
  check(rainBlinkTicker.attachments == attachments, "automatico repetido no reinicia");
  ticks += 500; rainBlinkTicker.fire();
  check(d7 == 15, "parpadeo restaurado");
  ticks = lastRainBurst + 60000; rainWindowEndTicker.fire();
  check(d7 == 0 && !rainWindowActive && !rainBlinkTicker.callback && rainBurstTicker.callback,
        "fin de ventana mantiene repeticion");
  ticks = lastRainBurst + 300000; rainBurstTicker.fire();
  check(rainWindowActive && rainBlinkTicker.callback && rainWindowEndTicker.callback, "siguiente rafaga");
  lluviaApi = true; actualizarPatronLluvia();
  check(d7 == 15 && stopped(), "actual tiene prioridad sobre pronostico");
  lluviaApi = lluviaPronosticoApi = false; actualizarPatronLluvia();
  check(d7 == 0 && stopped(), "fin de lluvia");
  ledApi.weather(34.9f); check(d6 == LOW, "D6 umbral inferior");
  ledApi.weather(35); check(d6 == HIGH, "D6 umbral original");
  manual(true);
  ledApi.begin();
  check(!ledApi.manual() && d6 == LOW && d7 == 0 && stopped(), "reinicio borra modo manual");
  std::cout << "PASS: " << checks << " comprobaciones D6/D7, PWM y callbacks Ticker reales.\n";
}
'''
args.output.mkdir(parents=True, exist_ok=True)
cpp = args.output / "rain_led_under_test.cpp"
cpp.write_text(prefix + constants + "\n" + globals_cpp + "\n" +
               "\n".join(function(name) for name in names) + suffix, encoding="utf-8")
binary = args.output / "rain_led_test.exe"
include = str(root / "ESP8266_DHT22")
if Path(args.compiler).stem.lower() == "cl":
    cmd = [args.compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
           "/I" + include, str(cpp), "/Fo" + str(binary.with_suffix(".obj")), "/Fe" + str(binary)]
else:
    cmd = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + include,
           str(cpp), "-o", str(binary)]
subprocess.run(cmd, check=True)
subprocess.run([str(binary)], check=True)
