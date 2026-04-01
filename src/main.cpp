#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ArduinoJson.h>

JsonDocument doc;

const int RELAY_PIN  = 12;  // D6

String GHAFEER_NAME = "ABBAS";  // change this to your ghafeer name
bool relayOn = false;
unsigned long lastRelayOnMs = 0;
bool DEBUG = false;

unsigned int RELAY_ON_DURATION_MS = 10000;   // actual randomized duration for this wake
const unsigned int RELAY_ON_MIN_DURATION_MS = 7000;
const unsigned int RELAY_ON_MAX_DURATION_MS = 10000;
const unsigned long AWAKE_WINDOW_MS      = 18000;   // total time awake before sleep
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 18000;
const unsigned long MQTT_CONNECT_TIMEOUT_MS = 8000;
const unsigned long TIME_SYNC_TIMEOUT_MS = 15000;
const unsigned long TRIGGER_COOLDOWN_MS = 30000;
const char* MQTT_SERVER = "192.168.1.246";
const int   MQTT_PORT   = 1883;
const uint32_t EEPROM_STATE_MAGIC = 0x47524652;
const int EEPROM_SIZE_BYTES = 64;
const int EEPROM_STATE_ADDR = 0;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;
String statusTopic;
String motionTopic;
String cmdTopic;

struct PersistedThrottleState {
  uint32_t magic;
  uint32_t checksum;
  uint32_t lastAcceptedEpoch;
  uint32_t suppressedWakeCount;
};

uint32_t calculateChecksum(const PersistedThrottleState &state) {
  return state.magic ^ state.lastAcceptedEpoch ^ state.suppressedWakeCount ^ 0xA5A5A5A5;
}

bool readPersistedThrottleState(PersistedThrottleState &state) {
  EEPROM.get(EEPROM_STATE_ADDR, state);
  if (state.magic != EEPROM_STATE_MAGIC) {
    return false;
  }
  return state.checksum == calculateChecksum(state);
}

void writePersistedThrottleState(const PersistedThrottleState &sourceState) {
  PersistedThrottleState state = sourceState;
  state.checksum = calculateChecksum(state);
  EEPROM.put(EEPROM_STATE_ADDR, state);
  EEPROM.commit();
}

bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  unsigned long start = millis();

  while (millis() - start < TIME_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      return true;
    }
    delay(250);
  }

  return false;
}

bool isTriggerThrottled(time_t nowEpoch, PersistedThrottleState &state) {
  if (!readPersistedThrottleState(state)) {
    return false;
  }

  return nowEpoch >= state.lastAcceptedEpoch &&
         static_cast<uint32_t>(nowEpoch - state.lastAcceptedEpoch) < (TRIGGER_COOLDOWN_MS / 1000UL);
}

void recordSuppressedWake() {
  PersistedThrottleState state;
  if (!readPersistedThrottleState(state)) {
    state = {EEPROM_STATE_MAGIC, 0, 0, 0};
  }
  state.suppressedWakeCount++;
  writePersistedThrottleState(state);
}

void debugPrint(const String &msg) {
  if (DEBUG) Serial.println(msg);
}

void publishStatusStep(const String &msg) {
  if (client.connected()) {
    client.publish(statusTopic.c_str(), msg.c_str());
  }
}

bool setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < WIFI_CONNECT_TIMEOUT_MS)) {
    delay(500);
    debugPrint("Connecting...");
  }
  if (WiFi.status() != WL_CONNECTED) {
    debugPrint("Wi-Fi connect timeout");
    return false;
  }
  debugPrint("Wi-Fi connected: " + WiFi.localIP().toString());
  return true;
}

void buildTopics() {
  mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  statusTopic = "home/" + GHAFEER_NAME + "/" + mac + "/status";
  motionTopic = "home/" + GHAFEER_NAME + "/" + mac + "/motion";
  cmdTopic    = "home/" + GHAFEER_NAME + "/" + mac + "/cmd";
}

void callback(char* topic, byte* payload, unsigned int length) {
  String cmd; cmd.reserve(length+1);
  for (unsigned int i=0; i<length; i++) cmd += (char)payload[i];
  cmd.trim();

  if (cmd == "REL_ON") {
    digitalWrite(RELAY_PIN, HIGH);
    relayOn = true;
    lastRelayOnMs = millis();
    client.publish(statusTopic.c_str(), "Relay forced ON (MQTT)");
  } 
  else if (cmd == "REL_OFF") {
    digitalWrite(RELAY_PIN, LOW);
    relayOn = false;
    client.publish(statusTopic.c_str(), "Relay forced OFF (MQTT)");
  }
  else if (cmd == "PING") {
    client.publish(statusTopic.c_str(), "Awake and responding");
  }
}

