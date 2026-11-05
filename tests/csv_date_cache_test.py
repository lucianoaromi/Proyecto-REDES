"""CSV real y rutas /csv, /clearcsv con SPIFFS simulado, sin hardware ni red."""
import argparse
from pathlib import Path
import re
import subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--compiler', required=True)
parser.add_argument('--output', type=Path, required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=(root/'ESP8266_DHT22/ESP8266_DHT22.ino').read_text(encoding='utf-8')
def function(name):
    m=re.search(r'^(?:String|void|bool) '+name+r'\([^\n]*\) \{',source,re.M)
    assert m,name
    return source[m.start():source.index('\n}',m.start())+2]
def route(name,next_name):
    return source.split(f'  if (requestLine.startsWith("GET /{name} ")) {{\n',1)[1].split(f'\n  // /{next_name}',1)[0].rsplit('\n  }',1)[0]
prefix=r'''
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include "csv_weather_format.h"
using __FlashStringHelper=char;
#define F(s) s
#define PERF_CSV_BEGIN(...) ((void)0)
#define PERF_HTTP_ROUTE(...) ((void)0)
#define PERF_HTTP_PHASE(...) ((void)0)
struct String: std::string {
  using std::string::string;
  String(const std::string &s):std::string(s){}
  String substring(size_t a,size_t b) const{return substr(a,b-a);}
  void toCharArray(char *out,size_t count)const{size_t n=std::min(size(),count-1);memcpy(out,data(),n);out[n]=0;}
};
void yield(){}
struct { template<class T> void println(const T&){} template<class...T>void printf(const char*,T...){} } Serial;
struct FSInfo { size_t totalBytes, usedBytes, blockSize; };
struct FakeFs {
  std::map<std::string,std::string> files;
  std::string denyOpen, denyWrite, denyRead, denyRenameFrom, denyRenameTo, denyRemove;
  bool infoOk=true, full=false, shortWrite=false, flushError=false;
  size_t reads=0,writes=0;
  bool exists(const char *path)const{return files.count(path)!=0;}
  bool remove(const char *path){if(denyRemove==path)return false;return files.erase(path)!=0;}
  bool rename(const char *a,const char *b){
    if(denyRenameFrom==a || denyRenameTo==b || !exists(a) || exists(b))return false;
    files[b]=files.at(a);files.erase(a);return true;
  }
  bool info(FSInfo &out){out={2000000,full?2000000U:100000U,4096};return infoOk;}
  struct File {
    FakeFs *fs=nullptr; std::string path; size_t pos=0; char mode='r'; bool ok=false,err=false;
    explicit operator bool()const{return ok;}
    bool available()const{return ok && mode=='r' && pos<fs->files.at(path).size();}
    size_t size()const{return ok?fs->files.at(path).size():0;}
    int read(){if(!available() || fs->denyRead==path)return -1;++fs->reads;return static_cast<unsigned char>(fs->files.at(path)[pos++]);}
    size_t write(const uint8_t *data,size_t n){
      ++fs->writes;
      if(!ok || fs->denyWrite==path){err=true;return 0;}
      size_t length=fs->shortWrite?std::min(n,size_t(10)):n;
      fs->files[path].append(reinterpret_cast<const char*>(data),length);
      return length;
    }
    size_t println(const char *s){std::string text=std::string(s)+"\r\n";return write(reinterpret_cast<const uint8_t*>(text.data()),text.size());}
    int getWriteError()const{return err?1:0;}
    void flush(){if(fs && fs->flushError)err=true;}
    void close(){ok=false;}
  };
  File open(const char *path,const char *mode){
    if(denyOpen==path || (mode[0]=='r' && !exists(path)))return {};
    if(mode[0]=='w')files[path].clear();
    if(mode[0]=='a' && !exists(path))files[path]="";
    return {this,path,0,mode[0],true,false};
  }
} SPIFFS;
using File=FakeFs::File;
struct Client {
  std::string response;size_t maxWrite=512;bool fail=false;int stops=0;
  size_t write(const uint8_t *data,size_t n){if(fail)return 0;n=std::min(n,maxWrite);response.append(reinterpret_cast<const char*>(data),n);return n;}
  void print(const std::string &s){response+=s;}
  void println(const std::string &s=""){print(s+"\r\n");}
  void stop(){++stops;}
};
const char* LOG_FILE="/historial_clima.csv";
const char* LEGACY_LOG_FILE="/historial.csv";
const char* LOG_TEMP_FILE="/historial_clima.tmp";
const char* LOG_BACKUP_FILE="/historial_clima.bak";
const unsigned long MAX_LOG_LINES=8640;
bool spiffsOk=true,csvReady=false,csvReadable=false;
unsigned long logLines=0;
char primeraFechaCSV[11]="",ultimaFechaCSV[11]="",primeraFechaLegacy[11]="",ultimaFechaLegacy[11]="";
float tempApi=NAN,humApi=NAN;
time_t weatherLastSuccessTime=0;
int checks=0;
void check(bool ok,const char *label){++checks;if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
'''
suffix=r'''
void reset(){SPIFFS=FakeFs{};spiffsOk=true;csvReady=false;csvReadable=false;tempApi=humApi=NAN;weatherLastSuccessTime=0;}
std::string header(){return std::string(WeatherCsv::HEADER)+"\r\n";}
int main(){
 reset();
 std::string original="FechaHora,Temperatura,Hum.edad\r\n";
 for(int i=0;i<5291;++i)original+=(i==5290?"18/10/2026 12:00:00,24.0,50\r\n":"01/10/2026 12:00:00,20.0,40\r\n");
 SPIFFS.files[LEGACY_LOG_FILE]=original;
 iniciarLog();
 check(csvReady && logLines==0,"arranque prepara v2");
 check(SPIFFS.files.at(LEGACY_LOG_FILE)==original,"5292 lineas legadas intactas byte a byte");
 check(SPIFFS.files.at(LOG_FILE)==header(),"cabecera nueva de seis columnas");
 const size_t reads=SPIFFS.reads;String first,last;
 for(int i=0;i<10;++i)obtenerFechasCSV(first,last);
 check(first=="01/10/2026" && last=="18/10/2026" && SPIFFS.reads==reads,"csvinfo sin acceso al archivo");
 check(obtenerNombreCSV()=="historial_20261001_20261018.csv" && SPIFFS.reads==reads,"nombre combinado sin recorrido");
 agregarRegistroLog("01/11/2026 12:00:00",25.1f,51);
 check(SPIFFS.files.at(LOG_FILE)==header()+"01/11/2026 12:00:00,25.1,51,,,\r\n","local sin API: tres campos exteriores vacios");
 tempApi=0;humApi=0;weatherLastSuccessTime=1792311000;
 char stamp[20];auto date=localtime(&weatherLastSuccessTime);strftime(stamp,sizeof(stamp),"%d/%m/%Y %H:%M:%S",date);
 agregarRegistroLog("01/11/2026 12:05:00",26,52);
 check(SPIFFS.files.at(LOG_FILE).find(std::string(",26.0,52,0.0,0,")+stamp+"\r\n")!=std::string::npos,"cero meteorologico real no equivale a ausente");
 tempApi=32.4f;humApi=64;
 agregarRegistroLog("01/11/2026 12:10:00",26,53);
 // Un fallo externo no modifica las variables del ultimo exito.
 agregarRegistroLog("01/11/2026 12:15:00",27,54);
 check(SPIFFS.files.at(LOG_FILE).find(std::string(",27.0,54,32.4,64,")+stamp)!=std::string::npos,"repite ultimo clima y fecha original sin nueva consulta");
 const auto saved=SPIFFS.files;
 SPIFFS.denyOpen=LOG_FILE;agregarRegistroLog("02/11/2026 12:00:00",30,60);
 check(SPIFFS.files==saved && logLines==4,"error abrir no cambia registros");SPIFFS.denyOpen.clear();
 SPIFFS.full=true;agregarRegistroLog("02/11/2026 12:00:00",30,60);
 check(SPIFFS.files==saved && csvReady,"sin espacio no borra ni bloquea intentos futuros");SPIFFS.full=false;
 SPIFFS.infoOk=false;agregarRegistroLog("02/11/2026 12:00:00",30,60);
 check(SPIFFS.files==saved,"error info conserva datos");SPIFFS.infoOk=true;
 spiffsOk=false;agregarRegistroLog("02/11/2026 12:00:00",30,60);check(SPIFFS.files==saved,"sin montaje no escribe");spiffsOk=true;
 Client download;download.maxWrite=7;csvDownload(download);
 check(download.stops==1 && download.response.find("HTTP/1.1 200 OK")==0,"descarga HTTP");
 auto body=download.response.substr(download.response.find("\r\n\r\n")+4);
 check(body.find(header())==0,"un solo encabezado de seis columnas");
 check(body.find("18/10/2026 12:00:00,24.0,50,,,\r\n")!=std::string::npos,"exporta legado con celdas vacias");
 check(body.find(std::string(",27.0,54,32.4,64,")+stamp)!=std::string::npos,"exporta meteorologia y fecha");
 check(SPIFFS.files==saved,"descarga no altera archivos");
 iniciarLog();check(csvReady && logLines==4 && std::string(ultimaFechaCSV)=="01/11/2026","reinicio conserva resumen de ambos");
 logLines=MAX_LOG_LINES-1;agregarRegistroLog("02/11/2026 12:00:00",30,60);
 check(csvReady && logLines==0 && SPIFFS.files.at(LOG_FILE)==header(),"rotacion v2 conserva limite original");
 check(SPIFFS.files.at(LEGACY_LOG_FILE)==original && std::string(ultimaFechaCSV)=="18/10/2026","rotacion no elimina legado y restaura fechas");
 check(!SPIFFS.exists(LOG_TEMP_FILE) && !SPIFFS.exists(LOG_BACKUP_FILE),"rotacion completa limpia auxiliares");
 agregarRegistroLog("03/11/2026 12:00:00",31,61);
 auto full=SPIFFS.files.at(LOG_FILE);
 SPIFFS.denyWrite=LOG_TEMP_FILE;check(!rotarCSV(),"fallo crear reemplazo");
 check(SPIFFS.files.at(LOG_FILE)==full && SPIFFS.files.at(LEGACY_LOG_FILE)==original,"error reemplazo conserva fuente");SPIFFS.denyWrite.clear();
 SPIFFS.denyRenameFrom=LOG_TEMP_FILE;check(!rotarCSV(),"fallo rename reemplazo");
 check(SPIFFS.files.at(LOG_FILE)==full,"revierte rename al archivo completo");SPIFFS.denyRenameFrom.clear();
 // Simular corte entre los dos rename: solo respaldo y temporal.
 SPIFFS.files[LOG_BACKUP_FILE]=full;SPIFFS.files.erase(LOG_FILE);SPIFFS.files[LOG_TEMP_FILE]=header();
 iniciarLog();check(csvReady && SPIFFS.files.at(LOG_FILE)==full && !SPIFFS.exists(LOG_BACKUP_FILE),"recupera corte tras apartar archivo completo");
 // Simular corte tras instalar la cabecera nueva: rotacion ya comprometida.
 SPIFFS.files[LOG_BACKUP_FILE]=full;SPIFFS.files[LOG_FILE]=header();
 iniciarLog();check(csvReady && logLines==0 && !SPIFFS.exists(LOG_BACKUP_FILE),"completa rotacion interrumpida");
 SPIFFS.files[LOG_BACKUP_FILE]=full;SPIFFS.files[LOG_FILE]="cabecera incompleta";
 auto ambiguous=SPIFFS.files;iniciarLog();check(!csvReady && SPIFFS.files==ambiguous,"estado ambiguo no destruye archivos");
 SPIFFS.files.erase(LOG_BACKUP_FILE);SPIFFS.files[LOG_FILE]=header();iniciarLog();
 SPIFFS.shortWrite=true;agregarRegistroLog("04/11/2026 12:00:00",25,50);
 check(!csvReady && logLines==0,"escritura parcial suspende append sin concatenar filas");
 auto partial=SPIFFS.files.at(LOG_FILE);SPIFFS.shortWrite=false;
 agregarRegistroLog("04/11/2026 12:05:00",25,50);check(SPIFFS.files.at(LOG_FILE)==partial,"no altera fila interrumpida");
 iniciarLog();check(!csvReady && SPIFFS.files.at(LOG_FILE)==partial,"reinicio detecta fila incompleta");
 Client partialDownload;csvDownload(partialDownload);check(partialDownload.response.find("500 Internal Server Error")!=std::string::npos,"no entrega CSV incompleto como exito");
 Client clear;csvClear(clear);
 check(clear.response.find("200 OK")!=std::string::npos && csvReady && !SPIFFS.exists(LEGACY_LOG_FILE),"clear explicito elimina ambos y crea seis columnas");
 check(SPIFFS.files.at(LOG_FILE)==header() && !primeraFechaCSV[0] && !ultimaFechaCSV[0],"clear restablece fechas");
 SPIFFS.files[LEGACY_LOG_FILE]=original;iniciarLog();SPIFFS.denyRemove=LEGACY_LOG_FILE;
 Client denied;csvClear(denied);check(denied.response.find("500 Internal Server Error")!=std::string::npos && SPIFFS.files.at(LEGACY_LOG_FILE)==original,"clear fallido responde error sin falso exito");
 reset();SPIFFS.files[LEGACY_LOG_FILE]="";SPIFFS.files[LOG_FILE]="";iniciarLog();
 check(csvReady && SPIFFS.files.at(LEGACY_LOG_FILE).empty() && SPIFFS.files.at(LOG_FILE)==header(),"archivos vacios sin perdida");
 reset();SPIFFS.denyOpen=LOG_FILE;iniciarLog();check(!csvReady,"fallo crear archivo sin datos ficticios");
 reset();SPIFFS.files[LEGACY_LOG_FILE]="formato desconocido\r\n";auto unknown=SPIFFS.files.at(LEGACY_LOG_FILE);iniciarLog();
 check(!csvReady && SPIFFS.files.at(LEGACY_LOG_FILE)==unknown,"cabecera desconocida preservada");
 Client bad;csvDownload(bad);check(bad.response.find("500 Internal Server Error")!=std::string::npos,"descarga rechaza cabecera desconocida");
 reset();iniciarLog();SPIFFS.flushError=true;agregarRegistroLog("05/11/2026 12:00:00",25,50);
 check(!csvReady && logLines==0,"error flush no confirma registro");
 std::cout<<"PASS: "<<checks<<" verificaciones CSV/clima: legado 5292 lineas, vacios/ceros/fecha, descarga, cache, espacio, errores, rotacion y recuperacion.\n";
}
'''
parts=[prefix]
for name in ['reiniciarFechasCSV','registrarFechaCSV','crearCabeceraCSV','cabeceraNuevaValida','recuperarRotacionCSV','rotarCSV','resumirCSV','iniciarLog','agregarRegistroLog','obtenerNombreCSV','obtenerFechasCSV']:
    parts.append(function(name))
parts+=['void csvDownload(Client &client) {\n'+route('csv','clearcsv')+'\n}\n','void csvClear(Client &client) {\n'+route('clearcsv','csvinfo')+'\n}\n',suffix]
args.output.mkdir(parents=True,exist_ok=True)
cpp=args.output/'csv_date_cache_under_test.cpp'
cpp.write_text('\n'.join(parts),encoding='utf-8')
binary=cpp.with_suffix('.exe')
include=str(root/'ESP8266_DHT22')
if Path(args.compiler).stem.lower()=='cl':
    cmd=[args.compiler,'/nologo','/std:c++17','/EHsc','/W4','/WX','/utf-8','/I'+include,str(cpp),'/Fo:'+str(cpp.with_suffix('.obj')),'/Fe:'+str(binary)]
else:
    cmd=[args.compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I'+include,str(cpp),'-o',str(binary)]
subprocess.run(cmd,check=True)
subprocess.run([str(binary)],check=True)
