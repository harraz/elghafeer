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

CONFIG_DIR = PROJECT_DIR / "config"
BOARD_ID_PATH = CONFIG_DIR / "board_id.txt"
EXAMPLE_CONFIG_PATH = CONFIG_DIR / "device_config.example.json"
HEADER_PATH = PROJECT_DIR / "include" / "settings.h"

# Keep the schema in one place so bad or stale deployment configs fail early
# with a clear error instead of generating a broken settings header.
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

OPTIONAL_KEYS = {
    "relay_gpio_pin": int,
    "wake_gpio_pin": int,
}

REMOVED_KEYS = {
    "default_pir_interval_ms",
    "default_relay_max_on_duration_ms",
    "default_max_pir_interval_ms",
    "awake_window_ms",
}


def load_board_id():
    """Read the branch-specific board id used to select the local config."""
    if not BOARD_ID_PATH.exists():
        raise RuntimeError("Missing config/board_id.txt for this firmware branch.")

    board_id = BOARD_ID_PATH.read_text(encoding="utf-8").strip()
    if not board_id or "/" in board_id or "\\" in board_id:
        raise RuntimeError("config/board_id.txt must contain a single board id.")

    return board_id


def local_config_path(board_id):
    """Return the ignored local config path for this board branch."""
    return CONFIG_DIR / "local" / f"{board_id}.json"


def load_config():
    """Load and validate the per-device JSON config used for header generation."""
    board_id = load_board_id()
    config_path = local_config_path(board_id)

    if not config_path.exists():
        raise RuntimeError(
            f"Missing {config_path.relative_to(PROJECT_DIR)}. Copy "
            f"{EXAMPLE_CONFIG_PATH.relative_to(PROJECT_DIR)} to "
            f"{config_path.relative_to(PROJECT_DIR)} and set the values for "
            "the board you are flashing."
        )

    with config_path.open("r", encoding="utf-8") as config_file:
        config = json.load(config_file)

    for key, expected_type in REQUIRED_KEYS.items():
        if key not in config:
            stale_keys = sorted(REMOVED_KEYS.intersection(config))
            stale_note = ""
            if stale_keys:
                stale_note = (
                    " Found removed key(s): "
                    + ", ".join(stale_keys)
                    + f". Refresh {config_path.relative_to(PROJECT_DIR)} from "
                    + f"{EXAMPLE_CONFIG_PATH.relative_to(PROJECT_DIR)}."
                )
            raise RuntimeError(
                f"{config_path.relative_to(PROJECT_DIR)} is missing required "
                f"key: {key}.{stale_note}"
            )
        if not isinstance(config[key], expected_type):
            raise RuntimeError(
                f"{config_path.relative_to(PROJECT_DIR)} key {key} must be a "
                f"{expected_type.__name__}"
            )

    for key, expected_type in OPTIONAL_KEYS.items():
        if key in config and not isinstance(config[key], expected_type):
            raise RuntimeError(
                f"{config_path.relative_to(PROJECT_DIR)} key {key} must be a "
                f"{expected_type.__name__}"
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

// This file is generated during the PlatformIO build from config/local/<board_id>.json.
// Edit that local config when flashing a different board or deployment.

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
constexpr int DEFAULT_RELAY_GPIO_PIN = {config.get("relay_gpio_pin", 12)};
constexpr int DEFAULT_WAKE_GPIO_PIN = {config.get("wake_gpio_pin", 2)};
"""

    HEADER_PATH.write_text(header_contents, encoding="utf-8")


def main():
    """Entry point used by PlatformIO's pre-build hook and direct test runs."""
    write_header(load_config())


main()
