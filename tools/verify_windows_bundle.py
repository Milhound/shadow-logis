#!/usr/bin/env python3
"""Validate an x64 Windows package and run synthetic GUI/CLI checks in isolation.

pefile is needed only for the real Windows inspection, not portable helper tests.
No SDK/MSVC runtime DLLs are redistributed; the official VC++ runtime is a
prerequisite on the target machine. This script never opens a shipping database.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

PRIVATE_SUFFIXES = {'.pdf', '.sqlite', '.sqlite3', '.db', '.tps', '.csv', '.tsv', '.xlsx', '.sql'}
MSVC_RUNTIME = re.compile(r'^(?:vcruntime|msvcp|msvcr|concrt|vcomp)\d+.*\.dll$', re.I)


def inspect_files(root):
    binaries = []
    for path in root.rglob('*'):
        if path.is_symlink() or getattr(path.lstat(), 'st_file_attributes', 0) & 0x400:
            raise ValueError(f'Package links are not supported: {path.relative_to(root)}')
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            header = stream.read(16)
        name = path.name.lower()
        if (path.suffix.lower() in PRIVATE_SUFFIXES or '.sqlite' in name or
                name.endswith(('.db-wal', '.db-shm', '.db-journal')) or
                header.startswith((b'%PDF-', b'SQLite format 3\0'))):
            raise ValueError(f'Operational file is not allowed in the package: {path.relative_to(root)}')
        if MSVC_RUNTIME.fullmatch(name):
            raise ValueError('Use the official Microsoft redistributable instead of bundling ' + name)
        if path.suffix.lower() in ('.exe', '.dll'):
            if not header.startswith(b'MZ'):
                raise ValueError('Invalid executable header: ' + name)
            binaries.append(path)
    return binaries


def dependency_name(raw):
    name = raw.decode('ascii').lower() if isinstance(raw, bytes) else raw.lower()
    if not re.fullmatch(r'[a-z0-9_.+-]+\.dll', name) or '..' in name:
        raise ValueError('Invalid DLL import name: ' + name)
    return name


def resolve_dependency(name, binary, app_bin, system32, cache=None):
    name = dependency_name(name)
    if name.startswith(('api-ms-win-', 'ext-ms-win-')):
        return None  # Windows API-set contracts are resolved by the OS loader.
    for directory in (binary.parent, app_bin, system32):
        # Case-insensitive on Windows and also in portable fixture tests.
        if cache is None or directory not in cache:
            entries = {p.name.lower(): p for p in directory.iterdir() if p.is_file()}
            if cache is not None:
                cache[directory] = entries
        else:
            entries = cache[directory]
        if name in entries:
            return entries[name]
    raise ValueError(f'{binary.name}: missing DLL {name}; no developer PATH fallback is allowed.')


def read_pe(path):
    import pefile
    with pefile.PE(str(path), fast_load=True) as pe:
        if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.Magic != 0x20b:
            raise ValueError(f'{path.name}: expected x64 PE32+ code.')
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
                                               pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT'],
                                               pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
        imports = [dependency_name(item.dll) for group in ('DIRECTORY_ENTRY_IMPORT', 'DIRECTORY_ENTRY_DELAY_IMPORT')
                   for item in getattr(pe, group, [])]
        resources = {entry.id for entry in getattr(getattr(pe, 'DIRECTORY_ENTRY_RESOURCE', None), 'entries', [])}
        return pe.OPTIONAL_HEADER.Subsystem, imports, resources


def verify(root):
    root = root.resolve()
    app_bin = root / 'bin'
    executable = app_bin / 'shadow-logis.exe'
    console = app_bin / 'shadow-logis-cli.exe'
    binaries = inspect_files(root)
    required = {'shadow-logis.exe', 'shadow-logis-cli.exe', 'qwindows.dll', 'qsqlite.dll',
                'qt6core.dll', 'qt6widgets.dll', 'qt6sql.dll', 'qt6pdf.dll', 'qt6pdfwidgets.dll', 'qt6printsupport.dll'}
    missing = required - {p.name.lower() for p in binaries}
    if missing:
        raise ValueError('Missing runtime components: ' + ', '.join(sorted(missing)))
    for name in ('LICENSE', 'NOTICE.txt', 'Apache-2.0.txt', 'reference-data.txt', 'Inter-OFL.txt'):
        if not (root / 'share/shadow-logis/licenses' / name).is_file():
            raise ValueError('Missing notice: ' + name)
    system32 = Path(os.environ['SystemRoot']) / 'System32'
    directory_cache = {}
    for binary in binaries:
        subsystem, imports, resources = read_pe(binary)
        if binary in (executable, console) and not {14, 16}.issubset(resources):
            raise ValueError(binary.name + ': application icon/version resources are missing.')
        if binary == executable and subsystem != 2:
            raise ValueError('Desktop executable must use the Windows GUI subsystem.')
        if binary == console and subsystem != 3:
            raise ValueError('CLI executable must retain console I/O.')
        for name in imports:
            resolve_dependency(name, binary, app_bin, system32, directory_cache)
    with tempfile.TemporaryDirectory(prefix='shadow-logis-windows-') as temporary:
        work = Path(temporary)
        env = {key: value for key, value in os.environ.items()
               if not key.upper().startswith(('QT_', 'QML_', 'DYLD_', 'LD_')) and
               key.upper() not in ('PATH', 'HOME', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA')}
        env.update(PATH=str(system32) + os.pathsep + os.environ['SystemRoot'], QT_QPA_PLATFORM='windows',
                   HOME=str(work), USERPROFILE=str(work), APPDATA=str(work / 'roaming'),
                   LOCALAPPDATA=str(work / 'local'))
        for app in (executable, console):
            result = subprocess.run([str(app), '--self-test'], cwd=work, env=env,
                                    capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise ValueError(f'{app.name} runtime check failed ({result.returncode}): {result.stdout}\n{result.stderr}')
            # GUI-subsystem stdout availability differs with the launching shell.
            if app == console and 'PASS: application runtime (windows)' not in result.stdout:
                raise ValueError('CLI did not complete the Windows platform runtime check.')
        print('PASS: GUI and console runtime, native Windows platform, fonts/icon, SQLite, images, PDF preview/printing.')
    print(f'PASS: {len(binaries)} x64 binaries, dependencies, notices and private-data exclusion.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Run package verification on Windows; helper tests are portable.')
    try:
        verify(args.package)
    except (OSError, ValueError, ImportError, subprocess.SubprocessError) as error:
        print(f'Windows package check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
