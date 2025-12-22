#ifndef GLOBALS_H
#define GLOBALS_H
#define MQTT_MAX_PACKET_SIZE 512  // Increase from default 128 bytes for larger payloads
#define MIN_RELAY_ON_DURATION_MS   3000UL      // 3 s: prevents chattering / rapid toggles
#define MAX_RELAY_ON_DURATION_MS   3600000UL   // 1 h: safety cutoff for long activations

#include <Arduino.h>

extern const int RELAY_PIN;
extern unsigned int PIR_INTERVAL;
extern bool SKIP_LOCAL_RELAY;
extern int RELAY_MIN_ON_DURATION;
extern unsigned int RELAY_MAX_ON_DURATION;
extern unsigned int MAX_PIR_INTERVAL_MS;
extern bool DEBUG;
extern String GHAFEER_NAME;
extern String mac;
extern String statusTopic;

extern unsigned int relayActivatedMillis;

#endif   