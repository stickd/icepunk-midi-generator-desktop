# ICEPUNK release build

The current product is a **JUCE standalone application**, not a VST3. The Python engine is bundled with
its own runtime via PyInstaller onedir. The music generator, dataset, five-variation workflow and the
two-bar cut/repeat remain unchanged. Users of the installer need neither Python nor a terminal.

## Windows: one command

From this project folder, run:

```bat
build_windows.bat
```

Build-machine prerequisites: Windows x64, Python 3.14 (validated with 3.14.4), Visual Studio 2022 C++
tools/Windows SDK, CMake 3.22+, Git, and Inno Setup 6/7. These are **developer** requirements only.
The script creates an isolated `.release-tools` environment and installs pinned packaging dependencies.

Outputs:

- `dist/windows/ICEPUNK MIDI Generator/ICEPUNK.exe` — native GUI.
- `dist/windows/ICEPUNK MIDI Generator/engine/icepunk-engine.exe` — frozen engine and adjacent `_internal` runtime/data.
- `dist/ICEPUNK_MIDI_Generator_1.0.0_Windows_Setup.exe` — installer, if Inno Setup is available.
- `.release-build/windows/packaged-test.json` — actual relocated-engine/GUI test results.
- `.release-build/windows/release-summary.json` — whether tests, installer and signing hook ran.

Never distribute just the engine `.exe`; its adjacent runtime directory is required. Distribute the
installer or the **entire** staged product folder. C++ links the MSVC runtime statically; PyInstaller
collects the runtime DLLs its Python engine needs.

The build clears only its marked staging directories, runs PyInstaller with a clean analysis cache,
incrementally builds JUCE, stages explicit payload files, runs tests, optionally signs and invokes Inno.
It never deletes the dataset, root generated MIDI or exported user files. Compilation cache is retained
to keep repeat builds practical. A copied/relocated checkout should configure a fresh CMake cache.

JUCE is pinned to commit `91ad83ae34a81e0833b1a2b0866f54846370ae53` (8.0.15). CMake can fetch it, or use:

```powershell
.\scripts\build_windows.ps1 -JuceSource 'D:\dependencies\JUCE' -InnoCompiler 'C:\path\ISCC.exe'
```

With a custom JUCE source, use that exact revision. The current machine's initial build used a local
JUCE checkout; that location is a build cache setting only and is never needed by the packaged app.
Inno is found via PATH, common installation directories or its per-user uninstall registry record.
If absent, the script reports **no installer compiled**; it does not label the payload an installer.

The current installer installs per-user under `%LOCALAPPDATA%\Programs\ICEPUNK MIDI Generator`, creates
a Start-menu shortcut, optionally a Desktop shortcut, supports uninstall, and does not need admin.
Upgrade keeps the same AppId. User exports/settings/logs are outside the installation and are preserved.

## Runtime layout and diagnostics

Windows GUI locates `engine/icepunk-engine.exe` relative to itself. macOS GUI locates the engine in
its app bundle's `Contents/Resources/engine`. A release build has **no system-Python fallback**, no
compiled developer interpreter path and no dependency on the Desktop checkout.

Resources explicitly shipped: unchanged `icepunk_midi_generator.py`, the current analysis JSON, Python
stdlib/native runtime, mido and its dependency. Analyzer, raw source MIDIs, tests, caches, `.git`, backups
and generated files are not shipped. There are no separate visual assets/models required by this GUI.

Writable output and logs:

- Windows: `%LOCALAPPDATA%\ICEPUNK\MIDIGenerator\sessions` and `logs`.
- macOS: `~/Library/Application Support/ICEPUNK/MIDIGenerator/sessions` and `logs`.

The current and previous generated batches are kept in a marked session and cleaned on exit. Save files
you want to keep with Export or DAW drag. Failed batches preserve the previous valid five files.
The app log includes process starts/exits; the rotating engine log records exception details. User-facing
errors are short messages, not Python tracebacks. `ICEPUNK_DATA_DIR` is an optional absolute diagnostic
override, used by automated tests. Regular users never need to set it.

```powershell
& '.\dist\windows\ICEPUNK MIDI Generator\engine\icepunk-engine.exe' --diagnostics
```

The packaged test copies the complete app to a different Unicode/spaced location, strips Python from
PATH, points Python environment variables at nonexistent directories, verifies bundled imports, runs
three engine batches and the actual JUCE Generate/preview/export/error-recovery path, and checks that
the installation tree is unchanged. This is isolation testing on the build machine, **not a clean VM**.

An additional real installer test exercises isolated installation, the installed GUI, same-version
reinstallation, uninstall and preservation of user data. It refuses to run if this product is already
installed, to avoid modifying an existing installation:

```powershell
& .\.release-tools\Scripts\python.exe scripts/test_windows_installer.py --installer dist/ICEPUNK_MIDI_Generator_1.0.0_Windows_Setup.exe --report .release-build/windows/installer-test.json
```

## VST3 status

There is no `.vst3` or plugin target to package today. Do not rename the standalone executable to VST3.
`-Vst3Path 'D:\real-build\ICEPUNK.vst3'` supports a future standard Windows bundle. That installer mode
requires admin, installs support files in Program Files and the plugin under
`C:\Program Files\Common Files\VST3\ICEPUNK.vst3`. The future plugin must be built/tested to locate
the installed engine there; the packaging option does not create that plugin implementation.

## macOS: execute later on a Mac

Install Xcode Command Line Tools, CMake, Git and native Python matching the target architecture, then:

```bash
bash scripts/build_macos.sh
```

Output: `dist/ICEPUNK_MIDI_Generator_1.0.0_macOS.pkg`. The app is installed in `/Applications/ICEPUNK.app`.
`ICEPUNK_VST3_PATH` optionally supplies a real macOS VST3 bundle for the standard system VST3 directory.
The scripts deliberately refuse to build a Mac binary on Windows. Apple Silicon and Intel builds must
be built/tested separately; universal2 is not claimed. See `docs/MAC_SIGNING.md` for certificates,
inside-out signing, hardened runtime, notarization and stapling. No Mac installer is produced on Windows.

## Version updates

Change the root `VERSION` from `1.0.0` to the new semantic version, then rebuild. This controls the
C++ app version, manifest and installer filenames/metadata for both platforms. The Inno script has a
1.0.0 fallback for manual compilation; normally the release script passes the root VERSION explicitly.

## Before public/commercial release

Windows: test the actual installer, upgrade/uninstall, non-admin account and no-Python clean VM; then
configure signing as documented in `docs/WINDOWS_SIGNING.md`. macOS: build/test/sign/notarize on real Macs.
Confirm target-DAW drag behavior, supported OS/architecture matrix, and redistribution rights for the
analysis dataset and the chosen JUCE licence. No certificate, signature or notarization is assumed.
Signing hooks and package scripts alone do not certify a release as commercially ready.
