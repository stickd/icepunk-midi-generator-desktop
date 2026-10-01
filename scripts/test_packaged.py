"""Exercise a relocated frozen engine and GUI with system Python paths disabled."""
import argparse
from collections import defaultdict, deque
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from mido import MidiFile

def verify_midi(file):
    midi = MidiFile(file)
    notes = []
    for track in midi.tracks:
        time = 0
        active = defaultdict(deque)
        for msg in track:
            time += msg.time
            assert time <= 16 * midi.ticks_per_beat
            if msg.type == 'note_on' and msg.velocity:
                active[msg.channel, msg.note].append((time, msg.velocity))
            elif msg.type == 'note_off' or (msg.type == 'note_on' and not msg.velocity):
                start, velocity = active[msg.channel, msg.note].popleft()
                notes.append((start, time - start, msg.note, velocity))
        assert not any(active.values())
        assert time == 16 * midi.ticks_per_beat
    half = 8 * midi.ticks_per_beat
    first = sorted(n for n in notes if n[0] < half)
    second = sorted((s-half,d,p,v) for s,d,p,v in notes if s >= half)
    assert first and first == second
    assert all(s+d <= half for s,d,_,_ in first)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='icepunk-release-') as temp:
        base = Path(temp)
        relocated = base / 'Moved install проба'  # Spaces and Unicode deliberately included.
        shutil.copytree(args.stage, relocated)
        if sys.platform == 'darwin':
            app = relocated / 'ICEPUNK.app'
            resource = app / 'Contents/Resources'
            gui = app / 'Contents/MacOS/ICEPUNK'
        else:
            resource = relocated
            gui = relocated / 'ICEPUNK.exe'
        engine = resource / 'engine' / ('icepunk-engine.exe' if os.name == 'nt' else 'icepunk-engine')
        env = os.environ.copy()
        env['PATH'] = str(Path(env.get('SystemRoot', 'C:/Windows')) / 'System32') if os.name == 'nt' else '/usr/bin:/bin'
        for name in ['PYTHONHOME', 'PYTHONPATH', 'VIRTUAL_ENV', 'ICEPUNK_PYTHON']:
            env[name] = str(base / 'nonexistent-python')
        env['PYTHONNOUSERSITE'] = '1'
        env['ICEPUNK_DATA_DIR'] = str(base / 'user-data')
        def run_engine(*arguments, expected=0):
            p = subprocess.run([str(engine), *map(str, arguments)], cwd=base, env=env,
                               capture_output=True, text=True, encoding='utf-8', timeout=60)
            assert p.returncode == expected, p.stderr + p.stdout
            return json.loads(p.stdout)
        diagnostics = run_engine('--diagnostics')
        assert diagnostics['frozen'] is True
        for path in diagnostics['sys_path'] + [diagnostics['mido'], diagnostics['resources'], diagnostics['prefix']]:
            assert Path(path).resolve().is_relative_to(resource.resolve()), path
        root = Path(__file__).resolve().parents[1]
        assert diagnostics['generator_sha256'] == hashlib.sha256((root / 'icepunk_midi_generator.py').read_bytes()).hexdigest()
        assert diagnostics['dataset_sha256'] == hashlib.sha256((root / 'analysis_output/midi_analysis.json').read_bytes()).hexdigest()
        hashes = []
        for i in range(3):
            batch = base / f'batch-{i}'
            result = run_engine('--output-dir', batch)
            assert result['ok'] and len(result['clips']) == 5
            assert len(list(batch.glob('*.mid'))) == 5
            for clip in result['clips']:
                verify_midi(Path(clip['path']))
            hashes.append([hashlib.sha256(Path(c['path']).read_bytes()).hexdigest() for c in result['clips']])
            assert len(set(hashes[-1])) == 5
        assert hashes[0] != hashes[1]
        profile = base / 'custom-profile' / 'profile.json'
        custom = run_engine('--analyze-dir', base / 'batch-0', '--profile-file', profile)
        assert custom['ok'] and custom['files_analyzed'] == 5
        result = run_engine('--output-dir', base / 'custom-batch', '--analysis-file', profile)
        assert result['ok'] and len(result['clips']) == 5
        for clip in result['clips']:
            verify_midi(Path(clip['path']))
        failure = run_engine('--output-dir', base / 'batch-0', expected=1)
        assert not failure['ok'] and 'Traceback' not in failure['error']
        assert hashes[0] == [hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((base/'batch-0').glob('*.mid'))]
        # Real JUCE Generate path, not just engine command-line execution.
        gui_report = base / 'gui-test'
        completed = subprocess.run([str(gui), '--self-test', str(gui_report)], env=env, cwd=base, timeout=120,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        assert completed.returncode == 0
        native = (gui_report / 'native-smoke-test.txt').read_text()
        assert native.startswith('PASS:'), native
        for midi in gui_report.glob('*.mid'):
            verify_midi(midi)
        logs = base / 'user-data/logs'
        assert (logs / 'engine.log').is_file() and (logs / 'app.log').is_file()
        assert not list((base / 'user-data/sessions').glob('session-*'))
        # Bundle resources must never be modified by execution.
        def file_hashes(folder):
            return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in folder.rglob('*') if p.is_file()}
        assert file_hashes(args.stage) == file_hashes(relocated), 'Runtime wrote into the installation directory'
        summary = {'passed': True, 'frozen': True, 'relocation_and_unicode_paths': True,
                   'system_python_disabled': True, 'engine_batches': 3, 'files_per_batch': 5,
                   'all_midis_16_beats': True, 'two_halves_identical': True, 'native_gui': native.strip(),
                   'logs_written': True, 'session_cleanup': True, 'install_tree_unchanged': True,
                   'custom_dataset_analyze_and_generate': True, 'clean_vm_tested': False}
        args.report.write_text(json.dumps(summary, indent=2), encoding='utf-8')
        shutil.copyfile(gui_report / 'ICEPUNK-preview.png', args.report.with_suffix('.png'))
        print(json.dumps(summary, indent=2))

if __name__ == '__main__':
    main()
