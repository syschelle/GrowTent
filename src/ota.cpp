#include "ota.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
extern WebServer server;

#if __has_include("build_version.h")
#include "build_version.h"
#endif

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ---- configuration ----
static const char* GITHUB_OWNER = "syschelle";
static const char* GITHUB_REPO = "GrowTent";
static const char* OTA_ASSET_NAME = "firmware.bin";

// v0.1.12 -> 0,1,12
static bool parseSemver(const String& in, int& major, int& minor, int& patch) {
  String s = in;
  s.trim();

  if (s.startsWith("v") || s.startsWith("V")) {
    s.remove(0, 1);
  }

  int p1 = s.indexOf('.');
  int p2 = s.indexOf('.', p1 + 1);

  if (p1 <= 0 || p2 <= p1) {
    return false;
  }

  major = s.substring(0, p1).toInt();
  minor = s.substring(p1 + 1, p2).toInt();
  patch = s.substring(p2 + 1).toInt();
  return true;
}

static int compareSemver(const String& a, const String& b) {
  int aMajor, aMinor, aPatch;
  int bMajor, bMinor, bPatch;

  bool aOk = parseSemver(a, aMajor, aMinor, aPatch);
  bool bOk = parseSemver(b, bMajor, bMinor, bPatch);

  // If the installed version is "dev" or otherwise invalid,
  // treat it as older than any valid release tag.
  if (!aOk && bOk) return -1;

  // If the latest version is invalid but the installed one is valid,
  // do not offer an update.
  if (aOk && !bOk) return 1;

  // If both are invalid, consider them equal.
  if (!aOk && !bOk) return 0;

  if (aMajor != bMajor) return (aMajor < bMajor) ? -1 : 1;
  if (aMinor != bMinor) return (aMinor < bMinor) ? -1 : 1;
  if (aPatch != bPatch) return (aPatch < bPatch) ? -1 : 1;

  return 0;
}

void handleOtaCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"wifi_not_connected\"}");
    return;
  }

  // Since v0.2.31 the release workflow publishes a tiny manifest.json next to
  // firmware.bin.  Reading this small file is much cheaper and more robust on
  // the ESP32 than parsing GitHub's full /releases/latest API response.
  const String manifestUrl =
      String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
      "/releases/latest/download/manifest.json";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);  // connection-close response; avoids chunked buffering surprises

  Serial.println("[OTA CHECK] begin");
  Serial.print("[OTA CHECK] freeHeap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" largestBlock=");
  Serial.println(ESP.getMaxAllocHeap());

  if (!http.begin(client, manifestUrl)) {
    Serial.println("[OTA CHECK] http.begin failed");
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"http_begin_failed\"}");
    return;
  }

  http.addHeader("User-Agent", "GrowTent-ESP32");
  const int code = http.GET();

  Serial.print("[OTA CHECK] HTTP=");
  Serial.print(code);
  Serial.print(" contentLength=");
  Serial.println(http.getSize());

  if (code != HTTP_CODE_OK) {
    String err = String("{\"ok\":false,\"error\":\"manifest_http_") + code + "\"}";
    http.end();
    server.send(502, "application/json; charset=utf-8", err);
    return;
  }

  JsonDocument filter;
  filter["version"] = true;
  filter["changelog"] = true;
  filter["firmwareUrl"] = true;

  JsonDocument doc;
  DeserializationError jerr =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

  http.end();

  if (jerr) {
    Serial.print("[OTA CHECK] JSON parse failed: ");
    Serial.println(jerr.c_str());
    String err = "{\"ok\":false,\"error\":\"json_parse_failed:";
    err += jerr.c_str();
    err += "\"}";
    server.send(502, "application/json; charset=utf-8", err);
    return;
  }

  const String latestTag = doc["version"] | "";
  const String changelog = doc["changelog"] | "";
  String firmwareUrl = doc["firmwareUrl"] | "";

  if (latestTag.isEmpty()) {
    Serial.println("[OTA CHECK] manifest missing version");
    server.send(502, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"manifest_missing_version\"}");
    return;
  }

  if (firmwareUrl.isEmpty()) {
    firmwareUrl = String("https://github.com/") + GITHUB_OWNER + "/" + GITHUB_REPO +
                  "/releases/latest/download/firmware.bin";
  }

  const bool updateAvailable = (compareSemver(String(FW_VERSION), latestTag) < 0);

  JsonDocument out;
  out["ok"] = true;
  out["currentVersion"] = FW_VERSION;
  out["latestVersion"] = latestTag;
  out["updateAvailable"] = updateAvailable;
  out["changelog"] = changelog;
  out["firmwareUrl"] = firmwareUrl;

  String resp;
  serializeJson(out, resp);

  Serial.print("[OTA CHECK] current=");
  Serial.print(FW_VERSION);
  Serial.print(" latest=");
  Serial.print(latestTag);
  Serial.print(" updateAvailable=");
  Serial.println(updateAvailable ? "true" : "false");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", resp);
}

