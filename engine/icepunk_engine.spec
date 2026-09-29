# Explicit allowlist: no project-wide data glob, MIDI library, tests or old builds.
from pathlib import Path
import os

root = Path(SPECPATH).parent
a = Analysis(
    [str(root / 'engine/entrypoint.py')],
    pathex=[str(root / 'prototype/bridge')],
    binaries=[],
    datas=[(str(root / 'icepunk_midi_generator.py'), 'resources'),
           (str(root / 'analysis_output/midi_analysis.json'), 'resources/analysis_output')],
    hiddenimports=['mido'],
    hookspath=[], runtime_hooks=[],
    excludes=['tkinter', 'unittest', 'pytest', 'pip', 'setuptools'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='icepunk-engine',
          debug=False, strip=False, upx=False, console=True,
          target_arch=os.environ.get('ICEPUNK_TARGET_ARCH') or None,
          codesign_identity=os.environ.get('MAC_APPLICATION_IDENTITY') or None,
          entitlements_file=str(root / 'installer/macos/engine.entitlements.plist') if os.sys.platform == 'darwin' else None)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='icepunk-engine')
