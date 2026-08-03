**Overview**

This firmware runs a Seeed XIAO ESP32-C3 PIR-triggered relay node that sleeps
until a PIR event drives the configured wake GPIO active. After waking, the
node connects to Wi-Fi and MQTT, optionally evaluates a persisted trigger
limiter using NTP time, publishes a motion event for accepted wakes, turns the
relay on for a randomized duration or skips local actuation when configured to
publish-only, and then returns to deep sleep.

**Current Behavior**

- Wake source: `PIR -> wake GPIO` using ESP32-C3 deep-sleep GPIO wake
- Relay ON duration: randomized between `7000` and `10000` ms
- Post-trigger awake window after an accepted trigger: `12000` ms
- Trigger window: `60000` ms
- Accepted triggers allowed in one window: `2`
- Lockout after the limit is exceeded: `300000` ms

The limiter state is stored in Preferences/NVS so it survives resets and power
loss. The firmware also keeps a suppressed-wake count and publishes that
summary on the next accepted wake as `Suppressed_wakes:N`.

Status messages that are followed immediately by Wi-Fi disconnect or deep
sleep are briefly flushed through the MQTT client before shutdown. This makes
terminal events such as suppressed wakes and sleep entry more reliable on the
broker while still using QoS 0 publishes.

**Configuration Layout**

- [config/device_config.example.json](/home/harraz/projects/home_projects_new/elghafeer/config/device_config.example.json)
  Template for the local JSON file used to generate `include/settings.h` at build time.

- [config/board_id.txt](/home/harraz/projects/home_projects_new/elghafeer/config/board_id.txt)
  Branch-specific board id used to select the ignored local config file.

- `config/local/esp32c3_deepsleep.json`
  Local per-device deployment settings. This file is intentionally not tracked.

- [include/settings.h](/home/harraz/projects/home_projects_new/elghafeer/include/settings.h)
  Build-generated constants from `device_config.json`.

- [include/secrets.h](/home/harraz/projects/home_projects_new/elghafeer/include/secrets.h)
  Wi-Fi credentials and any other secrets that should not be committed broadly.

- [scripts/generate_settings_header.py](/home/harraz/projects/home_projects_new/elghafeer/scripts/generate_settings_header.py)
  Build step that turns `config/local/esp32c3_deepsleep.json` into `include/settings.h`. It can also be run directly with `.venv/bin/python scripts/generate_settings_header.py` to validate local config changes before a full PlatformIO build.

- [platformio.ini](/home/harraz/projects/home_projects_new/elghafeer/platformio.ini)
  PlatformIO environment settings plus build-time Git metadata injection.

- [docs/firmware-flow.puml](/home/harraz/projects/home_projects_new/elghafeer/docs/firmware-flow.puml)
  PlantUML sequence diagram for the current wake / throttle / relay flow.

- [docs/firmware-flow.md](/home/harraz/projects/home_projects_new/elghafeer/docs/firmware-flow.md)
  Mermaid sequence diagram for Markdown previews of the same firmware flow.

- [docs/xiao-esp32c3-hardware-bootstrap.md](/home/harraz/projects/home_projects_new/elghafeer/docs/xiao-esp32c3-hardware-bootstrap.md)
  Wiring and pin-state checklist for the XIAO ESP32-C3 deep-sleep relay node.

**Build Metadata**

The build injects Git metadata through `build_flags` in `platformio.ini`.
That makes the current branch and short commit SHA available inside the
firmware as:

- `FW_GIT_BRANCH`
- `FW_GIT_SHA`

Those version details are added to accepted motion payloads so the running
firmware can be identified later without rebuilding it.

**PlatformIO Envs**

- `seeed_xiao_esp32c3`
  Full production firmware with Wi-Fi, MQTT, relay control, throttling, and deep sleep.

- `seeed_xiao_esp32c3_sleep_test`
  Minimal deep-sleep validation firmware. It only configures `D1 / GPIO3` as a wake-low source and immediately enters deep sleep.

- `seeed_xiao_esp32c3_mqtt_smoke_test`
  Minimal network baseline firmware. It connects to Wi-Fi, publishes one MQTT status message, and returns to deep sleep.

