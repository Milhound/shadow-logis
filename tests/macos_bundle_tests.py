#!/usr/bin/env python3
"""Portable regressions for bundle privacy and dependency validation."""
from pathlib import Path
import sys
import tempfile
import unittest
import os

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_macos_bundle import check_dependency, dependencies, expand_path, inspect_files, rpaths


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve() / 'Example App.app'
        self.executable = self.root / 'Contents/MacOS/shadow-logis'
        self.library = self.root / 'Contents/Frameworks/QtCore.framework/Versions/A/QtCore'
        for path in (self.executable, self.library):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(bytes.fromhex('cffaedfe') + b'fixture')

    def test_otool_formats(self):
        text = '/Example App.app/main:\n\t@rpath/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0, current version 6.12.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n'
        self.assertEqual(dependencies(text), ['@rpath/QtCore.framework/Versions/A/QtCore', '/usr/lib/libSystem.B.dylib'])
        self.assertEqual(rpaths('Load command 17\n          cmd LC_RPATH\n      cmdsize 48\n         path @executable_path/../Frameworks (offset 12)\n'), ['@executable_path/../Frameworks'])

    def test_internal_and_system_dependencies(self):
        search = [self.root / 'Contents/Frameworks']
        for dependency in ('@rpath/QtCore.framework/Versions/A/QtCore', '/usr/lib/libSystem.B.dylib'):
            check_dependency(dependency, self.executable, self.executable, self.root, search)
        self.assertEqual(expand_path('@loader_path/../Frameworks', self.executable, self.executable), search[0])

    def test_external_missing_and_escaping_dependencies_rejected(self):
        for dependency in ('/opt/homebrew/lib/libprivate.dylib', '@rpath/missing.dylib',
                           '@loader_path/../../../../private.dylib', 'librelative.dylib',
                           str(self.library), '/usr/lib/../../private/lib.dylib'):
            with self.subTest(dependency=dependency), self.assertRaises(ValueError):
                check_dependency(dependency, self.executable, self.executable, self.root, [])

    def test_private_data_and_disguised_documents_rejected(self):
        for name, content in (('contacts.csv', b'name'), ('record.db-wal', b'wal'),
                              ('document.bin', b'%PDF-1.4'), ('hidden.bin', b'SQLite format 3\0'),
                              ('shipment.TPS', b'tps')):
            path = self.root / name
            path.write_bytes(content)
            with self.subTest(name=name), self.assertRaises(ValueError):
                inspect_files(self.root)
            path.unlink()

    @unittest.skipIf(os.name == 'nt', 'Mac framework symlinks need Unix semantics; Windows uses DLLs.')
    def test_internal_framework_symlinks_and_external_escape(self):
        alias = self.library.parents[2] / 'QtCore'
        alias.symlink_to('Versions/A/QtCore')
        self.assertEqual(set(inspect_files(self.root)), {self.library, self.executable})
        alias.unlink()
        alias.symlink_to(self.root.parent)
        with self.assertRaises(ValueError):
            inspect_files(self.root)


if __name__ == '__main__':
    unittest.main()
