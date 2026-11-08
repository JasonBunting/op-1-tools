#!/usr/bin/env bash
# Pack the OP-1 plugins into build/<id>.knl. Run from Git Bash on Windows (it hands off to WSL) or inside WSL.
#   tools/build.sh                    every plugin: each <project>/plugin and research/<tool>/plugin folder
#   tools/build.sh patternplus        only the named projects (their folder names)
#   tools/build.sh -o DIR mirror      write into DIR (relative to the repository) instead of build/
# Needs HUNDEKRAFT_KENNEL, the WSL path of ext/kennel in the Hundekraft source that matches the OP-1's core. Set it in
# the environment or in .env at the repository root (copy .env.example). OP1_WSL_DISTRO picks the WSL distribution.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo"
if [ -f .env ]; then set -a; eval "$(tr -d '\r' < .env)"; set +a; fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        win="$(pwd -W)"                                                    # D:/src/op-1-tools
        drive="$(echo "${win:0:1}" | tr '[:upper:]' '[:lower:]')"
        exec env MSYS_NO_PATHCONV=1 wsl.exe -d "${OP1_WSL_DISTRO:-Ubuntu-24.04}" --cd "/mnt/$drive${win:2}" -- \
            env HUNDEKRAFT_KENNEL="${HUNDEKRAFT_KENNEL:-}" bash tools/build.sh "$@" ;;
esac

out=build names=()
while [ $# -gt 0 ]; do
    case "$1" in
        -o) out="$2"; shift 2 ;;
        -*) echo "build: unknown option $1" >&2; exit 2 ;;
        *) names+=("$1"); shift ;;
    esac
done
[ -n "${HUNDEKRAFT_KENNEL:-}" ] && [ -d "$HUNDEKRAFT_KENNEL" ] || {
    echo "build: set HUNDEKRAFT_KENNEL (in .env, see .env.example) to the WSL path of the Hundekraft source's ext/kennel" >&2
    exit 1
}

plugins=()
for json in */plugin/plugin.json research/*/plugin/plugin.json; do
    [ -f "$json" ] || continue
    dir="${json%/plugin.json}" project="$(basename "$(dirname "$dir")")"
    if [ ${#names[@]} -eq 0 ] || [[ " ${names[*]} " == *" $project "* ]]; then plugins+=("$dir"); fi
done
[ ${#plugins[@]} -gt 0 ] || { echo "build: no plugin folder matches: ${names[*]}" >&2; exit 1; }

for dir in "${plugins[@]}"; do
    id="$(sed -n 's/.*"name": *"\([^"]*\)".*/\1/p' "$dir/plugin.json" | head -1)"
    echo "== $dir -> $out/$id.knl"
    bash tools/pack-plugin.sh "$HUNDEKRAFT_KENNEL" "$dir" "$out/$id.knl"
done