**Build & Flash**

1. Copy `config/device_config.example.json` to `config/local/esp32c3_deepsleep.json`.
2. Edit the device name, broker host, timing values, and relay behavior.
3. Set `relay_gpio_pin` and `wake_gpio_pin` in `config/local/esp32c3_deepsleep.json` for the XIAO ESP32-C3 wiring.
4. Build or flash the environment you want.
5. The build generates `include/settings.h` from `config/local/esp32c3_deepsleep.json` automatically.

The generated settings also accept optional `relay_gpio_pin` and
`wake_gpio_pin` values. Wire the PIR to the chosen ESP32-C3 wake pin and
update `config/local/esp32c3_deepsleep.json` to match.

**Runtime Configuration**

Runtime values are generated at build time. Do not edit `include/settings.h`
directly; it is regenerated from the selected local JSON file.

```text
config/board_id.txt
  -> config/local/<board_id>.json
  -> scripts/generate_settings_header.py
  -> include/settings.h
  -> src/main.cpp
```

For this branch, `config/board_id.txt` selects
`config/local/esp32c3_deepsleep.json`.

| Local JSON key | Generated constant | Description | Use cases |
| --- | --- | --- | --- |
| `device_ghafeer_name` | `DEVICE_GHAFEER_NAME` | Human-readable device or location name used in MQTT topics and motion payloads. | Give each installed node a clear identity such as a room, door, or owner name; separate MQTT topic paths for multiple deployed nodes. |
| `mqtt_broker_host` | `MQTT_BROKER_HOST` | MQTT broker hostname or IP address. | Point the device at a local broker, lab broker, or production broker without changing firmware code. |
| `mqtt_broker_port` | `MQTT_BROKER_PORT` | MQTT broker TCP port. | Keep the default `1883` for plain MQTT, or change it if the broker listens on a different local port. |
| `default_debug` | `DEFAULT_DEBUG` | Enables extra serial logs and debug-only MQTT breadcrumbs. | Turn on during bring-up, Wi-Fi/MQTT troubleshooting, wake-loop debugging, and throttle-state inspection; keep off for normal quiet operation. |
| `default_skip_local_relay` | `DEFAULT_SKIP_LOCAL_RELAY` | Disables physical relay actuation while keeping wake, Wi-Fi, MQTT, payload, and throttle behavior active. | Test PIR wiring and MQTT delivery without switching the connected load; run a publish-only sensor node; isolate relay hardware issues from firmware/network behavior. |
| `relay_on_min_duration_ms` | `DEFAULT_RELAY_ON_MIN_DURATION_MS` | Lower bound for the randomized relay ON duration after an accepted wake. | Set the shortest acceptable load activation time; narrow the relay timing range for predictable tests. |
| `relay_on_max_duration_ms` | `DEFAULT_RELAY_ON_MAX_DURATION_MS` | Upper bound for the randomized relay ON duration after an accepted wake. | Cap how long the relay can stay on; tune power use, load runtime, and audible relay activity. |
| `post_trigger_awake_window_ms` | `DEFAULT_POST_TRIGGER_AWAKE_WINDOW_MS` | Total awake service window after an accepted trigger. During this window MQTT is serviced and the relay timer can expire before deep sleep. | Make this longer than the maximum relay ON duration so the relay can turn off before sleep; increase during debugging if MQTT messages are being missed. |
| `wifi_connect_timeout_ms` | `DEFAULT_WIFI_CONNECT_TIMEOUT_MS` | Maximum total time spent trying configured Wi-Fi networks on each wake. | Shorten to save battery when Wi-Fi may be unavailable; lengthen when signal is weak or association is slow. |
| `mqtt_connect_timeout_ms` | `DEFAULT_MQTT_CONNECT_TIMEOUT_MS` | Maximum time spent connecting to MQTT after Wi-Fi is available. | Shorten for battery-sensitive nodes; lengthen for slow brokers or networks where the first TCP connection often takes longer. |
| `time_sync_timeout_ms` | `DEFAULT_TIME_SYNC_TIMEOUT_MS` | Maximum time spent waiting for NTP time before continuing without throttle evaluation. | Increase if NTP is slow and rate limiting must be strict; shorten if relay response matters more than persisted throttle accuracy. |
| `trigger_window_ms` | `DEFAULT_TRIGGER_WINDOW_MS` | Duration of the persisted rate-limit counting window. | Define the time span in which repeated motion wakes are counted together. |
| `max_accepted_in_window` | `DEFAULT_MAX_ACCEPTED_IN_WINDOW` | Number of accepted wakes allowed inside one trigger window before lockout starts. | Allow a small number of legitimate repeated triggers while suppressing chatter, stuck PIR outputs, or rapid retriggers. |
| `lockout_ms` | `DEFAULT_LOCKOUT_MS` | Suppression period started after the accepted-trigger limit is exceeded. | Prevent a noisy PIR or held-low wake line from repeatedly actuating the relay and publishing motion events. |
| `relay_gpio_pin` | `DEFAULT_RELAY_GPIO_PIN` | ESP32-C3 GPIO used to drive the local relay. Current XIAO wiring uses `GPIO10` / `D10`. | Move relay control to a different safe GPIO for a revised board layout; match the firmware to the actual relay driver input. |
| `wake_gpio_pin` | `DEFAULT_WAKE_GPIO_PIN` | ESP32-C3 GPIO used as the deep-sleep wake input. Current XIAO wiring uses `GPIO3` / `D1` and wakes when pulled low. | Move the PIR/wake input to a different deep-sleep-capable GPIO; match the firmware to a board-specific wake circuit. |

