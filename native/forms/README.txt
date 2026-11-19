Shipping form provenance and registration
========================================

native/awbform.cpp draws the conventional air-waybill face using native vector
rules and labels. Its section order and wording follow the user's supplied
blank AWB stationery reference. Letter-size coordinates remain compatible with
the shipment-value layout and its overflow checks. No PDF, linework stream,
font subset or metadata recovered from the previous software is embedded.
The retired recovered background and recovery script are held only in private/.

This is a face-only draft for review against the airline's accepted form. It
has no airline logo, approval claim, signature or fabricated reverse contract.
Native print on preprinted stock emits shipment values only; its measured
registration profile is separate from this Letter-size vector form.

The VICS freight BOL is reproduced as native text and linework from the blank
form image supplied by the user on 2026-11-18. Its sections, wording and field
positions follow that reference. The user's accompanying site identifies this
as VICS freight/retail transport, distinct from an ocean carrier's B/L:
https://billoflading.org/
No carrier-specific terms from other online samples were adopted.
The initial implementation has the supplied face only; it rejects overflow
requiring an approved supplement. Signatures remain blank for execution.

The continuous AWB registration profile traces the second user-supplied image:
9.5 x 24 inch tractor stock, two forms repeated at 12 inches. Paper edges in the
reference are approximately (19,21) to (703,1749) pixels; the first form frame
is (111,124) to (645,868). The profile uses those physical dimensions and the
actual internal box positions, not a uniform scaling of the laser AWB page.
The original two-copy layout was inspected using an image overlay. Version
0.9 prints only one 9.5 x 12 inch form per job, preserving the first-form field
positions. Printing emits only shipment
text, including stored signature text, with no background or added draft mark.
The copy labels and contractual wording are already on the physical stock.

Offsets and scale controls adjust the field registration. The printer driver
must support 241.3 x 304.8 mm continuous paper and actual-size output. Physical
top-of-form, tractor feed, printer pitch and legibility remain to be tested on
the user's hardware. Native Print uses the installed OS driver, not raw ESC/P.

Air dangerous goods declaration (0.5.0)
--------------------------------------
native/dangerousgoods.cpp draws the standard column-format layout as vector
text and lines on A4 paper. Its visual reference is the five supplied examples,
including red diagonal borders and the seven identification/packing columns.
Static field names and declaration wording were cross-checked on 2026-11-18
against IATA's published column-format blank:
https://www.iata.org/contentassets/a9f496cd8c87466b98142fa6d4cdb209/shippers-declaration-column-format-non-fillable.pdf
https://www.iata.org/en/programs/cargo/dangerous-goods/shippers-declaration/
The IATA logo is not reproduced. This is a newly rendered form, not a claim of
IATA endorsement, regulatory validation or carrier acceptance. Signatures are
left blank. It is an air declaration, not the maritime/IMO declaration.
The goods table paginates whole rows and repeats parties, AWB number, choices,
handling and signatory details, with Page x of y. Oversized individual rows or
fixed fields fail atomically without clipping or replacing an existing PDF.
No background-free DG stock mode is implemented; preprinted DG support is not implemented.

Marine export forms (0.6.0)
---------------------------
native/marine.cpp renders Letter-size ocean BOL and dock receipt draft forms
as vector text and rules. Field arrangement is based on these public references,
reviewed 2026-11-18:
- US TRANSCOM DTR Part V, Chapter 513, Figure 513-1, Ocean Bill of Lading:
  https://www.ustranscom.mil/dtr/part-v/dtr_part_v_513.pdf
- Northwestern Shipping, Dock Receipt blank form:
  https://northwesternshipping.com/documents/DOCK_RECEIPT.pdf
These are native renditions of the conventional export form, not facsimiles of
any particular carrier's stationery. No carrier branding, signatures or
reverse-side contractual conditions are inferred. The standard delivery,
receiving-clerk and issuance areas remain for completion after review.
Cargo and document-note continuations repeat the identifying header and page
count. Fixed address overflow fails atomically. Blank HM columns do not classify
the goods. Ocean carrier/terminal acceptance must be established separately.
Dangerous goods is hidden from the 0.6.0 workflow; its earlier source is retained.

Logistics company scans and company identity (0.9.0)
---------------------------------------------------
The user supplied SCANNED DOCUMENTS.pdf (six image-only pages) on 2026-11-18.
Page 1 is a certificate of origin, page 2 a text-only ocean BOL, page 3 a dock
receipt with company masthead, page 4 a laser air waybill, and pages 5/6 the
first/second sight-draft exchanges. Each document type has a native printable
renderer. This covers the document types rather than guaranteeing identical
stationery or physical alignment. The native sight-draft layout follows the
scanned sections and wording, without the old product footer.
Imported SIGHT.TPS draft 241176 matches the scanned example, allowing its bank,
contact, enclosure and instruction fields to be checked against the form.
No bank account, contact, logo or other operational content from the scans is
bundled in the source distribution. Synthetic regression fixtures use examples.

Text-only ocean BOL coordinates follow the scan's visible party/routing/cargo
placement. Letter is a provisional physical size; the scan's 1653 x 2338-point
media box is not a measurement of the original stock. Alignment offsets and
scale remain adjustable. Fixed text, rules, logos and draft footers are omitted.
Long cargo still paginates with shipment references and page numbering.
Physical carrier stationery alignment is not established by the software tests.

Company profile logos are user-selected PNG/JPEG files normalized to bounded
PNG bytes. The profile is stored in database metadata and copied into each new
document; dock receipts use it as a masthead. Stored documents retain the logo
and details they were created with. This avoids rewriting historical documents
when company settings or the original image file change.

Certificate of origin (0.11.2)
------------------------------
native/certificate.cpp draws a Letter-size certificate using native text and
rules. The arrangement follows the archived certificates supplied with the
migration: shipper/consignee/notify party, export references and forwarding
agent, routing, five-column goods area, origin declaration and chamber block.
No archived PDF, recovered linework, product footer, logo or signature is bundled.
Both declaration blocks use the record's COUNTRY, including multiple countries;
missing origin, declarant and chamber values remain unfilled. Stored dates are
converted from the imported date representation, never inferred from today.
Normal cargo rows stay together; oversized descriptions continue without
duplicating quantities. Numbered pages repeat the header; only the final page
contains the declaration/signature block. Oversized fixed fields fail atomically
without clipping or replacing an existing output. Signatures remain blank for
execution. This does not perform chamber certification or determine goods origin.
