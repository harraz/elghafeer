#pragma once

#include <stdint.h>
#include <time.h>

// GPIO used to drive the relay module.
constexpr int RELAY_PIN = 12;  // D6

// Logical location/name used in MQTT topic paths and payloads.
constexpr const char* GHAFEER_NAME = "ABBAS";

// Set to true only while diagnosing the node. Debug mode enables extra serial
// and MQTT breadcrumb messages that are intentionally hidden in normal use.
constexpr bool DEBUG = false;

// The relay ON duration is randomized between these bounds for each accepted
// wake. The chosen per-wake value is stored in currentRelayOnDurationMs at runtime.
constexpr unsigned int RELAY_ON_MIN_DURATION_MS = 7000;
constexpr unsigned int RELAY_ON_MAX_DURATION_MS = 10000;

// How long the ESP stays awake after an accepted trigger. This must be long
// enough for the randomized relay ON duration plus a small safety margin.
constexpr unsigned long AWAKE_WINDOW_MS = 12000;

// Network timeouts used during the short wake cycle.
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 8000;
constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS = 3000;
constexpr unsigned long TIME_SYNC_TIMEOUT_MS = 4000;

// Rate-limiter policy:
// - count accepted triggers inside this window
// - once MAX_ACCEPTED_IN_WINDOW is reached, the next trigger starts lockout
constexpr unsigned long TRIGGER_WINDOW_MS = 30000;
constexpr uint32_t MAX_ACCEPTED_IN_WINDOW = 2;
constexpr unsigned long LOCKOUT_MS = 300000;

// Any epoch larger than this is treated as real NTP time instead of the
// uninitialized zero-like values seen before time sync completes.
constexpr time_t MIN_VALID_EPOCH = 1700000000UL;

// MQTT broker settings for this node.
constexpr const char* MQTT_SERVER = "192.168.1.246";
constexpr int MQTT_PORT = 1883;

// EEPROM layout settings for the persisted throttle state.
constexpr uint32_t EEPROM_STATE_MARKER = 0x47524652;
constexpr int EEPROM_SIZE_BYTES = 64;
constexpr int EEPROM_STATE_ADDR = 0;
