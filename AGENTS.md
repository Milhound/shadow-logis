# Shadow Logis development guide

This file contains contributor and coding-agent guidance. See [README.md](README.md)
for the product overview, installation and build instructions, and
[review/REVIEW.txt](review/REVIEW.txt) for dated implementation and validation notes.

## Working rules

- Preserve unrelated edits and private operational data. Use synthetic records for
  tests; tests against a supplied database must work on a temporary copy.
- Never publish workspaces, contacts, source TPS files, historical PDFs, uploaded
  company logos, credentials or extracted legacy software. Follow the explicit
  allowlist in `tools/source_manifest.py`; ignored files are not automatically safe
  to publish. Review tracked files and history before publication.
- Use “legacy TPS installation” in public import wording and documentation.
- Preserve source records and provenance during migration. Do not silently rewrite
  imported amounts, fractions, blank slots, relationships or stored status values.
- Keep document formats and physical print geometry stable. Measure text in the
  selected form's coordinates; reject overflow without replacing an existing PDF.
  Never disable a layout test merely to make packaging pass.
- Use native Qt/C++ on every platform. Build/package scripts do not install host
  dependencies or change system configuration.
- Run relevant tests for code changes; use `make test` for the full suite and
  `make check-publication` / `make source` before distributing source. Do not claim
  native platform or printer validation from offscreen tests on another platform.
- Keep build instructions aligned with the Makefile, platform scripts and CI.
  Update the source allowlist when adding or renaming public root files.
- Application code and original assets use AGPL-3.0-only; preserve third-party
  licenses and provide corresponding source with binary distributions.

## Platform and packaging notes

Linux tests include a font-isolation check using only Liberation Serif, Sans and
Mono. Inter is embedded for the UI, not a substitute for document fonts. Debian
and Fedora images need `file` for CPack dependency inspection, Fontconfig for the
font test, and Poppler for document extraction/rendering checks. Keep these in
Docker and CI dependencies as well as native build prerequisites.

The reported Debian container run passes all 20 tests and staged runtime
verification. Its subsequent missing-`file` packaging error has been addressed;
final package creation/export still needs a successful rerun. Apple Silicon and
Windows scripts are prepared; native clean-machine and physical printer checks
remain separate release gates. Consult dated review notes instead of treating
historical local results as current platform certification.

Mac packages use an ad-hoc integrity signature, not Developer ID signing or
notarization. Windows output is an unsigned deployment folder, not an installer;
the target machine needs the Visual C++ x64 Redistributable. Verify licenses for
the exact deployed third-party runtimes before distributing bundles. Native
verifiers exercise packaged runtime components without operational workspaces.

## Implementation reference

The following details were moved from the former README. They document intended
behavior and migration decisions; reconcile changing implementation details with
the code and the dated review notes.

### Implemented behavior and constraints

