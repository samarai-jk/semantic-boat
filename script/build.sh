#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' \
        'Usage: script/build.sh <firmware> [debug|release] [refresh] [flash|update]' \
        '' \
        '  firmware - directory name below src/dev or src/dev/<board>, for example'         '             remote-a or lab-test-01' \
        '  debug    - use the <firmware>-debug CMake preset' \
        '  release  - use the <firmware>-release CMake preset (default)' \
        '  refresh  - incremental build; otherwise perform a clean build' \
        '  flash    - flash the ELF after a successful build' \
        '  update   - shorthand for refresh + flash' \
        '' \
        'OpenOCD defaults to PATH and its installed scripts directory.' \
        'Override with OPENOCD and optionally OPENOCD_SCRIPTS.'
}

fail() {
    printf '[build.sh] Error: %s\n' "$*" >&2
    exit 1
}

if [[ $# -eq 0 ]]; then
    usage >&2
    exit 1
fi
case "${1,,}" in
    help|-h|--help|/h|/help) usage; exit 0 ;;
esac

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$repo_root"
firmware="$1"
shift
[[ "$firmware" =~ ^[a-zA-Z0-9_-]+$ ]] || fail "Invalid firmware name: $firmware"
firmware_dir=""
for candidate in "src/dev/$firmware" src/dev/*/"$firmware"; do
    if [[ -f "$candidate/CMakeLists.txt" ]]; then
        firmware_dir="$candidate"
        break
    fi
done
[[ -n "$firmware_dir" ]] || fail "Unknown firmware: $firmware"

build_type=release
do_refresh=0
do_flash=0
for argument in "$@"; do
    case "${argument,,}" in
        debug|release) build_type="${argument,,}" ;;
        refresh) do_refresh=1 ;;
        flash) do_flash=1 ;;
        update) do_refresh=1; do_flash=1 ;;
        help|-h|--help|/h|/help) usage; exit 0 ;;
        *) printf '[build.sh] Warning: unknown argument "%s" ignored.\n' "$argument" >&2 ;;
    esac
done

for tool in cmake ninja arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-objcopy arm-none-eabi-size; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool was not found on PATH. See the Linux setup in README.md."
done

preset="$firmware-$build_type"
elf_path="build/$firmware/$build_type/$firmware_dir/$firmware.elf"
printf '[build.sh] Firmware: %s | type: %s | refresh=%s | flash=%s\n' \
    "$firmware" "$build_type" "$do_refresh" "$do_flash"
cmake --preset "$preset"
build_arguments=(--build --preset "$preset")
if [[ "$do_refresh" -eq 0 ]]; then
    build_arguments+=(--clean-first)
fi
cmake "${build_arguments[@]}"
[[ -f "$elf_path" ]] || fail "Expected firmware image was not produced: $elf_path"
printf '[build.sh] Build succeeded: %s\n' "$elf_path"

if [[ "$do_flash" -eq 1 ]]; then
    case "$firmware" in
        remote-a|lab-test-01) openocd_target=target/stm32l4x.cfg ;;
        *) fail "No OpenOCD target is configured for $firmware; add its mapping to script/build.sh." ;;
    esac
    openocd="${OPENOCD:-openocd}"
    command -v "$openocd" >/dev/null 2>&1 || fail "OpenOCD was not found: $openocd"
    openocd_arguments=()
    if [[ -n "${OPENOCD_SCRIPTS:-}" ]]; then
        [[ -f "$OPENOCD_SCRIPTS/$openocd_target" ]] || fail "OpenOCD target script not found in $OPENOCD_SCRIPTS"
        openocd_arguments+=(-s "$OPENOCD_SCRIPTS")
    fi
    printf '[build.sh] Flashing %s...\n' "$firmware"
    # Programming needs no GDB/Tcl/telnet servers; skipping them avoids
    # failures when another tool already listens on the default ports.
    "$openocd" "${openocd_arguments[@]}" \
        -c "gdb_port disabled; tcl_port disabled; telnet_port disabled" \
        -f interface/stlink.cfg -f "$openocd_target" \
        -c "program {$elf_path} verify reset exit"
    printf '[build.sh] Flash completed successfully.\n'
fi