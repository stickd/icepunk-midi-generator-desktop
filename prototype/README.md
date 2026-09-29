# ICEPUNK — five MIDI ideas

Native JUCE/C++ standalone prototype, backed by the existing Python generator.

For distributable builds with bundled Python and installers, see **../BUILD_RELEASE.md**.
The instructions below describe the source/development build.

## Launch

From the project folder, double-click **Launch ICEPUNK.vbs**, or open **app/ICEPUNK.exe**.
Press **GENERATE**. Five independently generated patterns appear. Drag any preview into
your DAW, or click the small **EXPORT** label in that row to save its MIDI elsewhere.
Generate again for five new options. There are no parameter controls or playback.

Each MIDI has an end-of-track at exactly 16 beats. After generation, the bridge clips the
first two bars at beat 8 and repeats those same notes in bars 3–4. Notes crossing beat 4
are preserved; only the two-bar seam is clipped. Some DAWs choose a clip's visible size from the final note
rather than the MIDI end-of-track marker; the files themselves remain sixteen beats long.

This is a standalone application, not yet a VST3. Leave it open alongside FL Studio.

## Build on Windows

Requires Visual Studio 2022 C++ tools and Windows SDK, CMake 3.22+, Git, Python 3.10+ and mido.
From the project folder:

```powershell
python -m pip install -r prototype/requirements.txt
powershell -File prototype/build.ps1
```

CMake fetches pinned JUCE 8.0.15 on a clean build. The executable is written to `app/ICEPUNK.exe`.
The current machine already has Python/mido and the C++ compiler. No Python GUI packages are used.
The development build uses `python` on PATH, or an explicit override:

```powershell
$env:ICEPUNK_PYTHON = 'C:\path\to\python.exe'
& '.\app\ICEPUNK.exe'
```

Keep the executable inside this project: it locates the generator and analysis data by walking
up from its directory. For development, `--project "absolute project folder"` overrides this.
The Windows build is validated here; macOS requires its own build/signing and testing.

## Architecture

- `src/Main.cpp`: standalone JUCE window, project discovery, native smoke-test runner.
- `src/MainComponent.*`: one Generate control, five rows, status, async export, batch lifecycle.
- `src/MidiPreview.*`: actual note rectangles, four-bar grid, per-row pitch fit, native file dragging.
- `src/GeneratorEngine.*`: replaceable engine interface, background worker, Python process, MIDI parser.
- `bridge/generate.py`: loads the root Python module and original JSON; calls
  `generate_best_notes(..., attempts=50)` exactly five times, clips/repeats each two-bar phrase,
  then calls `write_midi` at the original style BPM.
- `tests/test_bridge.py`: Python integration tests, including comparison with original generation
  plus the requested clip/repeat operation and equality of both serialized halves.

The original generator, analyzer and analysis JSON are not edited or moved. The bridge does not
call the root CLI's output-folder clearing routine. There is no key/tempo override, re-ranking,
change to pattern selection. The requested clip/repeat is a postprocessing step after scoring.
Variations are independent stochastic draws;
the algorithm cannot mathematically guarantee uniqueness for every possible dataset.

Python runs in a separate process on a worker thread with a 30-second timeout. The previous five
previews stay visible during generation. All five new files must pass parsing before the UI replaces
the batch. On failure, previous files remain valid and Generate becomes available again.

JUCE reads MIDI ticks directly without inserting synthetic note-offs. A FIFO queue per track/channel/
pitch matches note-on/off events, including overlapping same-pitch notes. The preview uses a fixed
16-beat horizontal axis and fits each pattern's actual pitch range vertically.

Native drag-and-drop uses JUCE `performExternalDragDropOfFiles` with copy semantics. Each row sends
its own immutable MIDI path. Generation is blocked during an active drag/save dialog. Export writes
a separate user-owned file. Direct DAW drop behavior depends on the target accepting MIDI file drops;
Export is always available as the fallback.

## Temporary data

Newly built applications store output under `%LOCALAPPDATA%/ICEPUNK/MIDIGenerator/sessions/`
on Windows, or `~/Library/Application Support/ICEPUNK/MIDIGenerator/sessions/` on macOS,
inside `session-<uuid>/batch-<uuid>/`. Older prototype binaries used `generated_midi/prototype/`.
Each batch contains `midi_1.mid` through `midi_5.mid`. Current and previous batches are retained
(at most ten completed temporary MIDIs, plus a batch being generated). Older batches are removed.
The app cleans its marked session on normal exit and marked stale sessions on the next launch after
a crash. Root `generated_midi` files and exported files are never cleared. Export anything you want
to keep before closing the app. Failed partial batches are discarded without touching prior batches.

## Tests

```powershell
python -B -m unittest discover -s prototype/tests -v
ctest --test-dir prototype/build -C Release --output-on-failure
```

To run the native UI/engine smoke test, close the regular instance first:

```powershell
& '.\app\ICEPUNK.exe' --self-test 'C:\absolute\test-output'
```

It generates three real batches, reads all previews, exercises a simulated failure and recovery,
checks bounded batch retention, exports five MIDI copies and saves a screenshot and text report.
The existing external 100-generation/seven-edge-case regression test is also preserved.

## Before VST3

Add a JUCE audio-plugin target/host wrapper, settle runtime bundling or a native engine replacement,
make storage/lifetime per plugin instance, and test host lifecycle and dragging in FL Studio and
other Windows/macOS DAWs. Then address platform signing, distributable packaging and the chosen
JUCE distribution licence. The engine interface and preview components can be reused; none of this
packaging work is part of the current milestone.
