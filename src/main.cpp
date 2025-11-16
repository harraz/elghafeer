#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"   // #define WIFI_SSID, WIFI_PASSWORD

String GHAFEER_NAME = "DAHROOG";

const int PIR_PIN    = 4;  // D2
const int RELAY_PIN  = 12;  // D6

// -------------------------------
// User-tunable safety and timing limits
// -------------------------------

// Relay ON duration (ms)
#define MIN_RELAY_ON_DURATION_MS   3000UL      // 3 s: prevents chattering / rapid toggles
#define MAX_RELAY_ON_DURATION_MS   3600000UL   // 1 h: safety cutoff for long activations

// PIR interval (ms)
#define MAX_PIR_INTERVAL_MS        600000UL    // 10 min: upper limit between motion triggers

unsigned long PIR_INTERVAL = 60000; // ms (default, can change via MQTT)
unsigned long RELAY_MAX_ON_DURATION = 60000; // ms
bool SKIP_LOCAL_RELAY = true; // true to use local relay control, false to control other devices via MQTT
// Note: SKIP_LOCAL_RELAY is used to skip local relay activation when motion is detected
// and the device is configured to control the relay via MQTT only.

bool SKIP_LOCAL_PIR = false; // true to skip local PIR relay activation, false to allow local PIR relay activation

unsigned long lastMotionPublishMillis = 0;
const unsigned long MIN_MOTION_PUBLISH_INTERVAL = 5000; // 5s cooldown between publishes

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


// Parsers (strict numeric, with range) and bool
// -------------------------------
static bool parseInt(const String& arg, long& outVal, long minVal, long maxVal) {
  if (arg.length() == 0) return false;
  for (int i = 0; i < arg.length(); ++i) {
    char c = arg[i];
    if (i == 0 && (c == '+' || c == '-')) continue;
    if (c < '0' || c > '9') return false;
  }
  long v = arg.toInt();
  if (v < minVal || v > maxVal) return false;
  outVal = v;
  return true;
}

static bool parseBool(const String& arg, bool& outVal) {
  String v = arg; v.trim(); v.toLowerCase();
  if (v == "true" || v == "1")  { outVal = true;  return true; }
  if (v == "false"|| v == "0")  { outVal = false; return true; }
  return false;
}

