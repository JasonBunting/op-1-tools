#!/usr/bin/env bash
# Pack a plugin folder into a .knl with Hundekraft's packer. Run inside WSL.
#   tools/pack-plugin.sh <kennel dir of the matching Hundekraft source> <plugin dir> <out.knl>
set -euo pipefail
export PATH="$HOME/bfin/bin:$PATH"
kennel="$1"; plugin="$(realpath "$2")"; out="$(realpath -m "$3")"
mkdir -p "$(dirname "$out")"
cd "$kennel" && python3 tools/kennel/pack.py "$plugin" -o "$out"
