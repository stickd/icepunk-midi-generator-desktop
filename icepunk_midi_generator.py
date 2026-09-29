from __future__ import annotations

import json
import random
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from mido import Message, MetaMessage, MidiFile, MidiTrack, bpm2tempo


# =========================
# CONFIG
# =========================

ANALYSIS_FILE = Path("analysis_output/midi_analysis.json")

if len(sys.argv) > 1:
    OUTPUT_DIR = Path(sys.argv[1])
else:
    OUTPUT_DIR = Path("generated_midi")

GENERATE_COUNT = 10

DEFAULT_BPM = 146
DEFAULT_TICKS_PER_BEAT = 480

BEATS_PER_BAR = 4
BARS = 4
TOTAL_BEATS = BEATS_PER_BAR * BARS

PATTERN_LENGTH_BEATS = 8.0

GLOBAL_MIN_PITCH = 36
GLOBAL_MAX_PITCH = 84

NOTE_MUTATION_PROBABILITY = 0.18
RHYTHM_MUTATION_PROBABILITY = 0.10
VELOCITY_MUTATION_PROBABILITY = 0.35
OCTAVE_SHIFT_PROBABILITY = 0.12

TOP_LAYER_PROBABILITY = 0.55
MID_LAYER_PROBABILITY = 0.85
BASS_LAYER_PROBABILITY = 0.75

MAX_NOTES_PER_OUTPUT = 80

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

KEY_TO_ROOT = {
    "C": 0,
    "C#": 1,
    "D": 2,
    "D#": 3,
    "E": 4,
    "F": 5,
    "F#": 6,
    "G": 7,
    "G#": 8,
    "A": 9,
    "A#": 10,
    "B": 11,
}

MAJOR_SCALE = [0, 2, 4, 5, 7, 9, 11]
MINOR_SCALE = [0, 2, 3, 5, 7, 8, 10]


# =========================
# DATA TYPES
# =========================

@dataclass
class Note:
    pitch: int
    velocity: int
    start_beat: float
    duration_beats: float
    register: str


@dataclass
class SourcePattern:
    notes: list[Note]
    source_file: str
    estimated_key: str | None
    note_count: int
    register_distribution: dict[str, int]


# =========================
# BASIC HELPERS
# =========================

def clamp(value: int, min_value: int, max_value: int) -> int:
    return max(min_value, min(max_value, value))


def beat_to_ticks(beat: float, ticks_per_beat: int) -> int:
    return int(round(beat * ticks_per_beat))


def quantize_beat(value: float, grid: float = 0.25) -> float:
    return round(round(value / grid) * grid, 4)


def weighted_choice(counter: Counter[Any]) -> Any:
    items = list(counter.items())

    if not items:
        raise ValueError("Cannot choose from empty counter")

    values = [item[0] for item in items]
    weights = [max(1, item[1]) for item in items]

    return random.choices(values, weights=weights, k=1)[0]


def parse_key(key_name: str | None) -> tuple[int, str]:
    if not key_name:
        return KEY_TO_ROOT["A"], "minor"

    parts = key_name.split()

    if len(parts) < 2:
        return KEY_TO_ROOT["A"], "minor"

    root = KEY_TO_ROOT.get(parts[0], KEY_TO_ROOT["A"])
    mode = parts[1] if parts[1] in {"major", "minor"} else "minor"

    return root, mode


def pitch_in_key(pitch: int, root: int, mode: str) -> bool:
    scale = MINOR_SCALE if mode == "minor" else MAJOR_SCALE
    return (pitch - root) % 12 in scale


def nearest_pitch_in_key(pitch: int, root: int, mode: str) -> int:
    pitch = clamp(pitch, GLOBAL_MIN_PITCH, GLOBAL_MAX_PITCH)

    if pitch_in_key(pitch, root, mode):
        return pitch

    candidates: list[int] = []

    for offset in range(-6, 7):
        candidate = clamp(pitch + offset, GLOBAL_MIN_PITCH, GLOBAL_MAX_PITCH)

        if pitch_in_key(candidate, root, mode):
            candidates.append(candidate)

    if not candidates:
        return pitch

    return min(candidates, key=lambda p: abs(p - pitch))


