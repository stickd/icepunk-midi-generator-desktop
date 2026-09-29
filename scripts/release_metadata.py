"""Record content hashes and third-party notices, never developer filesystem paths."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import platform
import sys

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--juce', type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    stage = args.stage.resolve()
    notices = stage / 'licenses'
    notices.mkdir(exist_ok=True)
    for package in ['mido', 'packaging', 'pyinstaller']:
        dist = importlib.metadata.distribution(package)
        for path in dist.files or []:
            if path.name.lower().startswith(('license', 'copying')):
                source = Path(dist.locate_file(path))
                if source.is_file():
                    (notices / f'{package}-{path.name}').write_bytes(source.read_bytes())
    python_license = Path(sys.base_prefix) / 'LICENSE.txt'
    if python_license.is_file():
        (notices / 'Python-LICENSE.txt').write_bytes(python_license.read_bytes())
    juce_license = args.juce / 'LICENSE.md'
    if not juce_license.exists():
        raise SystemExit('JUCE license file not found')
    (notices / 'JUCE-LICENSE.md').write_bytes(juce_license.read_bytes())
    manifest = {
        'version': (root / 'VERSION').read_text().strip(),
        'platform': platform.system(), 'architecture': platform.machine(),
        'python': platform.python_version(), 'juce': '8.0.15',
        'packages': {name: importlib.metadata.version(name) for name in ['pyinstaller', 'mido', 'packaging']},
        'source_sha256': {str(p.relative_to(root)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in [root / 'icepunk_midi_generator.py', root / 'analysis_output/midi_analysis.json',
                                    root / 'prototype/bridge/generate.py']},
        'vst3_included': (stage / 'vst3').exists(),
    }
    (stage / 'BUILD_INFO.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    (stage / 'VERSION').write_text(manifest['version'] + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
