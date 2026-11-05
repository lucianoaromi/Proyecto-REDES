#pragma once
// Dobles de las primitivas de Arduino/SDK; no emulan radio, TCP, heap ni flash real.
#include <cstdint>
#include <cstring>
#include <cctype>
#include <string>
#include <sstream>
extern "C" {
#include "user_interface.h"
}
using __FlashStringHelper = char;
#define F(s) s
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string &s) : std::string(s) {}
  void toCharArray(char *p, size_t n) const { if (n) { std::strncpy(p, c_str(), n - 1); p[n - 1] = 0; } }
};
inline uint32_t ticks = 0;
inline uint32_t millis() { return ticks; }
struct FakeSerial {
  std::ostringstream log;
  template<class T> void print(const T &v) { log << v; }
  template<class T> void println(const T &v) { log << v << '\n'; }
};
inline FakeSerial Serial;
struct FakeESP { unsigned getFreeHeap() const { return 40000; } unsigned getMaxFreeBlockSize() const { return 20000; } };
inline FakeESP ESP;
struct IPAddress {
  uint32_t value;
  IPAddress(int a, int b, int c, int d) : value((a<<24)|(b<<16)|(c<<8)|d) {}
  bool operator==(IPAddress b) const { return value==b.value; }
  bool operator!=(IPAddress b) const { return !(*this==b); }
};
enum { WIFI_OFF=0, WIFI_STA=1, WIFI_AP=2, WIFI_AP_STA=3, WL_CONNECTED=3, WL_DISCONNECTED=6 };
using wl_status_t = int;
inline station_config savedConfig{}, currentConfig{};
inline int writes=0;
inline bool persistOk=true;
extern "C" inline bool wifi_station_get_config_default(station_config *c) { *c=savedConfig; return true; }
extern "C" inline bool wifi_station_get_config(station_config *c) { *c=currentConfig; return true; }
extern "C" inline bool wifi_station_set_config_current(station_config *c) { currentConfig=*c; return true; }
extern "C" inline bool wifi_station_set_config(station_config *c) { if(!persistOk) return false; savedConfig=*c; ++writes; return true; }
struct FakeWiFi {
  bool persistentFlag=false, autoReconnect=false, linked=false, ip=true;
  int wifiMode=WIFI_STA, begins=0, beginsWithoutCredentials=0, beginsWithCredentials=0, reconnects=0;
  void persistent(bool v) { persistentFlag=v; }
  bool mode(int m) { wifiMode=m; return true; }
  int getMode() const { return wifiMode; }
  void setAutoReconnect(bool v) { autoReconnect=v; }
  void disconnect(bool) { linked=false; }
  wl_status_t begin() { ++begins; ++beginsWithoutCredentials; return status(); }
  wl_status_t begin(const char *s, const char *p) {
    ++begins; ++beginsWithCredentials; currentConfig={}; std::memcpy(currentConfig.ssid,s,std::strlen(s));
    std::memcpy(currentConfig.password,p,std::strlen(p));
    if(persistentFlag) wifi_station_set_config(&currentConfig);
    return status();
  }
  bool reconnect() { ++reconnects; return (wifiMode & WIFI_STA) != 0; }
  void enableSTA(bool v) { if(v) wifiMode|=WIFI_STA; else {wifiMode &= ~WIFI_STA; linked=false;} }
  int status() const { return linked ? WL_CONNECTED : WL_DISCONNECTED; }
  IPAddress localIP() const { return linked && ip ? IPAddress(192,168,1,20) : IPAddress(0,0,0,0); }
  IPAddress softAPIP() const { return (wifiMode & WIFI_AP) ? IPAddress(192,168,4,1) : IPAddress(0,0,0,0); }
};
inline FakeWiFi WiFi;
extern "C" inline int wifi_station_get_connect_status() { return WiFi.linked && WiFi.ip ? 5 : 1; }
