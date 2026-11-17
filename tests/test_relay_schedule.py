"""Compile the production scheduler and verify physical relay transitions."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class RelayScheduleTests(unittest.TestCase):
    def test_physical_transitions(self):
        source = (ROOT / 'src/function.h').read_text()
        scheduler = function(source, 'static void applyRelaySchedules()')
        first = 3 if 'CONTROL_RELAY_FIRST_IDX' in scheduler else 0
        code = r"""
#include <Arduino.h>
#include <cassert>
#include <ctime>
constexpr int HIGH=1, LOW=0, pdTRUE=1;
constexpr int BOARD_RELAY_COUNT=6, CONTROL_RELAY_FIRST_IDX=3;
int activeRelayCount=8, minuteNow=0, fanIndex=-1, writes=0, offWrites=0;
bool clockReady=true, climateFresh=true;
void* relayMutex=nullptr;
int relayPins[8]={0,1,2,3,4,5,6,7}, levels[8]={};
bool relayStates[8]={};
struct RelaySchedule {bool enabled=false, ifLightOff=false; int onMin=0, offMin=0;};
struct ShellyDevice {};
struct ShellyValues {bool ok=true, switchCapable=true, isOn=true;};
ShellyValues light;
struct Settings {
 struct {ShellyDevice light;} shelly;
 struct {RelaySchedule schedule[8];} relay;
 struct {int sourceType=0, Relay=0;} heating;
} settings;
bool getLocalTime(tm* t,int) {t->tm_min=minuteNow;return clockReady;}
ShellyValues shellyCachedValues(const ShellyDevice&) {return light;}
bool humidifierFanOwnsRelay(int i) {return i==fanIndex;}
bool climateReadingsFresh() {return climateFresh;}
int digitalRead(int pin) {return levels[pin];}
void digitalWrite(int pin,int value) {levels[pin]=value;++writes;if(value==LOW)++offWrites;}
int pdMS_TO_TICKS(int v) {return v;}
int xSemaphoreTake(void*,int) {return pdTRUE;}
void xSemaphoreGive(void*) {}
void logPrint(const String&) {}
""" + function(source, 'static bool isMinuteInWindowHour(') + scheduler
        code += r"""
int main() {
 const int target=FIRST_RELAY;
 auto& sc=settings.relay.schedule[target];
 sc={true,true,0,0};
 applyRelaySchedules();
 assert(levels[target]==HIGH && writes==1);
 // Invalid, OFF and meter-only samples must never pulse an independent fan OFF.
 for(int minute=0;minute<60;++minute) {
  minuteNow=minute;
  for(int state=0;state<4;++state) {
   light={state!=0,state!=1,state==3};
   applyRelaySchedules();
   assert(levels[target]==HIGH && relayStates[target]);
  }
 }
 assert(writes==1 && offWrites==0);
 // Timed windows remain effective even when light status is unknown.
 light={false,false,false};
 for(int wrap=0;wrap<2;++wrap) {
  sc.onMin=wrap?50:10;sc.offMin=wrap?10:20;
  for(int minute=0;minute<60;++minute) {
   minuteNow=minute;applyRelaySchedules();
   bool expected=wrap?(minute>=50 || minute<10):(minute>=10 && minute<20);
   assert((levels[target]==HIGH)==expected);
  }
 }
 sc={true,false,0,0};
 light={true,true,true};applyRelaySchedules();assert(levels[target]==HIGH);
 light.ok=false;applyRelaySchedules();assert(levels[target]==LOW);
 light={true,true,false};applyRelaySchedules();assert(levels[target]==LOW);
 light={true,false,true};applyRelaySchedules();assert(levels[target]==LOW);
 light={true,true,true};applyRelaySchedules();assert(levels[target]==HIGH);
 // Heating fault protection still overrides the independent schedule.
 sc.ifLightOff=true;settings.heating.sourceType=1;settings.heating.Relay=target+1;
 climateFresh=false;applyRelaySchedules();assert(levels[target]==LOW);
 climateFresh=true;applyRelaySchedules();assert(levels[target]==HIGH);
 settings.heating.sourceType=0;
 // Disabled/invalid schedules, unready clock and fan ownership preserve state.
 sc.enabled=false;levels[target]=LOW;int before=writes;
 applyRelaySchedules();assert(levels[target]==LOW && writes==before);
 sc.enabled=true;sc.onMin=-1;applyRelaySchedules();assert(writes==before);
 sc.onMin=0;clockReady=false;applyRelaySchedules();assert(writes==before);
 clockReady=true;fanIndex=target;applyRelaySchedules();assert(writes==before);
 fanIndex=-1;
 // Pump/unscheduled relays are never touched by this scheduler.
 for(int i=0;i<8;++i) settings.relay.schedule[i]={true,true,0,0};
 applyRelaySchedules();
 for(int i=0;i<8;++i) {
  bool scheduled=FIRST_RELAY==3?(i>=3 && i<6):(i<5);
  assert((levels[i]==HIGH)==scheduled);
 }
}
""".replace('FIRST_RELAY', str(first))
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp)
            (path / 'test.cpp').write_text(code)
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'tests/host_arduino'),
                            str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