Available:
  - Original Shadow Logis S/route logo and dark application icon, embedded for
    Linux windows, the Mac Dock/Finder and Windows Explorer/taskbar. Editable
    vectors and generated PNG/ICNS/ICO assets are under native/branding. Company logos on dock receipts
    remain separately configurable; the app logo does not replace them.
  - Bundled Inter 4.1 fonts work offline without installation. The UI uses
    clearer section cards, rounded controls, quieter tables, visible focus and
    emphasized Add/Save actions. Colors follow the Qt desktop palette and accent,
    including live light/dark palette changes. Desktop font sizes above 10pt
    and Qt display scaling are respected. Shipping PDFs retain their own fonts.
    Help > Inter font license displays the included SIL Open Font License.
  - Reference menu: Airline codes, Airport codes, Countries / territories,
    Currency codes and Incoterms 2020. Each opens a separate searchable window.
    Airline lookup supports country/name/IATA/ICAO/numeric-code searches and
    copying codes. The snapshot has 6,284 entries: 383 IATA member entries shown
    by default, plus optional historical/unverified OpenFlights records. It is
    not the complete current IATA register. Country means the airline's home
    country, not the destinations it serves. Source/date are visible in the UI.
    AWB carrier fields have lookup buttons that insert only the selected IATA
    designator. Numeric codes are reference-only; AWB numbers are not changed.
    Airport lookups show Code and Name / location without the repeated Details
    column. Airport fields have a searchable picker using the migrated directory and
    fill only blank origin/destination names. Customer country and AWB currency
    fields also offer pickers. Incoterms lookup shows all 11 names and transport
    groups for copying; it does not change freight payment terms automatically.
    Customer selection lists show names, locations and phone numbers instead of
    internal record numbers. IDs remain intact for linking and saving.
  - Open directly into Bookings / shipments. Select a shipment to use its
    Shipment details, Air waybills, Ocean bills of lading, Dock receipts,
    Certificates of origin and Sight drafts tabs. Packing lists is hidden from
    this tab bar; its migrated records remain available in Archive. A selector
    retains every linked document sequence. Empty tabs explain how to create the document.
    With no shipment selected, the details panel is hidden and the list fills
    the workspace. Press Escape or click the small X at the top right of the
    details panel to clear the selected booking and return to that list view.
    Search, sorting and column order are preserved. Escape in a separate editor
    or preview retains its normal dialog behavior. A small version label remains at the bottom right for support
    screenshots; it uses the same CMake version as the command line and bundles.
  - Customers and Contacts open in a separate Address book window over the
    shipment workspace. Search, add, edit and history are available there;
    closing it preserves the shipment selection, search and document tab.
    Database open, migration, backup and reports are in File. Archive contains
    unmatched documents and original PDFs. Workspace can reveal empty bookings.
  - A compact search bar provides Add, Edit and Delete.
    Drag booking column headings to rearrange them. The order is remembered on
    this computer across saves, archive visits and restarts, independently of
    the shipping database. Workspace > Reset booking column order restores the
    default arrangement. Clicking a heading still sorts the list.
    The shipment editor shows references, parties, routing, dates, cargo and notes
    together in side-by-side sections, with vertical scrolling on smaller windows.
    Address book uses the same concise Add / Edit buttons.
  - Double-click a shipment to edit it and use the Documents controls inside that
    editor to select a type and saved sequence, then Add, Edit, View or Delete. All six
    document types remain available here, including the hidden Packing lists type.
    New shipments must be saved first. Creating a document prompts to save any
    pending shipment edits so its shared fields use current data. Each saved
    document is independent and remains saved when the shipment editor closes.
  - Delete removes the shipment from the active list. Workspace > Deleted shipments
    can restore it; linked documents, cargo, history and the reference remain intact.
    Deletion and restoration are versioned and reject conflicting updates.
  - Status is hidden from the shipment list, editor and detail view. BOO:STATUS
    remains unchanged in the database and history; possible future cleanup is
    recorded in review/REVIEW.txt. Deleting a shipment does not change Status.
  - Create, edit, preview, save PDF and print ocean bills of lading and dock
    receipts on Letter-size export forms. Marine editors show document references,
    parties, routing, cargo amounts, instructions and issue details in one page.
    Review notify party, vessel, voyage, ports, terminal and cargo before printing.
  - New marine documents reuse booking parties, routing and initial cargo.
    If the shipment already has the other marine document type, choose its
    sequence to copy parties, routing and every cargo line instead. Copies
    are independent; changing one never silently overwrites another.
  - Dangerous goods is paused and hidden from the workspace. Its saved records,
    legacy tables and renderer remain preserved for later work.
  - Browse, search, add and edit customers and contacts.
  - Browse, add and edit bookings, addresses, contacts, shipment dates,
    weights, volumes and booking notes.
  - Select existing customers as shippers and consignees.
    Choose contact copies the person's name and phone, plus email for the
    shipment's customer contact. Date fields offer a calendar, Today and Clear.
  - Shipment and cargo-line weight entry can calculate lb from kg or kg from lb.
    Cargo lines also convert cubic meters/cubic feet. Editing the calculated
    result switches to Manual values; select a calculation again to resume.
    Modes survive Save/reopen. Imported records start manual; their values are
    not normalized just by opening the editor. Fractional weights are supported.
  - Use shipment details in a document reviews shared fields against its current
    saved shipment. Empty fields are preselected; replacing an existing value
    requires selecting that field. Cargo and charges are separate. Ambiguous
    legacy reference matches are not guessed. AWB airport-code fields search
    by code or name and fill an empty origin/destination name on selection.
  - AWB Dimensions / weight supports package sizes with a piece count and cargo
    row assignment, or volume entered directly in CBM. Formula presets are:
      Metric: total cubic centimeters / 6000 = kg.
      Imperial: total cubic inches / 166 = lb.
      Direct volume: CBM * 167 = kg (intentionally not 1000000 / 6000).
    Each factor/divisor is editable. Dimension units can be changed with value
    conversion. New AWBs carry the shipment's CBM; it is prefilled when cargo is
    in one row. For multiple cargo rows, assign dimensions or CBM per row.
    Chargeable weight is the greater of gross and allocated dimensional weight,
    rounded up to a whole unit, half unit or 0.01. Dimensional output is converted
    to the cargo's kg/lb unit. Package dimensions can be used as entered, rounded
    to nearest whole cm/inch, or rounded up, independently of weight rounding.
    Automatic dimension-based weight requires full piece coverage for each row.
    Manual chargeable weights remain editable for service-specific exceptions.
  - AWB Rates / charges edits per-unit rates, rate classes, fees, payment sides
    and agent/carrier allocations. Optional calculations use exact decimal money,
    rounding each line to cents. Weight charges, other charges and final totals
    have separate calculation switches; each line can also be manual. Editing a
    result disables its calculation. Imported amounts remain manual until enabled.
    Calculations run in order: dimensions, chargeable weight, line amounts, fees,
    totals. Preview and Save use the same logic. Apply in a helper dialog changes
    the editor only; Save commits the document and Cancel discards pending changes.
  - Create air waybill, ocean bill of lading, dock receipt, packing list and certificate records,
    including copying shared shipping fields and initial cargo from a booking and retaining an
    explicit link back to that booking. Shipment document tabs also find imported
    shipping documents by exact shipment reference, listing each sequence.
  - Browse/add/edit cargo lines for bills of lading, dock receipts, packing lists and
    certificates, linked by BOTH reference and sequence number.
  - Certificate Preview / Print renders a Letter-size certificate of origin,
    following the archived export-form layout: parties, routing, goods, origin
    declaration, chamber and signature areas. Country of origin, declarant,
    chamber, place and dates are editable. Signature lines remain blank.
    Long cargo continues on numbered pages, with the declaration on the last
    page. No country, chamber approval or signature is inferred from a booking.
  - Air waybills, ocean bills of lading, dock receipts, certificates of origin
    and packing lists use compact editors with side-by-side sections. Fields
    remain on one scrollable page; Save, Preview / Print and Cancel stay visible.
    Multiline editors hide unused trailing slots in imported fixed-size arrays,
    avoiding scrollbars caused only by empty padding. Internal blank lines and
    unchanged stored arrays are preserved; longer content can still be scrolled.
    Preview includes unsaved changes and new-document cargo without saving them. Optional VICS fields
    expand within the ocean BOL editor. Marine documents, certificates and
    packing lists show an inline Cargo / document notes table with packages,
    descriptions, marks, weights and volume. Blank imported rows are hidden by
    default; Show blank rows reveals them without changing their saved order or
    contents. Add and Edit save each cargo row separately, independently of the
    document's Save/Cancel. Save a new document before editing its cargo rows.
    The shipment workspace's Cargo shortcut opens the same table in a window.
  - Air waybills group parties, routing, cargo, handling and execution details.
    Five cargo rows support pieces, gross weight, kg/lb and chargeable weight.
    The sixth TPS storage slot is a legacy working value, preserved unchanged.
    Exact totals are recalculated when cargo changes; mixed units are rejected.
  - Transfer booking addresses, goods, handling notes, pieces and weight into
    an air waybill. Review the copied origin/destination and all shipment data.
  - Preview AWBs on a native vector rendition of the standard Letter-size face, including
    the preserved charges, declarations, dimensions and stored signature text.
    No background or binary from the previous software is bundled.
    Whole weights print without .00; genuine fractions retain their precision.
    Truncated destination names can be completed from the migrated airport
    directory and final routing code, with a compact label inside the same box.
  - Documents > Preview truck bill of lading (VICS) retains the earlier truck
    form for a selected BOL. Enable Show truck freight fields in its editor.
    Edit VICS carrier, freight terms, bill-to, instructions, customer orders and
    cargo classification fields without overwriting preserved ocean fields.
  - Preview one text-only AWB on a 9.5 x 12 inch form from the continuous stock.
    Each job prints only one colored copy, with registration controls.
    Goods descriptions share the full panel with the populated dimension rows.
    Windows memo line endings are normalized for printing; empty spacer lines
    are compacted only when needed. Stored text and all printed words remain.
  - Print from the native preview through the operating-system printer driver
    at actual size. The printer's paper size must match the PDF.
    PDF text stays readable on white paper regardless of the desktop theme.
  - Preview generated documents and original historical PDFs in the app, with
    page navigation, fit-page/fit-width views and percentage zoom. Save PDF is
    optional and saves exactly the bytes shown; original PDFs remain unchanged.
  - Search booking numbers and air-waybill/master numbers.
  - View prior shipping-field changes through History. Reject stale updates
    and duplicate unlinked references; a failed save keeps the editor and inputs open.
    Linked documents can have separate sequences for the same shipment.
  - Back up the open database to a new, owner-only SQLite file. The snapshot
    includes edits, history and all archived source files, and is integrity checked.

