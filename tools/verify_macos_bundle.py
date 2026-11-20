#!/usr/bin/env python3
"""Check a deployed ARM64 app, then run its synthetic check away from the source.

Read-only inspection except for temporary synthetic runtime files. No contact
database is opened. Importable helpers also have portable fixture-based tests.
"""
import argparse
import os
from pathlib import Path
import plistlib
import posixpath
import re
import subprocess
import sys
import tempfile

MACHO_MAGICS = {bytes.fromhex(value) for value in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
PRIVATE_SUFFIXES = {'.pdf', '.sqlite', '.sqlite3', '.db', '.tps', '.csv', '.tsv', '.xlsx', '.sql'}


def within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def system_path(path):
    path = posixpath.normpath(str(path))
    return path in ('/System/Library', '/usr/lib') or path.startswith(('/System/Library/', '/usr/lib/'))


def dependencies(output):
    return [line.strip().split(' (compatibility version', 1)[0]
            for line in output.splitlines() if line.startswith('\t')]


def rpaths(output):
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset \d+\)', output)


def expand_path(path, binary, executable):
    for prefix, base in (('@loader_path', binary.parent), ('@executable_path', executable.parent)):
        if path == prefix or path.startswith(prefix + '/'):
            return (base / path[len(prefix):].lstrip('/')).resolve()
    return Path(path).resolve() if path.startswith('/') else None


def check_dependency(dependency, binary, executable, root, search_paths):
    if system_path(dependency):
        return
    if dependency.startswith('/'):
        raise ValueError(f'{binary.name}: non-relocatable absolute dependency: {dependency}')
    if dependency.startswith('@rpath/'):
        candidates = [path / dependency[len('@rpath/'):] for path in search_paths]
    else:
        candidate = expand_path(dependency, binary, executable)
        candidates = [candidate] if candidate else []
    if not any(within(path, root) and path.is_file() for path in candidates):
        raise ValueError(f'{binary.name}: dependency is missing or outside the app: {dependency}')


def inspect_files(root):
    binaries = []
    for path in root.rglob('*'):
        if path.is_symlink():
            if not within(path, root) or not path.exists():
                raise ValueError(f'Broken or external bundle symlink: {path.relative_to(root)}')
            continue
        if not path.is_file():
            continue
        with path.open('rb') as stream:
            header = stream.read(16)
        name = path.name.lower()
        if (path.suffix.lower() in PRIVATE_SUFFIXES or '.sqlite' in name or
                name.endswith(('.db-wal', '.db-shm', '.db-journal')) or
                header.startswith((b'%PDF-', b'SQLite format 3\0'))):
            raise ValueError(f'Operational document/database is not allowed in the app: {path.relative_to(root)}')
        if header[:4] in MACHO_MAGICS:
            binaries.append(path)
    return binaries


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs).stdout


def verify(root):
    root = root.resolve()
    executable = root / 'Contents/MacOS/shadow-logis'
    with (root / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    if (info.get('CFBundleExecutable') != executable.name or
            info.get('CFBundleName') != 'Shadow Logis' or
            not info.get('CFBundleShortVersionString') or not info.get('CFBundleVersion') or
            info.get('CFBundleIconFile') != 'app-icon.icns' or
            not re.fullmatch(r'\d+\.\d+(?:\.\d+)?', info.get('LSMinimumSystemVersion', ''))):
        raise ValueError('App metadata, version or icon is missing.')
    for name in ('app-icon.icns', 'licenses/LICENSE', 'licenses/NOTICE.txt', 'licenses/Apache-2.0.txt',
                 'licenses/reference-data.txt', 'licenses/Inter-OFL.txt'):
        if not (root / 'Contents/Resources' / name).is_file():
            raise ValueError(f'Missing bundle resource: {name}')
    binaries = inspect_files(root)
    if executable not in binaries:
        raise ValueError('App executable is missing or not Mach-O.')
    for plugin in ('libqcocoa.dylib', 'libqsqlite.dylib'):
        if not any(path.name == plugin for path in binaries):
            raise ValueError(f'Missing runtime plugin: {plugin}')
    for library in ('QtPdf', 'QtPrintSupport'):
        if not any(path.name == library or path.name.startswith('lib' + library.replace('Qt', 'Qt6', 1))
                   for path in binaries):
            raise ValueError(f'Missing runtime framework: {library}')
    # The executable's runpaths are inherited by its dependencies. Each library
    # may additionally declare loader-relative paths of its own.
    executable_rpaths = rpaths(run('/usr/bin/otool', '-arch', 'arm64', '-l', str(executable)))
    for binary in binaries:
        archs = run('/usr/bin/lipo', '-archs', str(binary)).split()
        if 'arm64' not in archs:
            raise ValueError(f'No arm64 slice: {binary.relative_to(root)}')
        search_paths = []
        paths = [(path, executable) for path in executable_rpaths]
        paths += [(path, binary) for path in rpaths(run('/usr/bin/otool', '-arch', 'arm64', '-l', str(binary)))]
        for path, owner in paths:
            expanded = expand_path(path, owner, executable)
            if (expanded is None or
                    (path.startswith('/') and not system_path(path)) or
                    (not within(expanded, root) and not system_path(expanded))):
                raise ValueError(f'{binary.name}: external or unsupported runtime search path: {path}')
            search_paths.append(expanded)
        for dependency in dependencies(run('/usr/bin/otool', '-arch', 'arm64', '-L', str(binary))):
            check_dependency(dependency, binary, executable, root, search_paths)
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', '--verbose=2', str(root))
    with tempfile.TemporaryDirectory(prefix='shadow-logis-bundle-') as temporary:
        work = Path(temporary)
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(('QT_', 'QML_', 'DYLD_', 'LD_'))}
        env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', QT_QPA_PLATFORM='cocoa', HOME=str(work),
                   XDG_CONFIG_HOME=str(work / 'config'), XDG_DATA_HOME=str(work / 'data'),
                   XDG_CACHE_HOME=str(work / 'cache'))
        result = run(str(executable), '--self-test', cwd=work, env=env, timeout=60)
        if 'PASS: application runtime (cocoa)' not in result:
            raise ValueError('Packaged app did not complete its native Cocoa runtime check.')
        print(result.strip())
    print(f'PASS: {len(binaries)} ARM64 binaries, internal dependencies, native plugins, resources and signature.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Run bundle verification on macOS; parser tests are portable.')
    try:
        verify(args.app)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Bundle check failed: {error}', file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stdout or '', error.stderr or '', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
