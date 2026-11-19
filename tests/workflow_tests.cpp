// Integration checks use a disposable copy of the supplied migrated database.
// Snapshotting leaves the input untouched, even if another reader is active.
#define SHADOW_LOGIS_GUI_TESTING
#include "../native/main.cpp"
#include "fixtures.h"
#include <QTemporaryDir>
#include <QPdfView>
#include <QPdfSelection>
#include <cstdlib>
#include <QSet>

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
}
int main(int argc, char **argv) {
    QApplication app(argc,argv);
    installAppearance(app);
    if (argc > 3) { QTextStream(stderr)<<"Usage: shadow-logis-workflow-tests <migrated-database> [synthetic-artifact-directory]\n"; return 2; }
    try {
        QTimer::singleShot(90000,&app,[] { QTextStream(stderr)<<"FAILED: workflow test timed out\n"; std::exit(1); });
        QTemporaryDir temp; require(temp.isValid(),"No temporary directory");
        const auto copy = temp.path()+"/validation.sqlite";
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        if(argc==1) createSyntheticWorkspace(copy); else { Store input(QString::fromLocal8Bit(argv[1])); input.backup(copy); }
        Store store(copy);
        // Exercise imported relationships, not just documents created by this test.
        const auto importedBookings=store.records({"BOOKING.TPS"});
        const auto importedDocuments=store.records({"AWB.TPS","OBL.TPS","DODR.TPS","PLIST.TPS","CERTORGN.TPS","SIGHT.TPS",DangerousGoodsSource});
        Record historicalBooking, emptyBooking, historicalAir, previewAir, goodsAir;
        int checkedBookings=0, checkedAir=0;
        for (const auto &booking : importedBookings) {
            const auto ref=recordReference(booking).trimmed().toUpper();
            if (ref.isEmpty()) { if (!emptyBooking.id) emptyBooking=booking; continue; }
            QSet<qint64> expected;
            for (const auto &document : importedDocuments) {
                const auto explicitId=document.values.value("NATIVE:BOOKING_ID").toString();
                if (explicitId==QString::number(booking.id) || (explicitId.isEmpty() && recordReference(document).trimmed().toUpper()==ref)) expected.insert(document.id);
            }
            if (expected.isEmpty()) continue;
            if (!historicalBooking.id && expected.size()>1) historicalBooking=booking;
            QSet<qint64> actual; for (const auto &link : store.linkedDocuments(booking.id)) actual.insert(link.record.id);
            require(actual==expected,"Imported booking reference did not return every document sequence");
            if (++checkedBookings==16) break;
        }
        require(checkedBookings>0 && historicalBooking.id,"No imported reference matches exercised");
        if (emptyBooking.id) require(store.linkedDocuments(emptyBooking.id).isEmpty(),"Blank imported booking matched unrelated documents");
        for (const auto &document : importedDocuments) if (document.source=="AWB.TPS") {
            if (recordReference(document)=="TEST-PREVIEW" || (recordReference(document)=="A-7091-24" && valueText(document.values["AWB:ID_SEQUENCE"])=="3598")) previewAir=document;
            if (recordReference(document)=="TEST-GOODS" || (recordReference(document)=="A-8791-26" && valueText(document.values["AWB:ID_SEQUENCE"])=="4009")) goodsAir=document;
            // Independent arithmetic against persisted totals for the supplied legacy snapshot.
            const auto pieces=document.values["AWB:NUMBER_OF_PIECES"].toArray();
            const auto weights=document.values["AWB:GROSS_WEIGHT"].toArray();
            const auto cents=[](const QString &text) { return text.section('.',0,0).toLongLong()*100 + text.section('.',1,1).leftJustified(2,'0').toLongLong(); };
            qint64 count=0, weight=0;
            for (int i=0;i<5;++i) { count+=pieces[i].toString().toLongLong(); weight+=cents(weights[i].toString()); }
            require(count==document.values["AWB:PIECES_TOTAL"].toString().toLongLong(),"Imported piece totals disagree with five shipment rows");
            require(weight==cents(document.values["AWB:GROSS_WEIGHT_TOTAL"].toString()),"Imported weight totals disagree with five shipment rows");
            ++checkedAir;
            if (!historicalAir.id && recordReference(document)!="A-8791-26" && recordReference(document)!="TEST-GOODS" && pieces[5].toString().toLongLong()>0) {
                try { airCargoTotals(document.values); historicalAir=document; } catch (const std::exception &) {}
            }
        }
        require(historicalAir.id,"Legacy working-slot regression fixture missing");
        RecordEditor importedEditor(store,historicalAir);
        auto *importedGrid=importedEditor.findChild<QTableWidget *>("airCargo");
        require(importedGrid && importedGrid->rowCount()==5,"Legacy working slot exposed as shipment cargo");
        const auto oldCount=importedGrid->item(0,0)->text().toLongLong(); importedGrid->item(0,0)->setText(QString::number(oldCount+1));
        QTimer::singleShot(0,&importedEditor,[&] { importedEditor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); });
        require(importedEditor.exec()==QDialog::Accepted,"Imported cargo edit failed");
        const auto changedAir=store.record(historicalAir.id);
        require(changedAir.values["AWB:PIECES_TOTAL"].toString().toLongLong()==historicalAir.values["AWB:PIECES_TOTAL"].toString().toLongLong()+1,"Imported totals include working slot");
        require(changedAir.values["AWB:GROSS_WEIGHT_TOTAL"]==historicalAir.values["AWB:GROSS_WEIGHT_TOTAL"],"Unchanged gross weight was doubled");
        for (const auto &key : airCargoKeys()) require(changedAir.values[key].toArray()[5]==historicalAir.values[key].toArray()[5],"Legacy working value was overwritten");
        auto customer = store.records({"CUSTOMER.TPS"}).first();
        const auto original = customer.values;
        RecordEditor editor(store,customer);
        auto *contact = editor.findChild<QLineEdit *>("CUS:CONTACT"); require(contact,"Customer contact widget missing");
        contact->setText("Synthetic validation contact");
        QTimer::singleShot(0,&editor,[&] { editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); });
        require(editor.exec()==QDialog::Accepted,"Editor did not accept valid data");
        const auto updated=store.record(customer.id);
        require(updated.values["CUS:CONTACT"]=="Synthetic validation contact","Contact edit not saved");
        for (auto it=original.begin();it!=original.end();++it)
            if (it.key()!="CUS:CONTACT") require(updated.values[it.key()]==it.value(),"Editing changed an unrelated field");
        auto contactRecord=store.blankRecord("CONTACTS.TPS"); contactRecord.values["CTA:FIRST_NAME"]="Synthetic";
        contactRecord.values["CTA:LAST_NAME"]="Contact"; require(store.save(contactRecord)>0,"Create contact");
        auto booking=store.blankRecord("BOOKING.TPS"); booking.values["BOO:REFERENCE_NUMBER"]="TEST-NATIVE-01";
        booking.values["BOO:READY_DATE"]=dateStorage("2026-12-01"); booking.values["BOO:CBM"]="5.25";
        booking.values["BOO:QTY"]="12"; booking.values["BOO:KGS"]="850";
        booking.values["BOO:COMMODITY_1"]="Precision machine parts";
        booking.values["BOO:COMMODITY_2"]="Packed in protective cartons";
        booking.values["BOO:SHIPPER_ARRAY"]=QJsonArray{"Example Exporter","123 Sample Road","Chicago, IL","United States",""};
        booking.values["BOO:CONSIGNEE_ARRAY"]=QJsonArray{"Café Importer","456 Harbour Street","Montréal, QC","Canada",""};
        booking.values["BOO:ORIGIN_LOCATION"]="Chicago O'Hare"; booking.values["BOO:DESTINATION_LOCATION"]="Montréal";
        booking.values["BOO:HAZARDOUS_COMMENTS"]="Keep upright. Protect from moisture.";
        const auto bookingId=store.save(booking);
        require(fieldText("BOO:READY_DATE",store.record(bookingId).values["BOO:READY_DATE"])=="2026-12-01","Date persistence");
        for (const auto &[source,label] : DocumentTypes) {
            auto doc=documentFromBooking(store.record(bookingId),store.blankRecord(source));
            const auto id=store.save(doc); const auto saved=store.record(id);
            if (cargoSource(source).first.isEmpty()) continue;
            const auto lp=cargoSource(source).second;
            const auto shared=store.cargoLines(saved); require(shared.size()==1 && valueText(shared[0].values[lp+"DESCRIPTION"]).contains("Precision machine parts") && shared[0].values[lp+"WEIGHT_KGS"]=="850","Booking cargo not carried into new document");
            auto line=store.blankCargo(saved);
            line.values[lp+"DESCRIPTION"]="Synthetic cargo"; line.values[lp+"PCS"]="2";
            line.values[lp+"WEIGHT_KGS"]="120.5"; line.values[lp+"MEASURE_CBM"]="1.25";
            const auto first=store.save(line); store.save(line);
            const auto cargo=store.cargoLines(saved); require(cargo.size()==3,"Cargo parent linkage and shared booking line");
            const auto lineKey=cargoLineNumberKey(cargoSource(source).first);
            require(cargo[0].values[lineKey].toString().toDouble()==1 && cargo[1].values[lineKey].toString().toDouble()==2 && cargo[2].values[lineKey].toString().toDouble()==3,"Cargo line numbering");
            auto edit=store.record(first); edit.values[lp+"DESCRIPTION"]="Revised cargo"; store.save(edit);
            require(store.cargoLines(saved)[1].values[lp+"DESCRIPTION"]=="Revised cargo","Cargo edit saved");
        }
        const auto linked = store.linkedDocuments(bookingId); require(linked.size()==DocumentTypes.size(),"Explicit document links");
        for (const auto &match : linked) if (match.record.source=="OBL.TPS") {
            RecordEditor bolEditor(store,match.record);
            auto *carrier=bolEditor.findChild<QLineEdit *>("NATIVE:VICS_CARRIER");
            auto *orders=bolEditor.findChild<QTableWidget *>("vicsOrders");
            require(carrier && orders && orders->rowCount()==8,"VICS editor fields missing");
            carrier->setText("Example Motor Carrier"); orders->item(0,0)->setText("PO-GUI-001");
            orders->item(0,1)->setText("4"); orders->item(0,2)->setText("500.00"); orders->item(0,3)->setText("N");
            bolEditor.findChild<QLineEdit *>("NATIVE:VICS_TERMS")->setText("P");
            QTimer::singleShot(0,&bolEditor,[&] { bolEditor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); });
            require(bolEditor.exec()==QDialog::Accepted,"VICS editor save failed");
            const auto edited=store.record(match.record.id);
            require(edited.values["NATIVE:VICS_CARRIER"]=="Example Motor Carrier" && edited.values[vicsOrderKeys()[0]].toArray()[0]=="PO-GUI-001","VICS fields did not persist");
            for (auto it=match.record.values.begin();it!=match.record.values.end();++it) require(edited.values[it.key()]==it.value(),"VICS edit changed imported ocean fields");
        }
        Record awb;
        for (const auto &match : linked) if (match.record.source=="AWB.TPS") awb=match.record;
        require(awb.id && awb.values["AWB:PIECES_TOTAL"]=="12" && awb.values["AWB:GROSS_WEIGHT_TOTAL"]=="850.00","Booking cargo transfer");
        awb.values["AWB:AWB_NUMBER"]="DEMO-AWB-001"; awb.values["AWB:MAWB_NUMBER"]="DEMO-MASTER-001";
        awb.values["AWB:FIRST_CARRIER"]="Example Air"; awb.values["AWB:FLIGHT_DATE1"]="EX123 / 01 DEC";
        awb.values["AWB:AIRPORT_FROM"]="ORD"; awb.values["AWB:AIRPORT_TO_1"]="YUL";
        awb.values["AWB:EXECUTION_DATE"]=dateStorage("2026-12-01"); awb.values["AWB:EXECUTION_PLACE"]="Chicago";
        awb.values["AWB:RATE_CHARGE"]=QJsonArray{"123.45","0.00","0.00","0.00","0.00","0.00"};
        store.save(awb); awb=store.record(awb.id); const auto awbBefore=awb.values;
        RecordEditor airEditor(store,awb); airEditor.show(); app.processEvents();
        auto *cargo=airEditor.findChild<QTableWidget *>("airCargo"); require(cargo,"AWB cargo table missing");
        require(airEditor.findChild<QScrollArea *>("documentEditorScroll"),"Compact AWB editor missing");
        cargo->item(0,0)->setText("2"); cargo->item(0,1)->setText("0.001");
        cargo->item(1,0)->setText("3"); cargo->item(1,1)->setText("0.20");
        qobject_cast<QComboBox *>(cargo->cellWidget(1,2))->setCurrentIndex(1);
        bool invalidRetained=false;
        QTimer::singleShot(0,&airEditor,[&] {
            auto *save=airEditor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save); save->click();
            invalidRetained=airEditor.isVisible() && !airEditor.findChild<QLabel *>("saveError")->isHidden() && cargo->item(0,1)->text()=="0.001";
            cargo->item(0,1)->setText("0.10"); save->click();
        });
        require(airEditor.exec()==QDialog::Accepted && invalidRetained,"Invalid save keeps form and data, then valid retry saves");
        auto savedAir=store.record(awb.id);
        require(savedAir.values["AWB:PIECES_TOTAL"]=="5" && savedAir.values["AWB:GROSS_WEIGHT_TOTAL"]=="0.30","AWB exact totals persisted");
        for (auto it=awbBefore.begin();it!=awbBefore.end();++it)
            if (!airCargoKeys().contains(it.key()) && it.key()!="AWB:PIECES_TOTAL" && it.key()!="AWB:GROSS_WEIGHT_TOTAL")
                require(savedAir.values[it.key()]==it.value(),"Cargo edit changed unrelated AWB data");
        require(store.history(awb.id).size()==2,"AWB revisions recorded exactly once per save");
        RecordEditor staleEditor(store,savedAir);
        auto other=savedAir; other.values["AWB:SHIPPER_CONTACT"]="Another editor"; store.save(other);
        staleEditor.findChild<QLineEdit *>("AWB:SHIPPER_CONTACT")->setText("Unsaved local work");
        bool conflictRetained=false;
        QTimer::singleShot(0,&staleEditor,[&] {
            staleEditor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            conflictRetained=staleEditor.isVisible() && !staleEditor.findChild<QLabel *>("saveError")->isHidden() &&
                staleEditor.findChild<QLineEdit *>("AWB:SHIPPER_CONTACT")->text()=="Unsaved local work";
            staleEditor.reject();
        });
        staleEditor.exec(); require(conflictRetained,"Conflict keeps form and input");
        require(store.record(awb.id).values["AWB:SHIPPER_CONTACT"]=="Another editor","Conflict did not overwrite newer edit");
        const auto pdfPath=temp.path()+"/draft.pdf"; exportAirWaybill(savedAir,pdfPath);
        QFile pdfFile(pdfPath); require(pdfFile.open(QIODevice::ReadOnly) && pdfFile.read(4)=="%PDF","Native AWB PDF export");
        if (argc==3) {
            const QString artifacts=QString::fromLocal8Bit(argv[2]); require(QDir().mkpath(artifacts),"Artifact directory");
            exportAirWaybill(savedAir,artifacts+"/awb-draft.pdf");
            auto longAir=savedAir; QStringList notes;
            for (int i=1;i<=80;++i) notes << QString("NOTE-%1: Synthetic shipping instructions for pagination validation.").arg(i,3,10,QChar('0'));
            longAir.values["AWB:EXTENDED_DESCRIPTION"]=notes.join('\n'); bool tooLong=false;
            try { exportAirWaybill(longAir,temp.filePath("too-long.pdf")); } catch (const std::exception &e) { tooLong=QString::fromUtf8(e.what()).contains("does not fit"); }
            require(tooLong && !QFile::exists(temp.filePath("too-long.pdf")),"Original AWB form must not clip or change layout for oversize notes");
            RecordEditor preview(store,savedAir); preview.show(); app.processEvents();
            require(preview.grab().save(artifacts+"/awb-editor.png"),"Editor screenshot");
        }
        // Header-only ocean totals must survive a dock-receipt copy too.
        auto emptyShipment=store.blankRecord("BOOKING.TPS"); emptyShipment.values["BOO:REFERENCE_NUMBER"]="TEST-HEADER-ONLY";
        emptyShipment=store.record(store.save(emptyShipment));
        auto headerOnly=documentFromBooking(emptyShipment,store.blankRecord("OBL.TPS")); headerOnly.values["OBL:PCS"]="3"; headerOnly.values["OBL:KGS"]="48";
        headerOnly=store.record(store.save(headerOnly)); require(store.cargoLines(headerOnly).isEmpty(),"Header-only fixture unexpectedly has cargo");
        auto headerReceipt=marineDocumentFromDocument(headerOnly,documentFromBooking(emptyShipment,store.blankRecord("DODR.TPS")));
        headerReceipt=store.record(store.save(headerReceipt)); const auto headerCargo=store.cargoLines(headerReceipt);
        require(headerCargo.size()==1 && headerCargo[0].values["DLI:PCS"]=="3" && headerCargo[0].values["DLI:WEIGHT_KGS"]=="48","Legacy header-only amounts lost during copy");
        // Shipment-first workspace: menu destinations, document tabs and sequences.
        Window window(copy); window.show(); app.processEvents();
        auto *recordsView=window.findChild<QTableView *>("shipmentsTable"); require(recordsView,"Shipments table missing");
        auto *documentTabs=window.findChild<QTabBar *>("shipmentDocumentTabs");
        auto *picker=window.findChild<QComboBox *>("shipmentDocumentVersion");
        require(documentTabs && !documentTabs->isVisible() && documentTabs->count()==DocumentTypes.size()+1,"Unselected startup workspace should keep document tabs hidden");
        require(recordsView->model()->columnCount()==4 && recordsView->model()->headerData(3,Qt::Horizontal).toString()=="Booking number","Startup must show shipments");
        require(window.findChildren<QToolBar *>().isEmpty(),"Database tools still dominate the workspace");
        const auto navigate=[&](int section) {
            const QStringList names{"showCustomers","showContacts","showShipments","showAllDocuments","showOriginalPdfs"};
            auto *action=window.findChild<QAction *>(names[section]); require(action,"Menu destination missing");
            if(section<2) QTimer::singleShot(0,&window,[&] { if(auto *dialog=window.findChild<QDialog *>("addressBook")) dialog->reject(); });
            action->trigger(); app.processEvents();
        };
        for(int section : {0,1,3,4,2}) navigate(section);
        const auto chooseBooking=[&](qint64 id) {
            navigate(2);
            for(int row=0;row<recordsView->model()->rowCount();++row)
                if(recordsView->model()->index(row,0).data(Qt::UserRole).toLongLong()==id) { recordsView->selectRow(row); return; }
            throw std::runtime_error("Booking missing from workspace");
        };
        chooseBooking(historicalBooking.id);
        const auto expectedLinks=store.linkedDocuments(historicalBooking.id);
        for(int i=0;i<DocumentTypes.size();++i) {
            documentTabs->setCurrentIndex(i+1); QSet<qint64> expected,actual;
            for(const auto &link : expectedLinks) if(link.record.source==DocumentTypes[i].first) expected.insert(link.record.id);
            for(int row=0;row<picker->count();++row) if(picker->itemData(row).toLongLong()) actual.insert(picker->itemData(row).toLongLong());
            require(actual==expected,"Shipment tab lost a linked document sequence");
            require(window.findChild<QPushButton *>("previewDocumentButton")->isEnabled()==!expected.isEmpty(),"Empty tab enables preview");
        }
        // Create a dock receipt through the actual shipment tab, including
        // selecting a specific ocean document and copying all its cargo rows.
        Record ocean;
        for(const auto &link : store.linkedDocuments(bookingId)) if(link.record.source=="OBL.TPS") ocean=store.record(link.record.id);
        ocean.values["OBL:BL_NUMBER"]="DEMO-OCEAN-001"; ocean.values["OBL:VESSEL_NAME"]="Example Vessel";
        ocean.values["OBL:NOTIFY_ARRAY"]=QJsonArray{"Example Agent","Dock Road","Rotterdam","Netherlands","","",""};
        store.save(ocean); ocean=store.record(ocean.id);
        chooseBooking(bookingId); documentTabs->setCurrentIndex(3);
        require(documentTabs->tabText(3).startsWith("Dock receipts"),"Dock receipt tab missing");
        for(int i=0;i<documentTabs->count();++i) require(!documentTabs->tabText(i).contains("Dangerous"),"Paused DG workflow remains visible");
        bool copied=false,savedDock=false;
        QTimer createTimer; createTimer.setInterval(30);
        QObject::connect(&createTimer,&QTimer::timeout,&window,[&] {
            if(auto *choice=qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { choice->accept(); return; }
            if(auto *dockEditor=dynamic_cast<RecordEditor *>(QApplication::activeModalWidget())) {
                auto *number=dockEditor->findChild<QLineEdit *>("DOD:BL_NUMBER");
                copied=number && number->text()=="DEMO-OCEAN-001" && dockEditor->findChild<QLineEdit *>("DOD:VESSEL_NAME")->text()=="Example Vessel";
                if(!copied) { createTimer.stop(); dockEditor->reject(); return; }
                dockEditor->findChild<QLineEdit *>("DOD:DO_NUMBER")->setText("DEMO-DOCK-001");
                dockEditor->findChild<QLineEdit *>("DOD:PIER_TERMINAL")->setText("Example Terminal");
                dockEditor->findChild<QLineEdit *>("DOD:CUTOFF")->setText("2026-12-02");
                if(argc==3) dockEditor->grab().save(QString::fromLocal8Bit(argv[2])+"/dock-receipt-editor.png");
                createTimer.stop(); dockEditor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); savedDock=dockEditor->result()==QDialog::Accepted;
            }
        });
        createTimer.start(); window.findChild<QPushButton *>("createDocumentButton")->click();
        require(copied && savedDock && picker->count()==2,"Create dock receipt from ocean BOL failed");
        const auto createdDock=store.record(picker->currentData().toLongLong());
        require(createdDock.values["NATIVE:MARINE_SOURCE_ID"]==QString::number(ocean.id),"Selected marine source provenance lost");
        const auto copiedCargo=store.cargoLines(createdDock); const auto sourceCargo=store.cargoLines(ocean);
        require(copiedCargo.size()==3 && copiedCargo.size()==sourceCargo.size(),"Marine cargo copy omitted or duplicated booking cargo");
        for(int i=0;i<copiedCargo.size();++i) for(const auto &suffix : {"DESCRIPTION","WEIGHT_KGS","PCS","MARKS_NUMBERS"})
            require(copiedCargo[i].values["DLI:"+QString(suffix)]==sourceCargo[i].values["OLI:"+QString(suffix)],"Cargo copy changed data");
        bool tabPreview=false;
        QTimer::singleShot(1200,&window,[&] {
            auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
            auto *view=dialog->findChild<QPdfView *>("previewView");
            if(view && view->document() && view->document()->status()==QPdfDocument::Status::Ready) {
                const auto text=view->document()->getAllText(0).text(); tabPreview=text.contains("DOCK RECEIPT") && text.contains("DEMO-DOCK-001") && text.contains("Revised cargo");
            }
            dialog->reject();
        });
        window.findChild<QPushButton *>("previewDocumentButton")->click(); require(tabPreview,"Shipment tab preview selected the wrong marine document");
        RecordEditor reopenDock(store,createdDock); require(reopenDock.findChild<QLineEdit *>("DOD:PIER_TERMINAL")->text()=="Example Terminal","Dock editor did not reopen saved data");
        // Reverse transfer creates an independent ocean document and preserves source versions.
        auto reverse=marineDocumentFromDocument(createdDock,documentFromBooking(store.record(bookingId),store.blankRecord("OBL.TPS")));
        const auto reverseId=store.save(reverse); const auto reverseCargo=store.cargoLines(store.record(reverseId));
        require(reverseCargo.size()==3 && reverseCargo[1].values["OLI:WEIGHT_KGS"]=="120.5","Dock-to-ocean cargo copy failed");
        auto stale=marineDocumentFromDocument(ocean,documentFromBooking(store.record(bookingId),store.blankRecord("DODR.TPS")));
        auto changed=ocean; changed.values["OBL:VESSEL_NAME"]="Updated Vessel"; store.save(changed);
        const auto countBefore=store.records({"DODR.TPS"}).size(); bool staleRejected=false;
        try { store.save(stale); } catch(const std::exception &) { staleRejected=true; }
        require(staleRejected && store.records({"DODR.TPS"}).size()==countBefore,"Stale source left a partial marine document");
        if(argc==3) window.grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-document-workspace.png");
        chooseBooking(historicalBooking.id); documentTabs->setCurrentIndex(0);
        if(argc==3) window.grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-overview.png");
        QTextStream(stdout)<<"Imported regression: "<<checkedBookings<<" booking matches, "<<checkedAir<<" AWB totals, shipment tabs, all linked sequences, menus and dock receipt creation from ocean BOL.\n";
        require(store.documents().size()>0,"Historical PDFs missing");
        const auto historical=store.document(store.documents()[0].first); require(historical.startsWith("%PDF"),"Historical PDF signature");
        // Exercise the user's Preview button, including imported and generated documents.
        auto *previewButton=window.findChild<QPushButton *>("previewDocumentButton"); require(previewButton,"Preview action missing");
        const auto previewRecord=[&](int section, qint64 id, const QString &source, const QString &artifact, bool continuous=false) {
            navigate(section);
            if (section==3) {
                auto *type=window.findChild<QComboBox *>("archiveDocumentType"); type->setCurrentIndex(type->findData(source));
            }
            bool selected=false;
            for (int row=0;row<recordsView->model()->rowCount();++row)
                if (recordsView->model()->index(row,0).data(Qt::UserRole).toLongLong()==id) { recordsView->selectRow(row); selected=true; break; }
            require(selected && previewButton->isEnabled(),"Could not select document for preview");
            bool displayed=false, unchanged=false; QString failureDetail;
            QTimer::singleShot(2500,&window,[&] {
                auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
                if (const auto *error=qobject_cast<QMessageBox *>(dialog)) failureDetail=error->text();
                auto *pdfView=dialog->findChild<QPdfView *>("previewView");
                displayed=dialog->objectName()=="documentPreview" && pdfView && pdfView->document()->pageCount()>0 &&
                          pdfView->document()->status()==QPdfDocument::Status::Ready;
                if (displayed && continuous) displayed=pdfView->document()->pagePointSize(0)==QSizeF(684,864);
                if (displayed) {
                    const auto image=pdfView->viewport()->grab().toImage(); int ink=0;
                    // Inspect the central paper area, excluding surrounding viewer controls.
                    for (int y=image.height()/10;y<image.height()*9/10;++y)
                        for (int x=image.width()/4;x<image.width()*3/4;++x) if (qGray(image.pixel(x,y))<128) ++ink;
                    displayed=ink>100;
                }
                if (displayed && argc==3) dialog->grab().save(QString::fromLocal8Bit(argv[2])+"/"+artifact+".png");
                if (displayed && section==4) {
                    auto *preview=dynamic_cast<DocumentPreview *>(dialog);
                    if (preview) {
                        const auto savedPath=temp.filePath("original-copy.pdf"); preview->saveCopy(savedPath);
                        QFile originalCopy(savedPath); unchanged=originalCopy.open(QIODevice::ReadOnly) && originalCopy.readAll()==store.document(id);
                    }
                }
                dialog->reject();
            });
            if (continuous) {
                QTimer::singleShot(0,&window,[&] {
                    auto *setup=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (setup && setup->objectName()=="preprintedAwbSetup") {
                        setup->findChild<QDoubleSpinBox *>("printOffsetX")->setValue(1.25);
                        setup->findChild<QDoubleSpinBox *>("printOffsetY")->setValue(-.75);
                        setup->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                    }
                });
                window.findChild<QPushButton *>("preprintedAwbButton")->click();
            } else previewButton->click();
            if (!displayed) throw std::runtime_error(("Preview action did not open a readable document: "+artifact+" "+failureDetail).toStdString());
            if (section==4) require(unchanged,"Original PDF preview changed the saved bytes");
        };
        previewRecord(2,historicalBooking.id,"BOOKING.TPS","booking-preview");
        for (const auto &link : store.linkedDocuments(bookingId))
            previewRecord(3,link.record.id,link.record.source,"created-"+link.record.source+"-preview");
        // Reproduce the dark desktop palette that exposed the invisible export text.
        QPalette dark; dark.setColor(QPalette::Window,QColor("#20262e")); dark.setColor(QPalette::WindowText,Qt::white);
        dark.setColor(QPalette::Base,QColor("#161b22")); dark.setColor(QPalette::Text,Qt::white);
        dark.setColor(QPalette::Button,QColor("#30363d")); dark.setColor(QPalette::ButtonText,Qt::white);
        QApplication::setPalette(dark);
        require(previewAir.id,"Reported AWB preview regression fixture missing");
        previewRecord(3,previewAir.id,"AWB.TPS","imported-awb-preview-dark");
        previewRecord(3,previewAir.id,"AWB.TPS","preprinted-awb-preview-dark",true);
        require(goodsAir.id,"Reported AWB goods overflow fixture missing");
        require(airWaybillContext(store,goodsAir).destination=="SIN - Singapore (Changi)","Imported airport directory did not complete the destination");
        RecordEditor weightDisplay(store,goodsAir);
        const auto *weightGrid=weightDisplay.findChild<QTableWidget *>("airCargo");
        require(weightGrid && weightGrid->item(0,1)->text()=="11828" && weightGrid->item(0,3)->text()=="11828","Whole weights still have .00 in the editor");
        require(weightDisplay.findChild<QLineEdit *>("AWB:GROSS_WEIGHT_TOTAL")->text()=="11828","Whole total still has .00 in the editor");
        const auto goodsHistory=store.history(goodsAir.id).size();
        previewRecord(3,goodsAir.id,"AWB.TPS","goods-awb-preview-dark");
        previewRecord(3,goodsAir.id,"AWB.TPS","goods-preprinted-awb-preview-dark",true);
        require(store.record(goodsAir.id).values==goodsAir.values && store.history(goodsAir.id).size()==goodsHistory,"Preview changed the reported AWB's stored data");
        previewRecord(4,store.documents()[0].first,{},"original-pdf-preview-dark");
        for (const auto &[id,path] : store.documents()) {
            QBuffer bytes; bytes.setData(store.document(id)); bytes.open(QIODevice::ReadOnly);
            QPdfDocument pdf; pdf.load(&bytes);
            require(pdf.status()==QPdfDocument::Status::Ready && pdf.pageCount()>0,"A preserved PDF failed to load for preview");
        }
        QTextStream(stdout)<<"Preview regression: booking, six generated document types, imported AWB, exact original PDF saving and "<<store.documents().size()<<" preserved PDFs loaded.\n";
        QTextStream(stdout)<<"PASS: editor save and hidden-field preservation; new contact, booking, six document types; cargo create/edit/link/numbering; dates; shipment workspace and menu destinations; original PDF retrieval. AWB booking transfer, exact cargo totals, validation retry, conflict input retention, hidden-field preservation, history, native PDFs and snapshot backup. Input database unchanged.\n";
        return 0;
    } catch (const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
