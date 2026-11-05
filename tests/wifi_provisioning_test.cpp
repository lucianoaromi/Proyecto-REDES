// Ejecuta wifi_provisioning.h real con primitivas simuladas, sin red ni secretos reales.
#include "wifi_provisioning.h"
#include <cstdlib>
#include <iostream>
static int assertions=0;
static const char *AP_TEST_PASSWORD="CLAVE-FICTICIA-SOLO-TEST";
static const char *FALLBACK_TEST_SSID="fixture-fallback";
static const char *FALLBACK_TEST_PASSWORD="fixture-fallback-password";
void check(bool ok,const char *label){++assertions;if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
void reset(uint32_t now=0,bool withSavedNetwork=true){
  check(managersAlive==0,"sin gestor de la prueba anterior");
  WiFi=FakeWiFi{}; ticks=now; savedConfig={}; currentConfig={};
  if(withSavedNetwork){
    std::memcpy(savedConfig.ssid,"fixture-old",11);
    std::memcpy(savedConfig.password,"dummy-old-password",18);
  }
  writes=otaCalls=defaultSaveCalls=0;persistOk=true;Serial.log.str("");
}
void open(WifiProvisioning &p){
  p.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD); ticks+=120000; p.process();
  check(p.portalActive() && !p.ready(),"portal abierto y TLS aun no listo");
}
void submit(WifiProvisioning &p){
  activeWeb->args={{"s","fixture-new"},{"p","dummy-new-password"}};
  check(activeWeb->request("/wifisave",HTTP_POST)==202,"envio aceptado");
  check(writes==0,"no guardar antes de probar red");
  p.process();
  check(writes==0 && defaultSaveCalls==0,"save propio asincrono, sin save de biblioteca");
}
int main(){
  reset();{
    WifiProvisioning p;p.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);
    check(WiFi.beginsWithoutCredentials==1 && WiFi.beginsWithCredentials==0,
        "la red guardada tiene prioridad sobre secrets.h");
    check(Serial.log.str().find("RESTAURAR_FLASH lectura=1 ssid_presente=1 restauracion=1 ram_ssid_igual=1 ram_clave_igual=1")!=std::string::npos,
        "el diagnostico confirma la copia de flash a RAM sin imprimir valores");
    ticks=93000;p.process();
    check(!p.portalActive(),"93 s no activan el portal prematuramente");
    WiFi.linked=true;p.process();check(p.ready() && managersAlive==0,"arranque normal sin WiFiManager");
    WiFi.linked=false;ticks=600000;p.process();
    check(!p.portalActive() && !p.ready(),"perdida posterior no abre portal");
    WiFi.linked=true;p.process();check(p.ready(),"recuperacion normal");
  }
  reset();{
    WifiProvisioning p;open(p);
    for(const char *uri:{"/update","/u","/erase","/restart","/info","/close"}){
      check(activeWeb->request(uri)==404,"ruta administrativa bloqueada GET");
      check(activeWeb->request(uri,HTTP_POST,true)==404,"ruta administrativa y upload bloqueados");
    }
    check(otaCalls==0,"ningun callback OTA ejecutado");
    activeWeb->connection.address=IPAddress(192,168,1,20);
    check(activeWeb->request("/wifisave",HTTP_POST)==404,"portal inaccesible por interfaz STA");
    activeWeb->connection.address=IPAddress(192,168,4,1);
    check(activeWeb->request("/wifisave",HTTP_GET)==405,"no guardar con GET");
    activeWeb->args={{"s","fixture-new"},{"p","short"}};
    check(activeWeb->request("/wifisave",HTTP_POST)==400,"clave invalida rechazada");
    submit(p);WiFi.linked=true;WiFi.ip=false;p.process();
    check(writes==0 && p.portalActive(),"asociacion sin IPv4 no persiste");
    WiFi.ip=true;p.process();
    check(writes==1 && p.ready() && !p.portalActive(),"guardar solo con IPv4 y cerrar portal");
    check(Serial.log.str().find("FLASH_TRAS_GUARDAR lectura=1 ssid_igual=1 clave_igual=1")!=std::string::npos,
        "el diagnostico comprueba la lectura posterior de flash");
    check(managersAlive==0 && !activeWeb && WiFi.getMode()==WIFI_STA,"liberar gestor, servidor y AP");
    check(std::memcmp(savedConfig.ssid,"fixture-new",11)==0,"red nueva persistida");
    check(Serial.log.str().find("dummy-new-password")==std::string::npos,"no registrar clave de red");
    check(Serial.log.str().find(AP_TEST_PASSWORD)==std::string::npos,"no registrar clave AP");
    WiFi=FakeWiFi{};currentConfig={};WifiProvisioning reboot;
    reboot.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);
    check(std::memcmp(currentConfig.ssid,"fixture-new",11)==0,"reinicio usa red guardada nueva");
    check(WiFi.beginsWithCredentials==0,"red del portal conserva prioridad sobre secrets.h");
  }
  reset(0,false);{
    WifiProvisioning p;p.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);
    check(WiFi.beginsWithCredentials==1,"sin flash intenta credenciales locales");
    check(std::memcmp(currentConfig.ssid,FALLBACK_TEST_SSID,std::strlen(FALLBACK_TEST_SSID))==0,
        "usa el SSID local sin registrarlo");
    WiFi.linked=true;p.process();
    check(p.ready() && writes==1,"red local con IPv4 se guarda para reinicios");
    check(std::memcmp(savedConfig.ssid,FALLBACK_TEST_SSID,std::strlen(FALLBACK_TEST_SSID))==0,
        "flash contiene la red local tras conectarse");
    check(Serial.log.str().find(FALLBACK_TEST_SSID)==std::string::npos &&
        Serial.log.str().find(FALLBACK_TEST_PASSWORD)==std::string::npos,
        "no registrar credenciales locales");
    WiFi=FakeWiFi{};currentConfig={};WifiProvisioning reboot;
    reboot.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);
    check(WiFi.beginsWithoutCredentials==1 && WiFi.beginsWithCredentials==0,
        "reinicio prioriza la red local ya guardada");
  }
  reset(0,false);{
    WifiProvisioning p;p.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);
    ticks=120000;p.process();
    check(p.portalActive(),"si la red local no conecta abre el portal al vencer 120 s");
  }
  reset();{
    WifiProvisioning p;open(p);submit(p);
    ticks+=120000;p.process();
    check(writes==0 && p.portalActive(),"candidata fallida conserva flash y portal");
    ticks=420000;p.process();
    check(!p.portalActive() && managersAlive==0,"limite absoluto 5 min cierra portal");
    check(std::memcmp(currentConfig.ssid,"fixture-old",11)==0,"restaura red anterior despues del timeout");
    ticks+=29999;p.process();check(WiFi.reconnects==0,"reintento respeta 30 s");
    ticks++;p.process();check(WiFi.reconnects==1,"reintento controlado al cumplir plazo");
    check(Serial.log.str().find("RECONNECT_ACEPTADO=1")!=std::string::npos,
        "el diagnostico distingue el reintento de WiFi.begin");
    ticks+=300000;p.process();check(!p.portalActive(),"no reabrir automaticamente tras timeout");
  }
  reset();{
    WifiProvisioning p;open(p);submit(p);persistOk=false;WiFi.linked=true;p.process();
    check(writes==0 && p.portalActive(),"error de persistencia no afirma exito");
    activeWeb->request("/exit");p.process();
    check(managersAlive==0 && !p.portalActive(),"salida manual libera portal");
  }
  reset();{
    WifiProvisioning p;p.begin("",FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);ticks=120000;p.process();
    check(managersAlive==0 && !p.portalActive(),"sin clave AP nunca crea red abierta");
  }
  reset(UINT32_MAX-50000);{
    WifiProvisioning p;p.begin(AP_TEST_PASSWORD,FALLBACK_TEST_SSID,FALLBACK_TEST_PASSWORD);ticks+=119999;p.process();
    check(!p.portalActive(),"rollover espera completa");ticks++;p.process();
    check(p.portalActive(),"rollover abre al cumplir 120 s");
    ticks+=299999;p.process();check(p.portalActive(),"portal abierto antes del limite");
    ticks++;p.process();check(!p.portalActive() && managersAlive==0,"rollover cierra al cumplir limite");
  }
  std::cout<<"PASS: "<<assertions<<" verificaciones; tiempos, persistencia, IPv4, OTA, aislamiento STA, cierre y rollover.\n";
}
