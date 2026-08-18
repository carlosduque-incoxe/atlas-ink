#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: ATLAS_INK_SIGNING_KEY_FILE=/secure/key.pem $0 VERSION FIRMWARE_BIN" >&2
  exit 2
fi

version="$1"
firmware="$2"
key="${ATLAS_INK_SIGNING_KEY_FILE:-}"
repo="carlosduque-incoxe/atlas-ink"

[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "invalid semantic version" >&2; exit 2; }
[[ -f "$firmware" ]] || { echo "firmware not found" >&2; exit 2; }
[[ -f "$key" ]] || { echo "signing key not found" >&2; exit 2; }
[[ -f keys/atlas-ink-release-public.pem ]] || { echo "public key not found" >&2; exit 2; }
git diff --quiet && git diff --cached --quiet || { echo "working tree is dirty" >&2; exit 2; }
[[ "$(git rev-parse HEAD)" == "$(git rev-list -n1 "$version")" ]] || {
  echo "tag $version does not identify HEAD" >&2
  exit 2
}

out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cp "$firmware" "$out/firmware.bin"
digest="sha256:$(sha256sum "$out/firmware.bin" | cut -d' ' -f1)"
size="$(stat -c '%s' "$out/firmware.bin")"
printf 'ATLAS-INK-RELEASE-V1\nversion=%s\nsize=%s\ndigest=%s\n' \
  "$version" "$size" "$digest" > "$out/firmware.bin.manifest"
openssl dgst -sha256 -sign "$key" -out "$out/firmware.bin.sig" "$out/firmware.bin.manifest"
openssl dgst -sha256 -verify keys/atlas-ink-release-public.pem \
  -signature "$out/firmware.bin.sig" "$out/firmware.bin.manifest"

gh release create "$version" \
  "$out/firmware.bin" "$out/firmware.bin.manifest" "$out/firmware.bin.sig" \
  --repo "$repo" --verify-tag --title "Atlas Ink $version" \
  --notes "Signed Atlas Ink bootstrap for XTEINK X4 (ESP32-C3)."