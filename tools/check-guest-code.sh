#!/usr/bin/env bash
# Checks that generated/default was written by tools/codegen.ps1 from the files in this tree: the codegen record
# generated/codegen-inputs.stamp must match the manifest, the XEX, the manifest's includes and the codegen patches.
# Codegen itself only runs on Windows. Exit status: 0 current, 2 stale or unverifiable.
#
#   tools/check-guest-code.sh [ROOT]
set -euo pipefail

ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
STAMP="$ROOT/generated/codegen-inputs.stamp"
PATCH_DIR="$ROOT/patches/rexglue-sdk"

stale() { echo "Guest code: stale ($*)" >&2; echo "Run tools\\codegen.ps1 on Windows, then build again." >&2; exit 2; }
sha_upper() { sha256sum "$1" | cut -d' ' -f1 | tr 'a-f' 'A-F'; }

[ -f "$ROOT/generated/default/sources.cmake" ] || stale "generated/default is missing"
[ -f "$STAMP" ] || stale "no successful codegen record"

# The codegen tool: stock, or built from the patches that touch src/codegen (the Windows patch set).
codegen_patches=()
for patch in $(ls "$PATCH_DIR"/*.patch | LC_ALL=C sort); do
  case "$patch" in *.linux.patch) continue ;; esac
  grep -q '^diff --git a/src/codegen/' "$patch" && codegen_patches+=("$patch")
done
tool=stock
if [ "${#codegen_patches[@]}" -gt 0 ]; then
  lines=""
  for patch in "${codegen_patches[@]}"; do
    lines+="${lines:+$'\n'}$(basename "$patch")=$(sha_upper "$patch")"
  done
  tool="patched-$(printf '%s' "$lines" | sha256sum | cut -c1-12)"
fi

sdk_version="$(sed -n 's/^SDK_VERSION=//p' "$PATCH_DIR/baseline.env" | tr -d '\r')"
expected_first="rexglue  $sdk_version"
[ "$tool" = stock ] || expected_first+=" $tool"
[ "$(head -n 1 "$STAMP" | tr -d '\r')" = "$expected_first" ] || stale "the codegen tool or the SDK version changed"

# The recorded inputs: the manifest, the XEX, then the manifest's includes, each at the recorded hash.
includes="$(sed -n 's/^[[:space:]]*includes[[:space:]]*=[[:space:]]*\[\(.*\)\].*/\1/p' "$ROOT/crackdown_manifest.toml" |
  grep -o '"[^"]*"' | tr -d '"' || true)"
expected_keys="crackdown_manifest.toml"
xex_key="$(sed -n '3p' "$STAMP" | sed 's/^[0-9A-F]*  //' | tr -d '\r')"
expected_keys+=$'\n'"$xex_key"
[ -n "$includes" ] && expected_keys+=$'\n'"$includes"
recorded_keys="$(tail -n +2 "$STAMP" | sed 's/^[0-9A-Fa-f]*  //' | tr -d '\r')"
[ "$recorded_keys" = "$expected_keys" ] || stale "the manifest's inputs changed"

while IFS= read -r line || [ -n "$line" ]; do
  line="${line%$'\r'}"
  hash="${line%%  *}"
  key="${line#*  }"
  [ -f "$ROOT/$key" ] || stale "$key is missing"
  [ "$(sha_upper "$ROOT/$key")" = "$hash" ] || stale "$key changed"
done < <(tail -n +2 "$STAMP")
echo "Guest code: current"
