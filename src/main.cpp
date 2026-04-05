#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include "config.h"
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ArduinoJson.h>

JsonDocument doc;

// These values are injected by PlatformIO at build time from the current Git
// branch and commit. They make it possible to identify exactly which firmware
// source version was flashed onto the device.
const char* FW_GIT_BRANCH = BUILD_GIT_BRANCH;
const char* FW_GIT_SHA = BUILD_GIT_SHA;

bool relayOn = false;
unsigned long lastRelayOnMs = 0;

// The actual relay ON duration chosen for the current accepted wake.
unsigned int currentRelayOnDurationMs = RELAY_ON_MAX_DURATION_MS;

WiFiClient espClient;
PubSubClient client(espClient);

String mac;
String statusTopic;
String motionTopic;

// This struct is the small block of data we store in EEPROM.
// EEPROM is used here so the saved values survive resets and full power loss.
struct PersistedThrottleState {
  // A fixed marker that lets us tell whether EEPROM contains our data format.
  uint32_t formatMarker;
  // A quick integrity check so broken or random EEPROM contents are ignored.
  uint32_t checksum;
  // The Unix time, in seconds, when the current counting window started.
  uint32_t windowStartEpoch;
  // How many accepted wakes happened inside the current window.
  uint32_t acceptedCountInWindow;
  // If non-zero and still in the future, all triggers are suppressed until this time.
  uint32_t cooldownUntilEpoch;
  // How many wakes were blocked since the last accepted wake.
  uint32_t suppressedWakeCount;
};

uint32_t calculateChecksum(const PersistedThrottleState &state) {
  // Build one number from the important fields so we can later detect whether
  // the EEPROM contents were corrupted or do not match what we previously wrote.
  //
  // XOR (`^`) compares numbers bit-by-bit and mixes them together into a new
  // value. We use it here because it is cheap on a microcontroller and good
  // enough for a simple "does this still look like my saved data?" check.
  //
  // This is not encryption and it is not meant to stop tampering. It is only
  // meant to catch obviously invalid or random EEPROM contents.
  uint32_t checksum = state.formatMarker;
  checksum ^= state.windowStartEpoch;
  checksum ^= state.acceptedCountInWindow;
  checksum ^= state.cooldownUntilEpoch;
  checksum ^= state.suppressedWakeCount;
  checksum ^= 0xA5A5A5A5;
  return checksum;
}

bool readPersistedThrottleState(PersistedThrottleState &state) {
  // Copy the raw bytes from EEPROM into the struct in RAM.
  EEPROM.get(EEPROM_STATE_ADDR, state);
  // If the marker does not match, EEPROM does not contain our saved state yet.
  if (state.formatMarker != EEPROM_STATE_MARKER) {
    return false;
  }
  // Only accept the stored state if the checksum still matches.
  return state.checksum == calculateChecksum(state);
}

void writePersistedThrottleState(const PersistedThrottleState &sourceState) {
  // Copy the caller's values so we can add the checksum before writing.
  PersistedThrottleState state = sourceState;
  // Fill in the checksum field from the other values.
  state.checksum = calculateChecksum(state);
  // Write the struct into EEPROM.
  EEPROM.put(EEPROM_STATE_ADDR, state);
  // Flush the write so it is actually committed to flash-backed EEPROM storage.
  EEPROM.commit();
}

// Ask NTP servers for the current wall-clock time.
// This function stops waiting when one of these happens:
// 1. a believable Unix timestamp is received,
// 2. the time-sync timeout is reached.
bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  unsigned long syncStartedAt = millis();

  while (millis() - syncStartedAt < TIME_SYNC_TIMEOUT_MS) {
    time_t now = time(nullptr);
    // Before NTP finishes, `time(nullptr)` is usually 0 or another invalid value.
    // After NTP succeeds, it becomes a real Unix timestamp.
    if (now > MIN_VALID_EPOCH) {
      return true;
    }
    // Wait a short time before checking again so we do not spin in a tight loop.
    delay(250);
  }

  return false;
}

// Increase the suppressed-wake counter so the next accepted wake can report
// how many recent triggers were blocked while the ESP was repeatedly waking.
void recordSuppressedWake() {
  PersistedThrottleState state;
  if (!readPersistedThrottleState(state)) {
    state = {EEPROM_STATE_MARKER, 0, 0, 0, 0, 0};
  }
  state.suppressedWakeCount++;
  writePersistedThrottleState(state);
}

enum TriggerDecision {
  ACCEPT_TRIGGER,
  SUPPRESS_IN_LOCKOUT,
  SUPPRESS_RATE_LIMIT
};