void goToSleep(bool publishStatus = true) {
  if (publishStatus && client.connected()) {
    client.publish(statusTopic.c_str(), "Going to deep sleep...");
    delay(150);
    client.disconnect();
  }
  if (WiFi.isConnected()) {
    WiFi.disconnect(true);
  }
  debugPrint("Sleeping...");
  delay(2000);  // wait 2s to let PIR output go LOW
  ESP.deepSleep(0);   // forever, until RST triggered (PIR)
}

void setup() {
  digitalWrite(RELAY_PIN, LOW);  // preset output level before enabling pin to avoid boot pulse
  pinMode(RELAY_PIN, OUTPUT);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE_BYTES);
  debugPrint("Booting after motion...");

  if (!setup_wifi()) {
    goToSleep(false);
  }
  buildTopics();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  unsigned long mqttStart = millis();
  while (!client.connected() && (millis() - mqttStart < MQTT_CONNECT_TIMEOUT_MS)) {
    client.connect(mac.c_str());
    delay(500);
  }
  if (!client.connected()) {
    debugPrint("MQTT connect timeout, sleeping");
    goToSleep(false);
  }

  publishStatusStep("Boot complete: WiFi and MQTT connected");

  PersistedThrottleState persistedState;
  uint32_t suppressedWakeCount = 0;
  if (readPersistedThrottleState(persistedState)) {
    suppressedWakeCount = persistedState.suppressedWakeCount;
    publishStatusStep("Persisted state loaded");
  } else {
    persistedState = {EEPROM_STATE_MAGIC, 0, 0, 0};
    publishStatusStep("Persisted state missing; starting fresh");
  }

  if (!syncTime()) {
    publishStatusStep("Time sync failed; skipping throttle");
  } else {
    time_t nowEpoch = time(nullptr);
    publishStatusStep("Time sync OK; epoch:" + String(static_cast<unsigned long>(nowEpoch)));

    if (isTriggerThrottled(nowEpoch, persistedState)) {
      recordSuppressedWake();
      publishStatusStep("Wake suppressed: cooldown active");
      goToSleep(false);
    }

    if (persistedState.lastAcceptedEpoch > 0) {
      publishStatusStep("Wake accepted; last accepted epoch:" + String(persistedState.lastAcceptedEpoch));
    } else {
      publishStatusStep("Wake accepted; no previous accepted epoch");
    }
  }


  // Stay awake for both relay duration and MQTT commands
  unsigned long start = millis();

  uint32_t randomRange = RELAY_ON_MAX_DURATION_MS - RELAY_ON_MIN_DURATION_MS + 1;
  RELAY_ON_DURATION_MS = RELAY_ON_MIN_DURATION_MS + (ESP.random() % randomRange);

  doc["motion"] = true;
  doc["mac"] = mac;
  doc["location"] = GHAFEER_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["relay_duration_ms"] = RELAY_ON_DURATION_MS;
  doc["awake_window_ms"] = AWAKE_WINDOW_MS;

  // Publish motion message
  String payload;
  serializeJson(doc, payload);

  time_t acceptedEpoch = time(nullptr);
  if (acceptedEpoch > 1700000000) {
    persistedState.magic = EEPROM_STATE_MAGIC;
    persistedState.lastAcceptedEpoch = static_cast<uint32_t>(acceptedEpoch);
    persistedState.suppressedWakeCount = 0;
    writePersistedThrottleState(persistedState);
  }

  if (suppressedWakeCount > 0) {
    String suppressedMsg = "Suppressed_wakes:" + String(suppressedWakeCount);
    client.publish(statusTopic.c_str(), suppressedMsg.c_str());
  }

  publishStatusStep("Relay duration ms:" + String(RELAY_ON_DURATION_MS));
  client.publish(motionTopic.c_str(), payload.c_str());
  publishStatusStep("Motion event published");

  // Relay ON from local motion trigger
  digitalWrite(RELAY_PIN, HIGH);
  relayOn = true;
  lastRelayOnMs = millis();
  client.publish(statusTopic.c_str(), "Relay ON (local motion trigger)");

  while (millis() - start < AWAKE_WINDOW_MS) {
    client.loop();

    // Turn relay OFF when duration elapsed
    if (relayOn && millis() - lastRelayOnMs >= RELAY_ON_DURATION_MS) {
      digitalWrite(RELAY_PIN, LOW);
      relayOn = false;
      client.publish(statusTopic.c_str(), "Relay OFF (timer expired)");
      publishStatusStep("Relay timer expired");
    }
    delay(10);
  }

  goToSleep();
}

void loop() {}
