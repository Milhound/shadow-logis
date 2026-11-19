#!/usr/bin/env python3
"""Build the offline reference from OpenFlights CSV and public IATA member facts.

Input member JSON is an array of {name, country, iata, icao, numeric} objects.
Neither input is operational shipment data. See native/reference/README.txt.
"""
import argparse
import csv
import hashlib
import json
import re
from pathlib import Path


def build(openflights, members, retrieved):
    rows = []
    seen = set()
    aliases = {'Republic of Korea': 'South Korea', 'Vietnam': 'Viet Nam',
               'Turkey': 'Türkiye', 'Taiwan': 'Chinese Taipei', 'Czech Republic': 'Czechia',
               'Hong Kong': 'Hong Kong (SAR)', 'Macau': 'Macao (SAR)', 'Macao': 'Macao (SAR)',
               'United States of America': 'United States'}
    def clean(value):
        return '' if value in ('\\N', '-', 'N/A', 'None') else value.strip()
    for row in json.loads(members.read_text()):
        iata = clean(row['iata']).rstrip('*')
        if iata and not re.fullmatch('[A-Z0-9]{2}', iata): raise ValueError('Invalid member designator')
        icao = clean(row['icao'])
        if icao and not re.fullmatch('[A-Z0-9]{3}', icao): raise ValueError('Invalid member ICAO code')
        numeric = clean(row['numeric'])
        if numeric and not re.fullmatch('[0-9]{3}', numeric): raise ValueError('Invalid numeric code')
        key = (row['name'], iata, icao)
        if key in seen: continue
        seen.add(key)
        rows.append(dict(name=row['name'], country=row['country'], iata=iata, icao=icao,
                         numeric=numeric, member=True, controlled_duplicate=row['iata'].endswith('*')))
    member_codes = {(r['iata'], r['icao']) for r in rows}
    skipped = 0
    for row in csv.reader(openflights.read_text().splitlines()):
        if len(row) != 8: raise ValueError('Invalid OpenFlights row')
        ident, name, alias, iata, icao, callsign, country, active = map(clean, row)
        # Omit placeholders and one historical operator with the former project
        # name, to keep the publishable tree free of that name (source ID 3759).
        if ident in ('-1', '1', '3759'): skipped += 1; continue
        if not re.fullmatch('[A-Z0-9]{2}', iata): iata = ''
        if not re.fullmatch('[A-Z]{3}', icao): icao = ''
        if not (iata or icao) or not name: skipped += 1; continue
        if (iata, icao) in member_codes: skipped += 1; continue
        key = (name, iata, icao)
        if key in seen: skipped += 1; continue
        seen.add(key)
        rows.append(dict(name=name, country=aliases.get(country, country) or 'Unspecified', iata=iata,
                         icao=icao, alias=alias, callsign=callsign, member=False))
    rows.sort(key=lambda r: (r['name'].casefold(), r['country'], r['iata'], r['icao']))
    return dict(retrieved=retrieved, member_count=sum(r['member'] for r in rows),
                openflights_input_sha256=hashlib.sha256(openflights.read_bytes()).hexdigest(),
                members_input_sha256=hashlib.sha256(members.read_bytes()).hexdigest(),
                skipped_openflights_rows=skipped, airlines=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('openflights', type=Path)
    parser.add_argument('members', type=Path)
    parser.add_argument('--retrieved', required=True, help='YYYY-MM-DD retrieval date, not a guarantee of freshness')
    parser.add_argument('--output', type=Path, default=Path('native/reference/airlines.json'))
    args = parser.parse_args()
    result = build(args.openflights, args.members, args.retrieved)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    print(f"Built {len(result['airlines'])} entries, including {result['member_count']} IATA member entries")
