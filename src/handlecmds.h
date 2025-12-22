#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

// Forward declarations
extern WiFiClient wifiClient;
extern PubSubClient client;
extern String statusTopic;

// Function declarations
void handleCommand(String cmd);
void addHelp(JsonArray arr, const char* cmd, const char* desc);
void buildTopics();

// Parsers (strict numeric, with range) and bool
// -------------------------------
static bool parseInt(const String& arg, long& outVal, long minVal, long maxVal) {
  if (arg.length() == 0) return false;
  for (size_t i = 0; i < arg.length(); ++i) {
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

void handleCommand(String cmd) {
  JsonDocument doc;
  String response;

  if (cmd == "REL_ON") {
    digitalWrite(RELAY_PIN, HIGH);
    relayActivatedMillis = millis();
    doc["status"] = "ok";
    doc["relay"] = "ON";
  }
  else if (cmd == "REL_OFF") {
    digitalWrite(RELAY_PIN, LOW);
    relayActivatedMillis = 0;
    doc["status"] = "ok";
    doc["relay"] = "OFF";
  }
  else if (cmd == "REL_STATUS") {
    doc["status"] = "ok";
    doc["relay_status"] = digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF";
  }
  else if (cmd.startsWith("PIR_INTERVAL:")) {
      long temp = PIR_INTERVAL;
      if (!parseInt(cmd.substring(13), temp, 0L, (long)MAX_PIR_INTERVAL_MS)) {
      doc["status"] = "error";
    } else {
      PIR_INTERVAL = temp;
      doc["status"] = "ok";
      doc["PIR_INTERVAL"] = PIR_INTERVAL;
    }
  }
  else if (cmd.startsWith("SKIP_LOCAL_RELAY:")) {
    String value = cmd.substring(17);
    bool parsedValue;
    if (parseBool(value, parsedValue)) {
      SKIP_LOCAL_RELAY = parsedValue;
      doc["status"] = "ok";
      doc["SKIP_LOCAL_RELAY"] = SKIP_LOCAL_RELAY;
    } else {
      doc["status"] = "error";
      doc["message"] = "Invalid SKIP_LOCAL_RELAY value";
    }
  }
  else if (cmd.startsWith("RELAY_MAX_ON_DURATION:")) {
    RELAY_MAX_ON_DURATION = cmd.substring(21).toInt();
    doc["status"] = "ok";
    doc["RELAY_MAX_ON_DURATION"] = RELAY_MAX_ON_DURATION;
  }
  else if (cmd == "RESTART" || cmd == "REBOOT") {
    doc["status"] = "ok";
    doc["message"] = "Restarting...";
    serializeJson(doc, response);
    client.publish(statusTopic.c_str(), response.c_str());
    delay(1000);
    ESP.restart();
  }
  else if (cmd.startsWith("DEBUG:")) {
    String value = cmd.substring(6);
    if (value == "true" || value == "1") {
      DEBUG = true;
      doc["status"] = "ok";
      doc["debug"] = "enabled";
    } else if (value == "false" || value == "0") {
      DEBUG = false;
      doc["status"] = "ok";
      doc["debug"] = "disabled";
    } else {
      doc["status"] = "error";
      doc["message"] = "Invalid DEBUG value";
    }
  }
  else if (cmd.startsWith("GHAFEER_NAME:")) {
    String newName = cmd.substring(13);
    if (newName.length() > 0) {
      GHAFEER_NAME = newName;
      buildTopics();
      doc["status"] = "ok";
      doc["GHAFEER_NAME"] = GHAFEER_NAME;
    } else {
      doc["status"] = "error";
      doc["message"] = "Invalid GHAFEER_NAME";
    }
  }
  else if (cmd == "STATUS") {
    doc["status"] = "online";
    doc["name"] = GHAFEER_NAME;
    doc["mac"] = mac;
    doc["ip"] = WiFi.localIP().toString();
    doc["relay"] = digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF";
    doc["skip_local_relay"] = SKIP_LOCAL_RELAY;
    doc["pir_interval"] = PIR_INTERVAL;
    doc["debug"] = DEBUG;
  }
  else if (cmd == "HELP") {
    // JsonArray commands = doc.createNestedArray("commands");
    JsonArray commands = doc["commands"].to<JsonArray>();

    addHelp(commands, "REL_ON", "Turn relay ON");
    addHelp(commands, "REL_OFF", "Turn relay OFF");
    addHelp(commands, "REL_STATUS", "Get relay status");
    addHelp(commands, "PIR_INTERVAL:<ms>", "Set PIR sensing interval");
    addHelp(commands, "SKIP_LOCAL_RELAY:<true/false>", "Bypass local relay control");
    addHelp(commands, "DEBUG:<true/false>", "Enable/disable debug");
    addHelp(commands, "GHAFEER_NAME:<name>", "Set device name");
    addHelp(commands, "STATUS", "Get full device status");
    addHelp(commands, "RESTART/REBOOT", "Restart device");
    addHelp(commands, "RELAY_MAX_ON_DURATION:<ms>", "Set relay max ON duration");
    addHelp(commands, "HELP", "Show this help message");
  }
  else {
    doc["status"] = "error";
    doc["message"] = "Unknown command: " + cmd;
  }

  serializeJson(doc, response);
  client.publish(statusTopic.c_str(), response.c_str());
}

void addHelp(JsonArray arr, const char* cmd, const char* desc) {
  // JsonObject obj = arr.createNestedObject();
  JsonObject obj = arr.add<JsonObject>();
  obj["cmd"] = cmd;
  obj["desc"] = desc;
}

#endif   