static String otaJsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\r", "\\r");
  value.replace("\n", "\\n");
  return value;
}

static void sendOtaStreamEvent(const String& jsonLine) {
  // The OTA request itself stays open while HTTPUpdate downloads/flashes the
  // image.  Chunked NDJSON lets the browser receive progress from the existing
  // HTTPUpdate callbacks without creating another FreeRTOS task.
  if (!server.client().connected()) return;
  server.sendContent(jsonLine + "\n");
}

void handleOtaUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"wifi_not_connected\"}");
    return;
  }

  Serial.println("[OTA] Update requested");
  Serial.println("[OTA] WiFi connected");
  Serial.print("[OTA] IP: ");
  Serial.println(WiFi.localIP());

  const char* firmwareUrl =
    "https://github.com/syschelle/GrowTent/releases/latest/download/firmware.bin";
  Serial.print("[OTA] URL: ");
  Serial.println(firmwareUrl);

  // Send the response headers immediately.  The body stays open and receives
  // one compact JSON object per line as the update progresses.
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/x-ndjson; charset=utf-8", "");
  sendOtaStreamEvent("{\"type\":\"accepted\",\"percent\":0,\"message\":\"starting\"}");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPUpdate otaUpdater;
  otaUpdater.rebootOnUpdate(false);
  otaUpdater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  int lastPercentSent = -1;

  otaUpdater.onStart([&]() {
    Serial.println("[OTA] Start");
    sendOtaStreamEvent("{\"type\":\"start\",\"percent\":0,\"message\":\"download_started\"}");
  });

  otaUpdater.onProgress([&](int current, int total) {
    int percent = 0;
    if (total > 0) {
      percent = (int)((100LL * current) / total);
      if (percent < 0) percent = 0;
      if (percent > 100) percent = 100;
    }

    // Keep Serial useful without flooding it and only push a browser update
    // when the visible percentage changes.
    if (percent != lastPercentSent) {
      lastPercentSent = percent;
      Serial.printf("[OTA] Progress: %d%% (%d / %d)\n", percent, current, total);

      String evt;
      evt.reserve(112);
      evt = "{\"type\":\"progress\",\"current\":";
      evt += current;
      evt += ",\"total\":";
      evt += total;
      evt += ",\"percent\":";
      evt += percent;
      evt += "}";
      sendOtaStreamEvent(evt);
    }
  });

  otaUpdater.onEnd([&]() {
    Serial.println("[OTA] End");
    sendOtaStreamEvent("{\"type\":\"download_complete\",\"percent\":100,\"message\":\"finalizing\"}");
  });

  otaUpdater.onError([&](int error) {
    Serial.printf("[OTA] Error callback: %d\n", error);
  });

  t_httpUpdate_return ret = otaUpdater.update(client, firmwareUrl);

  if (ret == HTTP_UPDATE_OK) {
    Serial.println("[OTA] Update OK, restarting...");
    sendOtaStreamEvent("{\"type\":\"result\",\"ok\":true,\"percent\":100,\"message\":\"updated_restarting\"}");
    server.sendContent("");
    delay(1200);
    ESP.restart();
    return;
  }

  if (ret == HTTP_UPDATE_NO_UPDATES) {
    Serial.println("[OTA] No updates");
    sendOtaStreamEvent("{\"type\":\"result\",\"ok\":true,\"percent\":100,\"message\":\"no_update\"}");
    server.sendContent("");
    return;
  }

  String err = otaUpdater.getLastErrorString();
  Serial.print("[OTA] Failed: ");
  Serial.println(err);

  const String escaped = otaJsonEscape(err);
  sendOtaStreamEvent(String("{\"type\":\"error\",\"ok\":false,\"message\":\"") + escaped + "\"}");
  server.sendContent("");
}

