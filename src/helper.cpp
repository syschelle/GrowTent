#include "helper.h"

#include <LittleFS.h>

// kommt aus deinem Projekt
void logPrint(const String& msg);

bool ensureFsMounted() {
  static bool mounted = false;
  if (mounted) return true;

  mounted = LittleFS.begin(false);
  if (!mounted) {
    logPrint("[LITTLEFS] mount failed");
  }
  return mounted;
}