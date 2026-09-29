#!/bin/bash
set -euo pipefail
[[ "$(uname -s)" == Darwin ]] || { echo 'Notarization requires macOS/Xcode tools.' >&2; exit 1; }
PKG="${1:?Usage: notarize.sh path/to/installer.pkg keychain-profile}"
PROFILE="${2:?Provide the notarytool keychain profile name}"
[[ -f "$PKG" && "$PKG" == *.pkg ]] || { echo 'Expected an existing signed .pkg'; exit 1; }
pkgutil --check-signature "$PKG"
RESULT="$(mktemp)"
trap 'rm -f -- "$RESULT"' EXIT
xcrun notarytool submit "$PKG" --keychain-profile "$PROFILE" --wait --output-format json > "$RESULT"
python3 - "$RESULT" <<'PY'
import json, sys
result = json.load(open(sys.argv[1]))
print(json.dumps(result, indent=2))
if result.get('status') != 'Accepted':
    raise SystemExit('Notarization not accepted. Retrieve the log using the submission id; do not staple/distribute.')
PY
xcrun stapler staple "$PKG"
xcrun stapler validate "$PKG"
spctl --assess --type install --verbose=4 "$PKG"
