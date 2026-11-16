"""Execute the production Shelly status reader with mocked HTTP and real ArduinoJson."""
import pathlib,subprocess,tempfile,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
def function(source,signature):
 a=source.index(signature);i=source.index('{',a)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]
reader=function((ROOT/'src/function.h').read_text(),'static ShellyValues fetchShellyValuesSnapshot(')
reader=reader.replace('.substring(0, 200)','.substr(0, 200)').replace('deserializeJson(doc, body)','deserializeJson(doc, body.c_str())')
values=function((ROOT/'src/globals.h').read_text(),'struct ShellyValues {')+';'
code=r"""
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <vector>
using std::isfinite;
"""+values+r"""
struct ShellyDevice {String ip="192.0.2.1";int gen=3;ShellyValues values;};
struct Response {int code;String body;};std::vector<Response> responses;std::vector<String> paths;
bool request(const String& path,int& code,String& body){paths.push_back(path);assert(!responses.empty());auto r=responses.front();responses.erase(responses.begin());code=r.code;body=r.body;return code==200;}
bool httpGetWithDigestAutoAuth(const String&,int,const String& path,const String&,const String&,int& code,String& body){return request(path,code,body);}
bool httpGetGen1(const String&,int,const String& path,const String&,const String&,int& code,String& body){return request(path,code,body);}
bool hasValidIPv4(const String&){return true;}
bool detectShellyGeneration(const String&,int,const String&,const String&,int&,String&,int&){return false;}
void logPrint(const String&){}
"""+reader+r"""
int main(){
 ShellyDevice d;
 responses={{404,"{\"code\":404}"},{200,"{\"id\":0,\"apower\":42.7,\"aenergy\":{\"total\":1234.5}}"}};
 auto v=fetchShellyValuesSnapshot(d,0,80,"admin","password");assert(v.ok&&!v.switchCapable&&!v.isOn);assert(std::abs(v.powerW-42.7)<0.001);assert(v.energyWh==1234.5f);assert(paths.size()==2&&paths[1]=="/rpc/PM1.GetStatus?id=0");
 d.values=v;d.values.hasReading=true;paths.clear();
 responses={{200,"{\"id\":0,\"apower\":0,\"aenergy\":{\"total\":1234.5}}"}};
 v=fetchShellyValuesSnapshot(d,0,80,"","");assert(v.ok&&!v.switchCapable&&v.powerW==0);assert(paths.size()==1&&paths[0]=="/rpc/PM1.GetStatus?id=0");
 for(const auto& body:{"{}","{\"id\":1,\"apower\":3,\"aenergy\":{\"total\":1}}","{\"id\":0,\"apower\":null,\"aenergy\":{\"total\":1}}","{\"id\":0,\"apower\":3}","{\"id\":0,\"apower\":3,\"aenergy\":{\"total\":1},\"error\":{\"code\":404}}","broken"}){
  responses={{200,body}};assert(!fetchShellyValuesSnapshot(d,0,80,"","").ok);
 }
 d.values=ShellyValues{};responses={{200,"{\"id\":0,\"output\":true,\"apower\":7.5,\"aenergy\":{\"total\":9}}"}};
 v=fetchShellyValuesSnapshot(d,0,80,"","");assert(v.ok&&v.switchCapable&&v.isOn&&v.powerW==7.5f);
 // Authentication failure must not probe PM1. Existing Gen1 fallback remains.
 paths.clear();responses={{401,""},{401,""}};assert(!fetchShellyValuesSnapshot(d,0,80,"","").ok);for(auto p:paths)assert(p!="/rpc/PM1.GetStatus?id=0");
 d.gen=1;responses={{200,"{\"relays\":[{\"ison\":false}],\"meters\":[{\"power\":2,\"total\":4}]}"}};
 v=fetchShellyValuesSnapshot(d,0,80,"","");assert(v.ok&&v.switchCapable&&!v.isOn&&v.powerW==2);
}
"""
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/host_arduino'),'-I',sys.argv[1],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production Shelly reader: PM1 fallback/cache/zero/invalid/auth, Switch and Gen1 passed')
