#pragma once
#include <cstdint>
#include <cassert>
using SemaphoreHandle_t=void*;using TickType_t=uint32_t;
constexpr int pdTRUE=1;
inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
extern bool hostMutexAvailable,hostMutexHeld;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline int xSemaphoreTake(SemaphoreHandle_t,uint32_t){if(!hostMutexAvailable||hostMutexHeld)return 0;hostMutexHeld=true;return pdTRUE;}
inline void xSemaphoreGive(SemaphoreHandle_t){assert(hostMutexHeld);hostMutexHeld=false;}
