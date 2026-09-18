#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>

static Preferences prefs;
static const char *SENSOR_FILE = "/offline.jsonl";

namespace Storage {

void begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
    }
    prefs.begin("sdr", false);
}

String getDeviceId() {
    String id = prefs.getString("device_id", "");

    if (id.isEmpty()) {
        uint64_t chip = ESP.getEfuseMac();
        char buf[32];

        snprintf(
            buf,
            sizeof(buf),
            "SDR-%04X%08X",
            (uint32_t)(chip >> 32),
            (uint32_t)chip
        );

        id = String(buf);
        prefs.putString("device_id", id);
    }

    return id;
}

String getWifiSsid() {
    return prefs.getString("wifi_ssid", "");
}

String getWifiPassword() {
    return prefs.getString("wifi_pass", "");
}

void saveWifi(const String &ssid, const String &password) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", password);
}

bool appendSensor(const SensorRecord &record) {
    File f = LittleFS.open(SENSOR_FILE, FILE_APPEND);
    if (!f) return false;

    JsonDocument doc;

    doc["id"] = record.id;
    doc["rain"] = record.rain;
    doc["temperature"] = record.temperature;
    doc["pressure"] = record.pressure;
    doc["humidity"] = record.humidity;
    doc["timestamp"] = record.timestamp;

    bool ok = serializeJson(doc, f) > 0;
    f.println();
    f.close();

    return ok;
}

size_t count() {
    File f = LittleFS.open(SENSOR_FILE, FILE_READ);
    if (!f) return 0;

    size_t n = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() > 2) n++;
    }

    f.close();
    return n;
}

bool readBatch(JsonDocument &doc, size_t maxItems) {
    JsonArray arr = doc["items"].to<JsonArray>();

    File f = LittleFS.open(SENSOR_FILE, FILE_READ);
    if (!f) return false;

    size_t n = 0;

    while (f.available() && n < maxItems) {
        String line = f.readStringUntil('\n');
        line.trim();

        if (line.isEmpty()) continue;

        JsonDocument item;
        DeserializationError err = deserializeJson(item, line);

        if (err) continue;

        arr.add(item.as<JsonObject>());
        n++;
    }

    f.close();
    return n > 0;
}

bool removeBatch(size_t removeCount) {
    if (removeCount == 0) return true;

    File in = LittleFS.open(SENSOR_FILE, FILE_READ);
    if (!in) return false;

    File out = LittleFS.open("/offline.tmp", FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }

    size_t skipped = 0;

    while (in.available()) {
        String line = in.readStringUntil('\n');

        if (skipped < removeCount) {
            skipped++;
            continue;
        }

        out.print(line);
    }

    in.close();
    out.close();

    LittleFS.remove(SENSOR_FILE);
    LittleFS.rename("/offline.tmp", SENSOR_FILE);

    return true;
}

}
