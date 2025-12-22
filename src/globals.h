#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

extern const int RELAY_PIN;
extern unsigned long PIR_INTERVAL;
extern bool SKIP_LOCAL_RELAY;
extern unsigned long RELAY_MAX_ON_DURATION;
extern bool DEBUG;
extern String GHAFEER_NAME;
extern String mac;
extern String statusTopic;

extern unsigned long relayActivatedMillis;

#endif   