def detect_register(pitch: int) -> str:
    if pitch < 48:
        return "bass"
    if pitch < 72:
        return "mid"
    return "top"


# =========================
# LOAD STYLE DNA
# =========================

def load_analysis() -> dict[str, Any]:
    if not ANALYSIS_FILE.exists():
        raise FileNotFoundError(
            f"Analysis file not found: {ANALYSIS_FILE}. Run analyzer first."
        )

    with ANALYSIS_FILE.open("r", encoding="utf-8") as file:
        return json.load(file)


def load_patterns(analysis: dict[str, Any]) -> tuple[list[SourcePattern], dict[str, Any]]:
    patterns: list[SourcePattern] = []

    key_counter: Counter[str] = Counter()
    rhythm_counter: Counter[str] = Counter()
    register_counter: Counter[str] = Counter()
    bpm_values: list[float] = []

    for file_info in analysis.get("files", []):
        estimated_key = file_info.get("estimated_key")

        if estimated_key:
            key_counter[estimated_key] += 1

        bpm = file_info.get("tempo_bpm_estimate")

        if bpm:
            bpm_values.append(float(bpm))

        rhythm_counter.update(file_info.get("rhythm_position_distribution", {}))
        register_counter.update(file_info.get("register_distribution", {}))

        for pattern in file_info.get("patterns", []):
            raw_notes = pattern.get("notes", [])

            if not raw_notes:
                continue

            notes: list[Note] = []

            for raw_note in raw_notes:
                pitch = int(raw_note["pitch"])

                notes.append(
                    Note(
                        pitch=pitch,
                        velocity=int(raw_note["velocity"]),
                        start_beat=float(raw_note["relative_start_beat"]),
                        duration_beats=float(raw_note["duration_beats"]),
                        register=raw_note.get("register", detect_register(pitch)),
                    )
                )

            if 2 <= len(notes) <= 40:
                patterns.append(
                    SourcePattern(
                        notes=notes,
                        source_file=file_info.get("file_name", "unknown"),
                        estimated_key=estimated_key,
                        note_count=len(notes),
                        register_distribution=pattern.get("register_distribution", {}),
                    )
                )

    if not patterns:
        raise ValueError("No usable patterns found in analysis JSON.")

    average_bpm = round(sum(bpm_values) / len(bpm_values), 2) if bpm_values else DEFAULT_BPM

    style = {
        "keys": key_counter,
        "rhythm_positions": rhythm_counter,
        "registers": register_counter,
        "bpm": average_bpm,
    }

    return patterns, style


# =========================
# PATTERN SELECTION
# =========================

def choose_target_key(style: dict[str, Any]) -> str:
    if style["keys"]:
        return weighted_choice(style["keys"])

    return "A minor"


def choose_pattern_by_register(patterns: list[SourcePattern], target_register: str) -> SourcePattern:
    candidates = [
        pattern for pattern in patterns
        if pattern.register_distribution.get(target_register, 0) > 0
    ]

    if not candidates:
        return random.choice(patterns)

    return random.choice(candidates)


def transpose_note_to_key(note: Note, source_key: str | None, target_key: str) -> Note:
    source_root, _source_mode = parse_key(source_key)
    target_root, target_mode = parse_key(target_key)

    transpose = target_root - source_root

    if random.random() < OCTAVE_SHIFT_PROBABILITY:
        transpose += random.choice([-12, 12])

    new_pitch = note.pitch + transpose
    new_pitch = nearest_pitch_in_key(new_pitch, target_root, target_mode)

    return Note(
        pitch=new_pitch,
        velocity=note.velocity,
        start_beat=note.start_beat,
        duration_beats=note.duration_beats,
        register=detect_register(new_pitch),
    )


