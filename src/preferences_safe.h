#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Normal configuration reads are allowed to encounter a key that has never
// been stored (fresh install, newly added setting, factory reset, optional
// credential). Preferences::get*() can emit ESP-IDF error output for a missing
// key even when a fallback value is supplied, so check key existence first.
//
// IMPORTANT: These helpers are for ordinary reads only. Readback immediately
// after put*() deliberately keeps using Preferences::get*() directly so a
// failed/missing write remains visible as a real error.
static inline bool prefKeyExists(Preferences& prefs, const char* key) {
  return key != nullptr && key[0] != '\0' && prefs.isKey(key);
}

static inline String prefGetStringOrDefault(
    Preferences& prefs,
    const char* key,
    const char* fallback = "") {
  if (!prefKeyExists(prefs, key)) {
    return String(fallback ? fallback : "");
  }
  return prefs.getString(key, fallback ? fallback : "");
}

static inline String prefGetStringOrDefault(
    Preferences& prefs,
    const char* key,
    const String& fallback) {
  if (!prefKeyExists(prefs, key)) {
    return fallback;
  }
  return prefs.getString(key, fallback.c_str());
}

static inline int prefGetIntOrDefault(
    Preferences& prefs,
    const char* key,
    int fallback = 0) {
  if (!prefKeyExists(prefs, key)) {
    return fallback;
  }
  return prefs.getInt(key, fallback);
}

static inline float prefGetFloatOrDefault(
    Preferences& prefs,
    const char* key,
    float fallback = 0.0f) {
  if (!prefKeyExists(prefs, key)) {
    return fallback;
  }
  return prefs.getFloat(key, fallback);
}

static inline bool prefGetBoolOrDefault(
    Preferences& prefs,
    const char* key,
    bool fallback = false) {
  if (!prefKeyExists(prefs, key)) {
    return fallback;
  }
  return prefs.getBool(key, fallback);
}
