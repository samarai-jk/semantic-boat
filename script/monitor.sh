#!/usr/bin/env bash
# Live event monitor for can-mcu firmware: follows the serial and SWD logs,
# shows events in colour, and sends typed commands to the firmware console.
# Linux counterpart of script/monitor.ps1.
#
# Reads build/logs/serial.log (written by script/serial-log.sh, which must be
# running) and build/logs/swd.log (written by the SWD probe). Periodic lines
# such as the per-second module values are hidden unless --all is given;
# events are highlighted:
#
#   RESTART      reset cause after a restart (red)
#   FATAL/POWER  fatal errors, supply drops below 2.9 V, crash or hang of the previous run (red)
#   health       restart statistics and the previous run (white)
#   ALARM        measurement alarms becoming active (red: alarm/emergency, yellow: warning)
#   alarm/sensor alarms clearing or acknowledged, sensor console replies (white)
#   values       measurement values every 5 s, sensors found or lost (green)
#   button       button presses (cyan)
#   supply       supply dips below the firmware threshold (yellow)
#   can          CAN controller state changes (magenta)
#   module/i2c   input module and I2C devices appearing, digital input changes (green)
#   console      console commands and replies (white)
#   swd          SWD probe results (dark cyan)
#   warnings and errors not covered above (yellow / red)
#   heartbeat    every 10 s, as a sign of life (dark gray)
#
# If the serial log stays silent for --silence seconds, the monitor reports it
# once (board unpowered, hung, or the serial logger not running).
#
# Type a command and press Enter to send it to the firmware console (for
# example "help", "ain", "supply 3200"). Commands are passed to the serial
# logger through build/logs/serial-command.txt. Any number of monitors may
# run next to the one serial logger. Stop with Ctrl+C.
#
# Usage: script/monitor.sh [--all] [--silence <s>] [--history <lines>]
set -euo pipefail

all=0
silence_seconds=15
history=300
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all|-a) all=1; shift ;;
        --silence) silence_seconds="$2"; shift 2 ;;
        --history) history="$2"; shift 2 ;;
        -h|--help) sed -n '2,33p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 1 ;;
    esac
done

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
log_dir="$repo_root/build/logs"
mkdir -p "$log_dir"
serial_log="$log_dir/serial.log"
swd_log="$log_dir/swd.log"
command_file="$log_dir/serial-command.txt"

red=$'\e[1;31m' dark_red=$'\e[31m' yellow=$'\e[1;33m' green=$'\e[1;32m'
dark_green=$'\e[32m' cyan=$'\e[1;36m' dark_cyan=$'\e[36m' magenta=$'\e[1;35m'
white=$'\e[1;37m' gray=$'\e[37m' dark_gray=$'\e[90m' reset=$'\e[0m'

# Each rule: pattern, colour, label. The first matching rule wins; lines
# without a matching rule are shown only with --all.
rules=(
    'reset cause: (.+)$'                                     "$red"        RESTART
    '\*\*\* FATAL'                                           "$red"        FATAL
    'power: VDD fell'                                        "$red"        POWER
    'health: (previous run (ended|hung)|.*hung)'             "$red"        health
    'health:|supervision:'                                   "$white"      health
    'alarm: .*(ALARM|EMERGENCY) ACTIVE'                      "$red"        ALARM
    'alarm: .*WARNING ACTIVE'                                "$yellow"     ALARM
    '\] [IWE] alarm: '                                       "$white"      alarm
    '\] [IWE] (sensors?:|ack:|[a-z][a-z0-9_-]*: ((temperature|humidity|voltage) instance|offset|stored|applied|defaults|statistics|no valid|cal2|gain))' "$white" sensor
    'as3935: lightning'                                      "$magenta"    LIGHTNING
    '\] [IWE] values: '                                      "$green"      values
    '\] [IWE] (dht20|sht31|ds18b20|ct1780|veml7700|as3935|lightning|uv-index|bmp585|bno055|bmp280|attitude|scd4x)[ :]' "$green" sensor
    'lab-test-01 \(.*built'                                  "$dark_red"   boot
    'BF[0-9] pressed'                                        "$cyan"       button
    'supply dip'                                             "$yellow"     supply
    'can state:'                                             "$magenta"    can
    'input module: (DI[0-9]|ADC)'                            "$green"      module
    'i2c 0x[0-9A-F]{2} (appeared|disappeared)|i2c: [0-9]+ device' "$green" i2c
    'n2k: (claimed address [0-9]+$|address|.*lower NAME|.*NAME wins|no free address)' "$dark_green" n2k
    '\] I > '                                                "$white"      console
    '\] [IWE] (cal |ain |supply:|commands:|  |console:|usage:)' "$white"   console
    'heartbeat:'                                             "$dark_gray"  heartbeat
    '--- serial-log: sent'                                   "$dark_gray"  sent
    '\] E '                                                  "$red"        error
    '\] W '                                                  "$yellow"     warning
    '--- serial-log: .* opened'                              "$dark_gray"  logger
)

