#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ArduinoJson.h>

JsonDocument doc;

const int RELAY_PIN  = 12;  // D6

String GHAFEER_NAME = "ABBAS";
bool relayOn = false;
unsigned long lastRelayOnMs = 0;
bool DEBUG = false;

unsigned int RELAY_ON_DURATION_MS = 3000;   // how long relay stays ON
const unsigned long AWAKE_WINDOW_MS      = 18000;   // total time awake before sleep
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long MQTT_CONNECT_TIMEOUT_MS = 8000;
const char* MQTT_SERVER = "192.168.1.246";
const int   MQTT_PORT   = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;
String statusTopic;
String motionTopic;
String cmdTopic;

void debugPrint(const String &msg) {
  if (DEBUG) Serial.println(msg);
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


  // Stay awake for both relay duration and MQTT commands
  unsigned long start = millis();
// Remove or comment out:
// srand(time(NULL));
// RELAY_ON_DURATION_MS = rand() % RELAY_ON_DURATION_MS + 7000;

  uint32_t randomMs = ESP.random() % RELAY_ON_DURATION_MS + 7000;  // 7000 to 9999
  RELAY_ON_DURATION_MS = randomMs;   

  doc["motion"] = true;
  doc["mac"] = mac;
  doc["location"] = GHAFEER_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["relay_duration_ms"] = RELAY_ON_DURATION_MS;
  doc["awake_window_ms"] = AWAKE_WINDOW_MS;

  // Publish motion message
  String payload;
  serializeJson(doc, payload);

  client.publish(motionTopic.c_str(), payload.c_str());

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
    }
    delay(10);
  }

  goToSleep();
}

void loop() {}