// Decide whether this wake should be accepted, blocked because the device is
// already in lockout, or blocked because it just exceeded the rate limit.
TriggerDecision evaluateTrigger(time_t nowEpoch, PersistedThrottleState &state) {
  // Load the last saved limiter state from EEPROM.
  // If nothing valid was saved yet, start from an empty state.
  if (!readPersistedThrottleState(state)) {
    state = {EEPROM_STATE_MARKER, 0, 0, 0, 0, 0};
  }

  // If the device is still inside a previously started lockout period,
  // reject this wake immediately.
  if (state.cooldownUntilEpoch > 0 && nowEpoch < state.cooldownUntilEpoch) {
    return SUPPRESS_IN_LOCKOUT;
  }

  // Start a new counting window when:
  // - this is the first valid trigger we have seen,
  // - the saved window start time is somehow in the future,
  // - or the old window has already expired.
  if (state.windowStartEpoch == 0 ||
      nowEpoch < state.windowStartEpoch ||
      static_cast<uint32_t>(nowEpoch - state.windowStartEpoch) >= (TRIGGER_WINDOW_MS / 1000UL)) {
    state.windowStartEpoch = static_cast<uint32_t>(nowEpoch);
    state.acceptedCountInWindow = 0;
    state.cooldownUntilEpoch = 0;
  }

  // If we have already accepted as many triggers as allowed in this window,
  // start a lockout period and reject this wake.
  if (state.acceptedCountInWindow >= MAX_ACCEPTED_IN_WINDOW) {
    state.cooldownUntilEpoch = static_cast<uint32_t>(nowEpoch + (LOCKOUT_MS / 1000UL));
    writePersistedThrottleState(state);
    return SUPPRESS_RATE_LIMIT;
  }

  // Otherwise this wake is allowed.
  return ACCEPT_TRIGGER;
}

void debugPrint(const String &msg) {
  if (DEBUG) Serial.println(msg);
}

// Publish debug-only breadcrumbs to the MQTT status topic.
// These messages are useful during development but are intentionally hidden in
// normal operation so the status topic only shows operational events.
void publishStatusStep(const String &msg) {
  if (DEBUG && client.connected()) {
    client.publish(statusTopic.c_str(), msg.c_str());
  }
}

void publishFirmwareIdentity() {
  String versionMsg = "Firmware:" + String(FW_GIT_BRANCH) + "@" + String(FW_GIT_SHA);

  // Print to serial in debug builds so the flashed branch/SHA can be seen
  // even when MQTT is not being watched.
  debugPrint(versionMsg);
}

// Connect to Wi-Fi, but stop trying once the Wi-Fi timeout expires.
bool setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - wifiStartedAt < WIFI_CONNECT_TIMEOUT_MS)) {
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
  statusTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/status";
  motionTopic = "home/" + String(GHAFEER_NAME) + "/" + mac + "/motion";
}

void goToSleep(bool publishStatus = true) {
  // If MQTT is connected, send one last status message before sleeping.
  if (publishStatus && client.connected()) {
    client.publish(statusTopic.c_str(), "Going to deep sleep...");
    delay(75);
    client.disconnect();
  }
  // Turn Wi-Fi off before sleeping to reduce power usage and clean up state.
  if (WiFi.isConnected()) {
    WiFi.disconnect(true);
  }
  debugPrint("Sleeping...");
  delay(1500);  // wait briefly to let the PIR/reset path settle before sleeping
  ESP.deepSleep(0);   // forever, until RST triggered (PIR)
}

void publishThrottleStateSnapshot() {
  // This snapshot is diagnostic-only. It helps explain what the persisted
  // limiter state looked like right before sleep, but it is too noisy to keep
  // on the status topic during normal operation.
  if (!DEBUG || !client.connected()) {
    return;
  }

  PersistedThrottleState state;
  if (!readPersistedThrottleState(state)) {
    client.publish(statusTopic.c_str(), "Throttle_state:missing");
    return;
  }

  String snapshot = "Throttle_state:window_start=" + String(state.windowStartEpoch) +
                    ",accepted=" + String(state.acceptedCountInWindow) +
                    ",lockout_until=" + String(state.cooldownUntilEpoch) +
                    ",suppressed=" + String(state.suppressedWakeCount);
  client.publish(statusTopic.c_str(), snapshot.c_str());
}

