#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "globals.h"
#include "function.h"

// ---- littlefs ensure mounted ----
static bool ensureFsMounted() {
  static bool mounted = false;
  if (mounted) return true;

  mounted = LittleFS.begin(false);
  if (!mounted) {
    logPrint("[LITTLEFS] mount failed");
  }
  return mounted;
}