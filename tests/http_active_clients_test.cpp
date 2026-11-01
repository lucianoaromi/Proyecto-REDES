#include "http_active_clients.h"
#include <cassert>
#include <iostream>
#include <sstream>
struct Print {
  std::ostringstream out;
  void print(const char *s) { out << s; }
  void print(uint8_t n) { out << unsigned(n); }
};
int main() {
  HttpActiveClients c;
  assert(c.count() == 0);
  c.record(1, 100); assert(c.count() == 1);
  c.record(1, 200); assert(c.count() == 1); // Dos pestanas, una IPv4.
  c.record(2, 300); assert(c.count() == 2);
  c.expire(30199); assert(c.count() == 2);
  c.expire(30200); assert(c.count() == 1);
  c.expire(30300); assert(c.count() == 0);
  c.record(0, 0); assert(c.count() == 0);
  c.record(1, UINT32_MAX - 10000);
  c.expire(19998); assert(c.count() == 1);
  c.expire(19999); assert(c.count() == 0);
  HttpActiveClients full;
  for (uint32_t ip=1; ip<=8; ++ip) full.record(ip, 100);
  full.record(9, 200); assert(full.count() == 8);
  Print limited; full.printState(limited);
  assert(limited.out.str() == "{\"active\":8,\"limited\":true}");
  // El noveno no reemplaza al primero: actualizarlo conserva el registro.
  full.record(1, 300);
  full.expire(30100); assert(full.count() == 1);
  Print uncertain; full.printState(uncertain);
  assert(uncertain.out.str() == "{\"active\":1,\"limited\":true}");
  full.record(9, 30100); assert(full.count() == 2);
  full.expire(30200);
  Print exact; full.printState(exact);
  assert(exact.out.str() == "{\"active\":2,\"limited\":false}");
  full.expire(60100); assert(full.count() == 0);
  HttpActiveClients wrap;
  for (uint32_t ip=1; ip<=9; ++ip) wrap.record(ip, UINT32_MAX-10000);
  wrap.expire(19999);
  Print empty; wrap.printState(empty);
  assert(empty.out.str() == "{\"active\":0,\"limited\":false}");
  assert(HttpActiveClients::isDataRequest("GET /data HTTP/1.1"));
  assert(HttpActiveClients::isDataRequest("GET /data HTTP/1.0"));
  for (auto line : {"POST /data HTTP/1.1", "GET /database HTTP/1.1", "GET /data HTTP/1.1junk", "junk GET /data", "GET /data?x HTTP/1.1"})
    assert(!HttpActiveClients::isDataRequest(line));
  std::cout << "PASS: IPv4 unicas, actividad, 30 s, saturacion sin expulsion, limite inferior, wrap y GET valido. sizeof=" << sizeof(c) << '\n';
}
