#ifndef GLOBALS_H
#define GLOBALS_H

#define MQTT_MAX_PACKET_SIZE 2048  // Increase from default 128 bytes for larger payloads

#include <Arduino.h>
#include <PubSubClient.h>

#define RELAY_ON_DURATION_MIN_LIMIT_MS   3000UL      // 3 s: prevents chattering / rapid toggles
#define RELAY_ON_DURATION_MAX_LIMIT_MS   3600000UL   // 1 h: internal safety cutoff

extern const int RELAY_PIN;
extern const bool RELAY_ACTIVE_HIGH;
extern unsigned int RELAY_MAX_ON_DURATION;
extern bool DEBUG;
extern const char* FW_GIT_BRANCH;
extern const char* FW_GIT_SHA;
extern String GHAFEER_NAME;
extern String mac;
extern String statusTopic;
extern String activeWifiSsid;
extern bool wifiConnected;

extern unsigned int relayActivatedMillis;

void setRelayState(bool on);
bool isRelayOn();

#endif   
