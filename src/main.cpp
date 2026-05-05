#include <ESP8266WiFi.h>
#include "globals.h"
#include "secrets.h"   // #define WIFI_SSID, WIFI_PASSWORD
#include "settings.h"  // per-device name, broker, and debug defaults
#include "handlecmds.h"
#include <ArduinoJson.h>

// These values are injected by PlatformIO at build time from the current Git
// branch and commit so the published motion payload can identify the exact
// firmware build that produced it.
const char* FW_GIT_BRANCH = BUILD_GIT_BRANCH;
const char* FW_GIT_SHA = BUILD_GIT_SHA;

String GHAFEER_NAME = DEVICE_GHAFEER_NAME;

const int PIR_PIN    = 3;  // RX/GPIO3 on ESP-01S
const int RELAY_PIN  = 0;  // D3

// These are per-device startup defaults loaded from the generated settings
// header. MQTT commands may change them later while the device is running.
unsigned int PIR_INTERVAL = DEFAULT_PIR_INTERVAL_MS;
unsigned int RELAY_MAX_ON_DURATION = DEFAULT_RELAY_MAX_ON_DURATION_MS;
unsigned int MAX_PIR_INTERVAL_MS = DEFAULT_MAX_PIR_INTERVAL_MS;
bool SKIP_LOCAL_RELAY = DEFAULT_SKIP_LOCAL_RELAY;

bool DEBUG = DEFAULT_DEBUG; // initial debug state comes from the local settings file

unsigned int lastMillis = 0;
unsigned int relayActivatedMillis = 0;
bool pirWasHigh = false;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;           // No colons, uppercase
String statusTopic;
String motionTopic;
String cmdTopic;

bool initialized;  // thiis is to set the relay to low only once at startup

void debugPrint(const String &msg) {
  if (DEBUG) {
    Serial.println(msg);
  }
}

bool publishStatusAndFlush(const String &msg, unsigned long flushMs) {
  if (!client.connected()) {
    return false;
  }

  bool queued = client.publish(statusTopic.c_str(), msg.c_str());

  // Terminal actions such as RESTART can cut power to the network stack before
  // QoS 0 status bytes leave the device. This short service window gives
  // PubSubClient and the ESP Wi-Fi stack time to push the response out first.
  unsigned long startedAt = millis();
  while (millis() - startedAt < flushMs) {
    client.loop();
    delay(10);
  }
  return queued;
}

void setup_wifi() {
  delay(10);
  debugPrint("Connecting to Wi-Fi…");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debugPrint("…still connecting");
  }
  debugPrint("Wi-Fi connected. IP: " + WiFi.localIP().toString());
}

void buildTopics() {
  mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  statusTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/status";
  motionTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/motion";
  cmdTopic    = "home/" + String(GHAFEER_NAME) + "/" + mac + "/cmd";
}

void callback(char* topic, byte* payload, unsigned int length) {
  String cmd = "";
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  cmd.trim();
  debugPrint("MQTT cmd: " + cmd);

  handleCommand(cmd);
}

void reconnect() {
  while (!client.connected()) {
    debugPrint("Connecting to MQTT…");
    if (client.connect(mac.c_str())) {
      client.subscribe(cmdTopic.c_str());
      debugPrint("MQTT connected, subscribed to: " + cmdTopic);
      client.publish(statusTopic.c_str(), "Device_Online", true); // retained
    } else {
      debugPrint("MQTT connect failed, rc=" + String(client.state()));
      delay(2000);
    }
  }
}

void handleMotionDetected() {
  bool localRelayActivated = false;
  bool relayAlreadyActive = (relayActivatedMillis != 0 && digitalRead(RELAY_PIN) == HIGH);

  if (!SKIP_LOCAL_RELAY && !relayAlreadyActive) {
    relayActivatedMillis = millis();
    digitalWrite(RELAY_PIN, HIGH);
    localRelayActivated = true;
    debugPrint("Motion ON, relay ON (local control)");
  } else if (relayAlreadyActive) {
    debugPrint("Motion detected, relay already ON");
  } else {
    debugPrint("Motion detected, SKIP_LOCAL_RELAY enabled (no local relay)");
  }

  JsonDocument doc;
  doc["motion"] = true;
  doc["mac"] = mac;
  doc["location"] = GHAFEER_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["time"] = millis();
  doc["local_relay_activated"] = localRelayActivated;
  doc["relay_already_active"] = relayAlreadyActive;
  doc["skip_local_relay"] = SKIP_LOCAL_RELAY;
  doc["pir_interval"] = PIR_INTERVAL;
  doc["max_pir_interval_ms"] = MAX_PIR_INTERVAL_MS;
  doc["fw_branch"] = FW_GIT_BRANCH;
  doc["fw_sha"] = FW_GIT_SHA;

  String payload;
  serializeJson(doc, payload);
  client.publish(motionTopic.c_str(), payload.c_str());
}

void checkRelayTimeout() {

  if (initialized) {
    initialized = false;
    digitalWrite(RELAY_PIN, LOW);
    debugPrint("Initial relay OFF at startup");
  }
  
  if (relayActivatedMillis > 0 && digitalRead(RELAY_PIN) == HIGH) {
    if (millis() - relayActivatedMillis >= RELAY_MAX_ON_DURATION) {
      digitalWrite(RELAY_PIN, LOW);
      relayActivatedMillis = 0;
      publishStatusAndFlush("Relay_OFF (timer expired)", 50);
      debugPrint("Relay OFF (timer expired)");
    }
  }
}

void setup() {
  // Keep serial debug output on TX/GPIO1 without claiming RX/GPIO3.
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);
  initialized = true;
  
  debugPrint("Starting setup...");

  setup_wifi();
  buildTopics();

  pinMode(PIR_PIN, INPUT);

  client.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  client.setBufferSize(2048); // ensure MQTT can carry HELP payload
  client.setCallback(callback);

  debugPrint("Setup complete");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  checkRelayTimeout();

  bool pirIsHigh = (digitalRead(PIR_PIN) == HIGH);
  bool motionStarted = pirIsHigh && !pirWasHigh;
  pirWasHigh = pirIsHigh;

  if (motionStarted && now - lastMillis >= PIR_INTERVAL) {
    lastMillis = now;
    handleMotionDetected();
  }
}
