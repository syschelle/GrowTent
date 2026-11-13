#include "irrigation_plan_policy.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
using namespace gtplan;
int main(){
  const int d=civilDay(2026,11,13);
  assert(historyDateAllowed(-1,-1,d));assert(historyDateAllowed(-1,d-3,d));
  assert(historyDateAllowed(d-2,d-2,d));assert(historyDateAllowed(d-2,d,d));
  assert(!historyDateAllowed(d-2,-1,d));assert(!historyDateAllowed(d-2,d-3,d));
  assert(!historyDateAllowed(-1,d+1,d));assert(!historyDateAllowed(d-2,d+1,d));
  assert(civilDay(2026,11,14)==d+1);assert(civilDay(2024,3,1)-civilDay(2024,2,28)==2);
  assert(intervalDue(d,d-3,3));assert(!intervalDue(d,d-2,3));assert(!intervalDue(d,d,1));
  const int64_t due=1800000000;
  assert(decide(due-1,due,d,d-1,1,false)==Decision::Wait);
  assert(decide(due,due,d,d-1,1,false)==Decision::Start);
  assert(decide(due+60,due,d,d-1,2,false)==Decision::Start);
  assert(decide(due+61,due,d,d-1,1,false)==Decision::Missed);
  assert(decide(due,due,d,d,1,false)==Decision::AlreadyHandled);
  assert(decide(0,due,d,d-1,1,false)==Decision::ClockInvalid);
  assert(decide(due,due,d,d-1,3,false)==Decision::Drying);
  assert(decide(due,due,d,d-1,1,true)==Decision::Busy);
  uint8_t mask;assert(weekdayMask("*",mask)&&mask==127);
  assert(weekdayMask("MON-FRI",mask)&&mask==31);assert(weekdayMask("sun,6",mask)&&mask==96);
  assert(weekdayMask("1-5",mask)&&mask==31);
  for(auto value:{"7","MON-SUN","*/2","1,","8","","FUNDAY"})assert(!weekdayMask(value,mask));
  int days[7]={-1,-1,-1,-1,-1,-1,-1};
  assert(addCronOn("0 30 8 * * MON-FRI",days));assert(days[0]==510&&days[4]==510&&days[6]==-1);
  assert(addCronOn("0 30 8 * * 1",days));assert(!addCronOn("0 45 8 * * 1",days)); // ambiguous ON
  for(auto value:{"@sunrise","0 x 8 * * *","0 0 25 * * *","0 60 8 * * *","0 0 8 1 * *","0 0 8 * JAN *","0 0 8 * * * extra"})assert(!addCronOn(value,days));
  int gen1[7]={-1,-1,-1,-1,-1,-1,-1};
  assert(addGen1Rule("0830-15-on",gen1));assert(gen1[0]==510&&gen1[4]==510&&gen1[6]==-1);
  assert(addGen1Rule("0900-0-on",gen1));assert(gen1[6]==540);
  assert(addGen1Rule("2300-0123456-off",gen1));assert(!addGen1Rule("0900-1-on",gen1));
  assert(!addGen1Rule("2500-0-on",gen1));assert(!addGen1Rule("0900-7-on",gen1));
  ReplayWindow window;uint8_t n[16]={};
  for(int i=0;i<32;++i){n[0]=i;assert(window.accept(n,due)==ReplayWindow::Accepted);}
  n[0]=0;assert(window.accept(n,due+60)==ReplayWindow::Replay);
  n[0]=33;assert(window.accept(n,due+60)==ReplayWindow::Full); // no eviction/replay bypass
  n[0]=0;assert(window.accept(n,due+121)==ReplayWindow::Accepted);
  // Local calendar recurrence stays daily through DST even when UTC days differ.
  setenv("TZ","Europe/Berlin",1);tzset();tm t={};t.tm_year=2026-1900;t.tm_mon=2;t.tm_mday=28;t.tm_hour=8;t.tm_isdst=-1;
  time_t a=mktime(&t);t.tm_mday=29;t.tm_isdst=-1;time_t b=mktime(&t);assert(b-a==23*3600);
  assert(civilDay(2026,3,29)-civilDay(2026,3,28)==1);
  std::cout<<"PASS: civil dates, interval, grace/no catch-up, duplicate guard, drying, busy, clock, weekday/Gen1/Gen2 parsing, replay saturation, DST\n";
}
