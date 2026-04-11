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
"""

    HEADER_PATH.write_text(header_contents, encoding="utf-8")


write_header(load_config())
