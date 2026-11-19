#!/usr/bin/env python3
"""Read-only binary inventory. Requires Python 3 and GNU file/objdump on PATH.

Does not run target executables or read database rows/configuration secrets.
Usage: python tools/audit_legacy.py /path/to/legacy-installation --entry main.exe > private/binary-audit.json
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

WINDOWS_DLLS = {name.casefold() for name in (
    "ADVAPI32.dll COMCTL32.dll GDI32.dll KERNEL32.dll MPR.dll MSVCRT.dll "
    "OLEAUT32.dll SHELL32.dll USER32.dll WININET.dll WINMM.dll WINSPOOL.DRV "
    "WSOCK32.dll comdlg32.dll ole32.dll oledlg.dll"
).split()}


def audit(root, entry):
    binaries = {}
    for p in sorted(root.iterdir()):
        if p.suffix.casefold() not in {".exe", ".dll"} or not p.is_file():
            continue
        result = subprocess.run(["objdump", "-p", str(p)], text=True, capture_output=True)
        binaries[p.name] = {
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
            "format": subprocess.check_output(["file", "-b", str(p)], text=True).strip(),
            "imports": [line.split("DLL Name:", 1)[1].strip()
                        for line in result.stdout.splitlines() if "DLL Name:" in line],
            "imports_parsed": result.returncode == 0,
        }
    byname = {name.casefold(): name for name in binaries}
    seen = set()
    if entry.casefold() not in byname:
        raise ValueError("Entry executable was not found in the supplied installation")
    pending = [byname[entry.casefold()]]
    while pending:
        key = pending.pop()
        if key in seen:
            continue
        seen.add(key)
        pending.extend(byname[name.casefold()] for name in binaries[key]["imports"]
                       if name.casefold() in byname)
    missing = {}
    for name, binary in binaries.items():
        deps = [dep for dep in binary["imports"]
                if dep.casefold() not in byname and dep.casefold() not in WINDOWS_DLLS]
        if deps:
            missing[name] = deps
    return {
        "scope": "Root-directory EXE/DLL headers and static imports only; no runtime proof or database schema recovery.",
        "root_tps_count": len([p for p in root.iterdir() if p.suffix.casefold() == ".tps"]),
        "main_local_dependency_closure": sorted(seen),
        "unbundled_non_system_static_imports": missing,
        "binaries": binaries,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--entry", required=True, help="Main executable filename")
    args = parser.parse_args()
    print(json.dumps(audit(args.directory.resolve(), args.entry), indent=2))
