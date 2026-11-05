#pragma once
#include "ESP8266WiFi.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
class ESP8266WebServer;
class RequestHandler {
 public:
  virtual ~RequestHandler()=default;
  virtual bool canHandle(HTTPMethod,const String&) {return false;}
  virtual bool canUpload(const String&) {return false;}
  virtual bool handle(ESP8266WebServer&,HTTPMethod,const String&) {return false;}
  virtual void upload() {}
};
inline int otaCalls=0, defaultSaveCalls=0, managersAlive=0;
inline ESP8266WebServer *activeWeb=nullptr;
class ESP8266WebServer {
 public:
  struct Client { IPAddress address{192,168,4,1}; IPAddress localIP() const {return address;} } connection;
  struct Function : RequestHandler {
    String path; HTTPMethod method; std::function<void()> fn,up;
    Function(const char *p,HTTPMethod m,std::function<void()> f,std::function<void()> u):path(p),method(m),fn(f),up(u){}
    bool canHandle(HTTPMethod m,const String &p) override {return path==p && (method==HTTP_ANY || method==m);}
    bool canUpload(const String &p) override {return bool(up) && canHandle(HTTP_POST,p);}
    bool handle(ESP8266WebServer&,HTTPMethod,const String&) override {fn();return true;}
    void upload() override {if(up)up();}
  };
  std::vector<std::unique_ptr<RequestHandler>> handlers;
  std::map<std::string,String> args;
  HTTPMethod method_=HTTP_GET;
  int response=0;
  ESP8266WebServer(){activeWeb=this;}
  ~ESP8266WebServer(){activeWeb=nullptr;}
  Client &client(){return connection;}
  HTTPMethod method() const {return method_;}
  String arg(const char *key){return args[key];}
  void send(int code,const char*,const char*){response=code;}
  void addHandler(RequestHandler *h){handlers.emplace_back(h);}
  void on(const char *p,HTTPMethod m,std::function<void()> fn,std::function<void()> up={}){
    addHandler(new Function(p,m,fn,up));
  }
  int request(const char *p,HTTPMethod m=HTTP_GET,bool upload=false){
    method_=m; response=0;
    // Mismo orden del core 3.1.2: primer handler; upload solo en ese handler.
    for(auto &h:handlers) if(h->canHandle(m,p)){
      if(upload && h->canUpload(p))h->upload();
      h->handle(*this,m,p); return response;
    }
    return 404;
  }
};
class WiFiManager {
  std::function<void()> callback;
  bool active=false;
 public:
  std::unique_ptr<ESP8266WebServer> server;
  WiFiManager(){++managersAlive;}
  ~WiFiManager(){--managersAlive;}
  void setDebugOutput(bool){}
  void setRestorePersistent(bool){}
  void setShowPassword(bool){}
  void setConfigPortalBlocking(bool){}
  void setConfigPortalTimeout(unsigned){}
  void setMenu(const char**,uint8_t){}
  void setWebServerCallback(std::function<void()> f){callback=f;}
  bool startConfigPortal(const char*,const char*){
    active=true; WiFi.mode(WIFI_AP); server.reset(new ESP8266WebServer);
    callback();
    server->on("/wifisave",HTTP_ANY,[]{++defaultSaveCalls;});
    server->on("/update",HTTP_ANY,[]{++otaCalls;});
    server->on("/u",HTTP_POST,[]{++otaCalls;},[]{++otaCalls;});
    return false; // Como la biblioteca real en modo no bloqueante.
  }
  bool getConfigPortalActive() const {return active;}
  void process(){}
  void stopConfigPortal(){active=false;server.reset();WiFi.mode(WIFI_STA);}
};
