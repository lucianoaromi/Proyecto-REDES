#pragma once
#include <stdint.h>
#include <string.h>

// IPv4 recientes, no conexiones TCP ni dispositivos asociados al router.
class HttpActiveClients {
 public:
  static const uint8_t CAPACITY = 8;
  static const uint32_t ACTIVE_MS = 30000;
  static bool isDataRequest(const char *line) {
    return strcmp(line, "GET /data HTTP/1.1") == 0 ||
           strcmp(line, "GET /data HTTP/1.0") == 0;
  }
  void expire(uint32_t now) {
    for (auto &entry : entries_)
      if (entry.ip && uint32_t(now - entry.last) >= ACTIVE_MS) entry.ip = 0;
    if (limited_ && uint32_t(now - overflowLast_) >= ACTIVE_MS) limited_ = false;
  }
  void record(uint32_t ip, uint32_t now) {
    expire(now);
    if (!ip) return;
    for (auto &entry : entries_) {
      if (entry.ip == ip) { entry.last = now; return; }
    }
    for (auto &entry : entries_) {
      if (!entry.ip) { entry.ip = ip; entry.last = now; return; }
    }
    // No expulsar direcciones activas. El total deja de ser exacto.
    limited_ = true;
    overflowLast_ = now;
  }
  uint8_t count() const {
    uint8_t total = 0;
    for (const auto &entry : entries_) if (entry.ip) ++total;
    return total;
  }
  template <typename Output> void printState(Output &output) const {
    output.print("{\"active\":");
    output.print(count());
    output.print(limited_ ? ",\"limited\":true}" : ",\"limited\":false}");
  }
 private:
  struct Entry { uint32_t ip; uint32_t last; };
  Entry entries_[CAPACITY] = {};
  uint32_t overflowLast_ = 0;
  bool limited_ = false;
};
