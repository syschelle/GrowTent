#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "function.h"

// Example URL:
// https://github.com/<user>/<repo>/releases/latest/download/firmware.bin
static const char* OTA_BIN_URL =
    "https://github.com/syschelle/GrowTent/releases/latest/download/firmware.bin";

static bool otaUpdateFromUrl(const String& url) {
    if (WiFi.status() != WL_CONNECTED) {
        logPrint("[OTA] WiFi not connected");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();  // Simpler start; later replace with cert pinning if needed.

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    logPrint("[OTA] Downloading: " + url);

    if (!http.begin(client, url)) {
        logPrint("[OTA] http.begin failed");
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        logPrint("[OTA] HTTP GET failed: " + String(code));
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        logPrint("[OTA] Invalid content length");
        http.end();
        return false;
    }

    if (!Update.begin((size_t)contentLength)) {
        logPrint("[OTA] Update.begin failed");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);

    if (written != (size_t)contentLength) {
        logPrint("[OTA] Written only " +
                 String((unsigned long)written) +
                 " of " +
                 String(contentLength));
        Update.abort();
        http.end();
        return false;
    }

    if (!Update.end()) {
        logPrint("[OTA] Update.end failed: " + String(Update.getError()));
        http.end();
        return false;
    }

    if (!Update.isFinished()) {
        logPrint("[OTA] Update not finished");
        http.end();
        return false;
    }

    http.end();
    logPrint("[OTA] Update successful, rebooting...");
    delay(500);
    ESP.restart();

    return true;
}