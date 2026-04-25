import json
from pathlib import Path

Import("env")


PROJECT_DIR = Path(env["PROJECT_DIR"])
CONFIG_PATH = PROJECT_DIR / "device_config.json"
HEADER_PATH = PROJECT_DIR / "include" / "settings.h"

REQUIRED_KEYS = {
    "device_ghafeer_name": str,
    "mqtt_broker_host": str,
    "mqtt_broker_port": int,
    "default_debug": bool,
    "default_skip_local_relay": bool,
    "relay_on_min_duration_ms": int,
    "relay_on_max_duration_ms": int,
    "post_trigger_awake_window_ms": int,
    "wifi_connect_timeout_ms": int,
    "mqtt_connect_timeout_ms": int,
    "time_sync_timeout_ms": int,
    "trigger_window_ms": int,
    "max_accepted_in_window": int,
    "lockout_ms": int,
}


def load_config():
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
    return "true" if value else "false"


def escape_cpp_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


def write_header(config):
    header_contents = f"""#pragma once

// This file is generated during the PlatformIO build from device_config.json.
// Edit device_config.json when flashing a different board or deployment.

constexpr const char* DEVICE_GHAFEER_NAME = "{escape_cpp_string(config["device_ghafeer_name"])}";
constexpr const char* MQTT_BROKER_HOST = "{escape_cpp_string(config["mqtt_broker_host"])}";
constexpr int MQTT_BROKER_PORT = {config["mqtt_broker_port"]};
constexpr bool DEFAULT_DEBUG = {cpp_bool(config["default_debug"])};
constexpr bool DEFAULT_SKIP_LOCAL_RELAY = {cpp_bool(config["default_skip_local_relay"])};
constexpr unsigned int DEFAULT_RELAY_ON_MIN_DURATION_MS = {config["relay_on_min_duration_ms"]};
constexpr unsigned int DEFAULT_RELAY_ON_MAX_DURATION_MS = {config["relay_on_max_duration_ms"]};
constexpr unsigned long DEFAULT_POST_TRIGGER_AWAKE_WINDOW_MS = {config["post_trigger_awake_window_ms"]}UL;
constexpr unsigned long DEFAULT_WIFI_CONNECT_TIMEOUT_MS = {config["wifi_connect_timeout_ms"]}UL;
constexpr unsigned long DEFAULT_MQTT_CONNECT_TIMEOUT_MS = {config["mqtt_connect_timeout_ms"]}UL;
constexpr unsigned long DEFAULT_TIME_SYNC_TIMEOUT_MS = {config["time_sync_timeout_ms"]}UL;
constexpr unsigned long DEFAULT_TRIGGER_WINDOW_MS = {config["trigger_window_ms"]}UL;
constexpr uint32_t DEFAULT_MAX_ACCEPTED_IN_WINDOW = {config["max_accepted_in_window"]};
constexpr unsigned long DEFAULT_LOCKOUT_MS = {config["lockout_ms"]}UL;
"""

    HEADER_PATH.write_text(header_contents, encoding="utf-8")


write_header(load_config())
