Offline reference data
======================

airlines.json contains public airline names, countries/territories and codes.
Retrieved 2026-11-18. Retrieval time is not the date each assignment was verified.
The IATA membership snapshot has 383 unique entries from 39 public result pages;
one repeated row was deduplicated. Source pages were cached at different times.
This is not the complete official register of active IATA designators.

Sources:
  https://www.iata.org/en/about/members/airline-list/
  https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat
  https://openflights.org/data
The IATA member facts provide two-character designators, ICAO codes, and the
published three-digit numeric codes. Numeric codes are displayed for reference;
they are not automatically treated as cargo prefixes or inserted into AWB numbers.
An asterisk denotes a controlled duplicate IATA designator; selection inserts
the two-character code, not the marker. Multiple carriers may share a code.

OpenFlights supplies additional, historical/unverified entries. Its activity
flag is explicitly unreliable, so it is not presented as current status.
These entries are hidden by default. Valid code shapes do not establish current
assignment. Country refers to the airline's home country, not destinations served.
Blank prefixes remain blank; none are inferred from an ICAO code or carrier name.

OpenFlights data is available under ODbL 1.0 and its individual contents under
DbCL 1.0. This derived reference database is distributed under the same licenses:
  https://opendatacommons.org/licenses/odbl/1-0/
  https://opendatacommons.org/licenses/dbcl/1-0/
Attribution: OpenFlights contributors. IATA member facts: International Air
Transport Association. No claim of IATA endorsement or licensed access to its
subscription coding database is made. License notices do not cover application
source code or private shipment databases. Source attribution is also visible
in the lookup window. No IATA logo or paid dataset is included.

Processing: trim missing values, validate code shapes, normalize common country
aliases, deduplicate exact name/code rows and prefer the member snapshot when
both airline designators agree. Invalid and placeholder rows are omitted; one
historical entry (OpenFlights ID 3759) is omitted for repository naming cleanup.
Duplicate designators with different identities remain separate choices.

To update after reviewing the sources, save the public OpenFlights CSV and an
IATA member JSON array with name, country, iata, icao and numeric string fields.
Retain leading zeros and controlled-duplicate asterisks. Then run:
  python3 tools/build-airline-directory.py /path/to/airlines.dat \
    /path/to/members.json --retrieved YYYY-MM-DD
Review differences and run the reference lookup tests before packaging.
The application makes no background network calls and does not update records
merely because a reference table changes. Manual document entry stays available.

Other lookups:
- Airports use the open database's migrated AIRPORTS.TPS reference table. They
  are not bundled from private records and may include old names. City/country
  are searched as part of the supplied airport description. Coverage is limited
  to the imported directory, and custom codes/names remain editable.
- Countries and currencies use the installed Qt/CLDR locale data. Currency
  codes are grouped by country/territory; this is not an exchange-rate service.
- Incoterms 2020 names and transport-mode groups were checked against ICC:
  https://2go.iccwbo.org/incoterms-2020-app
  https://library.iccwbo.org/clp/clp-incoterms-qa-2020.htm?AGENT=ICC_HQ
  Only code/name/mode lookup is supplied, not contractual advice or the rulebook.
