#!/usr/bin/env bash
# Build the downloadable files for a GitHub release into build/release/<tag>/, and optionally publish them.
# Run from Git Bash on Windows (it uses WSL for the plugin and dotnet for the viewers).
#   tools/release.sh [--publish] [--allow-dirty] [tag]      tag defaults to v<plugin.json version>
# Needs:
#   HUNDEKRAFT_KENNEL   WSL path of ext/kennel in the Hundekraft 3.1.4 source (the plugin's SDK and packer)
#   OP1_KEYSTORE        Windows path of the Android signing keystore (alias op1mirror), made once:
#                         keytool -genkeypair -keystore op1mirror.keystore -alias op1mirror -keyalg RSA -keysize 2048 -validity 36500
#                       Keep it and its password safe and out of the repo: updates to the app must be signed with it.
#   OP1_KEYSTORE_PASS   its password (read by the build from the environment, never put on the command line)
# --publish creates the GitHub release with gh; without it the script prints the command to run.
set -euo pipefail

publish=0 allow_dirty=0 tag=
for a in "$@"; do
    case "$a" in
        --publish) publish=1 ;;
        --allow-dirty) allow_dirty=1 ;;
        -*) echo "unknown option $a" >&2; exit 2 ;;
        *) tag="$a" ;;
    esac
done

repo="$(cd "$(dirname "$0")/.." && pwd -W)"                     # D:/src/op-1-tools
cd "$repo"
version="$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' firmware/mirror/plugin.json)"
tag="${tag:-v$version}"
out="build/release/$tag"

fail() { echo "release: $*" >&2; exit 1; }
[ -n "${HUNDEKRAFT_KENNEL:-}" ] || fail "set HUNDEKRAFT_KENNEL to the WSL path of the Hundekraft 3.1.4 source's ext/kennel"
[ -n "${OP1_KEYSTORE:-}" ] && [ -f "$OP1_KEYSTORE" ] || fail "set OP1_KEYSTORE to the Android keystore (see the top of this script)"
[ -n "${OP1_KEYSTORE_PASS:-}" ] || fail "set OP1_KEYSTORE_PASS to the keystore's password"
if [ "$allow_dirty" = 0 ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    fail "uncommitted changes: a release should match a commit (or pass --allow-dirty for a trial build)"
fi

rm -rf "$out"
mkdir -p "$out"

echo "== plugin"
drive="$(echo "${repo:0:1}" | tr '[:upper:]' '[:lower:]')"
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 --cd "/mnt/$drive${repo:2}" -- \
    bash tools/pack-plugin.sh "$HUNDEKRAFT_KENNEL" firmware/mirror "$out/mirror.knl"

echo "== Windows viewer"
dotnet publish desktop/Op1Mirror/Op1Mirror.csproj -c Release -r win-x64 --self-contained -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true -p:EnableCompressionInSingleFile=true \
    -p:DebugType=none -o "build/release/obj/windows" -v quiet -nologo
cp build/release/obj/windows/Op1Mirror.exe "$out/Op1Mirror.exe"

echo "== Android viewer"
dotnet publish mobile/Op1Mirror.Mobile/Op1Mirror.Mobile.csproj -f net10.0-android -c Release \
    -p:AndroidPackageFormat=apk -p:RuntimeIdentifiers=android-arm64 \
    -p:AndroidKeyStore=true -p:AndroidSigningKeyStore="$OP1_KEYSTORE" -p:AndroidSigningKeyAlias=op1mirror \
    -p:AndroidSigningStorePass=env:OP1_KEYSTORE_PASS -p:AndroidSigningKeyPass=env:OP1_KEYSTORE_PASS \
    -o "build/release/obj/android" -v quiet -nologo
cp build/release/obj/android/*-Signed.apk "$out/op1-mirror.apk"

commit="$(git rev-parse --short HEAD)"
cat > "$out/notes.md" <<EOF
Mirror the OP-1's screen on Windows or Android. Built from $commit. Shared as-is and not actively supported.

**Needs** an original OP-1 running Hundekraft 3.1.4.

1. **mirror.knl**: the OP-1 plugin. In the Hundekraft workshop: Your plugins → drop it in → Review changes → Send to
   OP-1 → T1 on the OP-1. Then close the browser completely (it holds the OP-1's MIDI port).
2. **Op1Mirror.exe**: the Windows viewer (64-bit, no install, .NET included). Windows may warn that it is
   unrecognised: More info → Run anyway.
3. **op1-mirror.apk**: the Android viewer (Android 8+ on 64-bit ARM, which is nearly every recent phone). Allow
   installs from unknown sources, then plug the OP-1 into the phone with a USB-C OTG cable or adapter.

Licenses: the plugin is GPL-3.0-or-later with the OP-1 firmware permission (source: firmware/mirror at this tag);
the viewers are MIT. See the README.
EOF

rm -rf build/release/obj
echo
ls -l "$out"
files=("$out/mirror.knl" "$out/Op1Mirror.exe" "$out/op1-mirror.apk")
if [ "$publish" = 1 ]; then
    git rev-parse -q --verify "refs/tags/$tag" >/dev/null || fail "tag $tag doesn't exist: create and push it first"
    gh release create "$tag" "${files[@]}" --title "OP-1 Mirror $tag" --notes-file "$out/notes.md"
else
    echo
    echo "To publish (after pushing the commit and tag $tag):"
    echo "  gh release create $tag ${files[*]} --title \"OP-1 Mirror $tag\" --notes-file $out/notes.md"
fi
