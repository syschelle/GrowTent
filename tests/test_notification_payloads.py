"""Execute actual notification senders using mock HTTP; decode on the receiver side."""
import pathlib,subprocess,tempfile,json,sys,urllib.parse
ROOT=pathlib.Path(__file__).resolve().parents[1]
include=pathlib.Path(sys.argv[1])
def function(source,signature):
 a=source.index(signature);i=source.index('{',a)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]
source=(ROOT/'src/function.h').read_text()
expected={'token':'app&token+=%','user':'user+id&x=y','device':'Gerät + Süd & West','title':'Zelt "A" \\ Süd & + = %\n☀️','message':'Bewässerung: Größe 🌱\r\nZeile 2\t"quoted" \\ & + = % \x01','gotifyToken':'secret+&=/?%','priority':7}
literal=lambda s:json.dumps(s,ensure_ascii=False)
code=r"""
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include <vector>
#include "notification_payload.h"
bool pushoverSent=true,gotifySent=true;int sends=0;
String pushoverAppKey=TOKEN,pushoverUserKey=USER,pushoverDevice=DEVICE,gotifyServer="gotify.invalid",gotifyToken=GOTIFY;
String sentBody,sentUrl,contentType;std::vector<std::string> logs;
struct WiFiClientSecure{void setInsecure(){}};
struct HTTPClient {
 bool begin(WiFiClientSecure&,const String& url){sentUrl=url;return true;}
 void addHeader(const String&,const String& value){contentType=value;}
 int POST(const String& body){sentBody=body;++sends;return 200;}
 String getString(){return "synthetic provider response containing an irrelevant token";}
 void end(){}
};
void logPrint(const String& line){logs.push_back(line);}
"""
for name,key in [('TOKEN','token'),('USER','user'),('DEVICE','device'),('GOTIFY','gotifyToken')]:code=code.replace(name,literal(expected[key]))
code+=function(source,'bool sendPushover(const String& message, const String& title) {')+'\n'+function(source,'bool sendGotify(const String& msg, const String& title, int priority) {')
code+='\nint main(){\nString title='+literal(expected['title'])+',message='+literal(expected['message'])+';\n'+r"""
 JsonDocument out;
 assert(sendPushover(message,title));out["pushoverBody"]=sentBody.c_str();out["pushoverType"]=contentType.c_str();
 assert(sendGotify(message,title,7));out["gotifyBody"]=sentBody.c_str();out["gotifyUrl"]=sentUrl.c_str();out["gotifyType"]=contentType.c_str();
 String controls;for(int i=0;i<32;++i)controls+=char(i);
 assert(sendGotify(controls,"",-2));out["controlBody"]=sentBody.c_str();
 assert(sendPushover("",""));out["emptyBody"]=sentBody.c_str();
 pushoverSent=gotifySent=false;const int before=sends;assert(!sendPushover(message,title)&&!sendGotify(message,title,7)&&sends==before);
 auto a=out["logs"].to<JsonArray>();for(const auto& line:logs)a.add(line);
 serializeJson(out,std::cout);
}
"""
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/host_arduino'),'-I',str(ROOT/'src'),'-I',str(include),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 result=json.loads(subprocess.check_output([str(p/'test')],text=True))
form=urllib.parse.parse_qs(result['pushoverBody'],keep_blank_values=True,strict_parsing=True)
assert form=={key:[expected[key]] for key in ['token','user','device','title','message']}|{'priority':['1']},form
assert result['pushoverType']=='application/x-www-form-urlencoded'
body=json.loads(result['gotifyBody']);assert body=={k:expected[k] for k in ['title','message','priority']},body
assert result['gotifyType']=='application/json'
assert json.loads(result['controlBody'])=={'title':'','message':''.join(chr(i) for i in range(32)),'priority':-2}
empty=urllib.parse.parse_qs(result['emptyBody'],keep_blank_values=True);assert empty['title']==[''] and empty['message']==['']
assert urllib.parse.parse_qs(urllib.parse.urlsplit(result['gotifyUrl']).query)=={'token':[expected['gotifyToken']]}
logs='\n'.join(result['logs'])
for secret in ['app&token+=%','user+id&x=y','secret+&=/?%','synthetic provider response',expected['title'],expected['message']]:assert secret not in logs
print('Notification senders: receiver-side form/JSON/UTF-8 decoding, token URL encoding, disabled providers and log redaction passed')
