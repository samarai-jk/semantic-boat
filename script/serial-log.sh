#!/usr/bin/env bash
# Shows the firmware log from the ST-Link virtual COM port and appends it to
# build/logs/<name>.log so other tools can read the same stream. Reconnects
# when the port disappears. Stop with Ctrl+C.
#
# Usage: script/serial-log.sh [--port /dev/ttyACM0] [--baud 115200] [--name serial]
set -euo pipefail

port=""
baud=115200
name=serial
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) port="$2"; shift 2 ;;
        --baud) baud="$2"; shift 2 ;;
        --name) name="$2"; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 1 ;;
    esac
done

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$repo_root/build/logs"
log_file="$repo_root/build/logs/$name.log"
printf '[serial-log] Writing to %s\n' "$log_file"

while true; do
    device="$port"
    if [[ -z "$device" ]]; then
        device="$(ls /dev/serial/by-id/*STLINK* 2>/dev/null | head -n 1 || true)"
    fi
    if [[ -z "$device" || ! -e "$device" ]]; then
        sleep 1
        continue
    fi
    if stty -F "$device" "$baud" raw -echo 2>/dev/null; then
        printf -- '--- serial-log: %s %s opened %s ---\n' "$device" "$baud" "$(date '+%F %T')" |
            tee -a "$log_file"
        cat "$device" | tee -a "$log_file" || true
    fi
    sleep 1
done
