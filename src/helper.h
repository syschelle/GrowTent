#pragma once

#include <Arduino.h>

// Mount LittleFS without formatting. OTA-safe: never erases data automatically.
bool ensureFsMounted();

// Explicit recovery action. This erases only the LittleFS partition, not firmware/NVS.
// Use only after the user has confirmed it.
bool formatFsForRecovery();
