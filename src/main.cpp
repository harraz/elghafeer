#include <ESP8266WiFi.h>
#include "globals.h"
#include "secrets.h"   // #define WIFI_SSID, WIFI_PASSWORD
#include "handlecmds.h"
#include <ArduinoJson.h>

String GHAFEER_NAME = "ASHRAF";

const int PIR_PIN    = 2;  // D4
const int RELAY_PIN  = 0;  // D3

unsigned int PIR_INTERVAL = 60000UL; // ms (default, can change via MQTT)
unsigned int RELAY_MAX_ON_DURATION = 60000UL; // ms
unsigned int MAX_PIR_INTERVAL_MS = 30000UL; // ms (maximum interval between PIR detections)
bool SKIP_LOCAL_RELAY = true; // true to use local relay control, false to control other devices via MQTT
// Note: SKIP_LOCAL_RELAY is used to skip local relay activation when motion is detected
// and the device is configured to control the relay via MQTT only.

bool DEBUG = false; // Set to true for debug messages, false for normal operation

unsigned int lastMillis = 0;
unsigned int relayActivatedMillis = 0;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;           // No colons, uppercase
String statusTopic;
String motionTopic;
String cmdTopic;

void debugPrint(const String &msg) {
#if DEBUG
  Serial.println(msg);
#endif
}

void setup_wifi() {
  delay(10);
  Serial.begin(115200);
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

void handlePIR() {
  if (relayActivatedMillis == 0) {
    
    relayActivatedMillis = millis(); // Reset the timer when motion is detected

    if (!SKIP_LOCAL_RELAY) {
      digitalWrite(RELAY_PIN, HIGH);
      debugPrint("Motion ON, relay ON (local control)");
    } else {
      debugPrint("Motion detected, but SKIP_LOCAL_RELAY is enabled, not activating relay locally");
    }
    // digitalWrite(RELAY_PIN, HIGH);
    // debugPrint("Motion ON, relay ON");

    // Publish motion detection as JSON
    String payload = "{\"motion\":true,\"mac\":\"" + mac +
      "\",\"location\":\"" + String(GHAFEER_NAME) +
      "\",\"ip\":\"" + WiFi.localIP().toString() +
      "\",\"time\":" + String(millis()) + "}";
    client.publish(motionTopic.c_str(), payload.c_str());

    if (SKIP_LOCAL_RELAY) {
      client.publish(statusTopic.c_str(), "Motion detected, but SKIP_LOCAL_RELAY is enabled, not activating relay locally");
    } else {
      client.publish(statusTopic.c_str(), "Motion detected, relay activated");
    }
    // client.publish(statusTopic.c_str(), "Relay_ON");
  }
}

void checkRelayTimeout() {
  if (relayActivatedMillis > 0 && digitalRead(RELAY_PIN) == HIGH) {
    if (millis() - relayActivatedMillis >= RELAY_MAX_ON_DURATION) {
      digitalWrite(RELAY_PIN, LOW);
      relayActivatedMillis = 0;
      client.publish(statusTopic.c_str(), "Relay_OFF (timer expired)");
      debugPrint("Relay OFF (timer expired)");
    }
  }
}

void setup() {
  // pinMode(PIR_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN,   INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // set baud rate for serial communication
  Serial.begin(115200);
  
  debugPrint("Starting setup...");

  setup_wifi();
  buildTopics();

  client.setServer("192.168.1.246", 1883); // RPi broker IP
  client.setBufferSize(2048); // ensure MQTT can carry HELP payload
  client.setCallback(callback);

  JsonDocument doc;

  debugPrint("Setup complete");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();

  checkRelayTimeout();

  if (now - lastMillis >= PIR_INTERVAL) {
    bool motion = (digitalRead(PIR_PIN) == HIGH);
    if (motion) {
      relayActivatedMillis = 0;
      lastMillis = now;
      handlePIR();
    }
  }
}
