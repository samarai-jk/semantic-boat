#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
simulator_directory="$repo_root/src/tools/disp-sim"

command -v node >/dev/null 2>&1 || {
    printf '[sim.sh] Error: Node.js 20 or newer is required on PATH.\n' >&2
    exit 1
}
[[ -d "$simulator_directory/node_modules/serialport" ]] || {
    printf '[sim.sh] Error: simulator dependencies are missing. Run:\n  npm ci --prefix "%s"\n' \
        "$simulator_directory" >&2
    exit 1
}

cd -- "$simulator_directory"
exec node src/index.js "$@"