**Throttle Tuning**

The false-trigger limiter depends on Wi-Fi plus NTP wall-clock time. When
Wi-Fi or NTP is unavailable, the firmware skips throttle evaluation, treats the
wake as accepted, runs the local relay path, and sleeps. If false-trigger
suppression must work even when Wi-Fi is down, use a different limiter design
based on ESP-retained time/state instead of NTP wall-clock time.

To allow more motion events before lockout, increase
`max_accepted_in_window`. To make those events actually fit, make sure
`trigger_window_ms` is long enough for the full wake cycle count you want. Each
accepted wake spends time on Wi-Fi connect, MQTT connect, NTP sync, relay
duration, `post_trigger_awake_window_ms`, and the final sleep-settle delay.

For example, if the practical wake cycle is about 15-20 seconds, then a
`60000` ms trigger window can fit only a small number of accepted wakes before
the window expires and resets. If you raise `max_accepted_in_window`, consider
raising `trigger_window_ms` as well.

Common commands:

```bash
/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3
/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3 -t upload --upload-port /dev/ttyACM0

/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3_sleep_test
/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3_sleep_test -t upload --upload-port /dev/ttyACM0

/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3_mqtt_smoke_test
/home/harraz/.platformio/penv/bin/pio run -e seeed_xiao_esp32c3_mqtt_smoke_test -t upload --upload-port /dev/ttyACM0
```

If the board does not auto-enter bootloader mode on your Linux USB setup,
start the upload first and then manually hold `BOOT`, tap `RESET`, and release
`BOOT` after the uploader connects.

**Operational MQTT Statuses**

Normal operation keeps the status topic focused on important events:

- `Wake: firmware=<branch>@<sha> wifi_ssid=<ssid> ip=<ip>`
- `Wake accepted: count:N/M`
- `Relay ON (local motion trigger)`
- `Relay OFF (timer expired)`
- `Local relay skipped by config`
- `Going to deep sleep...`
- `Time sync failed; skipping throttle`
- `Wake suppressed: rate limit exceeded, count:N`
- `Wake suppressed: lockout active, count:N`
- `Suppressed_wakes:N`

More detailed breadcrumbs are emitted only when `DEBUG` is enabled.

**Limits Of The Current Design**

This branch assumes the PIR can be conditioned into a clean ESP32-C3 wake
signal. If the PIR output chatters or remains asserted, the board can wake
again immediately after re-entering deep sleep. The limiter still handles
suppression in firmware, but stable wake wiring matters for predictable sleep
cycles.
