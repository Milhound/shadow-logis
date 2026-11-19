"""One allowlist for source packaging and publication checks."""
from pathlib import Path


def source_files(root: Path):
    files = [root / name for name in (
        'CMakeLists.txt', 'Makefile', 'README.md', 'AGENTS.md', 'LICENSE', '.gitignore', 'review/REVIEW.txt',
        'native/forms/README.txt',
        'Formula/shadow-logis.rb',
        'native/macos/Info.plist.in',
        'native/windows/app.rc.in',
        '.github/workflows/macos.yml',
        '.github/workflows/windows.yml', '.github/workflows/linux.yml',
        'native/linux/README.txt', 'native/linux/CPackOptions.cmake', 'native/linux/shadow-logis.desktop',
        'native/linux/PKGBUILD.in', 'native/linux/makepkg.conf',
        'native/linux/Dockerfile.packages',
        'native/branding/mark.svg', 'native/branding/app-icon.svg',
        'native/branding/app-icon.png', 'native/branding/app-icon.icns',
        'native/branding/app-icon.ico',
        'native/branding/README.txt',
        'native/reference/airlines.json', 'native/reference/README.txt',
        'native/fonts/InterVariable.ttf', 'native/fonts/InterVariable-Italic.ttf',
        'native/fonts/OFL.txt', 'native/fonts/README.txt')]
    for folder, patterns in {'native': ('*.cpp', '*.h'), 'tests': ('*.cpp', '*.py', '*.h'),
                             'tools': ('*.py', '*.sh', '*.ps1', '*.cpp'), 'third_party': ('*.txt',)}.items():
        for pattern in patterns: files.extend((root / folder).glob(pattern))
    for path in files:
        if path.is_symlink() or not path.is_file():
            raise RuntimeError('Source must be a regular file: '+str(path.relative_to(root)))
    return sorted(set(files))