// -------------------------------
// MQTT callback
// -------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  // Build & sanitize command
  String cmd; cmd.reserve(length + 8);
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  cmd.trim();
  debugPrint("MQTT cmd: " + cmd);

  // Split once: prefix = before ':', arg = after ':'
  const int colon = cmd.indexOf(':');
  const String prefix = (colon >= 0) ? cmd.substring(0, colon) : cmd;
  String arg          = (colon >= 0) ? cmd.substring(colon + 1) : "";
  arg.trim();

  // -------- no-arg commands --------
  if (cmd == "REL_ON") {
    digitalWrite(RELAY_PIN, HIGH);
    relayActivatedMillis = millis();
    client.publish(statusTopic.c_str(), "Relay_ON");
  }
  else if (cmd == "REL_OFF") {
    digitalWrite(RELAY_PIN, LOW);
    relayActivatedMillis = 0;
    client.publish(statusTopic.c_str(), "Relay_OFF");
  }
  else if (cmd == "REL_STATUS") {
    String st = (digitalRead(RELAY_PIN) == HIGH) ? "ON" : "OFF";
    client.publish(statusTopic.c_str(), ("Relay_Status:" + st).c_str());
  }
  else if (cmd == "RESTART" || cmd == "REBOOT") {
    client.publish(statusTopic.c_str(),
                   (cmd == "RESTART") ? "Restarting..." : "Rebooting...");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "HELP") {
    String helpMsg = "{";
    helpMsg.reserve(256);
    helpMsg += "\"commands\":[";
    helpMsg += "{\"cmd\":\"REL_ON\",\"desc\":\"Turn relay ON\"},";
    helpMsg += "{\"cmd\":\"REL_OFF\",\"desc\":\"Turn relay OFF\"},";
    helpMsg += "{\"cmd\":\"REL_STATUS\",\"desc\":\"Get relay status\"},";
    helpMsg += "{\"cmd\":\"PIR_INTERVAL:<value>\",\"desc\":\"Set PIR interval in ms\"},";
    helpMsg += "{\"cmd\":\"RELAY_MAX_ON_DURATION:<value>\",\"desc\":\"Set maximum relay ON duration in ms\"},";
    helpMsg += "{\"cmd\":\"SKIP_LOCAL_RELAY:<true/false>\",\"desc\":\"Enable/disable local relay control\"},";
    helpMsg += "{\"cmd\":\"RESTART\",\"desc\":\"Restart the device\"},";
    helpMsg += "{\"cmd\":\"REBOOT\",\"desc\":\"Reboot the device\"},";
    helpMsg += "{\"cmd\":\"STATUS\",\"desc\":\"Get device status\"},";
    helpMsg += "{\"cmd\":\"DEBUG:<true/false>\",\"desc\":\"Enable/disable debug mode\"},";
    helpMsg += "{\"cmd\":\"GHAFEER_NAME:<name>\",\"desc\":\"Set GHAFEER name\"}";
    helpMsg += "]}";
    client.publish(statusTopic.c_str(), helpMsg.c_str());
  }
  else if (cmd == "STATUS") {
    String relStatus = (digitalRead(RELAY_PIN) == HIGH) ? "ON" : "OFF";
    String info = "{";
    info.reserve(200);
    info += "\"GHAFEER_NAME\":\"" + String(GHAFEER_NAME) + "\",";
    info += "\"Device_Status\":\"Online\",";
    info += "\"MAC\":\"" + mac + "\",";
    info += "\"IP\":\"" + WiFi.localIP().toString() + "\",";
    info += "\"SKIP_LOCAL_RELAY\":" + String(SKIP_LOCAL_RELAY ? "true" : "false") + ",";
    info += "\"SKIP_LOCAL_PIR\":" + String(SKIP_LOCAL_PIR ? "true" : "false") + ",";
    info += "\"RELAY_MAX_ON_DURATION\":" + String(RELAY_MAX_ON_DURATION) + ",";
    info += "\"REL_Status\":\"" + relStatus + "\",";
    info += "\"PIR_INTERVAL\":" + String(PIR_INTERVAL) + ",";
    info += "\"DEBUG\":" + String(DEBUG ? "true" : "false");
    info += "}";
    client.publish(statusTopic.c_str(), info.c_str());
  }

  // -------- key:value commands (strict parse with range) --------
  else if (prefix == "PIR_INTERVAL") {
    long v;
    if (!parseInt(arg, v, 0L, (long)MAX_PIR_INTERVAL_MS)) {
      client.publish(statusTopic.c_str(), "Invalid PIR_INTERVAL (expect 0..600000 ms)");
    } else {
      PIR_INTERVAL = (unsigned long)v;
      client.publish(statusTopic.c_str(), ("PIR_INTERVAL_set:" + String(PIR_INTERVAL)).c_str());
    }
  }
  else if (prefix == "RELAY_MAX_ON_DURATION") {
    long v;
    if (!parseInt(arg, v, (long)MIN_RELAY_ON_DURATION_MS, (long)MAX_RELAY_ON_DURATION_MS)) {
      client.publish(statusTopic.c_str(), "Invalid RELAY_MAX_ON_DURATION (expect 3000..3600000 ms)");
    } else {
      RELAY_MAX_ON_DURATION = (unsigned long)v;
      client.publish(statusTopic.c_str(), ("RELAY_MAX_ON_DURATION_set:" + String(RELAY_MAX_ON_DURATION)).c_str());
    }
  }
  else if (prefix == "SKIP_LOCAL_RELAY") {
    bool b;
    if (!parseBool(arg, b)) {
      client.publish(statusTopic.c_str(), "Invalid SKIP_LOCAL_RELAY (use true/false or 1/0)");
    } else {
      SKIP_LOCAL_RELAY = b;
      client.publish(statusTopic.c_str(), b ? "SKIP_LOCAL_RELAY set to true"
                                            : "SKIP_LOCAL_RELAY set to false");
    }
  }
  else if (prefix == "SKIP_LOCAL_PIR") {
    bool b;
    if (!parseBool(arg, b)) {
      client.publish(statusTopic.c_str(), "Invalid SKIP_LOCAL_PIR (use true/false or 1/0)");
    } else {
      SKIP_LOCAL_PIR = b;
      client.publish(statusTopic.c_str(), b ? "SKIP_LOCAL_PIR set to true"
                                            : "SKIP_LOCAL_PIR set to false");
    }
  }
  else if (prefix == "DEBUG") {
    bool b;
    if (!parseBool(arg, b)) {
      client.publish(statusTopic.c_str(), "Invalid DEBUG (use true/false or 1/0)");
    } else {
      DEBUG = b;
      client.publish(statusTopic.c_str(), b ? "DEBUG mode enabled" : "DEBUG mode disabled");
    }
  }
  else if (prefix == "GHAFEER_NAME") {
    if (arg.length() == 0 || arg.length() > 64) {
      client.publish(statusTopic.c_str(), "Invalid GHAFEER_NAME (1..64 chars)");
    } else {
      GHAFEER_NAME = arg;
      GHAFEER_NAME.trim();
      buildTopics();
      client.publish(statusTopic.c_str(), ("GHAFEER_NAME set to: " + GHAFEER_NAME).c_str());
    }
  }
  else {
    client.publish(statusTopic.c_str(), ("Unknown_CMD:" + cmd).c_str());
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
  const unsigned long now = millis();
  const bool canPublish = (now - lastMotionPublishMillis) >= MIN_MOTION_PUBLISH_INTERVAL;

  relayActivatedMillis = now; // start motion window

  const bool willActivateRelay = !SKIP_LOCAL_RELAY;

  // Local relay control + single debug
  if (willActivateRelay) {
    digitalWrite(RELAY_PIN, HIGH);
    debugPrint("Motion ON → relay ON (local control)");
  } else {
    debugPrint("Motion detected → SKIP_LOCAL_RELAY enabled (no local relay)");
  }

  // Only publish if cooldown passed (prevents floods on repeated triggers)
  if (canPublish) {
    lastMotionPublishMillis = now;

    // Build JSON once
    String payload;
    payload.reserve(160);
    payload += "{\"motion\":true";
    payload += ",\"mac\":\"";        payload += mac; payload += "\"";
    payload += ",\"location\":\"";   payload += String(GHAFEER_NAME); payload += "\"";
    payload += ",\"ip\":\"";         payload += WiFi.localIP().toString(); payload += "\"";
    payload += ",\"time\":";         payload += String(now);
    payload += "}";

    client.publish(motionTopic.c_str(), payload.c_str());

    client.publish(
      statusTopic.c_str(),
      willActivateRelay
        ? "Motion detected; relay activated"
        : "Motion detected; SKIP_LOCAL_RELAY enabled (no local relay)"
    );
  }
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

  client.setBufferSize(1024);   // for larger MQTT messages
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
    // bool piractive =(digitalRead(RELAY_PIN) == HIGH);
    if (motion) {
      // relayActivatedMillis = 0;
    lastMillis = now;
      handlePIR();
    }
  }
}
