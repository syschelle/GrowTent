#pragma once
#include "irrigation_plan_policy.h"
#include "irrigation_schedule_parser.h"
#include <atomic>
// Included once, after existing main-loop actions and HMAC helpers are defined.
namespace {
SemaphoreHandle_t lpMutex = nullptr;
JsonDocument lpState;
std::atomic<bool> lpEnabled{false}, lpOwns{false};
std::atomic<int> lpFinished{0}; // 1 completed, 2 aborted; task -> main-loop mailbox
bool lpReady=false, lpFault=false, lpStarting=false, lpSchedulePending=false;
uint32_t lpScheduleGeneration=1, lpLastAttemptMs=0, lpAttemptGeneration=0;
bool lpAttempted=false, lpFetchBusy=false;
int lpPhase=-1;
String lpIp, lpUser, lpPass, lpKey, lpAllowedIp;
int lpGen=0;
bool lpAuthEnabled=false;
struct LightSnapshot { int minutes[7]={-1,-1,-1,-1,-1,-1,-1}; int off[7]={-1,-1,-1,-1,-1,-1,-1}; int64_t at=0; bool ok=false; uint32_t generation=0; };
LightSnapshot lpPendingLight;
gtplan::ReplayWindow lpNonces;
enum class PlanOp {None, Replace, Ack};
PlanOp lpOp=PlanOp::None;
String lpPendingJson, lpPendingKey;
uint32_t lpNextRequest=0, lpApplied=0, lpFailed=0;
String lpLastError;
int64_t lpLastNtp=0;
uint32_t lpLastNtpMs=0;
bool lpNtpAttempted=false;

struct LpLock {
  bool held;
  explicit LpLock(TickType_t wait=pdMS_TO_TICKS(25)):held(lpMutex && xSemaphoreTake(lpMutex,wait)==pdTRUE){}
  ~LpLock(){if(held)xSemaphoreGive(lpMutex);}
};
int lpDay(time_t value) {struct tm t;localtime_r(&value,&t);return gtplan::civilDay(t.tm_year+1900,t.tm_mon+1,t.tm_mday);}
String lpDate(time_t value) {struct tm t;localtime_r(&value,&t);char b[11];strftime(b,sizeof(b),"%Y-%m-%d",&t);return String(b);}
int lpParseDay(const String& s) {
  return gtplan::civilDay(s.substring(0,4).toInt(),s.substring(5,7).toInt(),s.substring(8,10).toInt());
}
void lpDefaults() {
  lpState.clear();lpState["schema"]=1;lpState["revision"]=0;
  lpState["stream_id"]=String((uint32_t)esp_random(),HEX)+String((uint32_t)esp_random(),HEX);
  lpState["next_seq"]=1;lpState["last_handled_day"]=-1;lpState["last_run_day"]=-1;
  lpState["active"]=false;lpState["enabled"]=false;lpState["every_n_days"]=1;
  lpState["offset_after_light_on_min"]=0;lpState["last_schedule_sync"]=0;
  lpState["schedule_stale"]=true;lpState["events"].to<JsonArray>();
  auto a=lpState["light_on_minutes"].to<JsonArray>();for(int d=0;d<7;++d)a.add(-1);
}
bool lpPersist() {
  String encoded;serializeJson(lpState,encoded);
  if(encoded.length()>24000){lpFault=true;return false;}
  Preferences p;if(!p.begin("gt_irrig_v1",false)){lpFault=true;return false;}
  const bool ok=p.putString("state",encoded)>0 && p.getString("state","")==encoded;p.end();
  if(!ok)lpFault=true;
  return ok;
}
bool lpEvent(const char* type, const char* reason, int64_t scheduled=0, int count=1) {
  JsonArray events=lpState["events"].as<JsonArray>();
  if(events.size()>=gtplan::kMaxEvents)return false; // Never overwrite unacknowledged events.
  JsonObject e=events.add<JsonObject>();
  e["seq"]=lpState["next_seq"].as<uint32_t>();lpState["next_seq"]=e["seq"].as<uint32_t>()+1;
  e["type"]=type;e["reason"]=reason;const int64_t at=(int64_t)time(nullptr);if(at>=gtplan::kValidEpoch)e["occurred_at"]=at;else e["occurred_at"]=nullptr;
  e["scheduled_at"]=scheduled;e["count"]=count;e["revision"]=lpState["revision"].as<uint32_t>();
  e["phase"]=settings.grow.currentPhase;e["run_id"]=lpState["run_id"]|String("");
  return true;
}
void lpSend(AsyncWebServerRequest* r, int code, const String& body, const String& key, const String& nonce) {
  const String timestamp=String((uint32_t)time(nullptr));String hash,signature;
  sha256Hex(reinterpret_cast<const uint8_t*>(body.c_str()),body.length(),hash);
  const String method=r->method()==AsyncWebRequestMethod::HTTP_GET?"GET":"POST";
  const String canonical=String("RESPONSE\n")+method+"\n"+r->url()+"\n"+String(code)+"\n"+timestamp+"\n"+nonce+"\n"+hash;
  if(!hmacSha256Hex(key,canonical,signature)){sendBackendJson(r,500,"{\"ok\":false,\"error\":\"response_signing_failed\"}");return;}
  auto* response=r->beginResponse(code,"application/json; charset=utf-8",body);
  addAsyncCommonHeaders(response,false);
  response->addHeader("Cache-Control","no-store");
  response->addHeader("X-GrowTent-Timestamp",timestamp);response->addHeader("X-GrowTent-Nonce",nonce);
  response->addHeader("X-GrowTent-Signature",signature);r->send(response);
}
// Called only while lpMutex is held. No flash/GPIO/network mutation in Async context.
bool lpAuthenticate(AsyncWebServerRequest* r, String& key, String& nonce) {
  auto fail=[&](int code,const char* error){sendBackendJson(r,code,String("{\"ok\":false,\"error\":\"")+error+"\"}");return false;};
  if(!lpReady||lpFault)return fail(503,"irrigation_storage_unavailable");
  if(!lpAuthEnabled)return fail(403,"backend_write_disabled");
  if(!r->client()||r->client()->remoteIP().toString()!=lpAllowedIp)return fail(403,"source_ip_not_allowed");
  if(!isLowerHex(lpKey,64))return fail(503,"backend_secret_not_configured");
  if(r->params()!=0)return fail(400,"query_parameters_not_supported");
  if(!r->hasHeader("X-GrowTent-Timestamp")||!r->hasHeader("X-GrowTent-Nonce")||!r->hasHeader("X-GrowTent-Signature"))return fail(401,"missing_auth_headers");
  const String timestamp=r->header("X-GrowTent-Timestamp"),signature=r->header("X-GrowTent-Signature");
  nonce=r->header("X-GrowTent-Nonce");int64_t stamp=0,now=(int64_t)time(nullptr);
  if(now<gtplan::kValidEpoch)return fail(503,"time_not_synchronized");
  if(!parseInt64Strict(timestamp,stamp)||!isLowerHex(nonce,32)||!isLowerHex(signature,64)||stamp<now-60||stamp>now+60)return fail(401,"invalid_auth_headers_or_timestamp");
  const bool get=r->method()==AsyncWebRequestMethod::HTTP_GET;
  auto* b=static_cast<AsyncJsonBody*>(r->_tempObject);
  if(!get && (!b||b->tooLarge))return fail(b&&b->tooLarge?413:400,"invalid_body");
  if(!get && !r->contentType().startsWith("application/json"))return fail(415,"content_type_must_be_application_json");
  String hash,expected;
  const char* bytes=get?"":b->data;size_t len=get?0:b->len;
  sha256Hex(reinterpret_cast<const uint8_t*>(bytes),len,hash);
  key=lpKey;
  const String canonical=String(get?"GET":"POST")+"\n"+r->url()+"\n"+timestamp+"\n"+nonce+"\n"+hash;
  if(!hmacSha256Hex(key,canonical,expected)||!constantTimeHexEqual(expected,signature))return fail(401,"bad_signature");
  uint8_t nonceBytes[16];if(!hexToBytes(nonce,nonceBytes,sizeof(nonceBytes)))return fail(401,"invalid_nonce");
  const auto result=lpNonces.accept(nonceBytes,now);
  if(result==gtplan::ReplayWindow::Replay)return fail(409,"replayed_nonce");
  if(result==gtplan::ReplayWindow::Full)return fail(429,"nonce_cache_full");
  return true;
}
void lpHandle(AsyncWebServerRequest* r, PlanOp op, bool events=false) {
  LpLock guard;if(!guard.held){sendBackendJson(r,503,"{\"ok\":false,\"error\":\"irrigation_busy\"}");return;}
  String key,nonce;if(!lpAuthenticate(r,key,nonce))return;
  auto reply=[&](int code,const String& text){lpSend(r,code,text,key,nonce);};
  if(r->method()==AsyncWebRequestMethod::HTTP_GET){
    JsonDocument doc;doc["ok"]=true;doc["revision"]=lpState["revision"];
    doc["stream_id"]=lpState["stream_id"];doc["last_applied_request"]=lpApplied;
    doc["last_failed_request"]=lpFailed;doc["last_error"]=lpLastError;
    if(events){auto out=doc["events"].to<JsonArray>();int n=0;for(JsonObjectConst e:lpState["events"].as<JsonArrayConst>()){if(n++==8)break;out.add(e);}doc["has_more"]=lpState["events"].size()>8;}
    else {
      doc["enabled"]=lpState["enabled"];doc["every_n_days"]=lpState["every_n_days"];
      doc["offset_after_light_on_min"]=lpState["offset_after_light_on_min"];
      doc["last_run_date"]=lpState["last_run_date"];doc["active"]=lpState["active"];
      doc["pending_request"]=lpOp==PlanOp::None?0:lpNextRequest;
      doc["last_schedule_sync"]=lpState["last_schedule_sync"];doc["schedule_stale"]=lpState["schedule_stale"];
      doc["light_on_minutes"]=lpState["light_on_minutes"];doc["queued_events"]=lpState["events"].size();
      doc["drying_blocked"]=settings.grow.currentPhase==3;doc["clock_valid"]=(int64_t)time(nullptr)>=gtplan::kValidEpoch;
      doc["execution_owner"]=lpEnabled.load()?"esp":"legacy";
    }
    String json;serializeJson(doc,json);reply(200,json);return;
  }
  if(lpOp!=PlanOp::None){reply(409,"{\"ok\":false,\"error\":\"irrigation_write_pending\"}");return;}
  auto* b=static_cast<AsyncJsonBody*>(r->_tempObject);JsonDocument doc;
  if(deserializeJson(doc,b->data,b->len)||!doc.is<JsonObject>()){reply(400,"{\"ok\":false,\"error\":\"invalid_json\"}");return;}
  if(op==PlanOp::Replace){
    for(JsonPair kv:doc.as<JsonObject>()){String k=kv.key().c_str();if(k!="revision"&&k!="enabled"&&k!="every_n_days"&&k!="offset_after_light_on_min"&&k!="last_run_date"){reply(400,"{\"ok\":false,\"error\":\"unknown_field\"}");return;}}
    if(!doc["revision"].is<uint32_t>()||doc["revision"].as<uint32_t>()==0||!doc["enabled"].is<bool>()||
       !doc["every_n_days"].is<int>()||doc["every_n_days"].as<int>()<1||doc["every_n_days"].as<int>()>366||
       !doc["offset_after_light_on_min"].is<int>()||doc["offset_after_light_on_min"].as<int>()<0||doc["offset_after_light_on_min"].as<int>()>1440||
       !doc["last_run_date"].is<JsonVariant>()||(!doc["last_run_date"].isNull()&&!doc["last_run_date"].is<const char*>())){
      reply(400,"{\"ok\":false,\"error\":\"invalid_plan\"}");return;
    }
    const String last=doc["last_run_date"]|String("");
    if(last.length()&&(!isValidBackendCalendarDate(last)||last.compareTo(lpDate(time(nullptr)))>0)){reply(400,"{\"ok\":false,\"error\":\"invalid_last_run_date\"}");return;}
    const uint32_t rev=doc["revision"].as<uint32_t>(),old=lpState["revision"].as<uint32_t>();
    if(rev<=old){
      bool same=rev==old&&doc["enabled"]==lpState["enabled"]&&doc["every_n_days"]==lpState["every_n_days"]&&doc["offset_after_light_on_min"]==lpState["offset_after_light_on_min"]&&doc["last_run_date"]==lpState["input_last_run_date"];
      reply(same?200:409,same?"{\"ok\":true,\"unchanged\":true}":"{\"ok\":false,\"error\":\"revision_conflict\"}");return;
    }
    if(irrigation.irrigationRuns>0||lpOwns.load()){reply(409,"{\"ok\":false,\"error\":\"watering_running\"}");return;}
  }else{
    if(doc.size()!=2||!doc["stream_id"].is<const char*>()||doc["stream_id"].as<String>()!=lpState["stream_id"].as<String>()||!doc["through_seq"].is<uint32_t>()){
      reply(400,"{\"ok\":false,\"error\":\"invalid_ack\"}");return;
    }
    uint32_t seq=doc["through_seq"].as<uint32_t>();
    if(seq>=lpState["next_seq"].as<uint32_t>()){reply(400,"{\"ok\":false,\"error\":\"ack_out_of_range\"}");return;}
  }
  serializeJson(doc,lpPendingJson);lpPendingKey=key;lpOp=op;++lpNextRequest;
  reply(202,String("{\"ok\":true,\"applyQueued\":true,\"requestId\":")+String(lpNextRequest)+"}");
}
bool lpParseLight(const String& body,int generation,int (&minutes)[7],int (&off)[7]) {
  return gtplan::parseLightSchedule(body.c_str(),body.length(),generation,minutes,off);
}
}
String irrigationPlanStatusField(const char* key){
  LpLock guard;if(!guard.held)return "null";
  if(!strcmp(key,"enabled"))return lpEnabled.load()?"true":"false";
  if(!strcmp(key,"lastScheduleSync"))return String(lpState["last_schedule_sync"].as<uint32_t>());
  if(!strcmp(key,"queuedEvents"))return String(lpState["events"].size());
  const char* status=lpFault?"storage_error":!lpEnabled.load()?"disabled":settings.grow.currentPhase==3?"drying_blocked":lpState["events"].size()>62?"event_queue_full":lpState["last_schedule_sync"].as<int64_t>()==0?"waiting_for_shelly":lpState["schedule_stale"].as<bool>()?"stale_shelly":"ready";
  return String("\"")+status+"\"";
}
bool irrigationPlanEnabled(){return lpEnabled.load();}
bool irrigationPlanOwnsCycle(){return lpOwns.load();}
bool irrigationPlanReserveManualCycle(String& error){
  if(!lpEnabled.load())return true;
  if(lpOwns.load()){if(lpStarting)return true;error="irrigation_cycle_pending";return false;}
  LpLock guard;
  if(!guard.held||!lpReady||lpFault){error="irrigation_storage_unavailable";return false;}
  const time_t now=time(nullptr);if((int64_t)now<gtplan::kValidEpoch){error="time_not_synchronized";return false;}
  if(lpState["events"].size()>gtplan::kMaxEvents-2){error="event_queue_full";return false;}
  const int day=lpDay(now);
  if(day>lpState["last_handled_day"].as<int>())lpState["last_handled_day"]=day;
  if(day>lpState["last_run_day"].as<int>()){lpState["last_run_day"]=day;lpState["last_run_date"]=lpDate(now);}
  lpState["run_id"]=String(day)+"-manual-"+String(lpState["next_seq"].as<uint32_t>());
  lpState["active"]=true;lpEvent("started","manual_cycle_accepted");
  if(!lpPersist()){error="irrigation_persist_failed";return false;}
  lpOwns.store(true);return true;
}
void irrigationPlanCycleFinished(bool success){if(!lpOwns.load())return;if(!success)lpFinished.store(2);else {int expected=0;lpFinished.compare_exchange_strong(expected,1);}}
bool irrigationPlanRefreshShelly(bool force){
  String ip,user,pass;int gen;uint32_t generation;
  {
    LpLock guard;if(!guard.held||!lpReady)return false;
    const uint32_t now=millis();
    if(lpFetchBusy||(!force&&lpAttempted&&lpAttemptGeneration==lpScheduleGeneration&&(uint32_t)(now-lpLastAttemptMs)<3600000UL))return false;
    if(lpIp.isEmpty()||WiFi.status()!=WL_CONNECTED)return false;
    lpFetchBusy=true;lpAttempted=true;lpLastAttemptMs=now;lpAttemptGeneration=lpScheduleGeneration;
    generation=lpScheduleGeneration;ip=lpIp;gen=lpGen;user=lpUser;pass=lpPass;
  }
  LightSnapshot snap;snap.generation=generation;int code=0;String body;bool ok=false;
  if(gen==1)ok=httpGetGen1(ip,80,"/settings/relay/0",user,pass,code,body);
  else if(gen>=2 && gen<=4)ok=httpGetWithDigestAutoAuth(ip,80,"/rpc/Schedule.List",user,pass,code,body);
  snap.ok=ok&&code==200&&lpParseLight(body,gen,snap.minutes,snap.off);snap.at=(int64_t)time(nullptr);
  LpLock guard(pdMS_TO_TICKS(100));if(!guard.held)return false;
  lpFetchBusy=false;if(generation!=lpScheduleGeneration)return false;
  lpPendingLight=snap;lpSchedulePending=true;return snap.ok;
}
void irrigationPlanInit(){
  lpMutex=xSemaphoreCreateMutex();if(!lpMutex)return;
  LpLock guard;Preferences p;
  if(!p.begin("gt_irrig_v1",false)){lpFault=true;return;}
  bool exists=p.isKey("state");String saved=p.getString("state","");p.end();
  if(exists){
    if(saved.length()>24000||deserializeJson(lpState,saved)||lpState["schema"].as<int>()!=1||!lpState["events"].is<JsonArray>()||lpState["events"].size()>64||lpState["light_on_minutes"].size()!=7){lpFault=true;return;}
  }else{lpDefaults();if(!lpPersist())return;}
  lpReady=true;
  if(lpState["active"]|false){lpState["active"]=false;lpEvent("aborted","restart_interrupted");lpPersist();}
  lpEnabled.store(lpState["enabled"]|false);
  lpAuthEnabled=settings.backendWrite.enabled;lpKey=settings.backendWrite.secretHex;lpAllowedIp=settings.backendWrite.allowedIp;
}
// Factory-reset hook, called from the main loop before resetting the legacy namespace.
bool irrigationPlanFactoryReset(){
  requestIrrigationStop("factory reset");
  if(!lpMutex)return false;LpLock guard;if(!guard.held)return false;
  lpEnabled.store(false);lpOwns.store(false);lpFinished.store(0);lpOp=PlanOp::None;
  lpDefaults();lpFault=false;lpReady=lpPersist();return lpReady;
}
void irrigationPlanTick(){
  if(!lpMutex)return;
  const int phase=settings.grow.currentPhase;
  if(phase==3 && irrigation.irrigationRuns>0){requestIrrigationStop("drying phase");irrigationPlanCycleFinished(false);}
  const int64_t now=(int64_t)time(nullptr);
  if(now<gtplan::kValidEpoch && WiFi.status()==WL_CONNECTED && (!lpNtpAttempted||(uint32_t)(millis()-lpLastNtpMs)>=60000UL)){
    lpNtpAttempted=true;lpLastNtpMs=millis();configTzTime(tzInfo.c_str(),ntpServer.c_str());ntpSyncPending=true;ntpStartMs=millis();
  }
  bool start=false;
  {
    LpLock guard;if(!guard.held||!lpReady||lpFault)return;
    lpAuthEnabled=settings.backendWrite.enabled;lpKey=settings.backendWrite.secretHex;lpAllowedIp=settings.backendWrite.allowedIp;
    if(lpPhase!=phase||lpIp!=settings.shelly.light.ip||lpGen!=settings.shelly.light.gen){
      bool changedHost=lpState["cache_ip"].as<String>()!=settings.shelly.light.ip;
      lpPhase=phase;lpIp=settings.shelly.light.ip;lpGen=settings.shelly.light.gen;++lpScheduleGeneration;
      lpState["schedule_stale"]=true;
      if(changedHost){for(JsonVariant v:lpState["light_on_minutes"].as<JsonArray>())v.set(-1);lpState["last_schedule_sync"]=0;lpState["cache_ip"]=lpIp;lpPersist();}
    }
    lpUser=settings.shelly.username;lpPass=settings.shelly.password;
    if(lpSchedulePending){
      lpSchedulePending=false;
      if(lpPendingLight.generation==lpScheduleGeneration){
        lpState["schedule_stale"]=!lpPendingLight.ok;
        if(lpPendingLight.ok){
          auto a=lpState["light_on_minutes"].to<JsonArray>();for(int d=0;d<7;++d)a.add(lpPendingLight.minutes[d]);
          lpState["last_schedule_sync"]=lpPendingLight.at;lpState["cache_ip"]=lpIp;
          for(int d=0;d<7;++d){int minute=lpPendingLight.minutes[d];settings.shelly.light.schedules.days[d].onHour=minute<0?-1:minute/60;settings.shelly.light.schedules.days[d].onMinute=minute<0?-1:minute%60;int off=lpPendingLight.off[d];settings.shelly.light.schedules.days[d].offHour=off<0?-1:off/60;settings.shelly.light.schedules.days[d].offMinute=off<0?-1:off%60;}
        }
        lpPersist();
      }
    }
    const int finished=lpFinished.exchange(0);
    if(finished&&lpOwns.load()){
      lpState["active"]=false;lpEvent(finished==1?"completed":"aborted",finished==1?"successful_end":"interrupted");
      lpPersist();lpOwns.store(false);
    }
    if(lpOp!=PlanOp::None){
      JsonDocument pending;deserializeJson(pending,lpPendingJson);bool ok=lpPendingKey==lpKey&&lpAuthEnabled;
      if(ok&&lpOp==PlanOp::Replace){
        ok=irrigation.irrigationRuns<=0&&!lpOwns.load();
        if(ok){
          lpState["revision"]=pending["revision"];lpState["enabled"]=pending["enabled"];
          lpState["every_n_days"]=pending["every_n_days"];lpState["offset_after_light_on_min"]=pending["offset_after_light_on_min"];
          lpState["input_last_run_date"]=pending["last_run_date"];
          String last=pending["last_run_date"]|String("");int baseline=last.length()?lpParseDay(last):lpDay(time(nullptr))-pending["every_n_days"].as<int>();
          if(baseline>lpState["last_run_day"].as<int>()){lpState["last_run_day"]=baseline;lpState["last_run_date"]=pending["last_run_date"];}
          // First enable never backfills earlier dates; later edits never roll back handled dates.
          if(lpState["last_handled_day"].as<int>()<lpDay(time(nullptr))-1)lpState["last_handled_day"]=lpDay(time(nullptr))-1;
          ok=lpPersist();if(ok)lpEnabled.store(lpState["enabled"]|false);
        }
      }else if(ok){
        JsonArray a=lpState["events"].as<JsonArray>();const uint32_t through=pending["through_seq"].as<uint32_t>();
        while(a.size()&&a[0]["seq"].as<uint32_t>()<=through)a.remove(0);ok=lpPersist();
      }
      if(ok)lpApplied=lpNextRequest;else{lpFailed=lpNextRequest;lpLastError="apply_failed";}
      lpOp=PlanOp::None;lpPendingJson="";lpPendingKey="";
    }
    if(lpFault||!lpEnabled.load()||now<gtplan::kValidEpoch||lpOwns.load())return;
    const int today=lpDay(time(nullptr));
    int handled=lpState["last_handled_day"].as<int>();
    if(phase==3){
      if(handled<today){lpState["last_handled_day"]=today;lpPersist();}
      return; // Discard pending occurrences even without a usable Shelly cache.
    }
    // Record a bounded summary of genuinely eligible elapsed dates. Yesterday is
    // evaluated separately below because its light+delay may fall after midnight.
    if(handled>=0&&today-handled>2){
      if(lpState["events"].size()>=gtplan::kMaxEvents)return;
      int missed=0;
      for(int date=handled+1;date<=today-2;++date){
        int weekday=(date+3)%7;if(weekday<0)weekday+=7;
        if(gtplan::intervalDue(date,lpState["last_run_day"].as<int>(),lpState["every_n_days"].as<int>()) && (lpState["light_on_minutes"][weekday]|-1)>=0)++missed;
        if(date-handled>3660){lpFault=true;return;} // Implausible multi-year clock jump: fail closed.
      }
      if(missed&&phase!=3)lpEvent("missed","offline_or_clock_gap",0,missed);
      lpState["last_handled_day"]=today-2;if(!lpPersist())return;handled=today-2;
    }
    // Consider yesterday as well, so light-on + delay can cross local midnight/DST.
    for(int back=1;back>=0;--back){
      time_t base=(time_t)now;struct tm t;localtime_r(&base,&t);t.tm_mday-=back;t.tm_hour=12;t.tm_min=0;t.tm_sec=0;t.tm_isdst=-1;base=mktime(&t);
      struct tm day;localtime_r(&base,&day);const int date=lpDay(base),weekday=(day.tm_wday+6)%7;
      if(date<=handled||!gtplan::intervalDue(date,lpState["last_run_day"].as<int>(),lpState["every_n_days"].as<int>()))continue;
      const int light=lpState["light_on_minutes"][weekday]|-1;
      if(light<0)continue;
      day.tm_hour=light/60;day.tm_min=light%60;day.tm_sec=0;day.tm_isdst=-1;
      const int64_t due=(int64_t)mktime(&day)+(int64_t)lpState["offset_after_light_on_min"].as<int>()*60;
      const auto decision=gtplan::decide(now,due,date,handled,phase,irrigation.irrigationRuns>0||anyAsyncIrrigationPumpOnOrArmed());
      if(decision==gtplan::Decision::Wait||decision==gtplan::Decision::AlreadyHandled)continue;
      if(lpState["events"].size()>gtplan::kMaxEvents-2)return;
      if(decision==gtplan::Decision::Drying){lpState["last_handled_day"]=date;lpPersist();handled=date;continue;}
      if(decision!=gtplan::Decision::Start||asyncIrrigationFloodBlocked()||enabledIrrigationPumpCount()==0||irrigation.timePerTask<=0||irrigation.betweenTasks<=0||!isfinite(irrigation.amountOfWater)||irrigation.amountOfWater<=0||!isfinite(irrigation.amount)||irrigation.amount<=0){
        lpEvent("missed",decision==gtplan::Decision::Missed?"deadline_passed":"blocked",due);
        lpState["last_handled_day"]=date;if(!lpPersist())return;handled=date;continue;
      }
      lpState["last_handled_day"]=date;lpState["last_run_day"]=date;lpState["last_run_date"]=lpDate(base);
      lpState["run_id"]=String(date)+"-"+String(lpState["revision"].as<uint32_t>());
      lpState["active"]=true;lpEvent("started","local_schedule_accepted",due);
      if(!lpPersist())return;lpOwns.store(true);start=true;break;
    }
  }
  if(start){String error;lpStarting=true;const bool ok=startAsyncIrrigationNow(false,error);lpStarting=false;if(!ok)irrigationPlanCycleFinished(false);}
}
static void configureIrrigationPlanRoutes(){
  asyncServer.on("/api/backend/irrigation-plan",AsyncWebRequestMethod::HTTP_GET,[](AsyncWebServerRequest* r){lpHandle(r,PlanOp::None);});
  asyncServer.on("/api/backend/irrigation-plan",AsyncWebRequestMethod::HTTP_POST,[](AsyncWebServerRequest* r){lpHandle(r,PlanOp::Replace);},nullptr,handleAsyncJsonBody);
  asyncServer.on("/api/backend/irrigation-events",AsyncWebRequestMethod::HTTP_GET,[](AsyncWebServerRequest* r){lpHandle(r,PlanOp::None,true);});
  asyncServer.on("/api/backend/irrigation-events/ack",AsyncWebRequestMethod::HTTP_POST,[](AsyncWebServerRequest* r){lpHandle(r,PlanOp::Ack);},nullptr,handleAsyncJsonBody);
}
