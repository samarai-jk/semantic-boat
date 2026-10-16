#!/usr/bin/env bash
# Shows the firmware log from the ST-Link virtual COM port, appends it to
# build/logs/<name>.log, and sends console commands to the firmware: lines
# typed into this terminal, or lines written to build/logs/<name>-command.txt
# by another tool (sent, then the file is deleted). Reconnects when the port
# disappears. Stop with Ctrl+C.
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
command_file="$repo_root/build/logs/$name-command.txt"
printf '[serial-log] Writing to %s\n' "$log_file"
printf '[serial-log] Commands: type here (Enter sends) or write lines to %s\n' "$command_file"

send_command() {
    local device="$1" command="$2"
    command="${command%$'\r'}"
    [[ -z "${command// }" ]] && return 0
    printf '%s\r\n' "$command" > "$device"
    printf -- '--- serial-log: sent %s ---\n' "'$command'" >> "$log_file"
}

reader_pid=""
trap '[[ -n "$reader_pid" ]] && kill "$reader_pid" 2>/dev/null; exit 0' INT TERM

while true; do
    device="$port"
    if [[ -z "$device" ]]; then
        device="$(ls /dev/serial/by-id/*STLINK* 2>/dev/null | head -n 1 || true)"
    fi
    if [[ -z "$device" || ! -e "$device" ]] || ! stty -F "$device" "$baud" raw -echo 2>/dev/null; then
        sleep 1
        continue
    fi
    printf -- '--- serial-log: %s %s opened %s ---\n' "$device" "$baud" "$(date '+%F %T')" |
        tee -a "$log_file"
    (cat "$device" | tee -a "$log_file") &
    reader_pid=$!
    while kill -0 "$reader_pid" 2>/dev/null; do
        if [[ -f "$command_file" ]]; then
            while IFS= read -r line || [[ -n "$line" ]]; do
                send_command "$device" "$line"
            done < "$command_file"
            rm -f "$command_file"
        fi
        if [[ -t 0 ]] && IFS= read -r -t 0.2 line; then
            send_command "$device" "$line"
        else
            sleep 0.2
        fi
    done
    reader_pid=""
    sleep 1
done
