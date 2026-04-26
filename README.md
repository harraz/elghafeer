**Overview**

This firmware runs an ESP8266 PIR-triggered relay node that sleeps until a PIR
event pulls `RST` low. After waking, the node connects to Wi-Fi and MQTT,
optionally evaluates a persisted trigger limiter using NTP time, publishes a
motion event for accepted wakes, turns the relay on for a randomized duration,
or skips the local relay when configured to publish-only, and then returns to
deep sleep.

**Current Behavior**

- Wake source: `PIR -> transistor -> RST`
- Relay ON duration: randomized between `7000` and `10000` ms
- Post-trigger awake window after an accepted trigger: `12000` ms
- Trigger window: `60000` ms
- Accepted triggers allowed in one window: `3`
- Lockout after the limit is exceeded: `300000` ms

The limiter state is stored in EEPROM so it survives resets and power loss.
The firmware also keeps a suppressed-wake count and publishes that summary on
the next accepted wake as `Suppressed_wakes:N`.

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
  Generated header consumed by the firmware at compile time.

- [include/secrets.h](/home/harraz/projects/home_projects_new/elghafeer/include/secrets.h)
  Wi-Fi credentials and other local secrets that should not be committed broadly.

- [scripts/generate_settings_header.py](/home/harraz/projects/home_projects_new/elghafeer/scripts/generate_settings_header.py)
  Build step that turns `device_config.json` into `include/settings.h`.

- [platformio.ini](/home/harraz/projects/home_projects_new/elghafeer/platformio.ini)
  PlatformIO environment settings plus build-time Git metadata injection.

**Build Metadata**

The build injects Git metadata through `build_flags` in `platformio.ini`.
That makes the current branch and short commit SHA available inside the
firmware as:

- `FW_GIT_BRANCH`
- `FW_GIT_SHA`

Those version details are added to accepted motion payloads so the running
firmware can be identified later without rebuilding it.

**Build & Flash**

1. Copy `device_config.example.json` to `device_config.json`.
2. Edit the device name, broker host, timing values, and relay behavior.
3. Run `platformio run` or `platformio run -t upload`.
4. The build generates `include/settings.h` from `device_config.json` automatically.

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

**Operational MQTT Statuses**

Normal operation keeps the status topic focused on important events:

- `Relay ON (local motion trigger)`
- `Relay OFF (timer expired)`
- `Going to deep sleep...`
- `Local relay skipped by config`
- `Time sync failed; skipping throttle`
- `Wake suppressed: rate limit exceeded, count:N`
- `Wake suppressed: lockout active, count:N`
- `Suppressed_wakes:N`

More detailed breadcrumbs are emitted only when `DEBUG` is enabled.

**Limits Of The Current Design**

Because the PIR wakes the node by driving `RST`, the ESP8266 cannot ignore a
hardware reset while it is already awake. Firmware can suppress behavior after
the reboot, but it cannot prevent the reset itself without hardware changes.

That means the limiter works across wake cycles, but it cannot fully stop a
new reset from interrupting a currently running awake cycle.