Limitations that matter before operational use:
  - Full-template AWBs, ocean BOLs, dock receipts and VICS PDFs are marked DRAFT.
    Text-only ocean BOLs and AWBs omit the template and draft footer.
    Marine templates follow the conventional US export-form field arrangement;
    form references are recorded in native/forms/README.txt. These are not issued
    carrier documents. Carrier-specific contractual conditions, originals and
    acceptance are not established by generating the draft. Signatures remain
    blank. Packing lists still preview labeled summaries. Certificates render
    their own form; execution and chamber certification remain separate steps.
  - Marine cargo continues on numbered pages with repeated shipment details,
    including a single long cargo entry. Long routing/instruction text continues
    as explicitly labeled document information, separate from cargo quantities.
    An oversized fixed party/address field fails without replacing an existing
    PDF. Stored header weights are shown when an ocean document has no cargo rows.
  - Legacy DODR records share a delivery-order/dock-receipt schema. Existing
    TO_NAME and PD_NAME addresses are shown as delivery recipients/locations;
    they are not assumed to be consignees. New consignee/notify/terminal fields
    are JSON extensions; the original TPS schema and files remain unchanged.
  - VICS has eight order rows and a fixed cargo area; approved BOL supplement
    support remains to be added for larger shipments. Barcodes are not generated.
  - Review VICS ship-to details and enter the domestic carrier/freight terms:
    an imported ocean consignee is not automatically a terminal delivery party.
    Blank freight terms are explicitly marked unconfirmed. Ocean signatures
    are never copied into the VICS shipper or carrier signature areas.
  - AWB calculations use entered rates, not a carrier tariff database. Minimum,
    basic, ULD and other special rates require manual amounts; no rate-break
    optimization is inferred. Weight/fee payment terms and currency are explicit.
    The two unit-specific dimensional divisors and direct-CBM factor are distinct
    rules, not algebraically interchangeable. Declarations remain as imported.
    Ordinary package dimensions are not inferred from volume. In CBM mode the
    goods panel shows volume and dimensional weight, rather than inactive sizes.
  - Imported totals remain unchanged unless cargo rows change. Export refuses
    a mismatch with the five cargo rows. All 3,928 imported AWBs match their
    stored piece/gross totals using those five rows. Unclear units need review.
  - Imported document matches use the complete reference, ignoring case and
    outer spaces. They are labeled Shipment reference; if several bookings
    share that reference, they are labeled Shared reference - review. Empty
    references never match. An explicit native booking link takes precedence.
  - BOOKLINE relationships are unconfirmed. Those rows are migrated but are
    not attached to bookings in the interface. See review/REVIEW.txt.
  - Five TPS files remain archived without decoded rows. See migration report.
  - TPS index/deleted-record semantics and original business rules need
    reconciliation against the Windows application's reports before cutover.
  - This is a local, single-user workspace. There is no application login,
    shared server, cloud sync, or TPS write-back. Keep the database private.
  - Shipment deletion is recoverable, not a permanent purge. Documents remain
    available through Archive and deleted references remain reserved. Use version
    0.7 or newer when opening these databases: older versions do not filter deleted
    shipments from the list.

