"""Compile actual cache and status publication paths; mock only platform/HTTP."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
def function(source,signature):
 a=source.index(signature);i=source.index('{',a)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]
class CacheTests(unittest.TestCase):
 def test_cache_and_late_responses(self):
  globals=(ROOT/'src/globals.h').read_text();f=(ROOT/'src/function.h').read_text()
  types='\n'.join(function(globals,'struct '+n+' {')+';' for n in ['DailySchedule','ShellySchedules','ShellyValues','ShellyDevice','ShellySettings'])
  impl=(ROOT/'src/shelly_cache.cpp').read_text().replace('#include "globals.h"','')
  wrapper=function(f,'ShellyValues getShellyValues(ShellyDevice& dev, int switchId, int port) {')
  poll=function((ROOT/'src/task_CheckShellyStatus.h').read_text(),'  auto pollDevice =')+';'
  poll='struct PollState {uint32_t epoch=0,retryAfterMs=0;bool backingOff=false;}; constexpr uint32_t RETRY_BACKOFF_MS=30000UL; void poll(ShellyDevice& d,ShellyDevice& r,PollState& s){'+poll+'pollDevice(d,r,s);}'
  code=r"""
#include <Arduino.h>
#include <cassert>
#include <cmath>
#include <functional>
#include "shelly_cache.h"
using std::isnan;using std::isinf;
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
bool hostMutexAvailable=true,hostMutexHeld=false;
"""+types+r"""
struct {ShellySettings shelly;} settings;ShellySettings shelly;
"""+impl+r"""
ShellyValues getShellyValues(ShellyDevice&,int,int=80);
std::function<void()> duringRead;
bool readOK=true;float nextPower=21;int requests=0;
ShellyValues fetchShellyValuesSnapshot(ShellyDevice& d,int,int,const String&,const String&){
 assert(!hostMutexHeld);++requests;ShellyValues v;v.ok=readOK;v.isOn=true;v.powerW=nextPower;v.energyWh=100;
 if(d.gen==0)d.gen=2;
 if(duringRead){auto action=duringRead;duringRead=nullptr;action();}
 return v;
}
bool refreshShellyLightSchedule(bool){return true;}
"""+wrapper+poll+r"""
int main(){
 assert(initShellyCache());auto& d=settings.shelly.main;d.ip="A";d.gen=2;
 assert(!shellyCachedValues(d).ok&&!shellyCachedValues(d).hasReading);
 auto v=getShellyValues(d,0,80);assert(v.ok&&shellyCachedValues(d).ok&&d.values.powerW==21);
 clockMs+=SHELLY_STATUS_MAX_AGE_MS-1;assert(shellyCachedValues(d).ok);
 noteShellySwitch("A",false);assert(d.values.sampledAtMs==100);++clockMs;assert(!shellyCachedValues(d).ok);
 expireShellyCache();assert(!d.values.ok);
 v=getShellyValues(d,0,80);assert(v.ok);readOK=false;assert(!getShellyValues(d,0,80).ok);
 assert(d.values.hasReading&&!d.values.ok&&d.values.powerW==21);
 noteShellySwitch("A",true);assert(!shellyCachedValues(d).ok);
 readOK=true;assert(getShellyValues(d,0,80).ok);
 // IP change during HTTP must invalidate even if the old request succeeds.
 duringRead=[&](){ShellyCacheLock lock;assert(lock.held);invalidateShellyCacheLocked();d.ip="B";};
 assert(!getShellyValues(d,0,80).ok);assert(!shellyCachedValues(d).ok&&!d.values.hasReading);
 // Epoch catches A->B->A and credentials-only changes, not just IP equality.
 assert(getShellyValues(d,0,80).ok);
 duringRead=[&](){ShellyCacheLock lock;invalidateShellyCacheLocked();d.ip="C";d.ip="B";};
 assert(!getShellyValues(d,0,80).ok&&!d.values.hasReading);
 assert(getShellyValues(d,0,80).ok);
 duringRead=[&](){ShellyCacheLock lock;invalidateShellyCacheLocked();settings.shelly.password="new-secret";};
 assert(!getShellyValues(d,0,80).ok&&!d.values.hasReading);
 // A newer read wins, regardless of completion order.
 nextPower=21;duringRead=[&](){nextPower=42;assert(getShellyValues(d,0,80).ok);};
 assert(!getShellyValues(d,0,80).ok);assert(d.values.powerW==42&&shellyCachedValues(d).ok);
 // Generation discovery on an unchanged snapshot is still accepted.
 {ShellyCacheLock lock;invalidateShellyCacheLocked();d.gen=0;}
 assert(getShellyValues(d,0,80).ok&&d.gen==2);
 clockMs=0xfffffff0U;assert(getShellyValues(d,0,80).ok);clockMs=15;assert(shellyCachedValues(d).ok);
 hostMutexAvailable=false;assert(!shellyCachedValues(d).ok&&!getShellyValues(d,0,80).ok);hostMutexAvailable=true;
 {ShellyCacheLock lock;invalidateShellyCacheLocked();d.ip="";}
 assert(!shellyCachedValues(d).ok&&!d.values.hasReading);
 d.ip="D";ShellyDevice mirror;PollState state;clockMs=100;readOK=true;poll(d,mirror,state);assert(mirror.values.ok);
 readOK=false;poll(d,mirror,state);assert(!mirror.values.ok&&state.backingOff);const int before=requests;
 clockMs+=29999;poll(d,mirror,state);assert(requests==before);++clockMs;poll(d,mirror,state);assert(requests==before+1&&!mirror.values.ok);
 // A config edit cancels old backoff and never resurrects the old last-good copy.
 {ShellyCacheLock lock;invalidateShellyCacheLocked();d.ip="E";}readOK=true;poll(d,mirror,state);assert(mirror.values.ok&&!state.backingOff);
 clockMs=UINT32_MAX-29999;readOK=false;poll(d,mirror,state);assert(state.backingOff&&state.retryAfterMs==0);
 const int atWrap=requests;++clockMs;poll(d,mirror,state);assert(requests==atWrap);clockMs=0;poll(d,mirror,state);assert(requests==atWrap+1);
}
"""
  with tempfile.TemporaryDirectory() as td:
   path=pathlib.Path(td);(path/'test.cpp').write_text(code)
   subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests/host_arduino'),'-I',str(ROOT/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
   subprocess.run([str(path/'test')],check=True)
 def test_worker_and_consumers(self):
  worker=(ROOT/'src/task_CheckShellyStatus.h').read_text();f=(ROOT/'src/function.h').read_text();m=(ROOT/'src/main.cpp').read_text();registry=(ROOT/'src/vars_registry.cpp').read_text()
  self.assertNotIn('state.lastGood',worker);self.assertIn('state.backingOff',worker)
  self.assertIn('invalidateShellyCacheLocked();',m);self.assertIn('expireShellyCache();',m)
  self.assertIn('settings.shelly.password!=stored.password',m)
  for role in ['main','light','humidifier','heater','fan','exhaust','heatmat']:
   self.assertIn('cur.shelly.'+role+'.fresh',registry);self.assertIn('cur.shelly.'+role+'.ageMs',registry)
  for field in ['light','heater','humidifier','exhaust']:self.assertIn('shellyCachedValues(settings.shelly.'+field+')',f)
  self.assertNotIn('values.ok = true',f)
if __name__=='__main__':unittest.main()
