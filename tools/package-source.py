#!/usr/bin/env python3
"""Package only reviewable source and documentation, never operational data."""
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
from source_manifest import source_files

root = Path(__file__).resolve().parents[1]
files = source_files(root)
output = root / "dist/shadow-logis-source.zip"
output.parent.mkdir(exist_ok=True)
with ZipFile(output, "w", ZIP_DEFLATED) as archive:
    for path in sorted(files):
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"Source must be a regular file: {path.name}")
        archive.write(path, "shadow-logis/" + path.relative_to(root).as_posix())
print(f"Packaged {len(files)} source files: {output}")
