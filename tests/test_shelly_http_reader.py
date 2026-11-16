"""Compile the production HTTP response reader with a timed mock client."""
import pathlib,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
source=(ROOT/'src/function.h').read_text()
a=source.index('static bool readAllFromClient(');i=source.index('{',a)+1;depth=1
while depth:
 depth+=(source[i]=='{')-(source[i]=='}');i+=1
reader=source[a:i]
code=r"""
#include <Arduino.h>
#include <cassert>
#include <limits>
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
void delay(uint32_t ms){clockMs+=ms;}
struct WiFiClient {
 String bytes;size_t pos=0,split=0;uint32_t secondAt=0;bool keepOpen=false;
 explicit WiFiClient(const char* data):bytes(data),split(bytes.size()){}
 int available(){size_t end=clockMs<secondAt?split:bytes.size();return int(end-pos);}
 bool connected(){return keepOpen||pos<bytes.size();}
 int read(){return available()>0?static_cast<unsigned char>(bytes[pos++]):-1;}
 // Model Arduino Stream::readString: after the final byte it waits out the
 // configured 4-second Stream timeout, even when the peer has closed.
 String readString(){String out;while(available()>0)out+=char(read());clockMs+=4000;return out;}
};
"""+reader+r"""
int main(){
 String raw;
 WiFiClient complete("HTTP/1.0 200 OK\r\n\r\n{\"output\":true}");
 assert(readAllFromClient(complete,raw));assert(raw==complete.bytes);
 assert(uint32_t(clockMs-100)<20); // no trailing 4-second wait on a complete reply
 clockMs=100;WiFiClient fragmented("HTTP/1.0 200 OK\r\n\r\n{\"apower\":4.8}");
 fragmented.split=12;fragmented.secondAt=150;
 assert(readAllFromClient(fragmented,raw));assert(raw==fragmented.bytes);
 assert(clockMs>=150&&clockMs<200); // incomplete TCP fragments are still read
 clockMs=100;WiFiClient silent("");silent.keepOpen=true;
 assert(!readAllFromClient(silent,raw));assert(raw.empty());
 assert(uint32_t(clockMs-100)>=4000&&uint32_t(clockMs-100)<=6100);
 clockMs=UINT32_MAX-2;WiFiClient wrap("ok");
 assert(readAllFromClient(wrap,raw));assert(raw=="ok");assert(uint32_t(clockMs-(UINT32_MAX-2))<20);
}
"""
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/host_arduino'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production HTTP reader: prompt completion, fragmented replies, silent peer and millis wrap passed')
