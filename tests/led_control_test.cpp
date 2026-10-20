// Controlador y respuestas HTTP reales; GPIO simulado, sin red ni credenciales.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#define HIGH 1
#define LOW 0
std::vector<int> writes;
bool simulatedRainOn = false;
int rainChanges = 0;
void rainMode(bool, bool on) { simulatedRainOn = on; ++rainChanges; }
bool rainState() { return simulatedRainOn; }
void digitalWrite(unsigned char pin, int value) {
  if (pin != 12) std::abort(); // D7 se aplica mediante el adaptador rainMode.
  writes.push_back(value);
}
#include "../ESP8266_DHT22/led_api_control.h"

struct Output {
  std::string text;
  void print(const char *s) { text += s; }
  void println(const char *s = "") { text += s; text += "\r\n"; }
};
int checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition) { std::cerr << "FAIL " << checks << '\n'; std::exit(1); }
}
std::string state(LedApiControl &led) {
  Output out; led.printState(out); return out.text;
}
std::string request(LedApiControl &led, const char *line, int code) {
  Output out;
  check(led.handleRequest(line, out));
  check(out.text.find("HTTP/1.1 " + std::to_string(code)) == 0);
  check(out.text.find("Cache-Control: no-store\r\n") != std::string::npos);
  return out.text;
}
int main() {
  LedApiControl led(12, rainMode, rainState);
  led.begin();
  check(state(led) == "{\"mode\":\"auto\",\"on\":false,\"d6\":false,\"d7\":false}");
  check(writes.back() == LOW);
  led.weather(34.9f); check(writes.back() == LOW);
  led.weather(35); check(writes.back() == HIGH);
  request(led, "GET /led HTTP/1.1", 200);
  request(led, "PUT /led?mode=manual&on=0 HTTP/1.1", 200);
  check(state(led) == "{\"mode\":\"manual\",\"on\":false,\"d6\":false,\"d7\":false}");
  check(!simulatedRainOn);
  const size_t count = writes.size();
  led.weather(40); led.weather(20); led.weather(38);
  check(writes.size() == count && writes.back() == LOW);
  request(led, "PUT /led?mode=auto HTTP/1.1", 200);
  check(state(led) == "{\"mode\":\"auto\",\"on\":false,\"d6\":true,\"d7\":false}");
  check(writes.back() == HIGH); // Usa el ultimo clima, no espera otra consulta.
  request(led, "PUT /led?mode=manual&on=1 HTTP/1.0", 200);
  led.weather(10);
  check(writes.back() == HIGH);
  check(state(led) == "{\"mode\":\"manual\",\"on\":true,\"d6\":true,\"d7\":true}");
  check(simulatedRainOn);
  const char *bad[] = {
    "PUT /led HTTP/1.1", "GET /led?mode=manual&on=0 HTTP/1.1",
    "PUT /led?mode=manual HTTP/1.1", "PUT /led?mode=manual&on=2 HTTP/1.1",
    "PUT /led?mode=manual&on=true HTTP/1.1", "PUT /led?mode=bogus HTTP/1.1",
    "PUT /led?mode=auto&on=0 HTTP/1.1", "PUT /led?mode=manual&on=0&on=1 HTTP/1.1",
    "PUT /led?mode=manual&on=0 HTTP/2", "PUT /led?mode=auto "
  };
  for (auto line : bad) {
    const size_t before = writes.size();
    const int rainBefore = rainChanges;
    request(led, line, 400);
    check(writes.size() == before);
    check(rainChanges == rainBefore);
    check(state(led) == "{\"mode\":\"manual\",\"on\":true,\"d6\":true,\"d7\":true}");
  }
  for (auto method : {"POST", "DELETE", "OPTIONS", "HEAD"}) {
    const std::string line = std::string(method) + " /led HTTP/1.1";
    check(request(led, line.c_str(), 405).find("Allow: GET, PUT") != std::string::npos);
  }
  for (auto line : {"GET /data HTTP/1.1", "GET /ledOther HTTP/1.1", "invalid"}) {
    Output out;
    check(!led.handleRequest(line, out) && out.text.empty());
  }
  request(led, "PUT /led?mode=auto HTTP/1.1", 200);
  check(writes.back() == LOW);
  led.weather(NAN); check(writes.back() == LOW);
  request(led, "PUT /led?mode=manual&on=1 HTTP/1.1", 200);
  led.begin(); // Reinicio: automatico apagado hasta obtener clima.
  check(state(led) == "{\"mode\":\"auto\",\"on\":false,\"d6\":false,\"d7\":false}");
  check(!simulatedRainOn);
  check(writes.back() == LOW);
  std::cout << "PASS: " << checks << " comprobaciones LED/HTTP.\n";
}
