#pragma once

#include <math.h>
#include <string.h>

// Estado exclusivamente en RAM. El adaptador de lluvia controla D7 y sus Ticker.
class LedApiControl {
 public:
  LedApiControl(unsigned char pin, void (*rainMode)(bool, bool), bool (*rainState)())
      : pin_(pin), rainMode_(rainMode), rainState_(rainState) {}

  bool manual() const { return manual_; }

  void begin() {
    manual_ = false;
    automaticOn_ = false;
    write(false);
    rainMode_(false, false);
  }

  void weather(float temperature) {
    automaticOn_ = !isnan(temperature) && temperature >= 35.0f;
    if (!manual_) write(automaticOn_);
  }

  template <typename Output>
  void printState(Output &output) const {
    const bool rainOn = rainState_();
    output.print(manual_ ? "{\"mode\":\"manual\",\"on\":" : "{\"mode\":\"auto\",\"on\":");
    output.print(on_ && rainOn ? "true" : "false");
    output.print(on_ ? ",\"d6\":true" : ",\"d6\":false");
    output.print(rainOn ? ",\"d7\":true}" : ",\"d7\":false}");
  }

  // PUT sin cuerpo: no se aceptan GET con efectos ni parametros parciales.
  // Sin CORS: los navegadores de otros origenes no pueden enviar este PUT.
  template <typename Output>
  bool handleRequest(const char *line, Output &output) {
    const char *space = strchr(line, ' ');
    if (!space) return false;
    const char *target = space + 1;
    if (strncmp(target, "/led", 4) != 0 ||
        (target[4] != ' ' && target[4] != '?')) return false;
    const char *end = strchr(target, ' ');
    bool validVersion = end && (strcmp(end, " HTTP/1.1") == 0 ||
                                strcmp(end, " HTTP/1.0") == 0);
    const bool get = space - line == 3 && strncmp(line, "GET", 3) == 0;
    const bool put = space - line == 3 && strncmp(line, "PUT", 3) == 0;
    int status = 400;
    if (validVersion) {
      const size_t length = static_cast<size_t>(end - target);
      if (!get && !put) status = 405;
      else if (get && length == 4) status = 200;
      else if (put && matches(target, length, "/led?mode=auto")) {
        if (manual_) {
          manual_ = false;
          write(automaticOn_);
          rainMode_(false, false);
        }
        status = 200;
      } else if (put && (matches(target, length, "/led?mode=manual&on=1") ||
                         matches(target, length, "/led?mode=manual&on=0"))) {
        manual_ = true;
        const bool on = target[length - 1] == '1';
        // Bloquear los callbacks antes de desarmarlos y aplicar las dos salidas.
        rainMode_(true, on);
        write(on);
        status = 200;
      }
    }
    output.println(status == 200 ? "HTTP/1.1 200 OK" :
                   status == 405 ? "HTTP/1.1 405 Method Not Allowed" :
                                   "HTTP/1.1 400 Bad Request");
    output.println("Content-Type: application/json");
    output.println("Cache-Control: no-store");
    if (status == 405) output.println("Allow: GET, PUT");
    output.println("Connection: close");
    output.println();
    if (status == 200) printState(output);
    else output.print(status == 405 ? "{\"error\":\"method_not_allowed\"}" :
                                     "{\"error\":\"invalid_request\"}");
    output.println();
    return true;
  }

 private:
  static bool matches(const char *value, size_t length, const char *expected) {
    return length == strlen(expected) && strncmp(value, expected, length) == 0;
  }
  void write(bool on) {
    on_ = on;
    digitalWrite(pin_, on ? HIGH : LOW);
  }
  unsigned char pin_;
  void (*rainMode_)(bool, bool);
  bool (*rainState_)();
  bool manual_ = false;
  bool automaticOn_ = false;
  bool on_ = false;
};