void setup() {
  digitalWrite(RELAY_PIN, LOW);  // preset output level before enabling pin to avoid boot pulse
  pinMode(RELAY_PIN, OUTPUT);
  Serial.begin(115200);
  // Prepare EEPROM access before reading or writing saved throttle state.
  EEPROM.begin(EEPROM_SIZE_BYTES);
  debugPrint("Booting after motion...");

  if (!setup_wifi()) {
    goToSleep(false);
  }
  buildTopics();

  client.setServer(MQTT_SERVER, MQTT_PORT);

  unsigned long mqttStartedAt = millis();
  while (!client.connected() &&
         (millis() - mqttStartedAt < MQTT_CONNECT_TIMEOUT_MS)) {
    client.connect(mac.c_str());
    delay(500);
  }
  if (!client.connected()) {
    debugPrint("MQTT connect timeout, sleeping");
    goToSleep(false);
  }

  publishFirmwareIdentity();
  publishStatusStep("Boot complete: WiFi and MQTT connected");

  PersistedThrottleState persistedState;
  uint32_t suppressedWakeCount = 0;
  if (readPersistedThrottleState(persistedState)) {
    suppressedWakeCount = persistedState.suppressedWakeCount;
    publishStatusStep("Persisted state loaded");
  } else {
    persistedState = {EEPROM_STATE_MARKER, 0, 0, 0, 0, 0};
    publishStatusStep("Persisted state missing; starting fresh");
  }

  if (!syncTime()) {
    publishStatusStep("Time sync failed; skipping throttle");
  } else {
    time_t nowEpoch = time(nullptr);
    publishStatusStep("Time sync OK; epoch:" + String(static_cast<unsigned long>(nowEpoch)));

    TriggerDecision decision = evaluateTrigger(nowEpoch, persistedState);

    if (decision == SUPPRESS_IN_LOCKOUT) {
      recordSuppressedWake();
      PersistedThrottleState updatedState;
      uint32_t suppressedCount = 0;
      if (readPersistedThrottleState(updatedState)) {
        suppressedCount = updatedState.suppressedWakeCount;
      }
      String statusMsg = "Wake suppressed: lockout active, count:" + String(suppressedCount);
      client.publish(statusTopic.c_str(), statusMsg.c_str());
      goToSleep(false);
    }

    if (decision == SUPPRESS_RATE_LIMIT) {
      recordSuppressedWake();
      PersistedThrottleState updatedState;
      uint32_t suppressedCount = 0;
      if (readPersistedThrottleState(updatedState)) {
        suppressedCount = updatedState.suppressedWakeCount;
      }
      String statusMsg = "Wake suppressed: rate limit exceeded, count:" + String(suppressedCount);
      client.publish(statusTopic.c_str(), statusMsg.c_str());
      goToSleep(false);
    }

    if (persistedState.windowStartEpoch > 0) {
      publishStatusStep("Wake accepted; current window started at epoch:" + String(persistedState.windowStartEpoch));
    } else {
      publishStatusStep("Wake accepted; starting first window");
    }
  }
  // Pick a random relay ON duration inside the allowed range for this wake.
  uint32_t randomRange = RELAY_ON_MAX_DURATION_MS - RELAY_ON_MIN_DURATION_MS + 1;
  currentRelayOnDurationMs = RELAY_ON_MIN_DURATION_MS + (ESP.random() % randomRange);

  doc["motion"] = true;
  doc["mac"] = mac;
  doc["location"] = GHAFEER_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["relay_duration_ms"] = currentRelayOnDurationMs;
  doc["awake_window_ms"] = AWAKE_WINDOW_MS;
  doc["fw_branch"] = FW_GIT_BRANCH;
  doc["fw_sha"] = FW_GIT_SHA;

  // Publish the motion payload for an accepted trigger.
  String payload;
  serializeJson(doc, payload);

  time_t acceptedEpoch = time(nullptr);
  // Save the accepted trigger into EEPROM only when the NTP-based clock looks
  // valid. This updates the current trigger window and clears any previous
  // suppressed-wake count because that summary is about to be reported.
  if (acceptedEpoch > MIN_VALID_EPOCH) {
    persistedState.formatMarker = EEPROM_STATE_MARKER;
    if (persistedState.windowStartEpoch == 0 ||
        acceptedEpoch < persistedState.windowStartEpoch ||
        static_cast<uint32_t>(acceptedEpoch - persistedState.windowStartEpoch) >= (TRIGGER_WINDOW_MS / 1000UL)) {
      persistedState.windowStartEpoch = static_cast<uint32_t>(acceptedEpoch);
      persistedState.acceptedCountInWindow = 0;
    }
    persistedState.acceptedCountInWindow++;
    persistedState.cooldownUntilEpoch = 0;
    persistedState.suppressedWakeCount = 0;
    writePersistedThrottleState(persistedState);
  }

  if (suppressedWakeCount > 0) {
    String suppressedMsg = "Suppressed_wakes:" + String(suppressedWakeCount);
    client.publish(statusTopic.c_str(), suppressedMsg.c_str());

    // The summary has now been reported, so clear the persisted counter.
    // We save this immediately so the same old count is not announced again
    // on the next accepted wake.
    persistedState.suppressedWakeCount = 0;
    writePersistedThrottleState(persistedState);
    suppressedWakeCount = 0;
  }

  publishStatusStep("Relay duration ms:" + String(currentRelayOnDurationMs));
  client.publish(motionTopic.c_str(), payload.c_str());
  publishStatusStep("Motion event published");

  // Relay ON from local motion trigger
  digitalWrite(RELAY_PIN, HIGH);
  relayOn = true;
  lastRelayOnMs = millis();
  client.publish(statusTopic.c_str(), "Relay ON (local motion trigger)");

  // Stay awake for the configured post-trigger window so the relay can finish
  // its randomized ON duration before the ESP goes back to sleep.
  unsigned long awakeLoopStartedAt = millis();
  while (millis() - awakeLoopStartedAt < AWAKE_WINDOW_MS) {
    client.loop();

    // Turn relay OFF when duration elapsed
    if (relayOn && millis() - lastRelayOnMs >= currentRelayOnDurationMs) {
      digitalWrite(RELAY_PIN, LOW);
      relayOn = false;
      client.publish(statusTopic.c_str(), "Relay OFF (timer expired)");
      publishStatusStep("Relay timer expired");
    }
    delay(10);
  }

  publishThrottleStateSnapshot();
  goToSleep();
}

void loop() {}
