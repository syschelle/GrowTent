#include "helper.h"

#include <LittleFS.h>

// kommt aus deinem Projekt
void logPrint(const String& msg);

static bool g_fsMounted = false;
static bool g_fsMountTried = false;

bool ensureFsMounted() {
  if (g_fsMounted) return true;

  g_fsMountTried = true;

  // OTA-safe: false = do NOT auto-format on mount failure.
  // Auto-formatting here could erase existing diary data after an OTA update.
  g_fsMounted = LittleFS.begin(false);

  if (!g_fsMounted) {
    logPrint("[LITTLEFS] mount failed - NOT formatting automatically (OTA safe)");
  } else {
    logPrint("[LITTLEFS] mounted");
  }

  return g_fsMounted;
}

bool formatFsForRecovery() {
  logPrint("[LITTLEFS] recovery format requested");

  if (g_fsMounted) {
    LittleFS.end();
    g_fsMounted = false;
  }

  g_fsMountTried = true;

  if (!LittleFS.format()) {
    logPrint("[LITTLEFS] format failed");
    return false;
  }

  g_fsMounted = LittleFS.begin(false);
  if (!g_fsMounted) {
    logPrint("[LITTLEFS] mount after format failed");
    return false;
  }

  logPrint("[LITTLEFS] formatted and mounted");
  return true;
}
