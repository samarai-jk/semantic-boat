#!/usr/bin/env bash
# Builds the remote-a RS485 firmware and installs it on displays over RS485
# (through the Signal K RS485 plugin; src/tools/remote-a-update.py).
#
#   script/remote-a-update.sh remote-a-1 [remote-a-2 ...]   build, then update these displays
#   script/remote-a-update.sh all                           every display that is online
#   script/remote-a-update.sh --no-build all                update with the last build
#   script/remote-a-update.sh --debug remote-a-2            a debug build (-O0)
#
# Displays need the remote-a bootloader once (script/build.sh remote-a-rs485
# release flash-all with the ST-Link); after that this is all it takes.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build=1
build_type=release
devices=()
for argument in "$@"; do
    case "$argument" in
        --no-build) build=0 ;;
        --debug) build_type=debug ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) devices+=("$argument") ;;
    esac
done
[[ ${#devices[@]} -gt 0 ]] || { sed -n '2,12p' "$0" >&2; exit 1; }

if [[ "$build" -eq 1 ]]; then
    "$repo_root/script/build.sh" remote-a-rs485 "$build_type" refresh
fi
exec python3 "$repo_root/src/tools/remote-a-update.py" update "${devices[@]}" \
    --image "$repo_root/build/remote-a-rs485/$build_type/src/dev/remote-a/remote-a.bin"