### Shipment and document workflow

1. The app opens on Bookings / shipments. Search or select the shipment,
     or choose Add. Edit always edits the selected booking,
     including while viewing one of its document tabs. The single-page editor
     groups references, shipper/consignee, routing, dates, cargo and notes.
  2. Select the required document tab. Existing documents appear in the
     sequence selector; choose Edit or Preview to work with one. Alternatively,
     double-click the shipment and use its Documents selector and Add/Edit/View
     buttons without leaving the shipment editor.
  3. Choose Create from shipment to prefill shared information. Bills of
     lading, dock receipts, packing lists and certificates receive a first cargo line with
     booking descriptions, pieces, weights and volume. This is saved atomically
     with the document. Existing documents are never silently refreshed.
  4. For an air waybill, review the parties, routing, cargo and handling sections.
     Use Dimensions / weight and Rates / charges to enable the calculations you
     need; manual values stay under your control. For a marine document,
     choose the existing ocean BOL or dock receipt when offered, or use shipment
     details. Review the parties and routing, then edit the copied entries in
     Cargo / document notes on the same page. Document numbers and source
     provenance remain independent.
  5. Save to return to the shipment editor or its document tab. View/Preview opens in the app;
     saving a PDF is optional. Print requires the matching paper size. Signing
     and carrier acceptance remain separate from creating a document.
  6. Use History for prior values. Use File > Back up database for a separate
     verified snapshot. Address book and Archive hold the supporting records.
  7. To remove a shipment from the working list, select it and choose Delete.
     Confirm the reference in the dialog. To undo this, open Workspace
     > Deleted shipments, select the shipment and choose Restore.
     To edit an existing contact, use Address book > Contacts, select the contact,
     then Edit (or double-click its row). Close returns to the shipment workspace.

