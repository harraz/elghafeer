#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"   // #define WIFI_SSID, WIFI_PASSWORD

const int RELAY_PIN  = 0;  // D3

String GHAFEER_NAME = "MASROOOR";
bool SKIP_LOCAL_RELAY = false;
bool DEBUG = false;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;
String statusTopic;
String motionTopic;

void debugPrint(const String &msg) {
  if (DEBUG) Serial.println(msg);
}

void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    debugPrint("Connecting...");
  }
  debugPrint("Wi-Fi connected: " + WiFi.localIP().toString());
}

void buildTopics() {
  mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  statusTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/status";
  motionTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/motion";
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // default OFF

  Serial.begin(115200);
  debugPrint("Booting after motion detection...");

  setup_wifi();
  buildTopics();

  client.setServer("192.168.1.246", 1883);
  while (!client.connected()) {
    client.connect(mac.c_str());
    delay(1000);
  }

  // Activate relay if local control is enabled and the relay is not already active
  if (!SKIP_LOCAL_RELAY && (digitalRead(RELAY_PIN) == LOW)) {
    digitalWrite(RELAY_PIN, HIGH);
  }

  client.publish(statusTopic.c_str(), "Ghafeer is awaken...");
  // Publish motion message
  String payload = "{\"motion\":true,\"mac\":\"" + mac +
      "\",\"location\":\"" + GHAFEER_NAME +
      "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  client.publish(motionTopic.c_str(), payload.c_str());

  // Publish relay status
  String relayMsg = SKIP_LOCAL_RELAY ? 
    "Motion detected, SKIP_LOCAL_RELAY enabled" : 
    "Motion detected, relay activated";
  client.publish(statusTopic.c_str(), relayMsg.c_str());

  client.publish(statusTopic.c_str(), "Going to deep sleep...");
  delay(60000);  // allow MQTT to flush


  client.disconnect();
  WiFi.disconnect(true);

  debugPrint("Going to deep sleep...");
  ESP.deepSleep(0);  // sleep forever until RST is pulled LOW
}

void loop() {
  // never reached
}
