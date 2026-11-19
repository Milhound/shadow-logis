#!/usr/bin/env python3
"""Verify staged Linux contents and runtime without opening a user's workspace."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

def verify(root):
    root = Path(root).resolve()
    required = {'bin/shadow-logis', 'share/applications/shadow-logis.desktop',
                'share/icons/hicolor/scalable/apps/shadow-logis.svg',
                'share/shadow-logis/README-linux.txt',
                'share/shadow-logis/licenses/LICENSE', 'share/shadow-logis/licenses/NOTICE.txt',
                'share/shadow-logis/licenses/Apache-2.0.txt', 'share/shadow-logis/licenses/Inter-OFL.txt',
                'share/shadow-logis/licenses/reference-data.txt'}
    found = set()
    for path in root.rglob('*'):
        if path.is_symlink():
            raise ValueError('Unexpected package symlink: ' + str(path.relative_to(root)))
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if relative not in required:
            raise ValueError('Unexpected package file: ' + relative)
        found.add(relative)
    if found != required:
        raise ValueError('Missing package files: ' + ', '.join(sorted(required - found)))
    with tempfile.TemporaryDirectory(prefix='shadow-logis-linux-') as temporary:
        env = {k: v for k, v in os.environ.items() if not k.startswith(('QT_', 'QML_', 'LD_'))}
        env.update(HOME=temporary, XDG_CONFIG_HOME=temporary, XDG_DATA_HOME=temporary,
                   QT_QPA_PLATFORM='offscreen')
        result = subprocess.run([str(root/'bin/shadow-logis'), '--self-test'], cwd=temporary,
                                env=env, capture_output=True, text=True, timeout=45)
        if result.returncode or 'PASS: application runtime' not in result.stdout:
            raise ValueError('Staged runtime failed: '+result.stdout+'\n'+result.stderr)
    print('PASS: staged Linux runtime, exact package allowlist, fonts, PDF, SQLite and license files.')

if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('Usage: verify_linux_package.py <staged-prefix>')
        verify(sys.argv[1])
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
