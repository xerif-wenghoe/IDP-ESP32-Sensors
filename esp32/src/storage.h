#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct SensorRecord {
    String id;
    bool rain;
    float temperature;
    float pressure;
    float humidity;
    String timestamp;
};

namespace Storage {
    void begin();
    bool appendSensor(const SensorRecord &record);
    size_t count();
    bool readBatch(JsonDocument &doc, size_t maxItems);
    bool removeBatch(size_t count);

    String getDeviceId();
    String getWifiSsid();
    String getWifiPassword();
    void saveWifi(const String &ssid, const String &password);
}
