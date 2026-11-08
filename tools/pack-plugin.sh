#!/usr/bin/env bash
# Pack a plugin folder into a .knl with Hundekraft's packer. Run inside WSL (tools/build.sh calls it for you).
#   tools/pack-plugin.sh <kennel dir of the matching Hundekraft source> <plugin dir> <out.knl>
# The packer wants the folder named after the plugin's id, so a copy is packed from a temporary folder of that name.
set -euo pipefail
export PATH="$HOME/bfin/bin:$PATH"
kennel="$1"; plugin="$(realpath "$2")"; out="$(realpath -m "$3")"
id="$(sed -n 's/.*"name": *"\([^"]*\)".*/\1/p' "$plugin/plugin.json" | head -1)"
stage="$(mktemp -d)"; trap 'rm -rf "$stage"' EXIT
cp -r "$plugin" "$stage/$id"
mkdir -p "$(dirname "$out")"
cd "$kennel" && python3 tools/kennel/pack.py "$stage/$id" -o "$out"
