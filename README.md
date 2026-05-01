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
- Accepted triggers allowed in one window: `3`
- Lockout after the limit is exceeded: `300000` ms

The limiter state is stored in Preferences/NVS so it survives resets and power
loss. The firmware also keeps a suppressed-wake count and publishes that
summary on the next accepted wake as `Suppressed_wakes:N`.

Status messages that are followed immediately by Wi-Fi disconnect or deep
sleep are briefly flushed through the MQTT client before shutdown. This makes
terminal events such as suppressed wakes and sleep entry more reliable on the
broker while still using QoS 0 publishes.

**Configuration Layout**

- [device_config.example.json](/home/harraz/projects/home_projects_new/elghafeer/device_config.example.json)
  Template for the local JSON file used to generate `include/settings.h` at build time.

- [device_config.json](/home/harraz/projects/home_projects_new/elghafeer/device_config.json)
  Local per-device deployment settings. This file is intentionally not tracked.

- [include/settings.h](/home/harraz/projects/home_projects_new/elghafeer/include/settings.h)
  Build-generated constants from `device_config.json`.

- [include/secrets.h](/home/harraz/projects/home_projects_new/elghafeer/include/secrets.h)
  Wi-Fi credentials and any other secrets that should not be committed broadly.

- [scripts/generate_settings_header.py](/home/harraz/projects/home_projects_new/elghafeer/scripts/generate_settings_header.py)
  Build step that turns `device_config.json` into `include/settings.h`. It can also be run directly with `.venv/bin/python scripts/generate_settings_header.py` to validate local config changes before a full PlatformIO build.

- [platformio.ini](/home/harraz/projects/home_projects_new/elghafeer/platformio.ini)
  PlatformIO environment settings plus build-time Git metadata injection.

- [docs/firmware-flow.puml](/home/harraz/projects/home_projects_new/elghafeer/docs/firmware-flow.puml)
  PlantUML sequence diagram for the current wake / throttle / relay flow.

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

1. Copy `device_config.example.json` to `device_config.json`.
2. Edit the device name, broker host, timing values, and relay behavior.
3. Set `relay_gpio_pin` and `wake_gpio_pin` in `device_config.json` for the XIAO ESP32-C3 wiring.
4. Build or flash the environment you want.
5. The build generates `include/settings.h` from `device_config.json` automatically.

The generated settings also accept optional `relay_gpio_pin` and
`wake_gpio_pin` values. Wire the PIR to the chosen ESP32-C3 wake pin and
update `device_config.json` to match.

**Active Config Keys**

- `default_skip_local_relay`
  When `true`, the node still publishes motion/status MQTT messages but does not drive the local relay.

- `relay_on_min_duration_ms` / `relay_on_max_duration_ms`
  Bounds for the randomized local relay ON duration on accepted wakes.

- `post_trigger_awake_window_ms`
  How long the node stays awake after an accepted trigger so the relay timer and MQTT loop can complete.

- `trigger_window_ms`, `max_accepted_in_window`, `lockout_ms`
  The persisted limiter window and lockout policy.

- `wifi_connect_timeout_ms`, `mqtt_connect_timeout_ms`, `time_sync_timeout_ms`
  Upper bounds for Wi-Fi, MQTT, and NTP setup work on each wake.

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
