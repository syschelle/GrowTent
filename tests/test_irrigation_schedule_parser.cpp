#include "irrigation_schedule_parser.h"
#include <cassert>
#include <string>
#include <iostream>
struct Parsed {int on[7]={-1,-1,-1,-1,-1,-1,-1},off[7]={-1,-1,-1,-1,-1,-1,-1};};
bool parse(const std::string& s,int gen,Parsed& p){return gtplan::parseLightSchedule(s.c_str(),s.size(),gen,p.on,p.off);}
int main(){
 Parsed p;
 assert(parse(R"({"schedule":true,"schedule_rules":["0830-12345-on","2030-12345-off","0900-06-on"]})",1,p));
 assert(p.on[0]==510&&p.on[4]==510&&p.on[6]==540&&p.on[5]==540&&p.off[0]==1230);
 Parsed disabled;assert(parse(R"({"schedule":false})",1,disabled));assert(disabled.on[0]==-1);
 Parsed rpc;assert(parse(R"({"total":3,"offset":0,"jobs":[{"enable":true,"timespec":"0 30 8 * * MON-FRI","calls":[{"method":"Switch.Set","params":{"id":0,"on":true}}]},{"enable":true,"timespec":"0 30 20 * * 1-5","calls":[{"method":"Switch.Set","params":{"id":0,"on":false}}]},{"enable":false,"timespec":"@sunrise","calls":[]}]})",3,rpc));
 assert(rpc.on[0]==510&&rpc.on[6]==-1&&rpc.off[4]==1230);
 Parsed empty;assert(parse(R"({"total":0,"jobs":[]})",2,empty));assert(empty.on[0]==-1);
 for(const auto& s:{
 R"({"total":2,"jobs":[]})",R"({"offset":1,"jobs":[]})",R"({"jobs":[{"enable":true,"timespec":"@sunrise","calls":[{"method":"Switch.Set","params":{"id":0,"on":true}}]}]})",
 R"({"jobs":[{"enable":true,"timespec":"0 30 8 * * *","calls":[{"method":"Switch.Set","params":{"id":0}}]}]})",
 R"({"jobs":[{"enable":true,"timespec":"0 30 8 * * *","calls":[{"method":"Switch.Set","params":{"id":0,"on":true}}]},{"enable":true,"timespec":"0 45 8 * * *","calls":[{"method":"Switch.Set","params":{"id":0,"on":true}}]}]})",
 R"({"jobs":[{"enable":true,"timespec":"0 30 8 * * *","calls":[{"method":"Switch.Set","params":{"id":0,"on":true}},{"method":"Switch.Set","params":{"id":0,"on":false}}]}]})","not-json"}){
  Parsed bad;assert(!parse(s,2,bad));
 }
 Parsed other;assert(parse(R"({"jobs":[{"enable":true,"timespec":"@sunrise","calls":[{"method":"Switch.Set","params":{"id":1,"on":true}}]}]})",2,other));assert(other.on[0]==-1);
 std::cout<<"PASS: actual JSON Gen1/Gen2/Gen3 parser, weekday/off times, disabled/empty, pagination, malformed/solar/ambiguous/id filtering\n";
}
