import json
from pathlib import Path

# When PlatformIO runs this file as a pre-build hook it injects `Import` and
# `env` from SCons. When the script is run directly for testing, those symbols
# do not exist, so fall back to the repository root derived from this file path.
try:
    Import("env")
    PROJECT_DIR = Path(env["PROJECT_DIR"])
except NameError:
    PROJECT_DIR = Path(__file__).resolve().parents[1]

CONFIG_PATH = PROJECT_DIR / "device_config.json"
HEADER_PATH = PROJECT_DIR / "include" / "settings.h"

# Keep the schema in one place so bad or stale deployment configs fail early
# with a clear error instead of generating a broken settings header.
REQUIRED_KEYS = {
    "device_ghafeer_name": str,
    "mqtt_broker_host": str,
    "mqtt_broker_port": int,
    "default_debug": bool,
    "default_pir_interval_ms": int,
    "default_relay_max_on_duration_ms": int,
    "default_max_pir_interval_ms": int,
    "default_skip_local_relay": bool,
}


def load_config():
    """Load and validate the per-device JSON config used for header generation."""
    if not CONFIG_PATH.exists():
        raise RuntimeError(
            "Missing device_config.json. Copy device_config.example.json to "
            "device_config.json and set the values for the board you are flashing."
        )

    with CONFIG_PATH.open("r", encoding="utf-8") as config_file:
        config = json.load(config_file)

    for key, expected_type in REQUIRED_KEYS.items():
        if key not in config:
            raise RuntimeError(f"device_config.json is missing required key: {key}")
        if not isinstance(config[key], expected_type):
            raise RuntimeError(
                f"device_config.json key {key} must be a {expected_type.__name__}"
            )

    return config


def cpp_bool(value):
    """Convert a Python bool to a lowercase C++ boolean literal."""
    return "true" if value else "false"


def escape_cpp_string(value):
    """Escape a Python string so it can be embedded safely in a C++ string literal."""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_header(config):
    """Render the validated JSON config into the generated settings header."""
    header_contents = f"""#pragma once

// This file is generated during the PlatformIO build from device_config.json.
// Edit device_config.json when flashing a different board or deployment.

constexpr const char* DEVICE_GHAFEER_NAME = "{escape_cpp_string(config["device_ghafeer_name"])}";
constexpr const char* MQTT_BROKER_HOST = "{escape_cpp_string(config["mqtt_broker_host"])}";
constexpr int MQTT_BROKER_PORT = {config["mqtt_broker_port"]};
constexpr bool DEFAULT_DEBUG = {cpp_bool(config["default_debug"])};
constexpr unsigned int DEFAULT_PIR_INTERVAL_MS = {config["default_pir_interval_ms"]};
constexpr unsigned int DEFAULT_RELAY_MAX_ON_DURATION_MS = {config["default_relay_max_on_duration_ms"]};
constexpr unsigned int DEFAULT_MAX_PIR_INTERVAL_MS = {config["default_max_pir_interval_ms"]};
constexpr bool DEFAULT_SKIP_LOCAL_RELAY = {cpp_bool(config["default_skip_local_relay"])};
"""

    HEADER_PATH.write_text(header_contents, encoding="utf-8")


def main():
    """Entry point used by PlatformIO's pre-build hook and direct test runs."""
    write_header(load_config())


main()
