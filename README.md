**Overview**

This firmware runs a Seeed XIAO ESP32-C3 PIR-triggered relay node that sleeps
until a PIR event drives the configured wake GPIO active. After waking, the
node connects to Wi-Fi and MQTT, optionally evaluates a persisted trigger
limiter using NTP time, publishes a motion event for accepted wakes, turns the
relay on for a randomized duration, and then returns to deep sleep.

**Current Behavior**

- Wake source: `PIR -> wake GPIO` using ESP32-C3 deep-sleep GPIO wake
- Relay ON duration: randomized between `7000` and `10000` ms
- Awake window after an accepted trigger: `12000` ms
- Trigger window: `30000` ms
- Accepted triggers allowed in one window: `2`
- Lockout after the limit is exceeded: `300000` ms

The limiter state is stored in Preferences/NVS so it survives resets and power
loss. The firmware also keeps a suppressed-wake count and publishes that
summary on the next accepted wake as `Suppressed_wakes:N`.

**Configuration Layout**

- [include/config.h](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/include/config.h)
  Stable non-secret firmware settings.

- [device_config.example.json](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/device_config.example.json)
  Template for the local JSON file used to generate `include/settings.h` at build time.

- [src/secrets.h](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/src/secrets.h)
  Wi-Fi credentials and any other secrets that should not be committed broadly.

- [scripts/generate_settings_header.py](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/scripts/generate_settings_header.py)
  Build step that turns `device_config.json` into `include/settings.h`.

- [platformio.ini](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/platformio.ini)
  PlatformIO environment settings plus build-time Git metadata injection.

- [docs/firmware-flow.puml](/home/harraz/projects/home_projects/elghaffar-sensor-hub/elghafeer/docs/firmware-flow.puml)
  PlantUML sequence diagram for the current wake / throttle / relay flow.

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
2. Edit the device name, broker host, broker port, and debug default.
3. Set `relay_gpio_pin` and `wake_gpio_pin` in `device_config.json` for the XIAO ESP32-C3 wiring.
4. Run `platformio run -e seeed_xiao_esp32c3` or `platformio run -e seeed_xiao_esp32c3 -t upload`.
5. The build generates `include/settings.h` from `device_config.json` automatically.

The generated settings also accept optional `relay_gpio_pin` and
`wake_gpio_pin` values. Wire the PIR to the chosen ESP32-C3 wake pin and
update `device_config.json` to match.

**Operational MQTT Statuses**

Normal operation keeps the status topic focused on important events:

- `Relay ON (local motion trigger)`
- `Relay OFF (timer expired)`
- `Going to deep sleep...`
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
