#pragma once
#include "humidifier_fan_policy.h"
#include <atomic>
namespace {
SemaphoreHandle_t hfMutex=nullptr;
std::atomic<int> hfRelay{0}; // one based; zero disables coupling
String hfIp,hfUser,hfPass,hfError;
int hfGen=0,hfPendingRelay=-1;
uint32_t hfGeneration=1,hfLastAttempt=0,hfDuration=0,hfNextId=0,hfPendingId=0,hfApplied=0,hfFailed=0;
uint32_t hfSequence=0,hfAcceptedSequence=0;
bool hfAttempted=false,hfValid=false,hfStale=false,hfBusy=false;
gtfan::Timer hfTimer;
struct HfLock {
  bool held;
  explicit HfLock(TickType_t wait=pdMS_TO_TICKS(25)):held(hfMutex && xSemaphoreTake(hfMutex,wait)==pdTRUE){}
  ~HfLock(){if(held)xSemaphoreGive(hfMutex);}
};
bool hfAllowed(int relay) {
  return relay==0 || (relay>=1 && relay<=activeRelayCount &&
    irrigationPumpSlotFromRelayIndex(relay-1)<0 &&
    !(settings.heating.sourceType==1 && settings.heating.Relay==relay));
}
// Called from the main loop under the module lock. Retry on GPIO mutex contention.
void hfOutput(int relay,bool on) {
  if(relay<1 || relay>NUM_RELAYS)return;
  if(relayMutex && xSemaphoreTake(relayMutex,pdMS_TO_TICKS(10))!=pdTRUE)return;
  digitalWrite(relayPins[relay-1],on?HIGH:LOW);relayStates[relay-1]=on;
  if(relayMutex)xSemaphoreGive(relayMutex);
}
void hfInvalidate() {
  ++hfGeneration;hfSequence=0;hfAcceptedSequence=0;hfValid=false;hfStale=false;hfDuration=0;hfAttempted=false;
  hfTimer.running=false;hfError="auto_off_unknown";
}
void hfSyncDevice() {
  if(hfIp!=settings.shelly.humidifier.ip || hfGen!=settings.shelly.humidifier.gen ||
     hfUser!=settings.shelly.username || hfPass!=settings.shelly.password) {
    hfIp=settings.shelly.humidifier.ip;hfGen=settings.shelly.humidifier.gen;
    hfUser=settings.shelly.username;hfPass=settings.shelly.password;hfInvalidate();
  }
}
void hfSend(AsyncWebServerRequest* r,int code,const String& json) {
  auto* response=r->beginResponse(code,"application/json; charset=utf-8",json);
  addAsyncCommonHeaders(response,false);response->addHeader("Cache-Control","no-store");r->send(response);
}
void hfHandle(AsyncWebServerRequest* r) {
  if(!r)return;
  HfLock lock;
  if(!lock.held){hfSend(r,503,"{\"ok\":false,\"error\":\"busy\"}");return;}
  if(r->method()==AsyncWebRequestMethod::HTTP_POST) {
    if(!r->contentType().startsWith("application/json")) {
      hfSend(r,415,"{\"ok\":false,\"error\":\"content_type_must_be_application_json\"}");return;
    }
    auto* b=static_cast<AsyncJsonBody*>(r->_tempObject);JsonDocument d;
    if(!b || b->tooLarge || deserializeJson(d,b->data,b->len) || !d.is<JsonObject>() || !d["relay"].is<int>() ||
       d.as<JsonObject>().size()!=1 || !hfAllowed(d["relay"].as<int>())) {
      hfSend(r,400,"{\"ok\":false,\"error\":\"invalid_fan_relay\"}");return;
    }
    if(g_restartRequested || otaMaintenanceRequested() || hfPendingRelay>=0) {
      hfSend(r,409,"{\"ok\":false,\"error\":\"apply_pending\"}");return;
    }
    hfPendingRelay=d["relay"].as<int>();hfPendingId=++hfNextId;
    hfSend(r,202,String("{\"ok\":true,\"requestId\":")+hfPendingId+"}");return;
  }
  JsonDocument d;d["ok"]=true;d["relay"]=hfRelay.load();d["known"]=hfValid;
  d["stale"]=hfStale;d["autoOffSeconds"]=hfValid?hfDuration/1000.0:0;
  const uint32_t now=millis();
  d["running"]=hfTimer.running && !gtfan::expired(now,hfTimer.deadline);
  d["remainingSeconds"]=hfTimer.running && !gtfan::expired(now,hfTimer.deadline)?(hfTimer.deadline-now+999)/1000:0;
  d["error"]=hfError;d["appliedId"]=hfApplied;d["failedId"]=hfFailed;
  auto options=d["availableRelays"].to<JsonArray>();
  for(int n=1;n<=activeRelayCount;++n)if(hfAllowed(n))options.add(n);
  String encoded;serializeJson(d,encoded);hfSend(r,200,encoded);
}
}
bool humidifierFanOwnsRelay(int index){return index>=0 && hfRelay.load()==index+1;}
uint64_t humidifierFanCommandGeneration(const String& host,uint8_t gen) {
  HfLock lock;
  if(!lock.held || !hfRelay.load() || host!=hfIp || gen!=hfGen)return 0;
  return (uint64_t(hfGeneration)<<32) | ++hfSequence;
}
void humidifierFanCommandResult(uint64_t token,bool on,bool success,uint32_t startedMs) {
  if(!token || !success)return;
  const uint32_t generation=uint32_t(token>>32),sequence=uint32_t(token);
  HfLock lock(portMAX_DELAY);
  if(!lock.held || generation!=hfGeneration || int32_t(sequence-hfAcceptedSequence)<=0)return;
  hfAcceptedSequence=sequence;
  // No network worker ever writes a GPIO. A late HTTP response cannot extend runtime.
  hfTimer.command(on,true,startedMs,hfValid?hfDuration:0);
}
void humidifierFanRefreshConfig() {
  String ip,user,pass;int gen;uint32_t generation;
  {
    HfLock lock;
    if(!lock.held || !hfRelay.load() || hfIp.isEmpty() || hfBusy ||
       !gtfan::refreshDue(hfAttempted,millis(),hfLastAttempt) || WiFi.status()!=WL_CONNECTED)return;
    hfAttempted=true;hfLastAttempt=millis();hfBusy=true;
    ip=hfIp;gen=hfGen;user=hfUser;pass=hfPass;generation=hfGeneration;
  }
  int code=-1;String body;bool ok=false;IPAddress addr;
  if(addr.fromString(ip) && (gen==1 || gen==2 || gen==3)) {
    ok=gen==1?httpGetGen1(ip,80,"/settings/relay/0",user,pass,code,body):
      httpGetWithDigestAutoAuth(ip,80,"/rpc/Switch.GetConfig?id=0",user,pass,code,body);
  }
  JsonDocument d;bool parsed=ok && code==200 && !deserializeJson(d,body);
  uint32_t duration=0;
  if(parsed && gen!=1)parsed=d["id"].is<int>() && d["id"].as<int>()==0;
  if(parsed && gen==1) {
    parsed=d["auto_off"].is<double>() && d["auto_off"].as<double>()>=0;
    if(parsed){const double seconds=d["auto_off"].as<double>();duration=gtfan::durationMs(seconds);parsed=seconds==0 || duration>0;}
  } else if(parsed) {
    parsed=d["auto_off"].is<bool>();
    if(parsed && d["auto_off"].as<bool>()) {
      parsed=d["auto_off_delay"].is<double>();
      if(parsed){duration=gtfan::durationMs(d["auto_off_delay"].as<double>());parsed=duration>0;}
    }
  }
  HfLock lock(portMAX_DELAY);
  if(!lock.held)return;
  hfBusy=false;
  if(generation!=hfGeneration)return;
  if(parsed) {
    hfValid=true;hfStale=false;hfDuration=duration;hfError=duration?"":"auto_off_disabled";
    if(!duration)hfTimer.running=false;
  } else {hfStale=true;hfError="auto_off_read_failed";} // Keep same-device last known duration.
}
void humidifierFanInit() {
  hfMutex=xSemaphoreCreateMutex();if(!hfMutex)return;
  Preferences p;int relay=0;
  if(p.begin(PREF_NS,true)){relay=p.getInt("humFanRelay",0);p.end();}
  hfRelay.store(hfAllowed(relay)?relay:0);
  HfLock lock;if(lock.held)hfSyncDevice();
}
void humidifierFanTick() {
  HfLock lock;if(!lock.held)return;
  hfSyncDevice();
  if(hfPendingRelay>=0) {
    const int next=hfPendingRelay;hfPendingRelay=-1;
    bool ok=hfAllowed(next) && !g_restartRequested && !otaMaintenanceRequested();
    const int old=hfRelay.load();
    if(ok) {
      // Retain ownership and retry without repeated flash writes if GPIO is busy.
      hfOutput(old,false);
      if(old && digitalRead(relayPins[old-1])==HIGH){hfPendingRelay=next;return;}
      Preferences p;ok=p.begin(PREF_NS,false);
      if(ok){ok=p.putInt("humFanRelay",next)==sizeof(int32_t) && p.getInt("humFanRelay",-1)==next;p.end();}
    }
    if(ok) {
      hfRelay.store(next);hfInvalidate();hfOutput(next,false);hfApplied=hfPendingId;
    } else {hfFailed=hfPendingId;hfError="fan_relay_save_failed";}
  }
  const int relay=hfRelay.load();
  if(!relay)return;
  if(!hfAllowed(relay) || g_restartRequested || otaMaintenanceRequested())hfTimer.running=false;
  hfOutput(relay,hfTimer.tick(millis()));
}
static void configureHumidifierFanRoutes() {
  asyncServer.on("/api/settings/humidifier-fan",AsyncWebRequestMethod::HTTP_GET,hfHandle);
  asyncServer.on("/api/settings/humidifier-fan",AsyncWebRequestMethod::HTTP_POST,hfHandle,nullptr,handleAsyncJsonBody);
}
