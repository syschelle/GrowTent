#include "vpd_calculation.h"
#include <cassert>
#include <cstdio>
static bool close(float a,float b) { return fabsf(a-b)<0.00001f; }
int main() {
  const float raw=calcVPD(24,-1.5f,60);
  assert(close(raw,0.9350402f)); // independent reference: leaf SVP minus actual air VP
  for(int i=0;i<100;++i) { calcVPD(35,0,10); assert(close(raw,calcVPD(24,-1.5f,60))); }
  VpdControlFilter a,b;
  const float temp[]={22,24,21,29,23};
  const float rh[]={55,65,40,80,57};
  float expectedT=temp[0],expectedH=rh[0];
  for(int i=0;i<5;++i) {
    const float control=a.update(temp[i],-1.5f,rh[i]);
    for(int j=0;j<19;++j) calcVPD(30+j,-1.5f,10+j);
    assert(close(control,b.update(temp[i],-1.5f,rh[i])));
    expectedT=0.1f*temp[i]+0.9f*expectedT;
    expectedH=0.1f*rh[i]+0.9f*expectedH;
    assert(close(control,calcVPD(expectedT,-1.5f,expectedH)));
    assert(close(a.temperatureC,expectedT));
    assert(close(a.humidityPct,expectedH));
  }
  const float t=a.temperatureC,h=a.humidityPct;
  assert(std::isnan(a.update(NAN,0,50)));
  assert(std::isnan(a.update(22,INFINITY,50)));
  assert(std::isnan(a.update(22,0,INFINITY)));
  assert(a.temperatureC==t && a.humidityPct==h);
  assert(std::isnan(calcVPD(NAN,0,50)));
  assert(std::isnan(calcVPD(22,NAN,50)));
  assert(close(calcVPD(22,0,-5),calcVPD(22,0,0)));
  assert(close(calcVPD(22,0,150),calcVPD(22,0,100)));
  assert(calcVPD(22,-5,100)==0);
  // Exercise the exact singleton entry point used by runtime wiring.
  assert(close(calcControlVPD(22,-1.5f,55),calcVPD(22,-1.5f,55)));
  for(int j=0;j<20;++j) calcVPD(40,0,0);
  assert(close(calcControlVPD(24,-1.5f,65),calcVPD(22.2f,-1.5f,56)));
  puts("VPD calculation and control filter isolation passed");
}
