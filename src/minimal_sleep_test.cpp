#include <Arduino.h>
#include <esp_sleep.h>

// Mode: bare-minimum power and wake validation on XIAO D1 / GPIO3.
constexpr gpio_num_t WAKE_GPIO = GPIO_NUM_3;  // XIAO D1

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("minimal_sleep_test: boot");

  pinMode(static_cast<uint8_t>(WAKE_GPIO), INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(BIT(WAKE_GPIO), ESP_GPIO_WAKEUP_GPIO_LOW);

  Serial.println("minimal_sleep_test: entering deep sleep");
  delay(100);
  esp_deep_sleep_start();
}

void loop() {}
