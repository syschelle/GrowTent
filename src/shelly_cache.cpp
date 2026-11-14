#include "shelly_cache.h"
#include "globals.h"
#include <atomic>
extern ShellySettings shelly;
SemaphoreHandle_t shellyCacheMutex=nullptr;
namespace {std::atomic<uint32_t> cacheEpoch{1};}
bool initShellyCache(){shellyCacheMutex=xSemaphoreCreateMutex();return shellyCacheMutex!=nullptr;}
uint32_t shellyCacheEpoch(){return cacheEpoch.load();}
void invalidateShellyCacheLocked(){
  ++cacheEpoch;
  ShellyDevice* devices[]={&settings.shelly.main,&settings.shelly.light,&settings.shelly.humidifier,&settings.shelly.heater,&settings.shelly.fan,&settings.shelly.exhaust,&settings.shelly.heatmat,
    &shelly.main,&shelly.light,&shelly.humidifier,&shelly.heater,&shelly.fan,&shelly.exhaust,&shelly.heatmat};
  for(auto* d:devices){d->values=ShellyValues{};d->statusReadSequence=0;}
}
bool shellyValuesFresh(const ShellyValues& v,uint32_t nowMs){
  return v.ok && v.hasReading && v.cacheEpoch==shellyCacheEpoch() && uint32_t(nowMs-v.sampledAtMs)<SHELLY_STATUS_MAX_AGE_MS;
}
ShellyValues shellyCachedValues(const ShellyDevice& device){
  ShellyCacheLock lock(pdMS_TO_TICKS(5));if(!lock.held)return ShellyValues{};
  ShellyValues result=device.values;
  if(!shellyValuesFresh(result,millis()))result.ok=false;
  return result;
}
void expireShellyCache(){
  static uint32_t last=0;const uint32_t now=millis();if(uint32_t(now-last)<1000UL)return;last=now;
  ShellyCacheLock lock(pdMS_TO_TICKS(5));if(!lock.held)return;
  ShellyDevice* devices[]={&settings.shelly.main,&settings.shelly.light,&settings.shelly.humidifier,&settings.shelly.heater,&settings.shelly.fan,&settings.shelly.exhaust,&settings.shelly.heatmat,
    &shelly.main,&shelly.light,&shelly.humidifier,&shelly.heater,&shelly.fan,&shelly.exhaust,&shelly.heatmat};
  for(auto* d:devices)if(!shellyValuesFresh(d->values,now))d->values.ok=false;
}
void noteShellySwitch(const String& host,bool on){
  ShellyCacheLock lock;if(!lock.held)return;
  ShellyDevice* configured[]={&settings.shelly.main,&settings.shelly.light,&settings.shelly.humidifier,&settings.shelly.heater,&settings.shelly.fan,&settings.shelly.exhaust,&settings.shelly.heatmat};
  ShellyDevice* runtime[]={&shelly.main,&shelly.light,&shelly.humidifier,&shelly.heater,&shelly.fan,&shelly.exhaust,&shelly.heatmat};
  for(int i=0;i<7;++i)if(configured[i]->ip==host){configured[i]->values.isOn=on;runtime[i]->values.isOn=on;}
}
