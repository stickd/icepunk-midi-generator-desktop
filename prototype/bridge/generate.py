"""Local five-variation bridge. Original generator and dataset stay untouched."""
from __future__ import annotations
import argparse
from dataclasses import replace
import importlib.util
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True
PROJECT_ROOT = (Path(sys._MEIPASS) / 'resources' if getattr(sys, 'frozen', False)
                else Path(__file__).resolve().parents[2])


def app_data_dir():
    if os.environ.get('ICEPUNK_DATA_DIR'):
        return Path(os.environ['ICEPUNK_DATA_DIR']).expanduser().resolve()
    if sys.platform == 'win32':
        return Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'ICEPUNK/MIDIGenerator'
    if sys.platform == 'darwin':
        return Path.home() / 'Library/Application Support/ICEPUNK/MIDIGenerator'
    return Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'ICEPUNK/MIDIGenerator'


def engine_logger():
    logger = logging.getLogger('icepunk.engine')
    if not logger.handlers:
        directory = app_data_dir() / 'logs'
        directory.mkdir(parents=True, exist_ok=True)
        handler = RotatingFileHandler(directory / 'engine.log', maxBytes=262144, backupCount=2, encoding='utf-8')
        handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


def load_generator():
    name = "icepunk_original_generator"
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, PROJECT_ROOT / "icepunk_midi_generator.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def validate_midi(path: Path) -> int:
    from mido import MidiFile
    from collections import Counter
    midi = MidiFile(path)
    limit = 16 * midi.ticks_per_beat
    ends, count = [], 0
    for track in midi.tracks:
        tick, active = 0, Counter()
        for message in track:
            tick += message.time
            if tick > limit:
                raise ValueError("Generated MIDI exceeds four bars")
            if message.type == "note_on" and message.velocity > 0:
                count += 1
                active[message.channel, message.note] += 1
            elif message.type == "note_off" or (message.type == "note_on" and message.velocity == 0):
                active[message.channel, message.note] -= 1
        if any(active.values()):
            raise ValueError("Generated MIDI contains an unfinished note")
        ends.append(tick)
    if not ends or max(ends) != limit or not count:
        raise ValueError("Expected a non-empty, exactly four-bar MIDI")
    return count


def repeat_two_bar_phrase(notes, ticks_per_beat: int = 480):
    """Clip the first two bars, then copy them unchanged into bars three/four.

    Work on the MIDI tick grid so the seam is exact after serialization. Notes
    crossing the internal bar line (beat 4) are deliberately left intact.
    """
    half_ticks = 8 * ticks_per_beat
    first_half = []
    for note in notes:
        start = round(note.start_beat * ticks_per_beat)
        if not 0 <= start < half_ticks or note.duration_beats <= 0:
            continue
        end = min(start + max(1, round(note.duration_beats * ticks_per_beat)), half_ticks)
        first_half.append(replace(note, start_beat=start / ticks_per_beat,
                                  duration_beats=(end - start) / ticks_per_beat))
    repeated = [replace(note, start_beat=note.start_beat + 8) for note in first_half]
    return sorted(first_half + repeated, key=lambda note: (note.start_beat, note.pitch))


def generate_batch(output_dir: Path, analysis_file: Path | None = None) -> dict:
    g = load_generator()
    source = analysis_file or PROJECT_ROOT / "analysis_output/midi_analysis.json"
    analysis = json.loads(source.read_text(encoding="utf-8"))
    patterns, style = g.load_patterns(analysis)
    output_dir = output_dir.resolve()
    # Fresh batch only: never overwrite a previous successful result.
    output_dir.mkdir(parents=True, exist_ok=False)
    clips = []
    try:
        for index in range(1, 6):
            # Original selection, mutations, 50 attempts and scoring, unmodified.
            notes, key, score = g.generate_best_notes(patterns, style, attempts=50)
            notes = repeat_two_bar_phrase(notes, g.DEFAULT_TICKS_PER_BEAT)
            output = output_dir / f"midi_{index}.mid"
            g.write_midi(notes, output, bpm=style["bpm"])
            count = validate_midi(output)
            clips.append({"path": str(output), "key": key, "bpm": style["bpm"],
                          "bars": 4, "beats": 16, "notes": count, "score": score})
    except BaseException:
        # This call owns this freshly created directory, not any previous batch.
        shutil.rmtree(output_dir)
        raise
    return {"version": 1, "ok": True, "clips": clips}


def main() -> int:
    parser = argparse.ArgumentParser(description="ICEPUNK five-variation local bridge")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--diagnostics", action='store_true')
    args = parser.parse_args()
    if args.diagnostics:
        import mido
        import hashlib
        print(json.dumps({'frozen': bool(getattr(sys, 'frozen', False)), 'executable': sys.executable,
                          'prefix': sys.prefix, 'sys_path': sys.path, 'mido': mido.__file__,
                          'resources': str(PROJECT_ROOT), 'data_dir': str(app_data_dir()),
                          'generator_sha256': hashlib.sha256((PROJECT_ROOT / 'icepunk_midi_generator.py').read_bytes()).hexdigest(),
                          'dataset_sha256': hashlib.sha256((PROJECT_ROOT / 'analysis_output/midi_analysis.json').read_bytes()).hexdigest()}))
        return 0
    if args.output_dir is None:
        parser.error('--output-dir is required unless --diagnostics is used')
    try:
        engine_logger().info('Generation started (frozen=%s)', bool(getattr(sys, 'frozen', False)))
        result = generate_batch(args.output_dir)
        engine_logger().info('Generation completed: five files')
    except Exception as exc:
        try:
            engine_logger().exception('Generation failed')
        except OSError:
            pass
        print(json.dumps({"version": 1, "ok": False,
                          "error": "Generation failed. Please retry. Details are in the application logs."}, ensure_ascii=True))
        return 1
    print(json.dumps(result, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
