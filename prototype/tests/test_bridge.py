import hashlib
import importlib.util
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from mido import MidiFile, tempo2bpm

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / 'prototype/bridge/generate.py'
spec = importlib.util.spec_from_file_location('bridge', BRIDGE)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='icepunk-test-')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def test_five_distinct_files_full_length_and_no_overrun(self):
        result = bridge.generate_batch(self.folder / 'batch')
        self.assertEqual(len(result['clips']), 5)
        files = list((self.folder / 'batch').glob('*.mid'))
        self.assertEqual(len(files), 5)
        self.assertEqual(len({p.read_bytes() for p in files}), 5)
        for clip in result['clips']:
            self.assertEqual(bridge.validate_midi(Path(clip['path'])), clip['notes'])
            mid = MidiFile(clip['path'])
            for track in mid.tracks:
                tick = 0
                for msg in track:
                    tick += msg.time
                    self.assertLessEqual(tick, mid.ticks_per_beat * 16)
                    if msg.type == 'set_tempo':
                        self.assertAlmostEqual(tempo2bpm(msg.tempo), clip['bpm'], places=2)
                self.assertEqual(tick, mid.ticks_per_beat * 16)

    def test_custom_dataset_profile_can_be_analyzed_and_generated(self):
        profile = self.folder / 'custom' / 'profile.json'
        result = bridge.analyze_custom_dataset(ROOT / 'midi_dataset', profile)
        self.assertTrue(profile.is_file())
        self.assertGreater(result['files_analyzed'], 0)
        self.assertGreaterEqual(result['patterns'], 2)
        custom = bridge.generate_batch(self.folder / 'custom-batch', analysis_file=profile)
        self.assertEqual(len(custom['clips']), 5)
        self.assertTrue(all(Path(clip['path']).is_file() for clip in custom['clips']))

    def test_custom_dataset_rejects_folder_without_midi(self):
        empty = self.folder / 'empty'
        empty.mkdir()
        with self.assertRaisesRegex(ValueError, 'No MIDI files'):
            bridge.analyze_custom_dataset(empty, self.folder / 'profile.json')

    def test_bridge_matches_original_clipped_and_repeated_five_calls(self):
        g = bridge.load_generator()
        data = json.loads((ROOT / 'analysis_output/midi_analysis.json').read_text(encoding='utf-8'))
        patterns, style = g.load_patterns(data)
        random.seed(51)
        expected = []
        for i in range(5):
            notes, key, score = g.generate_best_notes(patterns, style, attempts=50)
            expected_notes = []
            # Independent construction of the requested two-bar cut/copy.
            for n in notes:
                start = round(n.start_beat * 480)
                if 0 <= start < 3840 and n.duration_beats > 0:
                    length = min(max(1, round(n.duration_beats * 480)), 3840 - start)
                    for offset in (0, 3840):
                        expected_notes.append(g.Note(n.pitch, n.velocity, (start + offset) / 480,
                                                     length / 480, n.register))
            notes = sorted(expected_notes, key=lambda n: (n.start_beat, n.pitch))
            target = self.folder / f'original-{i}.mid'
            g.write_midi(notes, target, bpm=style['bpm'])
            expected.append(target.read_bytes())
        random.seed(51)
        result = bridge.generate_batch(self.folder / 'batch')
        self.assertEqual(expected, [Path(c['path']).read_bytes() for c in result['clips']])

    def test_two_bar_seam_is_clipped_and_internal_bar_crossing_preserved(self):
        g = bridge.load_generator()
        source = [g.Note(60, 90, 3.5, 2, 'mid'), g.Note(64, 100, 7.5, 2, 'mid'),
                  g.Note(67, 80, 8, 1, 'mid')]
        result = bridge.repeat_two_bar_phrase(source)
        self.assertEqual([(n.pitch, n.velocity, n.start_beat, n.duration_beats) for n in result],
                         [(60, 90, 3.5, 2), (64, 100, 7.5, 0.5),
                          (60, 90, 11.5, 2), (64, 100, 15.5, 0.5)])
        self.assertEqual(source[1].duration_beats, 2)  # No mutation of generator results.

    def test_serialized_halves_are_identical_and_do_not_cross_seam(self):
        from collections import defaultdict, deque
        for clip in bridge.generate_batch(self.folder / 'repeated')['clips']:
            midi = MidiFile(clip['path'])
            notes = []
            for track in midi.tracks:
                active, tick = defaultdict(deque), 0
                for msg in track:
                    tick += msg.time
                    if msg.type == 'note_on' and msg.velocity:
                        active[msg.channel, msg.note].append((tick, msg.velocity))
                    elif msg.type == 'note_off' or (msg.type == 'note_on' and not msg.velocity):
                        start, velocity = active[msg.channel, msg.note].popleft()
                        notes.append((start, tick - start, msg.note, velocity))
            half = midi.ticks_per_beat * 8
            first = sorted(n for n in notes if n[0] < half)
            second = sorted((s-half, d, p, v) for s,d,p,v in notes if s >= half)
            self.assertTrue(first)
            self.assertEqual(first, second)
            self.assertTrue(all(s + d <= half for s,d,_,_ in first))

    def test_repeated_generation_keeps_separate_batches(self):
        first = bridge.generate_batch(self.folder / 'first')
        saved = [Path(c['path']).read_bytes() for c in first['clips']]
        for i in range(4):
            result = bridge.generate_batch(self.folder / f'next-{i}')
            self.assertEqual(len(result['clips']), 5)
            self.assertNotEqual(saved, [Path(c['path']).read_bytes() for c in result['clips']])
        self.assertEqual(saved, [Path(c['path']).read_bytes() for c in first['clips']])

    def test_partial_failure_removes_only_new_batch_and_keeps_previous(self):
        result = bridge.generate_batch(self.folder / 'valid')
        saved = [Path(c['path']).read_bytes() for c in result['clips']]
        generator = bridge.load_generator()
        original = generator.generate_best_notes
        calls = 0
        def fail_on_third(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 3:
                raise RuntimeError('injected failure')
            return original(*args, **kwargs)
        with patch.object(generator, 'generate_best_notes', side_effect=fail_on_third):
            with self.assertRaisesRegex(RuntimeError, 'injected'):
                bridge.generate_batch(self.folder / 'failed')
        self.assertFalse((self.folder / 'failed').exists())
        self.assertEqual(saved, [Path(c['path']).read_bytes() for c in result['clips']])
        self.assertEqual(len(bridge.generate_batch(self.folder / 'retry')['clips']), 5)

    def test_existing_directory_is_never_overwritten(self):
        result = bridge.generate_batch(self.folder / 'valid')
        saved = [Path(c['path']).read_bytes() for c in result['clips']]
        with self.assertRaises(FileExistsError):
            bridge.generate_batch(self.folder / 'valid')
        self.assertEqual(saved, [Path(c['path']).read_bytes() for c in result['clips']])

    def test_forced_key_is_reported_and_all_notes_stay_in_its_scale(self):
        target_key = 'F# minor'
        result = bridge.generate_batch(self.folder / 'forced-key', target_key=target_key)
        generator = bridge.load_generator()
        root, mode = generator.parse_key(target_key)
        self.assertEqual([clip['key'] for clip in result['clips']], [target_key] * 5)
        for clip in result['clips']:
            midi = MidiFile(clip['path'])
            for track in midi.tracks:
                for message in track:
                    if message.type == 'note_on' and message.velocity:
                        self.assertTrue(generator.pitch_in_key(message.note, root, mode))

    def test_invalid_target_key_is_rejected_without_creating_a_batch(self):
        with self.assertRaisesRegex(ValueError, 'valid key'):
            bridge.generate_batch(self.folder / 'invalid', target_key='F sharp minor')
        self.assertFalse((self.folder / 'invalid').exists())

    def test_missing_analysis_creates_no_partial_batch(self):
        with self.assertRaises(FileNotFoundError):
            bridge.generate_batch(self.folder / 'batch', self.folder / 'missing.json')
        self.assertFalse((self.folder / 'batch').exists())

    def test_cli_from_arbitrary_working_directory_and_unicode_path(self):
        target = self.folder / 'пять MIDI'
        result = subprocess.run([sys.executable, '-B', str(BRIDGE), '--output-dir', str(target)],
                                cwd=self.folder, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(result.stdout)['clips']), 5)
        failure = subprocess.run([sys.executable, '-B', str(BRIDGE), '--output-dir', str(target)],
                                 cwd=self.folder, capture_output=True, text=True, timeout=30)
        self.assertEqual(failure.returncode, 1)
        self.assertFalse(json.loads(failure.stdout)['ok'])

    def test_dataset_preserved(self):
        expected = {
            'analysis_output/midi_analysis.json': '50f96bcb87f004362ef1314bb062b359512a0368aa196d36629fae718772324e',
        }
        for name, digest in expected.items():
            self.assertEqual(hashlib.sha256((ROOT / name).read_bytes()).hexdigest(), digest)


if __name__ == '__main__':
    unittest.main()
