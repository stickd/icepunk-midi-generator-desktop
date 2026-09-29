# ICEPUNK MIDI Generator Desktop

Standalone desktop MIDI generator with a JUCE interface and a bundled Python engine.
The app generates five MIDI variations, displays note previews, and supports MIDI
export and drag-and-drop into a DAW.

## Project layout

- `prototype/`: JUCE application, Python bridge, and integration tests.
- `icepunk_midi_generator.py`: music generation logic.
- `icepunk_midi_analyzer.py`: dataset analysis.
- `midi_dataset/`: source MIDI dataset.
- `analysis_output/midi_analysis.json`: analysis used by the generator.
- `engine/`: bundled Python engine packaging.
- `scripts/` and `installer/`: release builds, validation, and installers.

## Build

See [BUILD_RELEASE.md](BUILD_RELEASE.md) for prerequisites, Windows and macOS build
instructions, packaging, and validation. On a configured Windows development
machine, run `build_windows.bat` from this directory.

Build outputs, local Python environments, and generated MIDI batches are excluded
from version control. The source dataset and its analysis are tracked.

## Version baseline

The initial commit records the existing desktop application before the planned
key/scale controls, key display, app icon, and window resizing changes.
The product version is recorded in `VERSION`.
