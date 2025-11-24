#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

const int RELAY_PIN  = 12;  // D6

String GHAFEER_NAME = "SHANKAL";
bool SKIP_LOCAL_RELAY = false;
bool DEBUG = false;

const unsigned long RELAY_ON_DURATION_MS = 3000;   // how long relay stays ON
const unsigned long AWAKE_WINDOW_MS      = 18000;   // total time awake before sleep
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

void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 15000)) {
    delay(500);
    debugPrint("Connecting...");
  }
  debugPrint("Wi-Fi connected: " + WiFi.localIP().toString());
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
    client.publish(statusTopic.c_str(), "Relay forced ON (MQTT)");
  } 
  else if (cmd == "REL_OFF") {
    digitalWrite(RELAY_PIN, LOW);
    client.publish(statusTopic.c_str(), "Relay forced OFF (MQTT)");
  }
  else if (cmd == "PING") {
    client.publish(statusTopic.c_str(), "Awake and responding");
  }
}

void goToSleep() {
  client.publish(statusTopic.c_str(), "Going to deep sleep...");
  delay(150);
  client.disconnect();
  WiFi.disconnect(true);
  debugPrint("Sleeping...");
  delay(2000);  // wait 2s to let PIR output go LOW
  ESP.deepSleep(0);   // forever, until RST triggered (PIR)
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // default OFF
  Serial.begin(115200);
  debugPrint("Booting after motion...");

  setup_wifi();
  buildTopics();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  while (!client.connected()) {
    client.connect(mac.c_str());
    delay(1000);
  }

  // Publish motion message
  String payload = "{\"motion\":true,\"mac\":\"" + mac +
                   "\",\"location\":\"" + GHAFEER_NAME +
                   "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  client.publish(motionTopic.c_str(), payload.c_str());

  // Relay ON if not skipped
  if (!SKIP_LOCAL_RELAY) {
    digitalWrite(RELAY_PIN, HIGH);
    client.publish(statusTopic.c_str(), "Relay ON (local motion trigger)");
  } else {
    client.publish(statusTopic.c_str(), "Awake, SKIP_LOCAL_RELAY enabled");
  }

  // Stay awake for both relay duration and MQTT commands
  unsigned long start = millis();
  while (millis() - start < AWAKE_WINDOW_MS) {
    client.loop();

    // Turn relay OFF when duration elapsed
    if (!SKIP_LOCAL_RELAY && millis() - start >= RELAY_ON_DURATION_MS) {
      digitalWrite(RELAY_PIN, LOW);
      SKIP_LOCAL_RELAY = true;  // prevent reactivation in same window
      client.publish(statusTopic.c_str(), "Relay OFF (timer expired)");
    }
    delay(10);
  }

  goToSleep();
}

void loop() {}
