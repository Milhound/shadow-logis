"""Exercise document rendering with only the declared Linux font dependency."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape


def main():
    families = {'Liberation Serif', 'Liberation Sans', 'Liberation Mono'}
    listing = subprocess.check_output(
        ['fc-list', '--format', '%{family}\t%{file}\n'], text=True)
    found, files = set(), set()
    for line in listing.splitlines():
        names, path = line.split('\t', 1)
        matches = families.intersection(names.split(','))
        if matches:
            found.update(matches)
            files.add(Path(path))
    if found != families:
        raise RuntimeError('Missing document fonts: ' + ', '.join(sorted(families - found)))
    with tempfile.TemporaryDirectory(prefix='shadow-logis-font-test-') as directory:
        root = Path(directory)
        fonts = root / 'fonts'
        fonts.mkdir()
        for number, path in enumerate(sorted(files)):
            shutil.copyfile(path, fonts / (str(number) + path.suffix))
        config = root / 'fonts.conf'
        config.write_text('<?xml version="1.0"?><fontconfig><dir>' + escape(str(fonts))
                          + '</dir><cachedir>' + escape(str(root / 'cache'))
                          + '</cachedir></fontconfig>', encoding='utf-8')
        env = dict(os.environ, FONTCONFIG_FILE=str(config), QT_QPA_PLATFORM='offscreen',
                   LANG='C.UTF-8', LC_ALL='C.UTF-8')
        # No desktop/user fonts can mask a missing dependency or layout failure.
        for executable in sys.argv[1:3]:
            subprocess.run([executable], env=env, check=True, timeout=60)
        subprocess.run(sys.argv[3:], env=env, check=True, timeout=60)
    print('PASS: document rendering with only Liberation Serif, Sans and Mono')


if __name__ == '__main__':
    main()