say() { printf '%s%-9s %s%s\n' "$2" "$1" "$3" "$reset"; }

show_line() {
    local line="${1%$'\r'}" source="$2" i
    [[ -z "$line" ]] && return 0
    if [[ "$source" == swd ]]; then
        if [[ "$line" == "--- swd-probe"* ]]; then
            printf '%s%s%s\n' "$dark_gray" "$line" "$reset"
        else
            say swd "$dark_cyan" "$line"
        fi
        return 0
    fi
    for ((i = 0; i < ${#rules[@]}; i += 3)); do
        if [[ "$line" =~ ${rules[i]} ]]; then
            say "${rules[i + 2]}" "${rules[i + 1]}" "$line"
            return 0
        fi
    done
    if [[ "$all" -eq 1 ]]; then say "" "$gray" "$line"; fi
}

# Reads the merged output of tail, which marks each change of file with a
# "==> path <==" header, and reports a silent serial log once.
follow() {
    local source=serial line="" chunk last_serial=$SECONDS silent=0 status
    while true; do
        status=0
        IFS= read -r -t 1 chunk || status=$?
        line+="$chunk"
        if [[ "$status" -ne 0 ]]; then
            # Status above 128 is a timeout (a partial line stays in $line);
            # anything else means tail has ended.
            [[ "$status" -gt 128 ]] || return 0
            if [[ "$silent" -eq 0 && $((SECONDS - last_serial)) -ge "$silence_seconds" ]]; then
                say SILENT "$red" "no serial output for $silence_seconds s: board unpowered or hung, or script/serial-log.sh not running"
                silent=1
            fi
            continue
        fi
        case "$line" in
            "==> $serial_log <==") source=serial ;;
            "==> $swd_log <==") source=swd ;;
            *)
                if [[ "$source" == serial ]]; then
                    last_serial=$SECONDS
                    if [[ "$silent" -eq 1 ]]; then
                        say monitor "$green" "board output resumed"
                        silent=0
                    fi
                fi
                show_line "$line" "$source"
                ;;
        esac
        line=""
    done
}

mode="events only, --all shows everything"
[[ "$all" -eq 1 ]] && mode="all lines"
printf '%s[monitor] Following %s and %s (%s)%s\n' "$dark_gray" "$serial_log" "$swd_log" "$mode" "$reset"
printf '%s[monitor] Recent events, then live. Type a console command and press Enter (try '\''help'\'').%s\n' "$dark_gray" "$reset"

tail -F -n "$history" "$serial_log" "$swd_log" 2>/dev/null | follow &
follower_pid=$!
trap 'kill "$follower_pid" 2>/dev/null; pkill -P $$ tail 2>/dev/null; exit 0' INT TERM EXIT

if [[ -t 0 ]]; then
    while IFS= read -e -r command; do
        command="${command#"${command%%[![:space:]]*}"}"
        command="${command%"${command##*[![:space:]]}"}"
        [[ -n "$command" ]] && printf '%s\n' "$command" >> "$command_file"
    done
else
    wait "$follower_pid"
fi
