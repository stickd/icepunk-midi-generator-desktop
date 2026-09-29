# macOS signing/notarization — preparation only

No macOS binary was built or tested on Windows. These scripts require a real Mac, Xcode Command Line
Tools, a working native Python build environment and, for distribution, the publisher's Apple Developer
membership and two certificates in the keychain:

- **Developer ID Application: Publisher (TEAMID)** — app, engine and any plugin binaries.
- **Developer ID Installer: Publisher (TEAMID)** — `.pkg` installer.

Never commit private keys, Apple passwords, API key files or keychain credentials.

## Build and sign

```bash
xcode-select --install  # if tools are not already installed; complete the system dialog
security find-identity -v -p codesigning
export MAC_APPLICATION_IDENTITY='Developer ID Application: Publisher (TEAMID)'
export MAC_INSTALLER_IDENTITY='Developer ID Installer: Publisher (TEAMID)'
bash scripts/build_macos.sh
```

`build_macos.sh` builds for the current `arm64` or `x86_64` architecture. Python, PyInstaller and JUCE
must all target that architecture. Produce/test separate builds on Apple Silicon and Intel as needed;
this pipeline does not claim a universal2 result or combine incompatible native packages.
The effective minimum OS is the highest minimum required by the bundled Python/framework and JUCE;
do not advertise the CMake deployment target alone as a verified minimum OS.

PyInstaller signs collected native libraries using the supplied identity. After assembling the final
bundle, `scripts/sign_macos.py` signs Mach-O files and nested bundles inside-out with `--options runtime`
and `--timestamp`, then signs the outer app. It uses `--deep` only for **verification**, not as a substitute
for correctly signing nested code.

The Python engine uses `installer/macos/engine.entitlements.plist` to allow loading bundled extension
libraries under hardened runtime. Only the engine executable receives the library-validation exception;
the JUCE GUI does not. Validate these entitlements on the actual release and remove the exception if the
tested distribution does not need it. No App Sandbox or microphone entitlement is requested.

Manual equivalents:

```bash
python3 scripts/sign_macos.py dist/macos/ICEPUNK.app --identity "$MAC_APPLICATION_IDENTITY"
codesign --verify --deep --strict --verbose=2 dist/macos/ICEPUNK.app
codesign -d --entitlements :- dist/macos/ICEPUNK.app/Contents/Resources/engine/icepunk-engine
MAC_INSTALLER_IDENTITY="$MAC_INSTALLER_IDENTITY" bash installer/macos/package.sh dist/macos/ICEPUNK.app
pkgutil --check-signature dist/ICEPUNK_MIDI_Generator_1.0.0_macOS.pkg
```

For an eventual real VST3 bundle, sign it separately with `sign_macos.py` and set `ICEPUNK_VST3_PATH`
before packaging. The package places it in `/Library/Audio/Plug-Ins/VST3/ICEPUNK.vst3`. There is no VST3
target in the current project. The future plugin must locate its support engine inside
`/Applications/ICEPUNK.app/Contents/Resources/engine`, or adopt its own self-contained bundle layout.

## Notarize and staple

Create a local keychain profile interactively (notarytool prompts for credentials):

```bash
xcrun notarytool store-credentials ICEPUNK-notary
```

An App Store Connect API key profile is also supported by notarytool. Follow Apple's credential
requirements and keep all secrets outside source control.

After signed package creation:

```bash
bash installer/macos/notarize.sh dist/ICEPUNK_MIDI_Generator_1.0.0_macOS.pkg ICEPUNK-notary
```

The script submits with `notarytool --wait`, verifies JSON status is **Accepted**, staples the package,
validates the ticket and runs `spctl --assess --type install`. For rejected submissions:

```bash
xcrun notarytool log SUBMISSION-ID --keychain-profile ICEPUNK-notary notarization-log.json
```

The notarization script deliberately does not run automatically during unsigned development builds.
Installing the pkg places the application in `/Applications`; user data remains in
`~/Library/Application Support/ICEPUNK/MIDIGenerator`. macOS has no automatic pkg uninstaller:
remove the installed app (and optional plugin) separately while preserving user data/exports.

## Required Mac verification

Run the packaged tests again after signing; install/uninstall on real machines; check hardened-runtime
Python loading, spaces/Unicode paths, repeat generation, all five previews, export/DAW drag, Gatekeeper
and an offline first launch after notarization. Verify the intended Intel/Apple Silicon and minimum-OS
matrix. A successful Windows build says nothing about these platform checks.

Reference: [Apple notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow),
[PyInstaller macOS notes](https://pyinstaller.org/en/stable/feature-notes.html#macos-multi-arch-support).
