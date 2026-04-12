#pragma once

#include <stdint.h>
#include <time.h>
#include "settings.h"

// GPIO used to drive the relay module.
constexpr int RELAY_PIN = 12;  // D6

// Logical location/name used in MQTT topic paths and payloads.
constexpr const char* GHAFEER_NAME = DEVICE_GHAFEER_NAME;

// Set to true only while diagnosing the node. Debug mode enables extra serial
// and MQTT breadcrumb messages that are intentionally hidden in normal use.
constexpr bool DEBUG = DEFAULT_DEBUG;

// The values below are deployment-tuned defaults loaded from device_config.json.
// That keeps one deep-sleep code branch reusable for multiple physical devices.
constexpr unsigned int RELAY_ON_MIN_DURATION_MS = DEFAULT_RELAY_ON_MIN_DURATION_MS;
constexpr unsigned int RELAY_ON_MAX_DURATION_MS = DEFAULT_RELAY_ON_MAX_DURATION_MS;
constexpr unsigned long AWAKE_WINDOW_MS = DEFAULT_AWAKE_WINDOW_MS;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = DEFAULT_WIFI_CONNECT_TIMEOUT_MS;
constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS = DEFAULT_MQTT_CONNECT_TIMEOUT_MS;
constexpr unsigned long TIME_SYNC_TIMEOUT_MS = DEFAULT_TIME_SYNC_TIMEOUT_MS;
constexpr unsigned long TRIGGER_WINDOW_MS = DEFAULT_TRIGGER_WINDOW_MS;
constexpr uint32_t MAX_ACCEPTED_IN_WINDOW = DEFAULT_MAX_ACCEPTED_IN_WINDOW;
constexpr unsigned long LOCKOUT_MS = DEFAULT_LOCKOUT_MS;

// Any epoch larger than this is treated as real NTP time instead of the
// uninitialized zero-like values seen before time sync completes.
constexpr time_t MIN_VALID_EPOCH = 1700000000UL;

// MQTT broker settings for this node.
constexpr const char* MQTT_SERVER = MQTT_BROKER_HOST;
constexpr int MQTT_PORT = MQTT_BROKER_PORT;

// EEPROM layout settings for the persisted throttle state.
constexpr uint32_t EEPROM_STATE_MARKER = 0x47524652;
constexpr int EEPROM_SIZE_BYTES = 64;
constexpr int EEPROM_STATE_ADDR = 0;