### Company profile, sight drafts and document deletion

- Address book > Company profile lets you select My company and My contact,
    review their details, and upload a PNG or JPEG logo. The image is stored in
    the database and its backups; moving or deleting the original file is safe.
    Choosing a company loads its details; choosing a contact fills name, phone
    and email. Later contact edits can be picked up by selecting it again.
  - New documents snapshot the profile. AWBs use its issuing-agent address,
    IATA code and contact information. Marine forms use its forwarding-agent
    details; sight drafts use its contact-in-case-of-need block. New certificates
    prefill the forwarding-agent address and declarant company; packing-list
    summaries include the logistics company. New dock receipts
    print the logo and company/contact header. Existing documents keep their
    details when the profile changes. Logo placement elsewhere is not enabled.
  - Sight drafts are available from the shipment tab and the shipment editor,
    including the 43 imported SIGHT.TPS records. The editor groups payment,
    banks, enclosures and handling instructions. Preview produces First and
    Second of Exchange pages matching the supplied example's sections. Select
    a page range to print just one. Enter the USD amount; if Amount in words is
    blank, it is generated with exact cents. Stored wording is preserved.
    Printed names are retained; signature lines remain blank for signing.
  - Delete on a document tab or beside Add/Edit/View in the shipment editor
    removes just that document from the active lists. Workspace > Deleted
    documents restores it with its cargo and history. It never deletes the
    shipment or the archived source files. Canceling the confirmation changes
    nothing. Stale editors cannot overwrite or resurrect a deleted document.

### Preprinted ocean bills of lading

Ocean BOL previews default to Text Only, using the logistics scan's page 2 as
an alignment reference. No boxes, headings, contractual wording, signature
lines, logo or draft footer are printed. The editor's Print format can select
the full form for review. Documents > Ocean BoL print alignment adjusts offsets
and scale; the editor opens the same controls for text-only preview.
Physical stock size is provisionally US Letter (8.5 x 11 inches); the scanner's
large PDF dimensions do not establish paper dimensions. Confirm the stock and
check a physical alignment print before use. PDF printing stays at actual size.

### Preprinted dot-matrix AWBs

1. Select a shipment, open Air waybills and select the required sequence.
  2. Choose Dot Matrix (Laser AWB selects the full template). The layout targets the supplied
     single 9.5 x 12 inch form from the supplied 9.5 x 24 inch paired stock.
  3. Preview the text-only sheet. It has NO background, boxes, fixed form text,
     copy labels or draft footer. The PDF contains vector shipment text only.
  4. Choose Print and the installed dot-matrix printer. Select a custom paper
     size of 9.5 x 12 inches (241.3 x 304.8 mm) in its driver. Print uses actual
     size and rejects a mismatched paper size instead of shrinking the form.
  5. Test one sheet. Positive horizontal offset moves text right; positive
     vertical offset moves it down. Scale controls adjust registration within
     the single form. Position the red or blue copy at top of form before printing.
     Print again only when you need the other copy. Print alignment settings are remembered per user; verify them again when
     changing printers or paper stock.

