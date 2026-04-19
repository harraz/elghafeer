#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_sleep.h>

#include "settings.h"
#include "secrets.h"

// Mode: minimal Wi-Fi + MQTT + deep-sleep baseline on XIAO D1 / GPIO3.
constexpr gpio_num_t WAKE_GPIO = GPIO_NUM_3;  // XIAO D1

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long wifiStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - wifiStartedAt < DEFAULT_WIFI_CONNECT_TIMEOUT_MS)) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);

    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();

    unsigned long mqttStartedAt = millis();
    while (!mqttClient.connected() &&
           (millis() - mqttStartedAt < DEFAULT_MQTT_CONNECT_TIMEOUT_MS)) {
      mqttClient.connect(mac.c_str());
      delay(250);
    }

    if (mqttClient.connected()) {
      String topic = "home/" + String(DEVICE_GHAFEER_NAME) + "/" + mac + "/status";
      String payload = "MQTT_SMOKE";
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        payload += " wake=gpio";
      } else {
        payload += " wake=cold_boot";
      }
      mqttClient.publish(topic.c_str(), payload.c_str());
      delay(100);
      mqttClient.disconnect();
    }

    WiFi.disconnect(true, true);
  }

  pinMode(static_cast<uint8_t>(WAKE_GPIO), INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_GPIO), ESP_GPIO_WAKEUP_GPIO_LOW);
  delay(100);
  esp_deep_sleep_start();
}

void loop() {}
