#pragma once
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <time.h>
namespace gtplan {
constexpr int kMaxEvents = 64;
constexpr int kStartGraceSeconds = 60;
constexpr int64_t kValidEpoch = 1609459200LL;
// Civil calendar arithmetic: local calendar days, independent of 23/25-hour DST days.
inline int civilDay(int y, unsigned m, unsigned d) {
  y -= m <= 2; const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  return era * 146097 + int(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}
// Unknown history is -1. Imports can advance known history, never erase/rewind it.
inline bool historyDateAllowed(int storedDay, int incomingDay, int today) {
  return incomingDay < 0 ? storedDay < 0 : incomingDay <= today && (storedDay < 0 || incomingDay >= storedDay);
}
inline bool intervalDue(int lightDay, int lastRunDay, int everyDays) {
  return everyDays >= 1 && lightDay > lastRunDay && lightDay - lastRunDay >= everyDays;
}
// Read-only projection using the same local-calendar/elapsed-offset rules as execution.
inline int64_t nextOccurrence(int64_t now,int lastRunDay,int everyDays,int handledDay,const int (&on)[7],int offsetMinutes){
  if(now<kValidEpoch||everyDays<1||everyDays>366||offsetMinutes<0||offsetMinutes>1440)return 0;
  const time_t current=(time_t)now;struct tm today;
  if(!localtime_r(&current,&today))return 0;
  const int todayDay=civilDay(today.tm_year+1900,today.tm_mon+1,today.tm_mday);
  int64_t first=(int64_t)lastRunDay+everyDays-todayDay;
  const int64_t unhandled=(int64_t)handledDay+1-todayDay;
  if(first<unhandled)first=unhandled;
  if(first< -1)first=-1;
  if(first>374)return 0;
  // Yesterday permits midnight offsets. Jump directly to the first eligible
  // calendar day, then inspect at most a week of cached weekday schedules.
  for(int ahead=(int)first;ahead<=(int)first+7&&ahead<=374;++ahead){
    struct tm noon=today;noon.tm_mday+=ahead;noon.tm_hour=12;noon.tm_min=0;noon.tm_sec=0;noon.tm_isdst=-1;
    time_t base=mktime(&noon);if(base==(time_t)-1)continue;
    struct tm day;if(!localtime_r(&base,&day))continue;
    const int date=civilDay(day.tm_year+1900,day.tm_mon+1,day.tm_mday),weekday=(day.tm_wday+6)%7;
    if(date<=handledDay||!intervalDue(date,lastRunDay,everyDays)||on[weekday]<0||on[weekday]>=1440)continue;
    day.tm_hour=on[weekday]/60;day.tm_min=on[weekday]%60;day.tm_sec=0;day.tm_isdst=-1;
    const time_t light=mktime(&day);if(light==(time_t)-1)continue;
    const int64_t due=(int64_t)light+(int64_t)offsetMinutes*60;
    if(due<now-kStartGraceSeconds)continue; // Missed occurrences are never forecast as catch-up.
    return due;
  }
  return 0;
}
inline bool manualCommandFresh(int64_t now,int64_t expires){return now>=kValidEpoch&&expires>now&&expires<=now+300;}
enum class Decision { Wait, Start, Missed, Drying, Busy, ClockInvalid, AlreadyHandled };
inline Decision decide(int64_t now, int64_t due, int day, int handledDay, int phase, bool busy) {
  if (phase == 3) return Decision::Drying;
  if (now < kValidEpoch) return Decision::ClockInvalid;
  if (day <= handledDay) return Decision::AlreadyHandled;
  if (now < due) return Decision::Wait;
  if (now - due > kStartGraceSeconds) return Decision::Missed;
  if (busy) return Decision::Busy;
  return Decision::Start;
}
// Numeric-only cron fragments: *, lists, ranges. Unsupported /steps, solar and expressions fail closed.
inline bool parseNumber(const char* s, unsigned len, int low, int high, int& value) {
  if (!len || len > 2) return false;
  value = 0; for (unsigned i=0;i<len;++i) {if(s[i]<'0'||s[i]>'9')return false;value=value*10+s[i]-'0';}
  return value>=low && value<=high;
}
inline bool weekdayNumber(const char* s,unsigned len,int& n) {
  if(parseNumber(s,len,0,6,n))return true;
  if(len!=3)return false;
  const char* names[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
  for(int d=0;d<7;++d){bool match=true;for(unsigned j=0;j<3;++j){char c=s[j];if(c>='a'&&c<='z')c-=32;if(c!=names[d][j])match=false;}if(match){n=d;return true;}}return false;
}
inline bool weekdayMask(const char* s, uint8_t& mask) {
  mask=0; if(!s||!*s)return false;
  if(s[0]=='*' && !s[1]) {mask=127;return true;}
  const char* p=s;
  while(*p) {
    const char* begin=p;while(*p && *p!=',' && *p!='-')++p;
    int first=0,last=0;if(!weekdayNumber(begin,unsigned(p-begin),first))return false;last=first;
    if(*p=='-'){++p;begin=p;while(*p&&*p!=',')++p;if(!weekdayNumber(begin,unsigned(p-begin),last)||last<first)return false;}
    for(int n=first;n<=last;++n)mask |= uint8_t(1U << ((n==0||n==7)?6:n-1));
    if(*p==','){++p;if(!*p)return false;}else if(*p)return false;
  }
  return mask!=0;
}
inline bool assignOn(int (&minutes)[7], uint8_t mask, int minute) {
  if(minute<0||minute>1439)return false;
  for(int d=0;d<7;++d) if(mask&(1U<<d)) {if(minutes[d]>=0 && minutes[d]!=minute)return false;minutes[d]=minute;}
  return true;
}
}

namespace gtplan {
inline bool addGen1Rule(const char* rule,int (&minutes)[7],bool wantOn=true) {
  if(!rule||strlen(rule)<8||rule[4]!='-')return false;
  const char* dash=strrchr(rule,'-');if(dash==rule+4)return false;
  const bool isOn=!strcmp(dash+1,"on");
  if(!isOn&&strcmp(dash+1,"off"))return false;
  if(isOn!=wantOn)return true;
  int h,m;if(!parseNumber(rule,2,0,23,h)||!parseNumber(rule+2,2,0,59,m))return false;
  uint8_t mask=0;
  for(const char* p=rule+5;p<dash;++p){if(*p<'0'||*p>'6')return false;mask|=1U<<((*p-'0'+6)%7);}
  return mask && assignOn(minutes,mask,h*60+m);
}
inline bool addCronOn(const char* spec,int (&minutes)[7]) {
  char sec[12],min[12],hour[12],dom[12],mon[12],days[32],extra[2];
  if(sscanf(spec,"%11s %11s %11s %11s %11s %31s %1s",sec,min,hour,dom,mon,days,extra)!=6||strcmp(sec,"0")||strcmp(dom,"*")||strcmp(mon,"*"))return false;
  int h,m;uint8_t mask;
  return parseNumber(hour,strlen(hour),0,23,h)&&parseNumber(min,strlen(min),0,59,m)&&weekdayMask(days,mask)&&assignOn(minutes,mask,h*60+m);
}
struct ReplayWindow {
  struct Entry {uint8_t nonce[16]={};int64_t until=0;};
  Entry entries[32];
  enum Result { Accepted, Replay, Full };
  Result accept(const uint8_t (&nonce)[16],int64_t now){
    int free=-1;
    for(int i=0;i<32;++i){if(entries[i].until<=now)free=i;else if(!memcmp(entries[i].nonce,nonce,16))return Replay;}
    if(free<0)return Full;
    memcpy(entries[free].nonce,nonce,16);entries[free].until=now+121;return Accepted;
  }
};
}
