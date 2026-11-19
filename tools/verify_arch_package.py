#!/usr/bin/env python3
"""Read an Arch package with pacman and validate its extracted payload without installing."""
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
from verify_linux_package import verify

def verify_arch(path):
    path = Path(path).resolve()
    subprocess.run(['pacman', '-Qip', str(path)], check=True)
    entries = subprocess.check_output(['bsdtar', '-tf', str(path)], text=True).splitlines()
    metadata = {'.PKGINFO', '.BUILDINFO', '.MTREE'}
    for name in entries:
        parts = PurePosixPath(name).parts
        if not parts or name.startswith('/') or '..' in parts:
            raise ValueError('Unsafe archive path: ' + name)
        if parts[0] != 'usr' and name not in metadata:
            raise ValueError('Unexpected package entry: ' + name)
    if not metadata.issubset(entries):
        raise ValueError('Missing Arch package metadata')
    with tempfile.TemporaryDirectory(prefix='shadow-logis-arch-') as directory:
        subprocess.run(['bsdtar', '--no-same-owner', '-xf', str(path), '-C', directory], check=True)
        verify(Path(directory)/'usr')
    print('PASS: pacman package metadata and extracted Arch payload/runtime.')

if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('Usage: verify_arch_package.py <package.pkg.tar.zst>')
        verify_arch(sys.argv[1])
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
