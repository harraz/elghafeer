**Config Headers**

This directory contains project headers that are part of the firmware source.

Current layout:

- `config.h`
  Non-secret firmware settings such as GPIO assignments, MQTT host/port,
  wake timing, relay timing, and the trigger window / lockout policy.

What does not belong here:

- Wi-Fi credentials and passwords
  Those stay in `src/secrets.h`.

- Build-generated metadata
  Git branch / commit information is injected from `platformio.ini`
  `build_flags` so each compiled firmware can identify the source revision
  it came from.
