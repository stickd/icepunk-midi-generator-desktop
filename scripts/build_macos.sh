#!/bin/bash
set -euo pipefail
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'A real Mac with Xcode Command Line Tools is required. No macOS binaries are built on Windows.' >&2
    exit 1
fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
WORK="$ROOT/.release-build/macos"
STAGE="$ROOT/dist/macos"
TOOLS="$ROOT/.release-tools-macos"
ARCH="$(uname -m)"
[[ "$ARCH" == arm64 || "$ARCH" == x86_64 ]] || { echo 'Unsupported Mac architecture'; exit 1; }
export ICEPUNK_TARGET_ARCH="$ARCH"
mkdir -p "$WORK"
if [[ ! -x "$TOOLS/bin/python" ]]; then python3 -m venv "$TOOLS"; fi
PY="$TOOLS/bin/python"
"$PY" -m pip install -r "$ROOT/engine/requirements-build.txt"

reset_owned() {
    local target="$1"
    case "$target" in "$WORK"/*|"$ROOT/dist/macos") ;; *) echo "Unsafe cleanup: $target"; exit 1 ;; esac
    if [[ -e "$target" ]]; then
        [[ -f "$target/.icepunk-build" ]] || { echo "Refusing unmarked directory: $target"; exit 1; }
        rm -rf -- "$target"
    fi
    mkdir -p "$target"
    printf 'Owned release staging\n' > "$target/.icepunk-build"
}
reset_owned "$WORK/engine-dist"
"$PY" -m PyInstaller --clean --noconfirm --workpath "$WORK/pyinstaller" --distpath "$WORK/engine-dist" "$ROOT/engine/icepunk_engine.spec"
: "${ICEPUNK_LICENSE_ORIGIN:?Set HTTPS licensing origin}"
: "${ICEPUNK_LICENSE_PUBLIC_KEY:?Set Ed25519 public key}"
: "${ICEPUNK_TEST_LICENSE_FILE:?Set signed authorization file for this machine}"
CMAKE_ARGS=("-DICEPUNK_LICENSE_ORIGIN=$ICEPUNK_LICENSE_ORIGIN" "-DICEPUNK_LICENSE_PUBLIC_KEY=$ICEPUNK_LICENSE_PUBLIC_KEY" -S "$ROOT/prototype" -B "$WORK/cmake" -DCMAKE_BUILD_TYPE=Release -DICEPUNK_DEVELOPMENT_ENGINE=OFF "-DCMAKE_OSX_ARCHITECTURES=$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
if [[ -n "${JUCE_SOURCE:-}" ]]; then CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$JUCE_SOURCE"); fi
cmake "${CMAKE_ARGS[@]}"
cmake --build "$WORK/cmake" --config Release --parallel 4
reset_owned "$STAGE"
APP="$STAGE/ICEPUNK.app"
/usr/bin/ditto "$WORK/cmake/release-app/ICEPUNK.app" "$APP"
RESOURCES="$APP/Contents/Resources"
mkdir -p "$RESOURCES"
/usr/bin/ditto "$WORK/engine-dist/icepunk-engine" "$RESOURCES/engine"
JUCE_META="${JUCE_SOURCE:-$WORK/cmake/_deps/juce-src}"
"$PY" "$ROOT/scripts/release_metadata.py" --stage "$RESOURCES" --juce "$JUCE_META"
"$PY" -B -m unittest discover -s "$ROOT/prototype/tests" -v
"$PY" "$ROOT/scripts/test_packaged.py" --stage "$STAGE" --report "$WORK/packaged-test.json" --license-file "$ICEPUNK_TEST_LICENSE_FILE"
if [[ -n "${MAC_APPLICATION_IDENTITY:-}" ]]; then
    "$PY" "$ROOT/scripts/sign_macos.py" "$APP" --identity "$MAC_APPLICATION_IDENTITY"
else
    echo 'No Developer ID Application identity: this is a local unsigned/ad-hoc developer build.'
fi
if [[ -n "${ICEPUNK_VST3_PATH:-}" && -n "${MAC_APPLICATION_IDENTITY:-}" ]]; then
    echo 'Sign the supplied VST3 bundle separately before packaging; see docs/MAC_SIGNING.md.'
    codesign --verify --deep --strict "$ICEPUNK_VST3_PATH"
fi
bash "$ROOT/installer/macos/package.sh" "$APP"
echo "macOS $ARCH pipeline completed. Notarization is a separate explicit step."
