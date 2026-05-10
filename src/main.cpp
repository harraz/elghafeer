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
constexpr unsigned long WIFI_CONNECT_TIMEOUT_PER_NETWORK_MS = 5000UL;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 30000UL;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000UL;

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
String activeWifiSsid = "";
String pendingWifiSsid = "";
bool wifiConnected = false;
unsigned long wifiAttemptStartedAt = 0;
unsigned long lastWifiCycleFinishedAt = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
size_t wifiNetworkIndex = 0;

enum WifiConnectState {
  WIFI_IDLE,
  WIFI_CONNECTING
};

WifiConnectState wifiConnectState = WIFI_IDLE;

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

void startWifiAttempt(size_t networkIndex) {
  WiFi.mode(WIFI_STA);
  wifiNetworkIndex = networkIndex;
  pendingWifiSsid = WIFI_NETWORKS[wifiNetworkIndex].ssid;
  wifiAttemptStartedAt = millis();
  wifiConnectState = WIFI_CONNECTING;

  debugPrint("Connecting to Wi-Fi: " + pendingWifiSsid);
  WiFi.begin(WIFI_NETWORKS[wifiNetworkIndex].ssid, WIFI_NETWORKS[wifiNetworkIndex].password);
}

void startWifiCycle() {
  wifiConnected = false;
  activeWifiSsid = "";
  pendingWifiSsid = "";
  startWifiAttempt(0);
}

void maintainWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      activeWifiSsid = WiFi.SSID();
      wifiConnected = true;
      debugPrint("Wi-Fi connected to " + activeWifiSsid + ". IP: " + WiFi.localIP().toString());
    }
    wifiConnectState = WIFI_IDLE;
    return;
  }

  if (wifiConnected) {
    debugPrint("Wi-Fi disconnected from " + activeWifiSsid);
    wifiConnected = false;
    activeWifiSsid = "";
    client.disconnect();
    lastWifiCycleFinishedAt = millis();
  }

  if (wifiConnectState == WIFI_CONNECTING) {
    if (millis() - wifiAttemptStartedAt < WIFI_CONNECT_TIMEOUT_PER_NETWORK_MS) {
      return;
    }

    debugPrint("Wi-Fi timeout: " + pendingWifiSsid);
    WiFi.disconnect();
    if (wifiNetworkIndex + 1 < WIFI_NETWORK_COUNT) {
      startWifiAttempt(wifiNetworkIndex + 1);
    } else {
      wifiConnectState = WIFI_IDLE;
      pendingWifiSsid = "";
      lastWifiCycleFinishedAt = millis();
      debugPrint("Wi-Fi unavailable, continuing offline");
    }
    return;
  }

  if (millis() - lastWifiCycleFinishedAt >= WIFI_RETRY_INTERVAL_MS) {
    startWifiCycle();
  }
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

bool reconnect() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  debugPrint("Connecting to MQTT...");
  if (client.connect(mac.c_str())) {
    client.subscribe(cmdTopic.c_str());
    debugPrint("MQTT connected, subscribed to: " + cmdTopic);
    String onlineMsg = "Device_Online via WiFi:" + activeWifiSsid + " IP:" + WiFi.localIP().toString();
    client.publish(statusTopic.c_str(), onlineMsg.c_str(), true); // retained
    String wifiMsg = "WiFi connected SSID:" + activeWifiSsid + " IP:" + WiFi.localIP().toString();
    client.publish(statusTopic.c_str(), wifiMsg.c_str());
    return true;
  }

  debugPrint("MQTT connect failed, rc=" + String(client.state()));
  return false;
}

void maintainMqtt() {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (client.connected()) {
    client.loop();
    return;
  }

  if (millis() - lastMqttReconnectAttemptMs >= MQTT_RETRY_INTERVAL_MS) {
    lastMqttReconnectAttemptMs = millis();
    reconnect();
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
  doc["wifi_connected"] = wifiConnected;
  doc["wifi_ssid"] = activeWifiSsid;
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
  if (client.connected()) {
    client.publish(motionTopic.c_str(), payload.c_str());
  }
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
      if (client.connected()) {
        publishStatusAndFlush("Relay_OFF (timer expired)", 50);
      }
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

  WiFi.mode(WIFI_STA);
  buildTopics();

  pinMode(PIR_PIN, INPUT);

  client.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  client.setBufferSize(2048); // ensure MQTT can carry HELP payload
  client.setCallback(callback);
  startWifiCycle();

  debugPrint("Setup complete");
}

void loop() {
  unsigned long now = millis();

  checkRelayTimeout();

  bool pirIsHigh = (digitalRead(PIR_PIN) == HIGH);
  bool motionStarted = pirIsHigh && !pirWasHigh;
  pirWasHigh = pirIsHigh;

  if (motionStarted && now - lastMillis >= PIR_INTERVAL) {
    lastMillis = now;
    handleMotionDetected();
  }

  maintainWifi();
  maintainMqtt();
}
