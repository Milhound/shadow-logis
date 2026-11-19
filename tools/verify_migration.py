#!/usr/bin/env python3
"""Independently verify source preservation and imported counts without printing records."""
import argparse
import hashlib
import json
from pathlib import Path
import sqlite3


def verify(source: Path, database: Path) -> dict:
    db = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True)
    assert db.execute("PRAGMA integrity_check").fetchall() == [("ok",)], "SQLite integrity failure"
    assert not db.execute("PRAGMA foreign_key_check").fetchall(), "Foreign key failure"
    expected = {p.relative_to(source).as_posix(): p for p in source.rglob("*")
                if p.is_file() and not p.is_symlink() and p.suffix.lower() in {".tps", ".pdf"}}
    found = set()
    for query in ("SELECT path,sha256,original FROM source_files", "SELECT path,sha256,pdf FROM documents"):
        for name, sha, blob in db.execute(query):
            assert name in expected and name not in found, f"Unexpected source path: {name}"
            digest = hashlib.sha256(blob).hexdigest()
            assert digest == sha == hashlib.sha256(expected[name].read_bytes()).hexdigest(), f"Source mismatch: {name}"
            found.add(name)
    assert set(expected) == found, "Some source files were not archived"
    rows = 0
    for name, table, schema, imported in db.execute("SELECT source,table_id,schema_json,imported_rows FROM datasets"):
        length = json.loads(schema)["record_length"]
        count, wrong = db.execute("SELECT count(*),sum(length(raw_record) != ?) FROM records WHERE source=? AND table_id=? AND legacy_id IS NOT NULL",
                                  (length, name, table)).fetchone()
        assert count == imported and not wrong, f"Record extent/count mismatch: {name}"
        rows += count
    report = json.loads(db.execute("SELECT value FROM metadata WHERE key='migration_report'").fetchone()[0])
    assert report["records"] == rows, "Migration report count differs"
    assert db.execute("SELECT count(*) FROM memos").fetchone()[0] == report["memos"], "Memo count differs"
    assert db.execute("SELECT count(*) FROM source_files WHERE status='decoded'").fetchone()[0] == report["decoded_tps"]
    assert db.execute("SELECT count(*) FROM source_files WHERE status='archived-only'").fetchone()[0] == report["archived_only_tps"]
    assert db.execute("SELECT count(*) FROM documents").fetchone()[0] == report["historical_pdfs"]
    assert db.execute("SELECT count(*) FROM records WHERE NOT json_valid(values_json)").fetchone()[0] == 0
    db.close()
    return {"verified_archived_files": len(found), "verified_imported_records": rows,
            "verified_memos": report["memos"], "sqlite_integrity": "ok", "source_hashes": "match",
            "archived_only_tps": report["archived_only_tps"],
            "scope": "Preservation and storage checks; not original-application semantic parity"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("database", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.source, args.database), indent=2))