def mutate_note(note: Note, target_key: str) -> Note:
    root, mode = parse_key(target_key)

    pitch = note.pitch
    velocity = note.velocity
    start = note.start_beat
    duration = note.duration_beats

    if random.random() < NOTE_MUTATION_PROBABILITY:
        pitch += random.choice([-7, -5, -3, -2, 2, 3, 5, 7])
        pitch = nearest_pitch_in_key(pitch, root, mode)

    if random.random() < RHYTHM_MUTATION_PROBABILITY:
        start = quantize_beat(start + random.choice([-0.25, 0.25]))
        start = clamp_float(start, 0.0, TOTAL_BEATS - 0.25)

    if random.random() < VELOCITY_MUTATION_PROBABILITY:
        velocity += random.randint(-10, 10)
        velocity = clamp(velocity, 35, 127)

    duration = max(0.125, min(duration, 4.0))

    return Note(
        pitch=pitch,
        velocity=velocity,
        start_beat=round(start, 4),
        duration_beats=round(duration, 4),
        register=detect_register(pitch),
    )


def clamp_float(value: float, min_value: float, max_value: float) -> float:
    return max(min_value, min(max_value, value))


# =========================
# GENERATION LOGIC
# =========================

def create_layer_from_pattern(
    pattern: SourcePattern,
    target_key: str,
    target_register: str,
    probability: float,
) -> list[Note]:
    notes: list[Note] = []

    if random.random() > probability:
        return notes

    for note in pattern.notes:
        if note.register != target_register:
            continue

        new_note = transpose_note_to_key(note, pattern.estimated_key, target_key)
        new_note = mutate_note(new_note, target_key)

        if 0.0 <= new_note.start_beat < TOTAL_BEATS:
            notes.append(new_note)

    return notes


def generate_call_response_loop(patterns: list[SourcePattern], style: dict[str, Any]) -> tuple[list[Note], str]:
    target_key = choose_target_key(style)

    bass_pattern = choose_pattern_by_register(patterns, "bass")
    mid_pattern = choose_pattern_by_register(patterns, "mid")
    top_pattern = choose_pattern_by_register(patterns, "top")

    generated: list[Note] = []

    generated.extend(
        create_layer_from_pattern(
            bass_pattern,
            target_key,
            "bass",
            BASS_LAYER_PROBABILITY,
        )
    )

    generated.extend(
        create_layer_from_pattern(
            mid_pattern,
            target_key,
            "mid",
            MID_LAYER_PROBABILITY,
        )
    )

    generated.extend(
        create_layer_from_pattern(
            top_pattern,
            target_key,
            "top",
            TOP_LAYER_PROBABILITY,
        )
    )

    if not generated:
        fallback_pattern = random.choice(patterns)

        for note in fallback_pattern.notes:
            new_note = transpose_note_to_key(note, fallback_pattern.estimated_key, target_key)
            new_note = mutate_note(new_note, target_key)

            if 0.0 <= new_note.start_beat < TOTAL_BEATS:
                generated.append(new_note)

    generated = normalize_to_4_bars(generated)
    generated = remove_too_dense_duplicates(generated)
    generated = reduce_repeated_notes(generated, target_key)
    generated = force_first_note_to_start(generated)

    generated.sort(key=lambda note: (note.start_beat, note.pitch))

    if len(generated) > MAX_NOTES_PER_OUTPUT:
        generated = generated[:MAX_NOTES_PER_OUTPUT]

    return generated, target_key


def normalize_to_4_bars(notes: list[Note]) -> list[Note]:
    normalized: list[Note] = []

    for note in notes:
        start = note.start_beat % TOTAL_BEATS
        duration = min(note.duration_beats, TOTAL_BEATS - start)

        if duration <= 0:
            continue

        normalized.append(
            Note(
                pitch=note.pitch,
                velocity=note.velocity,
                start_beat=round(start, 4),
                duration_beats=round(duration, 4),
                register=detect_register(note.pitch),
            )
        )

    return normalized


