#!/usr/bin/env python3
"""Check the publishable source allowlist and, when present, Git's tracked files.

Optional --forbid-text REGEX checks source paths and decoded text for a retired
name or other supplied pattern. It does not rewrite private data or Git history.
"""
import argparse
from pathlib import Path
import re
import subprocess
from source_manifest import source_files

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--forbid-text', action='append', default=[])
args = parser.parse_args()
files = source_files(root)
allowed = {p.relative_to(root).as_posix() for p in files}
issues = []
for path in files:
    relative = path.relative_to(root).as_posix()
    data = path.read_bytes()
    # Explicit binaries: licensed fonts and our authored, generated app icon.
    binaries = {'native/fonts/InterVariable.ttf',
                'native/fonts/InterVariable-Italic.ttf',
                'native/branding/app-icon.png', 'native/branding/app-icon.icns', 'native/branding/app-icon.ico'}
    if b'\0' in data and relative not in binaries: issues.append(relative+': unexpected binary')
    for pattern in args.forbid_text:
        text = data.decode('utf-8', errors='replace')
        if re.search(pattern, relative+'\n'+text, re.I): issues.append(relative+': matches forbidden text')
    if relative.endswith('.pdf'):
        extracted = subprocess.run(['pdftotext', str(path), '-'], capture_output=True, check=True).stdout.decode()
        for pattern in args.forbid_text:
            if re.search(pattern, extracted, re.I): issues.append(relative+': extracted PDF text matches forbidden text')
git = subprocess.run(['git','-C',str(root),'rev-parse','--show-toplevel'], capture_output=True, text=True)
if git.returncode == 0:
    if Path(git.stdout.strip()).resolve() != root.resolve():
        issues.append('This folder belongs to a different Git root; check publication from that root.')
    else:
        tracked = subprocess.check_output(['git','-C',str(root),'ls-files','-z']).decode().split('\0')
        issues.extend(p+': tracked but outside the source allowlist' for p in tracked if p and p not in allowed)
else:
    print('No Git repository present: checked source files; no history was inspected.')
if issues: raise SystemExit('\n'.join(issues))
print(f'PASS: {len(files)} publishable source files; private data, local review output and installed legacy software excluded.')
