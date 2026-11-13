#pragma once
#include <ArduinoJson.h>
#include "irrigation_plan_policy.h"
namespace gtplan {
inline bool parseLightSchedule(const char* body,size_t length,int generation,int (&minutes)[7],int (&off)[7]) {
  JsonDocument doc;if(length>16384||deserializeJson(doc,body,length))return false;
  if(generation==1){
    if(!doc["schedule"].is<bool>())return false;
    if(!doc["schedule"].as<bool>())return true;
    if(!doc["schedule_rules"].is<JsonArray>())return false;
    for(JsonVariantConst v:doc["schedule_rules"].as<JsonArrayConst>()){
      if(!v.is<const char*>()||!gtplan::addGen1Rule(v.as<const char*>(),minutes)||!gtplan::addGen1Rule(v.as<const char*>(),off,false))return false;
    }
    return true;
  }
  if(!doc["jobs"].is<JsonArray>()||doc["jobs"].size()>20||doc["offset"].as<int>()!=0||
     (doc["total"].is<int>()&&doc["total"].as<int>()>int(doc["jobs"].size())))return false;
  for(JsonObjectConst job:doc["jobs"].as<JsonArrayConst>()){
    if(!job["enable"].is<bool>())return false;
    if(!job["enable"].as<bool>())continue;
    if(!job["calls"].is<JsonArrayConst>())return false;
    bool relevant=false,offRelevant=false;for(JsonObjectConst c:job["calls"].as<JsonArrayConst>()){
      if(!strcmp(c["method"]|"","Switch.Set")&&c["params"]["id"].is<int>()&&c["params"]["id"].as<int>()==0){
        if(!c["params"]["on"].is<bool>())return false;
        if(c["params"]["on"].as<bool>())relevant=true;else offRelevant=true;
      }
    }
    if(!relevant&&!offRelevant)continue;
    if(!job["timespec"].is<const char*>()|| (relevant&&offRelevant))return false;
    if(relevant&&!gtplan::addCronOn(job["timespec"].as<const char*>(),minutes))return false;
    if(offRelevant&&!gtplan::addCronOn(job["timespec"].as<const char*>(),off))return false;
  }
  return true; // Explicit empty/disabled schedule cancels future starts; transport failure never clears cache.
}
}
