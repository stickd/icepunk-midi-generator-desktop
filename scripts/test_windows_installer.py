"""Isolated install/upgrade/uninstall test; refuses an existing product installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

def main():
    if sys.platform != 'win32':
        raise SystemExit('Windows only')
    import winreg
    key_name = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C6043015-6275-4E31-B695-576491B81E06}_is1'
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(hive, key_name, 0, winreg.KEY_READ | view):
                    raise SystemExit('Existing ICEPUNK product installation detected; test refused to avoid altering it.')
            except FileNotFoundError:
                pass
    p = argparse.ArgumentParser()
    p.add_argument('--installer', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--license-file', type=Path, required=True,
                   help='A valid signed authorization for this test machine; never copied into the installer')
    args = p.parse_args()
    if not args.license_file.is_file():
        raise SystemExit('A valid --license-file is required for the licensed GUI smoke test.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='icepunk-install-test-') as temp:
        root = Path(temp).resolve()
        install = root / 'Installed ICEPUNK'
        data = root / 'User data'
        data.mkdir()
        sentinel = data / 'keep-user-midi.mid'
        sentinel.write_bytes(b'user-owned-content')
        original = hashlib.sha256(sentinel.read_bytes()).hexdigest()
        env = os.environ.copy()
        env['PATH'] = str(Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32')
        env['ICEPUNK_DATA_DIR'] = str(data)
        env['PYTHONHOME'] = str(root / 'no-python')
        env['PYTHONPATH'] = str(root / 'no-python')
        def run(command):
            completed = subprocess.run(command, env=env, cwd=root, timeout=150, creationflags=subprocess.CREATE_NO_WINDOW)
            assert completed.returncode == 0, (command[0], completed.returncode)
        setup = [str(args.installer.resolve()), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
                 '/NOICONS', '/TASKS=', '/NOCLOSEAPPLICATIONS', f'/DIR={install}', f'/LOG={root / "setup.log"}']
        uninstaller = install / 'unins000.exe'
        try:
            run(setup)
            assert (install / 'ICEPUNK.exe').is_file()
            assert (install / 'engine/icepunk-engine.exe').is_file()
            assert not list(install.rglob('license.dat'))
            (data / 'licensing').mkdir()
            (data / 'licensing/license.dat').write_bytes(args.license_file.read_bytes())
            run([str(install / 'ICEPUNK.exe'), '--self-test', str(root / 'smoke')])
            assert (root / 'smoke/native-smoke-test.txt').read_text().startswith('PASS:')
            # Reinstall the same version to exercise Inno's upgrade/update path.
            run(setup)
            assert hashlib.sha256(sentinel.read_bytes()).hexdigest() == original
        finally:
            # Only execute the uninstaller inside this test's fresh private directory.
            assert uninstaller.resolve().is_relative_to(root)
            if uninstaller.is_file():
                run([str(uninstaller), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'])
        assert not (install / 'ICEPUNK.exe').exists()
        assert not (install / 'engine/icepunk-engine.exe').exists()
        assert sentinel.exists() and hashlib.sha256(sentinel.read_bytes()).hexdigest() == original
        report = {'passed': True, 'actual_installer': True, 'install': True, 'installed_gui_smoke': True,
                  'same_version_reinstall': True, 'uninstall': True, 'user_data_preserved': True,
                  'clean_vm_tested': False, 'signed': False}
        args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
