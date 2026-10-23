"""Cross-language integration: actual C++ preset IO -> unchanged Python generator.

CTest supplies ICEPUNK_PRESET_TEST_BIN. Direct Python discovery skips this one
integration case if the native test executable has not yet been built.
"""
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from test_bridge import bridge, ROOT


class PresetIntegrationTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('ICEPUNK_PRESET_TEST_BIN'), 'Build PresetTests and run ctest for native preset integration')
    def test_real_analysis_roundtrip_and_generation_without_dataset(self):
        with tempfile.TemporaryDirectory(prefix='icepunk-preset-integration-') as tmp:
            root = Path(tmp)
            dataset = root / 'dataset'
            dataset.mkdir()
            source = next((ROOT / 'midi_dataset').glob('*.mid'))
            shutil.copy2(source, dataset / source.name)
            profile = root / 'profile.json'
            bridge.analyze_custom_dataset(dataset, profile)
            original = json.loads(profile.read_text(encoding='utf-8'))
            subprocess.run([os.environ['ICEPUNK_PRESET_TEST_BIN'], str(profile), str(root / 'portable')], check=True, capture_output=True, text=True)
            preset = json.loads((root / 'portable/real-preset.json').read_text(encoding='utf-8'))
            restored = json.loads((root / 'portable/loaded-analysis.json').read_text(encoding='utf-8'))
            self.assertEqual(preset['schemaVersion'], 1)
            self.assertEqual(original, preset['analysis'])
            self.assertEqual(original, restored)
            g = bridge.load_generator()
            self.assertEqual(g.load_patterns(original), g.load_patterns(restored))
            random.seed(523)
            before = bridge.generate_batch(root / 'before', analysis_file=profile)
            shutil.rmtree(dataset)
            profile.unlink()
            relocated = root / 'another-computer'
            shutil.move(str(root / 'portable'), relocated)
            # Loading/generation must never invoke the analyzer.
            with patch.object(bridge, 'load_analyzer', side_effect=AssertionError('Preset reanalyzed MIDI')):
                random.seed(523)
                after = bridge.generate_batch(root / 'after', analysis_file=relocated / 'loaded-analysis.json')
            self.assertEqual(len(after['clips']), 5)
            for first, second in zip(before['clips'], after['clips']):
                self.assertEqual(Path(first['path']).read_bytes(), Path(second['path']).read_bytes())
                bridge.validate_midi(Path(second['path']))


if __name__ == '__main__':
    unittest.main()
