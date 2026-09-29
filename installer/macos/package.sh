#!/bin/bash
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'pkgbuild/productbuild require macOS.' >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
APP="${1:-$ROOT/dist/macos/ICEPUNK.app}"
[[ -d "$APP/Contents/MacOS" ]] || { echo 'A real built ICEPUNK.app is required'; exit 1; }
WORK="$ROOT/.release-build/macos/package"
mkdir -p "$WORK" "$ROOT/dist"
PAYLOAD="$(mktemp -d "$WORK/payload.XXXXXX")"
trap 'rm -rf -- "$PAYLOAD"' EXIT
mkdir -p "$PAYLOAD/Applications"
ditto "$APP" "$PAYLOAD/Applications/ICEPUNK.app"
if [[ -n "${ICEPUNK_VST3_PATH:-}" ]]; then
    [[ -d "$ICEPUNK_VST3_PATH/Contents" && "$ICEPUNK_VST3_PATH" == *.vst3 ]] || { echo 'Expected a built macOS VST3 bundle'; exit 1; }
    mkdir -p "$PAYLOAD/Library/Audio/Plug-Ins/VST3"
    ditto "$ICEPUNK_VST3_PATH" "$PAYLOAD/Library/Audio/Plug-Ins/VST3/ICEPUNK.vst3"
fi
pkgbuild --analyze --root "$PAYLOAD" "$WORK/components.plist"
# Fix the install location; don't relocate the app to an older copy elsewhere.
python3 - "$WORK/components.plist" <<'PY'
import plistlib, sys
from pathlib import Path
p = Path(sys.argv[1])
items = plistlib.loads(p.read_bytes())
for item in items:
    item['BundleIsRelocatable'] = False
    item['BundleOverwriteAction'] = 'upgrade'
p.write_bytes(plistlib.dumps(items))
PY
pkgbuild --root "$PAYLOAD" --component-plist "$WORK/components.plist" --install-location / \
    --identifier audio.icepunk.midigenerator --version "$VERSION" "$WORK/ICEPUNK-component.pkg"
SIGN=()
if [[ -n "${MAC_INSTALLER_IDENTITY:-}" ]]; then SIGN=(--sign "$MAC_INSTALLER_IDENTITY"); fi
productbuild --package "$WORK/ICEPUNK-component.pkg" "${SIGN[@]}" \
    "$ROOT/dist/ICEPUNK_MIDI_Generator_${VERSION}_macOS.pkg"
echo "Created $ROOT/dist/ICEPUNK_MIDI_Generator_${VERSION}_macOS.pkg"
