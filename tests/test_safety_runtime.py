"""Execute production pump/relay, climate freshness and sample paths with fault injection.
No GPIO, network, controller, flash or push provider is contacted.
"""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
def extract(source, signature):
    start=source.index(signature);pos=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[pos]=='{')-(source[pos]=='}');pos+=1
    return source[start:pos]
class SafetyRuntimeTests(unittest.TestCase):
    def compile_run(self, text):
        with tempfile.TemporaryDirectory() as d:
            path=pathlib.Path(d)
            (path/'Arduino.h').write_text('#pragma once\n#include <cstdint>\nuint32_t millis();\n')
            (path/'test.cpp').write_text(text)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(path),'-I',str(ROOT/'src'),str(path/'test.cpp'),str(ROOT/'src/climate_safety.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)
    def test_pump_retries_and_heater_gate(self):
        source=(ROOT/'src/function.h').read_text()
        is_s3=(ROOT/'src/board_profile.h').exists()
        board='#include "board_profile.h"\n' if is_s3 else 'bool isPumpRelayIndex(int i){return i>=5&&i<=7;}\n'
        text=r"""
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include "climate_safety.h"
using String=std::string;
constexpr int NUM_RELAYS=8,IRRIGATION_PUMP_COUNT=3,HIGH=1,LOW=0,pdTRUE=1;
constexpr int IRRIGATION_PUMP_RELAY_INDEX[3]={PUMP_MAPPING};
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
int relayPins[8]={0,1,2,3,4,5,6,7};int activeRelayCount=ACTIVE_COUNT;
bool gpio[8]={},relayStates[8]={},relayActive[8]={};unsigned long relayOffTime[8]={};
bool relayMutex=true,lockAvailable=true,irrigationStopRequested=false,irrigationStartNotifyPending=false;
int ignoreOutput=-1;
struct {struct {int sourceType=0,Relay=0;} heating;} settings;
struct {bool pumpEnabled[3]={true,true,true};int irrigationRuns=0;String wTimeLeft;} irrigation;
int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(bool,int){return lockAvailable?pdTRUE:0;}
void xSemaphoreGive(bool){}
void logPrint(const String&){}
bool humidifierFanOwnsRelay(int){return false;}
void digitalWrite(int pin,int value){if(pin!=ignoreOutput)gpio[pin]=value==HIGH;}
int digitalRead(int pin){return gpio[pin]?HIGH:LOW;}
""".replace('PUMP_MAPPING','0,1,2' if is_s3 else '5,6,7').replace('ACTIVE_COUNT','6' if is_s3 else '8')
        text+=board+'\n'.join(extract(source,sig) for sig in ['bool isRelayOn(', 'bool setRelay(', 'static void processPumpAutoOff()', 'static bool stopAllIrrigationPumps()', 'static void requestIrrigationStop(const char* source) {', 'static void processClimateSafety()'])
        text+=r"""
int main(){
 const int pump=IRRIGATION_PUMP_RELAY_INDEX[0];
 gpio[pump]=true;relayStates[pump]=true;relayActive[pump]=true;relayOffTime[pump]=50;
 lockAvailable=false;processPumpAutoOff();assert(gpio[pump]&&relayActive[pump]);
 lockAvailable=true;ignoreOutput=pump;processPumpAutoOff();assert(gpio[pump]&&relayActive[pump]);
 ignoreOutput=-1;processPumpAutoOff();assert(!gpio[pump]&&!relayActive[pump]&&relayOffTime[pump]==0);
 // Failed explicit stop must arm an immediate OFF retry even without a pulse timer.
 gpio[pump]=true;relayActive[pump]=false;lockAvailable=false;irrigation.irrigationRuns=2;
 requestIrrigationStop("test");assert(irrigationStopRequested&&irrigation.irrigationRuns==0&&relayActive[pump]);
 assert(!stopAllIrrigationPumps());lockAvailable=true;processPumpAutoOff();assert(!gpio[pump]&&!relayActive[pump]);assert(stopAllIrrigationPumps());
 // Deadline arithmetic survives millis rollover.
 gpio[pump]=true;relayActive[pump]=true;relayOffTime[pump]=0xfffffff0UL;clockMs=4;processPumpAutoOff();assert(!gpio[pump]&&!relayActive[pump]);
 // Heater ON is blocked without a fresh sample, OFF retries bypass controller dwell time.
 settings.heating.sourceType=1;settings.heating.Relay=4;gpio[3]=true;lockAvailable=false;processClimateSafety();assert(gpio[3]);
 lockAvailable=true;processClimateSafety();assert(!gpio[3]);assert(!setRelay(3,true));
 climateRecordSample(true,clockMs);assert(setRelay(3,true));clockMs+=CLIMATE_MAX_AGE_MS;processClimateSafety();assert(!gpio[3]&&!setRelay(3,true));
 climateRecordSample(true,clockMs);assert(setRelay(3,true));climateRecordSample(false,clockMs);processClimateSafety();assert(!gpio[3]);
 // A valid sample must not override S3 pump ownership/stop checks.
 settings.heating.sourceType=0;climateRecordSample(true,clockMs);
 S3_INTERLOCK_CHECKS
}
""".replace('S3_INTERLOCK_CHECKS',r'irrigationStopRequested=true;assert(!setRelay(pump,true));irrigationStopRequested=false;gpio[1]=relayStates[1]=true;assert(!setRelay(pump,true));' if is_s3 else '')
        self.compile_run(text)
    def test_invalid_sample_stale_age_and_recovery(self):
        source=(ROOT/'src/runtime.h').read_text()
        smooth=extract(source,'float getAdaptiveAlpha(')+'\n'+extract(source,'void updateSmoothedClimate(')
        prefix=source[source.index('String readSensorData() {'):source.index('  // === JSON BUILDING (unchanged) ===')]
        prefix=prefix.replace('String readSensorData() {','void readClimateSample() {')+'}\n'
        text=r"""
#include <cassert>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstring>
#include "climate_safety.h"
using std::isnan;using std::isfinite;
uint32_t clockMs=1000;uint32_t millis(){return clockMs;}
struct {float temperatureRawC=NAN,humidityRawPct=NAN,vpdRawKpa=NAN;
float temperatureSmoothedC=NAN,humiditySmoothedPct=NAN,alphaTempEffective=NAN,alphaHumidityEffective=NAN;
float temperatureC=NAN,humidityPct=NAN,vpdSmoothedKpa=NAN,vpdKpa=NAN,extTempC=NAN;} cur;
struct {float t=22,h=55;float readTemperature(){return t;}float readHumidity(){return h;}} bme;
struct {bool isConversionComplete(){return false;}float getTempCByIndex(int){return 0;}void requestTemperatures(){}} sensors;
bool DS18B20=false,bmeAvailable=true,ledState=false;float DS18B20STemperature=NAN,offsetLeafTemperature=-1.5;
uint32_t previousMillis=0,blinkInterval=1000,lastRead=0,READ_INTERVAL_MS=1000;
constexpr float DEVICE_DISCONNECTED_C=-127;constexpr int STATUS_LED_PIN=2;
void digitalWrite(int,bool){}
bool getLocalTime(tm*){return false;}
#include "vpd_calculation.h"
"""+smooth+prefix+r"""
int main(){
 assert(!climateReadingsFresh()&&climateReadingAgeMs()==UINT32_MAX);assert(!strcmp(climateReadingStatus(),"unavailable"));
 readClimateSample();assert(climateReadingsFresh()&&isfinite(cur.temperatureC));
 bme.t=NAN;clockMs+=1000;readClimateSample();assert(!climateReadingsFresh()&&isnan(cur.temperatureC)&&isnan(cur.vpdKpa));assert(!strcmp(climateReadingStatus(),"invalid"));
 bme.t=23;bme.h=56;clockMs+=1000;readClimateSample();assert(climateReadingsFresh()&&isfinite(cur.vpdKpa));
 bme.h=INFINITY;clockMs+=1000;readClimateSample();assert(!climateReadingsFresh()&&isnan(cur.humidityPct));
 bme.h=101;clockMs+=1000;readClimateSample();assert(!climateReadingsFresh());
 bme.h=50;clockMs+=1000;readClimateSample();assert(climateReadingsFresh());
 clockMs+=CLIMATE_MAX_AGE_MS-1;assert(climateReadingsFresh());++clockMs;assert(!climateReadingsFresh());assert(!strcmp(climateReadingStatus(),"stale"));
 clockMs=0xfffffff0U;climateRecordSample(true,clockMs);clockMs=15;assert(climateReadingsFresh()&&climateReadingAgeMs()==31);
 climateRecordSample(false,clockMs);assert(!climateReadingsFresh());
 climateRecordSample(true,0);clockMs=0;assert(climateReadingsFresh());
}
"""
        self.compile_run(text)
    def test_shelly_off_worker_backoff_and_recovery(self):
        function=extract((ROOT/'src/function.h').read_text(),'static bool processClimateSafetyShellyOff()')
        text=r"""
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include "climate_safety.h"
struct String:std::string{using std::string::string;using std::string::operator=;bool isEmpty()const{return empty();}};
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
constexpr int WL_CONNECTED=3;bool online=true;
struct {int status(){return online?WL_CONNECTED:0;}} WiFi;
struct ShellyDevice{String ip;int gen=2;};
struct {struct{ShellyDevice humidifier,heater;} shelly;struct{int sourceType=2;} heating;} settings;
std::vector<std::string> calls;bool accepted=false;
bool shellySwitchOff(const String& host,int,int,int){calls.push_back(host);return accepted;}
void logPrint(const std::string&){}
"""+function+r"""
int main(){
 settings.shelly.humidifier.ip="humidifier";settings.shelly.heater.ip="heater";
 climateRecordSample(true,clockMs);assert(!processClimateSafetyShellyOff()&&calls.empty());
 climateRecordSample(false,clockMs);assert(processClimateSafetyShellyOff());assert(processClimateSafetyShellyOff());assert(calls.size()==2);assert(!processClimateSafetyShellyOff());
 // A failure never suppresses later retries; one request per call and rate limited.
 clockMs+=9999;assert(!processClimateSafetyShellyOff());++clockMs;accepted=true;
 assert(processClimateSafetyShellyOff());assert(processClimateSafetyShellyOff());assert(calls.size()==4);
 climateRecordSample(true,clockMs);assert(!processClimateSafetyShellyOff());
 // Recovery ends network actions; a subsequent fault gets immediate attempts.
 climateRecordSample(false,clockMs);assert(processClimateSafetyShellyOff());assert(processClimateSafetyShellyOff());assert(calls.size()==6);
 online=false;clockMs+=10000;assert(!processClimateSafetyShellyOff());online=true;
 settings.heating.sourceType=0;assert(processClimateSafetyShellyOff());assert(calls.back()=="humidifier");assert(!processClimateSafetyShellyOff());
 settings.shelly.humidifier.ip="";clockMs+=10000;assert(!processClimateSafetyShellyOff());
}
"""
        self.compile_run(text)
    def test_worker_and_api_wiring(self):
        f=(ROOT/'src/function.h').read_text();m=(ROOT/'src/main.cpp').read_text();worker=(ROOT/'src/task_CheckShellyStatus.h').read_text();task=(ROOT/'src/task_Watering.h').read_text();registry=(ROOT/'src/vars_registry.cpp').read_text()
        self.assertIn('processClimateSafetyShellyOff()',worker)
        self.assertIn('if (!stopAllIrrigationPumps()) return true;',m)
        self.assertIn('if (!stopAllIrrigationPumps()) return;',task)
        self.assertIn('if (!stopAllIrrigationPumps()) { vTaskDelay',task)
        self.assertIn('host == settings.shelly.humidifier.ip',f)
        self.assertIn('host == settings.shelly.heater.ip',f)
        self.assertIn('on && switchId == 0 && !climateReadingsFresh()',f)
        for key in ['fresh','status','ageMs']:self.assertIn('sensors.climate.'+key,registry)
        for field in ['temperatureC','humidityPct','vpdKpa']:self.assertIn('climateReadingsFresh() ? cur.'+field+' : NAN',registry)
        loop=m[m.index('void loop() {'):]
        self.assertLess(loop.index('processClimateSafety();'),loop.index('otaProcessQueue();'))
        self.assertIn('!climateReadingsFresh() || !isfinite(cur.temperatureC)',f)
        self.assertIn('!climateReadingsFresh() || !isfinite(cur.vpdKpa)',f)
if __name__=='__main__':unittest.main()
