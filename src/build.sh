#!/usr/bin/env bash
# Build, flash and monitor the ESP32 firmware from the command line
# (no Arduino IDE needed).
#
#   src/build.sh [command] [options]
#
# Commands:
#   build      compile the firmware (default)
#   upload     compile and flash it to the board
#   monitor    open the serial monitor (57600 baud)
#   flash      upload, then monitor
#   setup      install the ESP32 core / platform only
#   clean      delete build output
#
# Options:
#   -p, --port PORT       serial port, e.g. /dev/ttyUSB0 or COM3 (auto if omitted)
#   -b, --backend NAME    arduino-cli or pio (default: arduino-cli if installed,
#                         otherwise pio)
#   --fqbn FQBN           arduino-cli board (default esp32:esp32:esp32,
#                         i.e. "ESP32 Dev Module")
#   -e, --env ENV         PlatformIO environment from platformio.ini
#                         (default esp32dev)
#   -h, --help            show this help
#
# Environment variables RFB_PORT, RFB_BACKEND, RFB_FQBN and RFB_ENV set the
# same defaults.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
sketch="$here/blinds"
build_dir="$here/build"

cmd="build"
port="${RFB_PORT:-}"
backend="${RFB_BACKEND:-}"
fqbn="${RFB_FQBN:-esp32:esp32:esp32}"
env="${RFB_ENV:-esp32dev}"
baud=57600
esp32_index="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; }
die() { echo "error: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    build|upload|monitor|flash|setup|clean) cmd="$1" ;;
    -p|--port)    port="${2:?missing port}"; shift ;;
    -b|--backend) backend="${2:?missing backend}"; shift ;;
    --fqbn)       fqbn="${2:?missing FQBN}"; shift ;;
    -e|--env)     env="${2:?missing environment}"; shift ;;
    -h|--help)    usage; exit 0 ;;
    *) usage >&2; die "unknown argument: $1" ;;
  esac
  shift
done

if [ -z "$backend" ]; then
  if command -v arduino-cli >/dev/null 2>&1; then backend=arduino-cli
  elif command -v pio >/dev/null 2>&1; then backend=pio
  elif command -v platformio >/dev/null 2>&1; then backend=pio
  else
    die "neither arduino-cli nor PlatformIO found.
  Install one of them:
    arduino-cli: https://arduino.github.io/arduino-cli/latest/installation/
    PlatformIO:  pip install platformio"
  fi
fi

if [ ! -f "$sketch/secrets.h" ] && [ "$cmd" != clean ] && [ "$cmd" != monitor ]; then
  echo "note: $sketch/secrets.h not found - building without Wi-Fi (USB serial only)." >&2
fi

# ---------------------------------------------------------------- arduino-cli
acli() { arduino-cli --additional-urls "$esp32_index" "$@"; }

acli_setup() {
  local core="${fqbn%:*}"   # esp32:esp32:esp32 -> esp32:esp32
  if acli core list | awk 'NR>1 {print $1}' | grep -qx "$core"; then
    return
  fi
  echo "Installing Arduino core $core (one-time download)..."
  acli core update-index
  acli core install "$core"
}

acli_build() {
  acli_setup
  acli compile --fqbn "$fqbn" --warnings default \
    --build-path "$build_dir/arduino-cli" --output-dir "$build_dir" "$sketch"
  echo "Firmware: $build_dir/blinds.ino.bin"
}

acli_upload() {
  acli_build
  [ -n "$port" ] || port="$(arduino-cli board list | awk '/\/dev\/|COM[0-9]/ {print $1; exit}')"
  [ -n "$port" ] || die "no board found - pass --port"
  acli upload --fqbn "$fqbn" -p "$port" --input-dir "$build_dir" "$sketch"
}

acli_monitor() {
  [ -n "$port" ] || port="$(arduino-cli board list | awk '/\/dev\/|COM[0-9]/ {print $1; exit}')"
  [ -n "$port" ] || die "no board found - pass --port"
  arduino-cli monitor -p "$port" -c "baudrate=$baud"
}

# ---------------------------------------------------------------- PlatformIO
pio_bin() { if command -v pio >/dev/null 2>&1; then echo pio; else echo platformio; fi; }

pio_run() { (cd "$here" && "$(pio_bin)" "$@"); }

pio_port_args=()
[ -z "$port" ] || pio_port_args=(--upload-port "$port")

# ---------------------------------------------------------------- dispatch
case "$backend:$cmd" in
  arduino-cli:setup)   acli_setup ;;
  arduino-cli:build)   acli_build ;;
  arduino-cli:upload)  acli_upload ;;
  arduino-cli:monitor) acli_monitor ;;
  arduino-cli:flash)   acli_upload; acli_monitor ;;
  arduino-cli:clean)   rm -rf "$build_dir" ;;

  pio:setup)   pio_run pkg install -e "$env" ;;
  pio:build)   pio_run run -e "$env"
               echo "Firmware: $here/.pio/build/$env/firmware.bin" ;;
  pio:upload)  pio_run run -e "$env" -t upload "${pio_port_args[@]}" ;;
  pio:monitor) pio_run device monitor -e "$env" ${port:+-p "$port"} ;;
  pio:flash)   pio_run run -e "$env" -t upload "${pio_port_args[@]}"
               pio_run device monitor -e "$env" ${port:+-p "$port"} ;;
  pio:clean)   pio_run run -e "$env" -t clean; rm -rf "$build_dir" ;;

  *) die "unknown backend '$backend' (use arduino-cli or pio)" ;;
esac