def remove_too_dense_duplicates(notes: list[Note]) -> list[Note]:
    seen: set[tuple[float, int]] = set()
    cleaned: list[Note] = []

    for note in sorted(notes, key=lambda n: (n.start_beat, n.pitch)):
        key = (round(note.start_beat, 3), note.pitch)

        if key in seen:
            continue

        seen.add(key)
        cleaned.append(note)

    return cleaned


def reduce_repeated_notes(notes: list[Note], target_key: str) -> list[Note]:
    if len(notes) < 3:
        return notes

    root, mode = parse_key(target_key)
    fixed: list[Note] = []

    repeat_count = 1
    previous_pitch: int | None = None

    for note in sorted(notes, key=lambda n: (n.start_beat, n.pitch)):
        pitch = note.pitch

        if previous_pitch == pitch:
            repeat_count += 1
        else:
            repeat_count = 1

        if repeat_count >= 3:
            possible_moves = [-5, -3, -2, 2, 3, 5, 7]
            random.shuffle(possible_moves)

            for move in possible_moves:
                candidate = nearest_pitch_in_key(pitch + move, root, mode)

                if candidate != previous_pitch:
                    pitch = candidate
                    break

            repeat_count = 1

        fixed.append(
            Note(
                pitch=pitch,
                velocity=note.velocity,
                start_beat=note.start_beat,
                duration_beats=note.duration_beats,
                register=detect_register(pitch),
            )
        )

        previous_pitch = pitch

    return fixed


def force_first_note_to_start(notes: list[Note]) -> list[Note]:
    if not notes:
        return notes

    notes.sort(key=lambda n: (n.start_beat, n.pitch))
    first_start = notes[0].start_beat

    if first_start <= 0.0:
        notes[0].start_beat = 0.0
        return notes

    shifted: list[Note] = []

    for note in notes:
        shifted.append(
            Note(
                pitch=note.pitch,
                velocity=note.velocity,
                start_beat=max(0.0, round(note.start_beat - first_start, 4)),
                duration_beats=note.duration_beats,
                register=note.register,
            )
        )

    return shifted


# =========================
# SCORING
# =========================

def score_generated_notes(notes: list[Note]) -> float:
    if not notes:
        return -9999.0

    score = 0.0

    pitches = [note.pitch for note in notes]
    unique_pitches = len(set(pitches))
    pitch_range = max(pitches) - min(pitches)

    bass_notes = [note for note in notes if note.register == "bass"]
    mid_notes = [note for note in notes if note.register == "mid"]
    top_notes = [note for note in notes if note.register == "top"]

    score += min(unique_pitches, 16) * 1.5

    if bass_notes:
        score += 10

    if mid_notes:
        score += 10

    if top_notes:
        score += 7

    if 12 <= pitch_range <= 42:
        score += 15
    elif pitch_range < 7:
        score -= 20
    elif pitch_range > 52:
        score -= 15

    starts = [note.start_beat for note in notes]
    repeated_positions = len(starts) - len(set(starts))
    score += min(repeated_positions, 14)

    big_jumps = 0

    sorted_notes = sorted(notes, key=lambda note: (note.start_beat, note.pitch))

    for prev, curr in zip(sorted_notes, sorted_notes[1:]):
        if abs(curr.pitch - prev.pitch) > 19:
            big_jumps += 1

    score -= big_jumps * 2.5

    if len(notes) > 90:
        score -= 20

    if len(notes) < 8:
        score -= 25

    return score


