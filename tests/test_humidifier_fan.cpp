#include "humidifier_fan_policy.h"
#include <cassert>
#include <limits>
int main(){
  using namespace gtfan;
  assert(durationMs(0)==0 && durationMs(-1)==0);
  assert(durationMs(std::numeric_limits<double>::quiet_NaN())==0);
  assert(durationMs(std::numeric_limits<double>::infinity())==0);
  assert(durationMs(2147484)==0 && durationMs(0.25)==250);
  assert(durationMs(60)==60000);
  assert(refreshDue(false,0,0));
  assert(!refreshDue(true,3599999,0));assert(refreshDue(true,3600000,0));
  assert(refreshDue(true,3600000-100,UINT32_MAX-99));
  Timer t;
  t.command(true,false,100,60000);assert(!t.running);
  t.command(true,true,100,60000);assert(t.tick(60099));assert(!t.tick(60100));
  t.command(true,true,100,60000);t.command(true,false,1000,60000);
  assert(t.deadline==60100); // Failed ON does not extend the timer.
  t.command(true,true,1000,60000);assert(t.deadline==61000);
  t.command(false,false,2000,60000);assert(t.running);
  t.command(false,true,2000,60000);assert(!t.running);
  t.command(true,true,2000,0);assert(!t.running); // Unknown/disabled timer.
  t.command(true,true,UINT32_MAX-499,1000);
  assert(t.tick(UINT32_MAX-1));assert(t.tick(499));assert(!t.tick(500));
  t.command(true,true,1000,500);assert(!t.tick(2000)); // Response arrives after deadline.
}
