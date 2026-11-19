#!/usr/bin/env python3
"""Render our vector app icon; never read user images or shipping documents."""
from pathlib import Path
import subprocess
from PIL import Image

root = Path(__file__).resolve().parents[1]
art = root / 'native/branding'
subprocess.run(['rsvg-convert', '--width', '1024', '--height', '1024',
                '--output', str(art / 'app-icon.png'), str(art / 'app-icon.svg')], check=True)
with Image.open(art / 'app-icon.png') as icon:
    icon.save(art / 'app-icon.icns', format='ICNS')
    icon.save(art / 'app-icon.ico', format='ICO', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
print('Rendered the authored app icon as PNG, ICNS and ICO.')
