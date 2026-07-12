# XIAO ESP32-C3 Hardware Bootstrap

This wiring matches the active `esp32c3_deepsleep` firmware config:

- `wake_gpio_pin`: `3` (`D1` on the XIAO header)
- `relay_gpio_pin`: `10` (`D10` on the XIAO header)
- Wake trigger: active low
- Relay trigger: active high

The firmware wakes from deep sleep through `D1` / `GPIO3`. It does not wake by
pulling `RST` low, so the ESP-01S-style reset pulse circuit is not part of this
hardware bootstrap.

## Required Pin States

Keep the ESP32-C3 boot strap pins free from forced levels during reset:

- Do not attach relay, PIR, or pull networks to `GPIO9` / `D9` / `BOOT`.
- Avoid using `GPIO2` / `D0` and `GPIO8` / `D8` for external circuits that can drive a fixed level during reset.
- If one of those pins must be used later, the external circuit must be high impedance during reset.

Use the current firmware pins instead:

| Function | XIAO pin | ESP32-C3 GPIO | Required idle state | Active state |
| --- | --- | --- | --- | --- |
| PIR wake input | `D1` | `GPIO3` | high | low |
| Relay control | `D10` | `GPIO10` | low | high |

## Startup And Deep-Sleep States

These are the states the external hardware should preserve while the ESP32-C3
is resetting, running, and sleeping:

| Phase | `D1` / `GPIO3` wake input | `D10` / `GPIO10` relay output |
| --- | --- | --- |
| Reset, before firmware runs | Held high by PIR idle state or pull-up | Held low or left pulled down so relay stays off |
| Firmware startup | Configured as `INPUT_PULLUP` | Written low before output mode is enabled |
| Normal awake idle | High | Low, relay off |
| Motion wake event | Pulled low by PIR or wake-conditioning circuit | Unchanged until firmware accepts the trigger |
| Relay active | Usually back high | High, relay on |
| Deep sleep | Must remain high | Low, relay off |
| Wake from deep sleep | Low wakes the ESP32-C3 | Unchanged |

Recommended external biasing:

```text
D1 / GPIO3    -> 47k-100k pull-up to 3V3
D10 / GPIO10  -> 47k-100k pull-down to GND
```

The `D1` pull-up is not strictly required because the firmware enables the
internal pull-up, but an external pull-up is more reliable with PIR wiring,
long leads, or open-drain wake conditioning. The `D10` pull-down keeps the
relay input off during reset before firmware has configured the pin.

## Wake Input Circuit

Wire the PIR or wake-conditioning output as an open-drain/open-collector style
active-low signal:

```text
XIAO 3V3  ---- PIR VCC, if the PIR supports 3.3 V operation
XIAO GND  ---- PIR GND
XIAO D1   ---- PIR wake output
```

The firmware enables `INPUT_PULLUP` on `D1`, so the wake line normally idles
high. The PIR should pull `D1` to ground to wake the board. If the PIR output
is push-pull and idles low, invert or condition the signal before feeding D1;
otherwise the ESP32-C3 will wake immediately after every deep sleep entry.

The wake signal is level-based, not a reset edge. A sharp capacitor pulse like
an ESP-01S `RST` wake circuit is not needed for normal operation. Use pulse or
one-shot conditioning only if the PIR holds the wake line low long enough to
cause repeated wake-sleep-wake cycles.

As a bring-up target, hold `D1` low for at least `100-200 ms`, then let it
return high before the firmware re-enters deep sleep.

For a noisy PIR output, add conditioning close to the XIAO:

- Keep a pull-up to `3V3` on `D1` if the PIR output is open drain and the
  internal pull-up is too weak for the wiring length.
- Use a small RC filter or logic buffer if the PIR output chatters.
- Make sure the wake-low pulse is held long enough to be sampled by the RTC
  wake logic.

## Relay Driver Circuit

Do not power a relay coil directly from `D10`. Use a relay module with a logic
input, or a transistor/MOSFET driver:

```text
XIAO D10  ---- relay module IN, or transistor/MOSFET gate/base driver
XIAO GND  ---- relay module GND / driver ground
Relay VCC ---- relay supply required by the relay module
```

The current firmware initializes the relay pin low before enabling output, then
drives `D10` high for relay ON and low for relay OFF. Use a relay module whose
input is active high, or change the firmware/config before using an active-low
module.

For a bare relay coil:

- Use a transistor or logic-level MOSFET.
- Add a flyback diode across the coil.
- Share ground between the XIAO and relay supply.
- Keep coil current off the XIAO `3V3` pin unless the full relay current budget
  is known to be safe.

## Pins To Avoid For This Circuit

Keep these pins out of the PIR and relay bootstrap circuit unless the board is
redesigned and the reset-time electrical behavior is verified:

| Pin | Reason |
| --- | --- |
| `D9` / `GPIO9` / `BOOT` | Used for bootloader entry; external pulls or drivers can break flashing or boot behavior. |
| `D8` / `GPIO8` | ESP32-C3 strap-sensitive pin; avoid fixed external levels during reset. |
| `D0` / `GPIO2` | ESP32-C3 strap-sensitive pin; avoid fixed external levels during reset. |
| `RST` | Not used by this firmware for wake. Keep available for manual reset and debugging. |

## Power And Upload

- USB-C is the simplest power source during bring-up.
- For battery deployment, use the XIAO battery pads or a regulated supply that
  matches the board requirements.
- Use a real data-capable USB-C cable for flashing and serial logs.
- If upload does not auto-enter the bootloader, start upload, hold `BOOT`, tap
  `RESET`, then release `BOOT` after the uploader connects.

## Bring-Up Checklist

1. Flash `seeed_xiao_esp32c3_sleep_test` first.
2. Confirm the board sleeps after boot.
3. Pull `D1` to `GND` briefly and confirm it wakes.
4. Flash `seeed_xiao_esp32c3_mqtt_smoke_test` and confirm Wi-Fi/MQTT works.
5. Flash `seeed_xiao_esp32c3` and confirm accepted wake events publish.
6. Confirm relay ON is `D10 = HIGH` and relay OFF is `D10 = LOW`.

If the board wakes repeatedly, inspect the `D1` idle voltage first. It should
be high while sleeping and only go low for a real trigger.
