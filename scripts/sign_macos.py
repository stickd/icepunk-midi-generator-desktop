"""Sign nested native code inside-out; run only on a real Mac with an identity."""
import argparse
from pathlib import Path
import subprocess
import sys

def main():
    if sys.platform != 'darwin':
        raise SystemExit('codesign requires macOS')
    p = argparse.ArgumentParser()
    p.add_argument('bundle', type=Path)
    p.add_argument('--identity', required=True)
    args = p.parse_args()
    bundle = args.bundle.resolve()
    if not bundle.is_dir() or bundle.suffix not in {'.app', '.vst3'}:
        raise SystemExit('Expected an existing .app or .vst3 bundle')
    entitlements = Path(__file__).resolve().parents[1] / 'installer/macos/engine.entitlements.plist'
    def sign(path, engine=False):
        cmd = ['codesign', '--force', '--timestamp', '--options', 'runtime', '--sign', args.identity]
        if engine:
            cmd += ['--entitlements', str(entitlements)]
        subprocess.run(cmd + [str(path)], check=True)
    files = sorted(bundle.rglob('*'), key=lambda f: len(f.parts), reverse=True)
    for file in files:
        if file.is_file() and not file.is_symlink():
            kind = subprocess.check_output(['/usr/bin/file', '-b', str(file)], text=True)
            if 'Mach-O' in kind:
                sign(file, engine=file.name == 'icepunk-engine')
    for file in files:
        if file.is_dir() and not file.is_symlink() and file.suffix in {'.framework', '.app', '.xpc'}:
            sign(file)
    sign(bundle)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(bundle)], check=True)

if __name__ == '__main__':
    main()