The initial field positions were measured from the supplied stock image and
checked with a local image overlay. Physical feed, top-of-form position,
tractor margins, driver support and ribbon legibility require a real printer
test. Use a printer driver that supports this custom continuous page length.
The native print path renders the preview through Qt at up to 300 dpi; it is
not a raw ESC/P command stream. Text-only export itself remains vector text.
The program does not send a job until Print is accepted in the print dialog.

Optional command-line PDF creation (read-only against the database):

  QT_QPA_PLATFORM=offscreen ./build/shadow-logis --database data/shadow-logis.sqlite \
    --export-awb-text RECORD_ID --output text-only-awb.pdf \
    --print-offset-x 0 --print-offset-y 0 --print-scale-x 100 --print-scale-y 100

Use --export-awb for the full Letter-size AWB, --export-ocean-bol for an ocean
text-only BOL, --export-dock-receipt for a dock receipt, --export-sight-draft
for both exchange copies, --export-certificate for a certificate of origin,
or --export-bol for the earlier
VICS truck form. Each takes a saved record ID and --output PDF_PATH.

An existing record's reference stays fixed to preserve relationships. If a
concurrent edit prevents saving, the input remains visible for copying before
reloading; this version does not merge conflicting edits automatically.

### Workspaces and importing an existing installation

Start with New workspace to create a private, empty SQLite workspace. All shipping
and address-book workflows are available immediately; no legacy files are needed.
File > Open workspace opens an existing file. The last successfully opened file,
window geometry, booking column order/sort, the empty-bookings filter, print alignment, last printer and PDF
save folder are remembered per operating-system user. Missing/invalid remembered
workspaces return to startup without creating a replacement. Passwords and company
data are never stored in preferences.

File > Import legacy TPS installation creates a SEPARATE workspace. Close the
legacy program first, preferably copy its installation as a consistent backup,
then point the wizard at that folder. The wizard discovers supported TPS data
folders. If several are found (for example live data and backups), explicitly
choose one; it never combines their TPS records. Filenames are case insensitive.

Use checkboxes to choose company addresses (shippers, consignees and other
companies share one address book), contact people, bookings, AWBs, ocean bills
of lading, dock receipts, certificates of origin, sight drafts, and reference
directories. Cargo files follow their document type. Archived packing lists and
original PDFs are optional and unchecked initially. PDF selection includes PDFs
under the selected data folder.
Documents imported without bookings remain accessible in Archive. Imported files
retain their complete original bytes privately, including fields outside the
current UI; selecting a category is not field-level redaction of its source file.

Review the selected file count and new workspace path, optionally enter the TPS
owner password, then choose Import into new workspace. No originals are edited,
no existing workspace is overwritten, and imports are not merged into an open
workspace. Files that cannot be decoded are reported and privately archived;
an import where all selected TPS files fail does not publish an empty workspace.
Unselected files are not archived. The original migration CLI remains available
for complete archival imports. Create an empty workspace from the CLI with:

  shadow-logis --create-workspace /path/to/new-workspace.sqlite

### CI data and validation

CTest now includes shipment/editor workflows, weight/rating UI, full shipping
workflows and new-workspace/import/settings checks. These create authored
synthetic workspaces, TPS samples and PDFs only inside temporary directories.
No operational fixture files are checked in or required by CI. Existing optional
commands using a supplied database still run against a private temporary copy.
A native build/package verifier is not a substitute for testing printers, native
file dialogs and a clean target machine; those follow the initial push.

### Migrate another snapshot

Close the original legacy application and copy the entire data folder first.
Keep that snapshot untouched. Choose a NEW output database filename:

  ./build/shadow-logis --migrate /path/to/legacy-data --output /path/to/new.sqlite

For an optional TPS OWNER password, append --password-prompt. Enter it at the
hidden prompt; do not put credentials into command arguments or source files.
The application login password may differ from the database OWNER password.
Only ASCII owner passwords are currently supported. The GUI also offers the
same import using Import legacy TPS installation.

The output parent directory must exist. Migration refuses to overwrite any
existing database and promotes a temporary database only after verification.
Failures leave an explicitly named .partial-* file for diagnosis. Undecodable
TPS files are marked archived-only, never reported as decoded. The original
TPS/PDF bytes and SHA-256 hashes are stored in the new database. Files over
256 MiB are rejected. SQLite files are created with owner-only permissions.

### Validation and migration plan