def generate_best_notes(
    patterns: list[SourcePattern],
    style: dict[str, Any],
    attempts: int = 50,
) -> tuple[list[Note], str, float]:
    best_notes: list[Note] = []
    best_key = "A minor"
    best_score = -9999.0

    for _ in range(attempts):
        notes, key = generate_call_response_loop(patterns, style)
        score = score_generated_notes(notes)

        if score > best_score:
            best_notes = notes
            best_key = key
            best_score = score

    return best_notes, best_key, best_score


# =========================
# MIDI WRITING
# =========================

def write_midi(notes: list[Note], output_path: Path, bpm: float) -> None:
    mid = MidiFile(ticks_per_beat=DEFAULT_TICKS_PER_BEAT)
    track = MidiTrack()
    mid.tracks.append(track)

    track.append(MetaMessage("track_name", name="Pattern Generator From Analysis", time=0))
    track.append(MetaMessage("set_tempo", tempo=bpm2tempo(bpm), time=0))
    track.append(MetaMessage("time_signature", numerator=BEATS_PER_BAR, denominator=4, time=0))
    track.append(Message("program_change", program=81, channel=0, time=0))

    events: list[tuple[int, Message]] = []
    loop_end_tick = beat_to_ticks(TOTAL_BEATS, DEFAULT_TICKS_PER_BEAT)

    for note in notes:
        start_tick = beat_to_ticks(note.start_beat, DEFAULT_TICKS_PER_BEAT)
        if start_tick < 0 or start_tick >= loop_end_tick or note.duration_beats <= 0:
            continue
        duration_ticks = max(1, beat_to_ticks(note.duration_beats, DEFAULT_TICKS_PER_BEAT))
        end_tick = min(start_tick + duration_ticks, loop_end_tick)

        pitch = clamp(note.pitch, 0, 127)
        velocity = clamp(note.velocity, 1, 127)

        events.append((start_tick, Message("note_on", note=pitch, velocity=velocity, channel=0, time=0)))
        events.append((end_tick, Message("note_off", note=pitch, velocity=0, channel=0, time=0)))

    events.sort(key=lambda item: item[0])

    last_tick = 0

    for absolute_tick, message in events:
        delta = absolute_tick - last_tick
        message.time = max(0, delta)
        track.append(message)
        last_tick = absolute_tick

    # Keep the full four-bar length even when the last note ends earlier.
    track.append(MetaMessage("end_of_track", time=loop_end_tick - last_tick))
    mid.save(output_path)


def clear_output_folder() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    for file in OUTPUT_DIR.iterdir():
        if file.is_file() and file.suffix.lower() in {".mid", ".midi"}:
            file.unlink()


# =========================
# MAIN
# =========================

def generate_midi_files() -> None:
    clear_output_folder()

    analysis = load_analysis()
    patterns, style = load_patterns(analysis)

    print("MIDI GENERATOR FROM STYLE ANALYSIS")
    print("==================================")
    print(f"Analysis file:  {ANALYSIS_FILE.resolve()}")
    print(f"Output folder:  {OUTPUT_DIR.resolve()}")
    print()
    print("STYLE DNA")
    print("---------")
    print(f"Patterns loaded: {len(patterns)}")
    print(f"Common keys: {dict(style['keys'].most_common(8))}")
    print(f"Rhythm positions: {dict(style['rhythm_positions'].most_common(12))}")
    print(f"Registers: {dict(style['registers'])}")
    print(f"BPM: {style['bpm']}")
    print()

    for index in range(1, GENERATE_COUNT + 1):
        notes, key, score = generate_best_notes(patterns, style, attempts=50)

        safe_key = key.replace(" ", "_").replace("#", "sharp")
        output_path = OUTPUT_DIR / f"generated_pattern_{index}_{safe_key}.mid"

        write_midi(notes, output_path, bpm=style["bpm"])

        print(
            f"✅ Generated: {output_path.name} | "
            f"notes: {len(notes)} | key: {key} | score: {round(score, 2)}"
        )

    print()
    print("Done.")


if __name__ == "__main__":
    generate_midi_files()
