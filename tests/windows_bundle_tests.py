#!/usr/bin/env python3
"""Portable Windows packaging/privacy regressions, no private input data."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_windows_bundle import dependency_name, inspect_files, resolve_dependency


class WindowsBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'Example Package'
        self.bin = self.root / 'bin'
        self.system = Path(self.temp.name) / 'System32'
        self.bin.mkdir(parents=True)
        self.system.mkdir()
        self.exe = self.bin / 'shadow-logis.exe'
        self.exe.write_bytes(b'MZ-synthetic-fixture')

    def test_dll_resolution_and_api_contracts(self):
        qt = self.bin / 'Qt6Core.dll'; qt.write_bytes(b'MZ-qt-fixture')
        kernel = self.system / 'KERNEL32.dll'; kernel.write_bytes(b'MZ-system-fixture')
        self.assertEqual(resolve_dependency(b'QT6CORE.dll', self.exe, self.bin, self.system), qt)
        self.assertEqual(resolve_dependency('kernel32.dll', self.exe, self.bin, self.system), kernel)
        self.assertIsNone(resolve_dependency('api-ms-win-core-file-l1-1-0.dll', self.exe, self.bin, self.system))
        spooler = self.system / 'WINSPOOL.DRV'
        spooler.write_bytes(b'MZ-system-fixture')
        self.assertEqual(resolve_dependency(b'WINSPOOL.DRV', self.exe, self.bin, self.system), spooler)
        with self.assertRaises(ValueError):
            resolve_dependency('MissingQt.dll', self.exe, self.bin, self.system)

    def test_untrusted_import_paths_rejected(self):
        for name in ('../bad.dll', 'C:\\Qt\\bad.dll', 'bad/Qt.dll', 'other.exe', 'other.drv', 'bad.dll\0'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                dependency_name(name)

    def test_private_files_and_compiler_dlls_rejected(self):
        for name, content in (('contacts.csv', b'name'), ('shipment.TPS', b'tps'),
                              ('record.db-shm', b'shm'), ('hidden.dat', b'%PDF-1.4'),
                              ('data.bin', b'SQLite format 3\0'), ('vcruntime140.dll', b'MZ-runtime')):
            p = self.bin / name; p.write_bytes(content)
            with self.subTest(name=name), self.assertRaises(ValueError):
                inspect_files(self.root)
            p.unlink()
        self.assertEqual(inspect_files(self.root), [self.exe])

    def test_invalid_binary_header_rejected(self):
        self.exe.write_bytes(b'not an executable')
        with self.assertRaises(ValueError):
            inspect_files(self.root)


if __name__ == '__main__':
    unittest.main()