1. Preserve the original ZIP and a closed-data snapshot.
  2. Import to a separate SQLite database; read the migration report.
  3. Independently verify storage and source preservation:

       python tools/verify_migration.py /path/to/legacy-data data/shadow-logis.sqlite

  4. Test the native workflows on a disposable copy. This command creates its
     own consistent temporary snapshot; it never edits the input database:

       QT_QPA_PLATFORM=offscreen ./build/shadow-logis-workflow-tests data/shadow-logis.sqlite

     The focused UI check also uses its own temporary database snapshot. It tests
     add/edit, contact changes, compact layouts, hidden Status, delete/restore,
     cancellation and preservation of linked documents. It also checks the separate
     address book, all six compact document editors and nested Add/Edit/View/Delete,
     including unsaved previews, explicit save-before-create, canceled parent edits,
     individual document restoration and Company Profile defaults/backup:

       QT_QPA_PLATFORM=offscreen ./build/shadow-logis-ui-tests data/shadow-logis.sqlite

  5. Reconcile customer totals, selected bookings, addresses, dates, decimals,
     memos, cargo lines and document output against original legacy reports.
     Investigate unmatched detail rows as needed. The user accepted the recovered
     scope; the five archived-only files are not blocking current development.
  6. Validate carrier forms and run the Mac build on the target Mac.
  7. Choose a final cutover snapshot, migrate again to a new database, reconcile,
     and then make SQLite authoritative. There is no automatic two-way sync.

Use Back up database for a consistent snapshot while the app is open. It uses
SQLite VACUUM INTO, verifies integrity, and refuses to replace existing files.
Open a backup through Open database to inspect it; further edits affect that
opened copy. There is no automatic backup schedule. Revision history does not
replace a separate backup. The original TPS archive is never updated by edits.
SQLite snapshot reference: https://www.sqlite.org/lang_vacuum.html

### Source layout

native/tps.*       Bounded read-only TPS decoder and optional owner decryption
native/store.*     SQLite migration, provenance, record editing and history
native/domain.*    Shipping field labels, date conversions and cargo mapping
native/airlines.*  Offline airline codes and search
native/reference.* Countries, currencies, imported airports and trade terms
native/main.cpp    Native desktop interface and migration CLI
native/documents.* Shipment-summary PDF generation and form APIs
native/airwaybill.cpp AWB values and text-only continuous-stock mapping
native/awbform.cpp    Native vector AWB face (no recovered PDF assets)
native/appearance.cpp Bundled fonts and desktop-palette-aware UI styling
native/billoflading.cpp Supplied VICS freight BOL layout and field mapping
native/marine.cpp   Ocean BOL and dock receipt, cargo and notes continuation
native/dangerousgoods.cpp Preserved air DG renderer (workflow paused)
native/forms/     Sanitized AWB template and source/registration provenance
native/preview.*   Native preview, exact-copy saving and actual-size printing
tests/            Synthetic parser/storage tests and copied-data workflow checks
review/REVIEW.txt  Code review, migration totals, remaining compatibility gaps
third_party/      Attribution and licence for the TPS format reference

Operational files, extracted Windows software, builds, databases and local
validation captures are excluded by .gitignore and from the source ZIP.

### Recreate the source package

python tools/package-source.py

This writes dist/shadow-logis-source.zip using an explicit source allowlist.
It excludes customer data, migrated databases, original Windows software,
local validation captures and compiled binaries.

### Publishing the source

Use dist/shadow-logis-source.zip or the file list in tools/source_manifest.py.
The root .gitignore permits the source directories and excludes everything else,
including private/, data/, build/, dist/, output/ and local review artifacts.
The original installation, its archive and detailed binary audit are preserved
in private/; operational data stays in data/shadow-logis.sqlite. Do not publish
those files or database backups. The AWB face is authored as native vector code;
no recovered template is included. Bundled binary assets are licensed Inter fonts and icons generated from the
project’s original vector artwork. Public airline reference data is included; customer records,
shipment documents and operational databases are not.
No PDFs, contact exports or uploaded company logos are in the source allowlist.
Generated PDFs are excluded too: they may contain real customer information.

  python3 tools/check-publication.py
  python3 tools/package-source.py

The publication check also rejects tracked files outside the source allowlist
when run in a Git repository. Ignore rules do not remove already tracked files
or erase Git history; history must be reviewed separately if importing an older
repository. The repository is initialized with the requested origin; the first source commit
and push are still pending.
