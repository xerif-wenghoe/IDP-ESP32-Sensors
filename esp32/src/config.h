#pragma once

#include <Arduino.h>

#define FIRMWARE_VERSION "0.1.0"

#define B1_WIFI_BUTTON 25
#define B2_SERVER_BUTTON 26

#define LED_WIFI 2
#define LED_SERVER 4
#define ALWAYS_ON_GPIO 34

// LED electrical polarity.
// false: GPIO HIGH turns LED on (GPIO -> resistor -> LED -> GND).
// true : GPIO LOW turns LED on  (3.3 V -> resistor -> LED -> GPIO).
// Change either value to true if that LED is wired active-LOW.
#define LED_WIFI_ACTIVE_LOW false
#define LED_SERVER_ACTIVE_LOW false

inline void setLed(uint8_t pin, bool on, bool activeLow) {
    digitalWrite(pin, activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

inline void setWifiLed(bool on) {
    setLed(LED_WIFI, on, LED_WIFI_ACTIVE_LOW);
}

inline void setServerLed(bool on) {
    setLed(LED_SERVER, on, LED_SERVER_ACTIVE_LOW);
}

#define RAIN_DO_PIN 27

#define I2C_SDA 21
#define I2C_SCL 22

// Dummy motor pins; another team can replace motor.cpp.
#define MOTOR_PIN_1 18
#define MOTOR_PIN_2 19
#define MOTOR_PIN_3 23
#define MOTOR_PIN_4 32

#define SENSOR_INTERVAL_MS (60UL * 1000UL)
#define HEARTBEAT_INTERVAL_MS (30UL * 1000UL)
#define WIFI_RETRY_INTERVAL_MS 5000UL
#define WS_RETRY_INTERVAL_MS 5000UL

#define BUTTON_LONG_PRESS_MS 1500UL

// Pairing portal lifetime. Keep aligned with server DEVICE_SECRET_TTL_SECONDS (default 900 s).
#define PAIRING_AP_TIMEOUT_MS (15UL * 60UL * 1000UL)

// This firmware is for the main ESP32 of the 3-ESP Smart Drying Rack system.
#define SYSTEM_ESP_COUNT 3
#define DEVICE_ROLE "main"

// Change to HIGH if the YL-83 comparator logic is reversed.
#define RAIN_ACTIVE_LEVEL LOW

#define OFFLINE_BATCH_SIZE 20
