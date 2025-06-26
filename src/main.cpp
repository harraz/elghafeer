#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"   // #define WIFI_SSID, WIFI_PASSWORD

String GHAFEER_NAME = "MARZOOQ";

const int PIR_PIN    = 2;  // D4
const int RELAY_PIN  = 0;  // D3

unsigned long PIR_INTERVAL = 60000; // ms (default, can change via MQTT)
const unsigned long RELAY_MAX_ON_DURATION = 120000; // ms
bool SKIP_LOCAL_RELAY = true; // true to use local relay control, false to control other devices via MQTT
// Note: SKIP_LOCAL_RELAY is used to skip local relay activation when motion is detected
// and the device is configured to control the relay via MQTT only.

bool DEBUG = false; // Set to true for debug messages, false for normal operation

unsigned long lastMillis = 0;
unsigned long relayActivatedMillis = 0;

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

  if (cmd == "REL_ON") {
    // if (SKIP_LOCAL_RELAY) {
    //   debugPrint("Skipping local relay activation due to SKIP_LOCAL_RELAY setting");
    //   client.publish(statusTopic.c_str(), "SKIP_LOCAL_RELAY is enabled, not activating relay locally");
    //   return;
    // }
    digitalWrite(RELAY_PIN, HIGH);
    relayActivatedMillis = millis();
    client.publish(statusTopic.c_str(), "Relay_ON");
  }
  else if (cmd == "REL_OFF") {
    // if (SKIP_LOCAL_RELAY) {
    //   debugPrint("Skipping local relay deactivation due to SKIP_LOCAL_RELAY setting");
    //   client.publish(statusTopic.c_str(), "SKIP_LOCAL_RELAY is enabled, not deactivating relay locally");
    //   return;
    // }
    digitalWrite(RELAY_PIN, LOW);
    relayActivatedMillis = 0;
    client.publish(statusTopic.c_str(), "Relay_OFF");
  }
  else if (cmd == "REL_STATUS") {
    String st = digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF";
    client.publish(statusTopic.c_str(), ("Relay_Status:" + st).c_str());
  }
  else if (cmd.startsWith("PIR_INTERVAL:")) {
    PIR_INTERVAL = cmd.substring(13).toInt();
    String info = "PIR_INTERVAL_set:" + String(PIR_INTERVAL);
    client.publish(statusTopic.c_str(), info.c_str());
  } 
  else if (cmd.startsWith("SKIP_LOCAL_RELAY:")) {
    String value = cmd.substring(17);
    if (value == "true" || value == "1") {
      SKIP_LOCAL_RELAY = true;
      client.publish(statusTopic.c_str(), "SKIP_LOCAL_RELAY set to true");
    } else if (value == "false" || value == "0") {
      SKIP_LOCAL_RELAY = false;
      client.publish(statusTopic.c_str(), "SKIP_LOCAL_RELAY set to false");
    } else {
      client.publish(statusTopic.c_str(), "Invalid SKIP_LOCAL_RELAY value");
    }

  }

  else if (cmd == "RESTART") {
    client.publish(statusTopic.c_str(), "Restarting...");
    delay(1000);
    ESP.restart();
  }
  
  else if (cmd.startsWith("DEBUG:")) {
    String debugValue = cmd.substring(6);
    if (debugValue == "true" || debugValue == "1") {
      DEBUG = true;
      client.publish(statusTopic.c_str(), "DEBUG mode enabled");
    } else if (debugValue == "false" || debugValue == "0") {
      DEBUG = false;
      client.publish(statusTopic.c_str(), "DEBUG mode disabled");
    } else {
      client.publish(statusTopic.c_str(), "Invalid DEBUG value");
    }
  }
  
  else if (cmd.startsWith("GHAFEER_NAME:")) {
    String newName = cmd.substring(13);
    if (newName.length() > 0) {
      GHAFEER_NAME = newName;
      buildTopics(); // Rebuild topics with the new name
      String msg = "GHAFEER_NAME set to: " + GHAFEER_NAME;
      client.publish(statusTopic.c_str(), msg.c_str());
    } else {
      client.publish(statusTopic.c_str(), "Invalid GHAFEER_NAME value");
    }
  }
  else if (cmd == "HELP") {
    String helpMsg = "{";
    helpMsg += "\"commands\":[";
    helpMsg += "{\"cmd\":\"REL_ON\",\"desc\":\"Turn relay ON\"},";
    helpMsg += "{\"cmd\":\"REL_OFF\",\"desc\":\"Turn relay OFF\"},";
    helpMsg += "{\"cmd\":\"REL_STATUS\",\"desc\":\"Get relay status\"},";
    helpMsg += "{\"cmd\":\"PIR_INTERVAL:<value>\",\"desc\":\"Set PIR interval in ms\"},";
    helpMsg += "{\"cmd\":\"SKIP_LOCAL_RELAY:<true/false>\",\"desc\":\"Enable/disable local relay control\"},";
    helpMsg += "{\"cmd\":\"RESTART\",\"desc\":\"Restart the device\"},";
    helpMsg += "{\"cmd\":\"REBOOT\",\"desc\":\"Reboot the device\"},";
    helpMsg += "{\"cmd\":\"STATUS\",\"desc\":\"Get device status\"},";
    helpMsg += "{\"cmd\":\"DEBUG:<true/false>\",\"desc\":\"Enable/disable debug mode\"},";
    helpMsg += "{\"cmd\":\"GHAFEER_NAME:<name>\",\"desc\":\"Set GHAFEER name\"}";
    helpMsg += "]}";
    client.publish(statusTopic.c_str(), helpMsg.c_str());
  }

  else if (cmd == "REBOOT") {
    client.publish(statusTopic.c_str(), "Rebooting...");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "STATUS") {
    String relStatus = digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF";
    String info = "GHAFEER_NAME:" + String(GHAFEER_NAME) + ",Device_Status:Online,MAC:" 
            + mac + ",IP:" + WiFi.localIP().toString() + ",SKIP_LOCAL_RELAY:" + SKIP_LOCAL_RELAY
            + ",REL_Status:" + relStatus;
    client.publish(statusTopic.c_str(), info.c_str());
  }
  else {
    String info = "Unknown_CMD:" + cmd;
    client.publish(statusTopic.c_str(), info.c_str());
  }
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
  // pinMode(PIR_PIN,   INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  setup_wifi();
  buildTopics();

  client.setServer("192.168.1.246", 1883); // RPi broker IP
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

  if (now - lastMillis >= PIR_INTERVAL) {
    bool motion = (digitalRead(PIR_PIN) == HIGH);
    if (motion) {
      relayActivatedMillis = 0;
      lastMillis = now;
      handlePIR();
    }
  }
}
