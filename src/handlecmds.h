#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <string.h>

// Forward declarations
extern PubSubClient client;
extern String statusTopic;
extern String activeWifiSsid;
extern bool wifiConnected;
bool publishStatusAndFlush(const String &msg, unsigned long flushMs = 200);

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

// Extracts the value part after a known prefix (e.g., "CMD:").
static bool extractValue(const String& cmd, const char* prefix, String& outVal) {
  size_t prefixLen = strlen(prefix);
  if (!cmd.startsWith(prefix)) return false;
  outVal = cmd.substring(prefixLen);
  outVal.trim();
  return outVal.length() > 0;
}

void handleCommand(String cmd) {
  // Use v7 JsonDocument; heap-backed and will grow as needed for HELP payload
  JsonDocument doc;

  String response;

  if (cmd == "REL_ON") {
    setRelayState(true);
    relayActivatedMillis = millis();
    doc["status"] = "ok";
    doc["relay"] = "ON";
  }
  else if (cmd == "REL_OFF") {
    setRelayState(false);
    relayActivatedMillis = 0;
    doc["status"] = "ok";
    doc["relay"] = "OFF";
  }
  else if (cmd == "REL_STATUS") {
    doc["status"] = "ok";
    doc["relay_status"] = isRelayOn() ? "ON" : "OFF";
  }
  else if (cmd.startsWith("RELAY_MAX_ON_DURATION:")) {
    String value;
    long temp = RELAY_MAX_ON_DURATION;
    if (extractValue(cmd, "RELAY_MAX_ON_DURATION:", value) &&
        parseInt(value, temp, (long)RELAY_ON_DURATION_MIN_LIMIT_MS, (long)RELAY_ON_DURATION_MAX_LIMIT_MS)) {
      RELAY_MAX_ON_DURATION = temp;
      doc["status"] = "ok";
      doc["RELAY_MAX_ON_DURATION"] = RELAY_MAX_ON_DURATION;
    } else {
      doc["status"] = "error";
      doc["message"] = "Invalid RELAY_MAX_ON_DURATION value";
    }
  }
  else if (cmd == "RESTART" || cmd == "REBOOT") {
    doc["status"] = "ok";
    doc["message"] = "Restarting...";
    serializeJson(doc, response);
    publishStatusAndFlush(response);
    ESP.restart();
  }
  else if (cmd.startsWith("DEBUG:")) {
    String value;
    bool parsedValue;
    if (extractValue(cmd, "DEBUG:", value) && parseBool(value, parsedValue)) {
      DEBUG = parsedValue;
      doc["status"] = "ok";
      doc["debug"] = DEBUG ? "enabled" : "disabled";
    } else {
      doc["status"] = "error";
      doc["message"] = "Invalid DEBUG value";
    }
  }
  else if (cmd.startsWith("GHAFEER_NAME:")) {
    String newName;
    if (extractValue(cmd, "GHAFEER_NAME:", newName) && newName.length() > 0) {
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
    doc["wifi_connected"] = wifiConnected;
    doc["wifi_ssid"] = activeWifiSsid;
    doc["relay"] = isRelayOn() ? "ON" : "OFF";
    doc["relay_gpio_pin"] = RELAY_PIN;
    doc["relay_active_high"] = RELAY_ACTIVE_HIGH;
    doc["relay_max_on_duration"] = RELAY_MAX_ON_DURATION;
    doc["relay_on_duration_min_limit_ms"] = RELAY_ON_DURATION_MIN_LIMIT_MS;
    doc["relay_on_duration_max_limit_ms"] = RELAY_ON_DURATION_MAX_LIMIT_MS;
    doc["debug"] = DEBUG;
    doc["fw_branch"] = FW_GIT_BRANCH;
    doc["fw_sha"] = FW_GIT_SHA;
  }
  else if (cmd == "HELP") {
    // Build help array explicitly to avoid null root quirks
    JsonArray commands = doc["commands"].to<JsonArray>();

    addHelp(commands, "REL_ON", "Turn relay ON");
    addHelp(commands, "REL_OFF", "Turn relay OFF");
    addHelp(commands, "REL_STATUS", "Get relay status");
    addHelp(commands, "DEBUG:<true/false>", "Enable/disable debug");
    addHelp(commands, "GHAFEER_NAME:<name>", "Set device name");
    addHelp(commands, "STATUS", "Get full device status");
    addHelp(commands, "RESTART/REBOOT", "Restart device");
    addHelp(commands, "RELAY_MAX_ON_DURATION:<ms>", "Set relay max ON duration, RELAY_ON_DURATION_MIN_LIMIT_MS..RELAY_ON_DURATION_MAX_LIMIT_MS");
    addHelp(commands, "HELP", "Show this help message");
  }
  else {
    doc["status"] = "error";
    doc["message"] = "Unknown command: " + cmd;
  }

  serializeJson(doc, response);
  if (client.connected()) {
    client.publish(statusTopic.c_str(), response.c_str());
  }
}

void addHelp(JsonArray arr, const char* cmd, const char* desc) {
  JsonObject obj = arr.add<JsonObject>();
  obj["cmd"] = cmd;
  obj["desc"] = desc;
}

#endif   
