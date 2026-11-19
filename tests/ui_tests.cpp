// Exercise actual UI actions on a temporary, consistent database snapshot.
#define SHADOW_LOGIS_GUI_TESTING
#include "../native/main.cpp"
#include "fixtures.h"
#include <QScrollBar>
#include <QPdfView>
#include <QPdfSelection>
#include <QKeyEvent>
#include <cstdlib>
namespace {
void require(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    installAppearance(app);
    if(argc>3) return 2;
    try {
        QTimer::singleShot(60000,&app,[] { std::exit(2); });
        QTemporaryDir tmp; const auto db=tmp.filePath("ui.sqlite"); if(argc==1) createSyntheticWorkspace(db); else { Store original(QString::fromLocal8Bit(argv[1])); original.backup(db); }
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,tmp.path());
        Store store(db); Window window(db); window.show(); app.processEvents();
        auto *table=window.findChild<QTableView *>("shipmentsTable"); auto *tabs=window.findChild<QTabBar *>("shipmentDocumentTabs");
        auto *add=window.findChild<QPushButton *>("newShipment"); auto *edit=window.findChild<QPushButton *>("editShipment"); auto *remove=window.findChild<QPushButton *>("deleteShipment");
        require(add && edit && remove && add->isVisible(),"Shipment actions missing");
        require(add->text()=="Add" && edit->text()=="Edit" && remove->text()=="Delete","Shipment button labels are not concise");
        require(!edit->isEnabled() && !remove->isEnabled(),"Selection actions enabled without shipment");
        auto *panel=window.findChild<QWidget *>("shipmentPanel");
        require(panel && !panel->isVisible(),"Shipment panel is visible without a selection");
        require(table->height()>window.centralWidget()->height()*.8,"Hidden panel still reserves space");
        if(argc==3) window.grab().save(QString::fromLocal8Bit(argv[2])+"/workspace-no-selection.png");
        auto *header=table->horizontalHeader();
        require(header->sectionsMovable() && header->isFirstSectionMovable(),"Booking columns cannot all be rearranged");
        header->moveSection(header->visualIndex(3),0); header->moveSection(header->visualIndex(0),2);
        const auto columnOrder=[&] { QList<int> result; for(int i=0;i<header->count();++i) result<<header->logicalIndex(i); return result; };
        const auto preferredColumns=columnOrder();
        require(table->model()->columnCount()==4,"Status still appears in list");
        for(auto *label : window.findChildren<QLabel *>()) require(!label->isVisible() || !label->text().startsWith("Select a shipment, then"),"Introductory text remains");
        const auto select=[&](qint64 id) {
            for(int row=0;row<table->model()->rowCount();++row) if(table->model()->index(row,0).data(Qt::UserRole).toLongLong()==id) { table->selectRow(row); app.processEvents(); return; }
            throw std::runtime_error("Record missing from UI list");
        };
        const auto runEditor=[&](QPushButton *button,const std::function<void(RecordEditor *)> &fill) {
            bool saved=false; qint64 id=0; QString failure;
            QTimer driver;
            QObject::connect(&driver,&QTimer::timeout,&window,[&] {
                if(auto *choice=qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { choice->findChild<QComboBox *>()->setCurrentIndex(0); choice->accept(); return; }
                driver.stop();
                auto *editor=dynamic_cast<RecordEditor *>(QApplication::activeModalWidget());
                if(!editor) { failure="Editor did not open"; if(auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); return; }
                try { fill(editor); editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); saved=editor->result()==QDialog::Accepted; id=editor->value.id; if(!saved) failure=editor->findChild<QLabel *>("saveError")->text(); }
                catch(const std::exception &e) { failure=e.what(); }
                if(!saved) editor->reject();
            });
            driver.start(0); button->click(); driver.stop(); if(!saved) throw std::runtime_error(("Editor save failed: "+failure).toStdString()); return id;
        };
        const auto id=runEditor(add,[&](RecordEditor *e) {
            require(e->value.source=="BOOKING.TPS" && !e->value.id,"Add opened wrong record");
            require(!e->findChild<QLineEdit *>("BOO:STATUS"),"Status remains editable");
            e->findChild<QLineEdit *>("BOO:REFERENCE_NUMBER")->setText("UI-DEMO-001");
            e->findChild<QLineEdit *>("BOO:BOOKING_NUMBER")->setText("BOOK-DEMO-001");
            e->findChild<QLineEdit *>("BOO:READY_DATE")->setText("2026-12-03");
            e->findChild<QLineEdit *>("BOO:VESSEL_NAME")->setText("Example Vessel");
            e->findChild<QLineEdit *>("BOO:LOAD_PORT")->setText("Houston"); e->findChild<QLineEdit *>("BOO:DISCHARGE_PORT")->setText("Rotterdam");
            e->findChild<QLineEdit *>("BOO:ORIGIN_LOCATION")->setText("Houston, TX"); e->findChild<QLineEdit *>("BOO:DESTINATION_LOCATION")->setText("Rotterdam, NL");
            e->findChild<QLineEdit *>("BOO:COMMODITY_1")->setText("Precision machine parts");
            e->findChild<QLineEdit *>("BOO:QTY")->setText("12"); e->findChild<QLineEdit *>("BOO:KGS")->setText("850");
            e->findChild<QPlainTextEdit *>("BOO:SHIPPER_ARRAY")->setPlainText("Example Exporter\n123 Sample Road\nHouston, TX 77001\nUnited States");
            e->findChild<QPlainTextEdit *>("BOO:CONSIGNEE_ARRAY")->setPlainText("Example Importer\n456 Harbour Street\nRotterdam\nNetherlands");
            app.processEvents();
            auto *scroll=e->findChild<QScrollArea *>("shipmentEditorScroll"); require(scroll,"Compact editor missing");
            require(scroll->horizontalScrollBar()->maximum()==0,"Shipment editor requires horizontal scrolling");
            int visible=0;
            for(const auto &[key,label] : editableFields("BOOKING.TPS")) if(auto *field=e->findChild<QWidget *>(key)) {
                const QRect area(field->mapTo(scroll->viewport(),QPoint(0,0)),field->size());
                if(scroll->viewport()->rect().intersected(area).height()>=field->height()*.9) ++visible;
            }
            require(visible>=22,"Too few shipment fields visible at once");
            if(argc==3) e->grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-editor.png");
            e->resize(900,700); app.processEvents();
            require(scroll->horizontalScrollBar()->maximum()==0,"Smaller editor requires horizontal scrolling");
            if(argc==3) e->grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-editor-small.png");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); app.processEvents();
            const auto *email=e->findChild<QLineEdit *>("BOO:CLIENE_EMAIL");
            require(scroll->viewport()->rect().contains(email->mapTo(scroll->viewport(),email->rect().center())),"Lower fields inaccessible in smaller editor");
            if(argc==3) e->grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-editor-small-bottom.png");
        });
        auto shipment=store.record(id); shipment.values["BOO:STATUS"]="Preserved legacy status"; store.save(shipment); shipment=store.record(id);
        require(columnOrder()==preferredColumns,"Saving a shipment reset the user's column order");
        {
            Window reopened(db); auto *reopenedHeader=reopened.findChild<QTableView *>("shipmentsTable")->horizontalHeader();
            for(int i=0;i<preferredColumns.size();++i) require(reopenedHeader->logicalIndex(i)==preferredColumns[i],"Reopening lost booking column order");
        }
        auto air=documentFromBooking(shipment,store.blankRecord("AWB.TPS"));
        air.values["AWB:AWB_ACCOUNTING_INFO"]=QJsonArray{"Agent reference","","Example agent","","","","","","",""};
        const auto awb=store.record(store.save(air));
        tabs->setCurrentIndex(1); app.processEvents();
        const auto beforeEdit=store.record(id);
        runEditor(edit,[&](RecordEditor *e) { require(e->value.id==id && e->value.source=="BOOKING.TPS","Edit shipment edited the selected document instead"); e->findChild<QLineEdit *>("BOO:PO_NUMBER")->setText("NEW-PO"); });
        shipment=store.record(id); require(shipment.values["BOO:STATUS"]=="Preserved legacy status","Hidden status was changed");
        for(auto it=beforeEdit.values.begin();it!=beforeEdit.values.end();++it) if(it.key()!="BOO:PO_NUMBER") require(shipment.values[it.key()]==it.value(),"Shipment edit changed another field");
        require(store.record(awb.id).values==awb.values,"Shipment edit rewrote linked document");
        const auto confirm=[&](QMessageBox::StandardButton action) {
            QTimer::singleShot(0,&window,[&window,action] { auto *box=window.findChild<QMessageBox *>("confirmShipmentDelete"); if(box) box->button(action)->click(); }); remove->click();
        };
        confirm(QMessageBox::Cancel); require(store.record(id).version==shipment.version,"Cancel deletion changed shipment");
        confirm(QMessageBox::Yes);
        require(store.deletedShipments().size()>=1 && !store.record(id).values["NATIVE:DELETED_AT"].toString().isEmpty(),"Delete did not mark shipment");
        for(const auto &r : store.records({"BOOKING.TPS"})) require(r.id!=id,"Deleted shipment still active");
        require(store.record(awb.id).values==awb.values && store.linkedDocuments(id).size()==1,"Deletion lost document links");
        auto duplicate=store.blankRecord("BOOKING.TPS"); duplicate.values["BOO:REFERENCE_NUMBER"]="UI-DEMO-001"; bool rejected=false;
        try { store.save(duplicate); } catch(const std::exception &) { rejected=true; } require(rejected,"Deleted shipment reference was reused");
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=window.findChild<QDialog *>("deletedShipments"); if(!dialog) return;
            auto *list=dialog->findChild<QListWidget *>("deletedShipmentsList");
            for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toLongLong()==id) list->setCurrentRow(i);
            if(argc==3) dialog->grab().save(QString::fromLocal8Bit(argv[2])+"/deleted-shipments.png");
            dialog->findChild<QPushButton *>("restoreShipment")->click();
        });
        window.findChild<QAction *>("showDeletedShipments")->trigger();
        require(store.record(id).values==shipment.values && store.record(id).version==shipment.version+2,"Restore changed shipment data");
        select(id); require(panel->isVisible(),"Selecting a shipment did not show its details");
        table->clearSelection(); app.processEvents();
        require(!panel->isVisible() && !edit->isEnabled() && !remove->isEnabled(),"Clearing selection leaves shipment details or actions active");
        select(id);
        window.findChild<QLineEdit *>("shipmentSearch")->setText("NO-MATCH-SHIPMENT-UI-TEST"); app.processEvents();
        require(table->model()->rowCount()==0 && !panel->isVisible(),"Filtering out the shipment leaves its panel visible");
        window.findChild<QLineEdit *>("shipmentSearch")->clear(); app.processEvents(); select(id);
        require(panel->isVisible() && panel->height()>200 && table->height()>100,"Reselection did not restore the split workspace");
        window.findChild<QAction *>("showOriginalPdfs")->trigger(); app.processEvents();
        require(header->count()==1 && !header->sectionsMovable(),"Booking order affected the PDF archive");
        window.findChild<QAction *>("showShipments")->trigger(); app.processEvents(); select(id);
        require(columnOrder()==preferredColumns,"Returning from the archive reset booking columns");
        window.findChild<QAction *>("resetShipmentColumnOrder")->trigger();
        require(columnOrder()==QList<int>({0,1,2,3}),"Reset column order did not restore the defaults");
        if(argc==3) window.grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-workspace.png");
        // Closing the panel clears only selection, from either the keyboard or mouse.
        const auto escape=[&](QWidget *target) {
            QKeyEvent press(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(target,&press);
            QKeyEvent release(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(target,&release); app.processEvents();
        };
        auto *close=window.findChild<QToolButton *>("closeShipmentDetails"); require(close,"Details close button missing");
        auto *searchBox=window.findChild<QLineEdit *>("shipmentSearch"); searchBox->setText("UI-DEMO"); tabs->setCurrentIndex(1);
        const auto beforeClose=store.record(id); const auto beforeOrder=columnOrder();
        for(QWidget *focus : QList<QWidget *>{searchBox,table,tabs,window.findChild<QTextBrowser *>("shipmentOverview")}) {
            select(id); window.activateWindow(); focus->setFocus(); app.processEvents(); escape(focus);
            require(!table->selectionModel()->hasSelection() && !panel->isVisible() && !edit->isEnabled() && !remove->isEnabled(),"Escape did not clear the selected booking");
            require(table->hasFocus() && table->height()>window.centralWidget()->height()*.8,"Escape did not restore the full-height list and keyboard focus");
            require(searchBox->text()=="UI-DEMO" && columnOrder()==beforeOrder,"Escape reset the list's search or column order");
        }
        select(id); require(close->isVisible(),"Details close button is hidden"); close->click(); app.processEvents();
        require(!panel->isVisible() && !table->selectionModel()->hasSelection() && searchBox->text()=="UI-DEMO","Mouse close did not clear only the booking selection");
        select(id);
        auto *picker=window.findChild<QComboBox *>("shipmentDocumentVersion"); picker->setFocus(); picker->showPopup(); app.processEvents();
        require(QApplication::activePopupWidget(),"Document picker popup did not open"); escape(QApplication::focusWidget());
        require(!QApplication::activePopupWidget() && panel->isVisible() && table->selectionModel()->hasSelection(),"Escape in a popup closed the booking instead of the popup");
        RecordEditor cancelEditor(store,store.record(id),&window);
        QTimer::singleShot(0,&cancelEditor,[&] {
            auto *input=cancelEditor.findChild<QLineEdit *>("BOO:PO_NUMBER"); input->setText("UNSAVED ESCAPE TEST");
            // Deliver the queued activation before simulating a key in a newly opened dialog.
            cancelEditor.activateWindow(); input->setFocus(); app.processEvents(); escape(input);
        });
        require(cancelEditor.exec()==QDialog::Rejected && panel->isVisible() && table->selectionModel()->hasSelection(),"Escape in an editor cleared the parent booking selection");
        require(store.record(id).values==beforeClose.values && store.record(id).version==beforeClose.version,"Closing details changed the booking record");
        window.activateWindow(); table->setFocus(); app.processEvents();
        // Address book opens over the workspace without replacing its selection/search/tab.
        auto *shipmentSearch=window.findChild<QLineEdit *>("shipmentSearch"); shipmentSearch->setText("UI-DEMO"); tabs->setCurrentIndex(1); app.processEvents();
        const auto workspaceIndex=QPersistentModelIndex(table->currentIndex());
        const auto documentId=window.findChild<QComboBox *>("shipmentDocumentVersion")->currentData();
        const auto addressBook=[&](int initial,const std::function<void(QDialog *)> &work) {
            QString failure; bool opened=false;
            QTimer::singleShot(0,&window,[&] {
                auto *dialog=window.findChild<QDialog *>("addressBook");
                if(!dialog) { failure="Address book did not open"; return; }
                try {
                    require(dialog->isWindow() && dialog->parentWidget()==&window && window.isVisible(),"Address book must be a separate owned window");
                    require(dialog->findChild<QTabBar *>("addressBookTabs")->currentIndex()==initial,"Wrong address book tab");
                    work(dialog); opened=true;
                } catch(const std::exception &e) { failure=e.what(); }
                dialog->reject();
            });
            window.findChild<QAction *>(initial==0 ? "showCustomers" : "showContacts")->trigger();
            if(!opened) throw std::runtime_error(failure.toStdString());
            require(table->currentIndex()==workspaceIndex && shipmentSearch->text()=="UI-DEMO" && tabs->currentIndex()==1 && window.findChild<QComboBox *>("shipmentDocumentVersion")->currentData()==documentId,"Address book changed shipment workspace context");
        };
        for(int initial : {1,0}) addressBook(initial,[&](QDialog *dialog) {
            const auto record=store.records({initial==0 ? "CUSTOMER.TPS" : "CONTACTS.TPS"}).first();
            auto *list=dialog->findChild<QTableView *>("addressBookTable");
            for(int row=0;row<list->model()->rowCount();++row) if(list->model()->index(row,0).data(Qt::UserRole).toLongLong()==record.id) list->selectRow(row);
            auto *button=dialog->findChild<QPushButton *>("addressBookEdit"); require(button->isEnabled(),"Address book Edit unavailable");
            const QString key=initial==0 ? "CUS:PH1_NUMBER" : "CTA:PHONE";
            runEditor(button,[&](RecordEditor *e) { require(e->value.id==record.id,"Wrong address book record opened"); e->findChild<QLineEdit *>(key)->setText("+1 202 555 0132"); });
            require(store.record(record.id).values[key]=="+1 202 555 0132","Address book edit not persisted");
            for(auto it=record.values.begin();it!=record.values.end();++it) if(it.key()!=key) require(store.record(record.id).values[it.key()]==it.value(),"Address book edit changed another field");
            if(argc==3) dialog->grab().save(QString::fromLocal8Bit(argv[2])+(initial==0 ? "/address-book-customers.png" : "/address-book-contacts.png"));
            if(initial==1) {
                const auto added=runEditor(dialog->findChild<QPushButton *>("addressBookAdd"),[&](RecordEditor *e) { e->findChild<QLineEdit *>("CTA:FIRST_NAME")->setText("UI Example"); e->findChild<QLineEdit *>("CTA:LAST_NAME")->setText("Contact"); });
                require(store.record(added).source=="CONTACTS.TPS","Address book Add failed");
                auto *filter=dialog->findChild<QLineEdit *>("addressBookSearch"); filter->setText("UI Example"); app.processEvents(); require(list->model()->rowCount()==1,"Address book search failed");
                dialog->findChild<QTabBar *>("addressBookTabs")->setCurrentIndex(0); app.processEvents(); require(filter->text().isEmpty() && list->model()->rowCount()>1,"Address book tab switch failed");
            }
        });
        for(const auto &[source,label] : DocumentTypes) {
            // Modeless references keep the shipment workspace and its selection.
            for(const auto &action : {"showAirlineCodes","showAirportCodes","showCountryCodes","showCurrencyCodes","showIncoterms"}) {
                window.findChild<QAction *>(action)->trigger(); app.processEvents();
                QDialog *lookup=nullptr;
                for(auto *child : window.findChildren<QDialog *>()) if(child->isVisible() && (child->objectName()=="airlineReferenceWindow" || child->objectName().startsWith("referenceWindow_"))) lookup=child;
                require(lookup && lookup->isWindow() && !lookup->isModal(),"Reference lookup is not a separate modeless window");
                require(table->currentIndex()==workspaceIndex && shipmentSearch->text()=="UI-DEMO","Reference lookup changed shipment workspace context");
                lookup->close(); app.processEvents();
            }
            const auto doc=source=="AWB.TPS" ? store.record(awb.id) : store.record(store.save(documentFromBooking(shipment,store.blankRecord(source))));
            if(source=="OBL.TPS") {
                for(int i=0;i<14;++i) store.save(store.blankCargo(doc));
                for(const auto &description : {"Dimensions: 2 @ 48 x 40 x 33 in","HS code: sample reference","Export filing: sample reference","FREIGHT PREPAID","EXPRESS RELEASE"}) {
                    auto note=store.blankCargo(doc); note.values["OLI:DESCRIPTION"]=description; store.save(note);
                }
            }
            RecordEditor editor(store,doc); editor.show(); app.processEvents(); app.processEvents();
            if(source=="OBL.TPS") {
                const auto beforeCargo=store.cargoLines(doc);
                auto *cargo=editor.findChild<QTableWidget *>("cargoTable"); auto *showBlank=editor.findChild<QCheckBox *>("showBlankCargoRows");
                auto *section=editor.findChild<QGroupBox *>("documentCargoSection");
                require(section && section->isAncestorOf(cargo) && !editor.findChild<QPushButton *>("documentCargoLines"),"Cargo is not embedded in the document page");
                require(cargo->rowCount()==6 && !showBlank->isChecked() && !cargo->isSortingEnabled(),"Cargo notes or blank filtering incorrect");
                require(cargo->item(0,3)->text().contains("850 kg") && !cargo->item(1,1)->text().isEmpty(),"Cargo weights or notes missing");
                showBlank->setChecked(true); require(cargo->rowCount()==20,"Blank cargo rows lost");
                for(int i=0;i<beforeCargo.size();++i) require(cargo->item(i,0)->data(Qt::UserRole).toLongLong()==beforeCargo[i].id,"Cargo order changed");
                cargo->selectRow(1); require(editor.findChild<QPushButton *>("cargoEdit")->isEnabled(),"Blank cargo row cannot be edited");
                showBlank->setChecked(false); require(!editor.findChild<QPushButton *>("cargoEdit")->isEnabled(),"Hidden selection can edit the wrong row");
                const auto unchanged=store.cargoLines(doc);
                for(int i=0;i<beforeCargo.size();++i) require(unchanged[i].values==beforeCargo[i].values && unchanged[i].version==beforeCargo[i].version,"Viewing cargo changed stored records");
                const auto noteId=beforeCargo.last().id; cargo->selectRow(5);
                const auto edited=runEditor(editor.findChild<QPushButton *>("cargoEdit"),[&](RecordEditor *row) {
                    require(row->value.id==noteId,"Filtered cargo Edit opened the wrong record"); row->findChild<QLineEdit *>("OLI:DESCRIPTION")->setText("REVISED RELEASE NOTE");
                });
                require(edited==noteId && cargo->item(cargo->currentRow(),1)->text()=="REVISED RELEASE NOTE","Cargo edit did not refresh the selected row");
                const auto added=runEditor(editor.findChild<QPushButton *>("cargoAdd"),[&](RecordEditor *row) { row->findChild<QLineEdit *>("OLI:DESCRIPTION")->setText("ADDITIONAL HANDLING NOTE"); });
                require(cargo->rowCount()==7 && cargo->item(cargo->currentRow(),0)->data(Qt::UserRole).toLongLong()==added,"New cargo was not selected");
                const auto changed=store.cargoLines(doc);
                for(int i=0;i<beforeCargo.size();++i) if(beforeCargo[i].id!=noteId) require(changed[i].values==beforeCargo[i].values && changed[i].version==beforeCargo[i].version,"Cargo edit changed a sibling record");
                require(store.record(doc.id).values==doc.values && store.record(doc.id).version==doc.version,"Cargo edit changed the document header");
                if(argc==3) {
                    auto *scroll=editor.findChild<QScrollArea *>("documentEditorScroll"); scroll->ensureWidgetVisible(section,0,0); app.processEvents();
                    editor.grab().save(QString::fromLocal8Bit(argv[2])+"/inline-cargo.png");
                    scroll->verticalScrollBar()->setValue(0); app.processEvents();
                }
            }
            if(source=="AWB.TPS") {
                for(const auto &key : {"AWB:SHIPPER_ARRAY","AWB:CONSIGNEE_ARRAY","AWB:AWB_ACCOUNTING_INFO"}) {
                    auto *text=editor.findChild<QPlainTextEdit *>(key); require(text,"AWB text box missing");
                    require(!text->toPlainText().endsWith('\n') && text->verticalScrollBar()->maximum()==0,"Unused array slots create unnecessary scrolling");
                }
                auto *account=editor.findChild<QPlainTextEdit *>("AWB:AWB_ACCOUNTING_INFO");
                const auto original=account->toPlainText(); require(original=="Agent reference\n\nExample agent","Internal blank line lost");
                account->setPlainText(QString("Long editable text\n").repeated(20)); app.processEvents();
                require(account->verticalScrollBar()->maximum()>0,"Long text cannot be scrolled");
                account->setPlainText(original); app.processEvents();
                require(account->verticalScrollBar()->maximum()==0,"Scrollbar remains after text fits");
            }
            auto *saveButton=editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            require(editor.rect().contains(saveButton->mapTo(&editor,saveButton->rect().center())),"Document Save button outside window");
            auto *scroll=editor.findChild<QScrollArea *>("documentEditorScroll"); require(scroll,"Document lacks compact editor");
            require(!editor.findChild<QTabWidget *>(),"Document still uses separate editor tabs");
            require(scroll->horizontalScrollBar()->maximum()==0,"Document needs horizontal scrolling");
            if(argc==3) editor.grab().save(QString::fromLocal8Bit(argv[2])+"/editor-"+source+".png");
            editor.resize(900,700); app.processEvents(); require(scroll->horizontalScrollBar()->maximum()==0,"Smaller document needs horizontal scrolling");
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); app.processEvents();
            if(argc==3) editor.grab().save(QString::fromLocal8Bit(argv[2])+"/editor-"+source+"-bottom.png");
            if(auto *toggle=editor.findChild<QCheckBox *>("showTruckFields")) { require(!editor.findChild<QTableWidget *>("vicsOrders")->isVisible(),"Truck fields should start hidden"); toggle->setChecked(true); app.processEvents(); }
            for(const auto &[key,fieldLabel] : editableFields(source)) {
                if(!doc.values.contains(key) || airCargoKeys().contains(key) || vicsOrderKeys().contains(key)) continue;
                auto *field=editor.findChild<QWidget *>(key); require(field && field->isVisibleTo(&editor),"A supported document field was lost or hidden");
            }
            const auto prefix=editableFields(source).first().first.section(':',0,0)+':';
            const QString key=source=="AWB.TPS" ? "AWB:EXECUTION_PLACE" : source=="SIGHT.TPS" ? "SIG:SD_NUMBER" : prefix+"VESSEL_NAME";
            auto *field=editor.findChild<QLineEdit *>(key); require(field,"Document test edit field missing"); field->setText("UI Example");
            auto *country=editor.findChild<QLineEdit *>("CRO:COUNTRY"); const auto originalCountry=country ? country->text() : QString();
            if(source=="CERTORGN.TPS") {
                require(country && editor.findChild<QLineEdit *>("CRO:COC_AREA") && editor.findChild<QLineEdit *>("CRO:SWORN_DATE"),"Certificate declaration fields are not editable");
                country->setText("Preview origin country");
            }
            auto *pendingCargo=editor.findChild<QTableWidget *>("airCargo"); QString originalWeight;
            if(pendingCargo) { originalWeight=pendingCargo->item(0,1)->text(); pendingCargo->item(0,1)->setText("123.45"); }
            auto *preview=editor.findChild<QPushButton *>("editorPreviewPrint"); require(preview && preview->isVisible(),"Editor Preview / Print missing");
            require(editor.rect().contains(preview->mapTo(&editor,preview->rect().center())),"Preview button outside window");
            bool displayed=false; QString failure;
            QTimer drive;
            QObject::connect(&drive,&QTimer::timeout,&editor,[&] {
                auto *modal=qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if(!modal) return;
                if(modal->objectName()=="preprintedBolSetup") { modal->accept(); return; }
                if(auto *pdf=modal->findChild<QPdfView *>("previewView")) {
                    const auto text=pdf->document()->getAllText(0).text();
                    displayed=text.contains("UI Example") && modal->windowTitle().contains("Unsaved changes") && (!pendingCargo || text.contains("123.45"));
                    if(source=="CERTORGN.TPS") displayed=displayed && text.contains("CERTIFICATE OF ORIGIN") && text.contains("Preview origin country") &&
                        !text.contains("Shipment summary") && modal->windowTitle().contains("Certificate of origin");
                    if(argc==3) modal->grab().save(QString::fromLocal8Bit(argv[2])+"/pending-"+source+".png");
                    drive.stop(); modal->reject();
                } else if(modal!=&editor) { failure=modal->windowTitle(); drive.stop(); modal->reject(); }
            });
            drive.start(100); preview->click(); drive.stop();
            if(!displayed) throw std::runtime_error(("Unsaved editor preview failed: "+source+" "+failure+" "+editor.findChild<QLabel *>("saveError")->text()).toStdString());
            require(store.record(doc.id).values==doc.values && store.record(doc.id).version==doc.version && field->text()=="UI Example","Preview saved or discarded changes");
            if(pendingCargo) pendingCargo->item(0,1)->setText(originalWeight);
            if(country) country->setText(originalCountry);
            editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click(); require(editor.result()==QDialog::Accepted,"Document save failed");
            const auto saved=store.record(doc.id); require(saved.values[key]=="UI Example","Document change not saved");
            for(auto it=doc.values.begin();it!=doc.values.end();++it) if(it.key()!=key) if(saved.values[it.key()]!=it.value()) throw std::runtime_error(("Document layout changed unrelated data: "+source+" "+it.key()+" before="+valueText(it.value())+" after="+valueText(saved.values[it.key()])).toStdString());
            RecordEditor reopened(store,saved); require(reopened.findChild<QLineEdit *>(key)->text()=="UI Example","Document change not reopened");
        }
        // Open through the shipment row's double-click action, and keep all document work inside it.
        QString nestedFailure; bool nestedComplete=false; QList<qint64> nestedIds;
        QTimer::singleShot(0,&window,[&] {
            auto *parent=dynamic_cast<RecordEditor *>(QApplication::activeModalWidget());
            if(!parent) { nestedFailure="Double-click did not open shipment editor"; return; }
            try {
                require(parent->value.id==id,"Double-click opened wrong shipment");
                auto *type=parent->findChild<QComboBox *>("editorDocumentType"); auto *versions=parent->findChild<QComboBox *>("editorDocumentVersion");
                auto *addDoc=parent->findChild<QPushButton *>("editorDocumentAdd"); auto *editDoc=parent->findChild<QPushButton *>("editorDocumentEdit"); auto *viewDoc=parent->findChild<QPushButton *>("editorDocumentView");
                require(type && versions && addDoc && editDoc && viewDoc,"Shipment editor lacks document actions");
                auto *origin=parent->findChild<QLineEdit *>("BOO:ORIGIN_LOCATION"); origin->setText("Updated origin");
                const auto originalShipment=store.record(id); const int originalDocuments=store.linkedDocuments(id).size();
                QTimer::singleShot(0,parent,[&] { if(auto *box=parent->findChild<QMessageBox *>("saveShipmentForDocument")) box->button(QMessageBox::Cancel)->click(); });
                addDoc->click(); require(store.record(id).values==originalShipment.values && store.linkedDocuments(id).size()==originalDocuments && origin->text()=="Updated origin","Cancel before document creation saved or discarded work");
                // Saving the shipment is explicit and must precede creation from its new values.
                QTimer::singleShot(0,parent,[&] {
                    auto *box=parent->findChild<QMessageBox *>("saveShipmentForDocument"); if(!box) return;
                    box->button(QMessageBox::Save)->click();
                    QTimer::singleShot(0,parent,[&] {
                        auto *child=dynamic_cast<RecordEditor *>(QApplication::activeModalWidget());
                        if(child && child!=parent) child->reject();
                    });
                });
                addDoc->click(); require(store.record(id).values["BOO:ORIGIN_LOCATION"]=="Updated origin" && store.linkedDocuments(id).size()==originalDocuments,"Save-before-create or child cancellation failed");
                for(const auto &[source,label] : DocumentTypes) {
                    type->setCurrentIndex(type->findData(source)); app.processEvents();
                    const auto newId=runEditor(addDoc,[&](RecordEditor *child) {
                        require(child->parentWidget()==parent && child->value.source==source && !child->value.id,"Nested Add opened wrong document");
                        bool shown=false; QTimer timer;
                        QObject::connect(&timer,&QTimer::timeout,child,[&] {
                            auto *modal=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!modal) return;
                            if(modal->objectName()=="preprintedBolSetup") { modal->accept(); return; }
                            if(auto *pdf=modal->findChild<QPdfView *>("previewView")) { shown=pdf->document()->getAllText(0).text().contains(source=="SIGHT.TPS" ? "First of Exchange" : "Precision machine parts"); timer.stop(); modal->reject(); }
                        });
                        timer.start(100); child->findChild<QPushButton *>("editorPreviewPrint")->click(); timer.stop();
                        require(shown && !child->value.id,"New document preview omitted cargo or saved the draft");
                        if(source=="AWB.TPS") require(child->findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->text()=="Updated origin","Nested document did not use latest shipment values");
                    });
                    nestedIds<<newId;
                    require(versions->currentData().toLongLong()==newId && editDoc->isEnabled() && viewDoc->isEnabled(),"New nested document not selected");
                    const auto before=store.record(newId); const QString prefix=editableFields(source).first().first.section(':',0,0)+':';
                    const QString key=source=="AWB.TPS" ? "AWB:AWB_NUMBER" : source=="SIGHT.TPS" ? "SIG:SD_NUMBER" : prefix+"BL_NUMBER";
                    runEditor(editDoc,[&](RecordEditor *child) { require(child->value.id==newId,"Nested Edit opened wrong sequence"); child->findChild<QLineEdit *>(key)->setText("NESTED-123");
                        if(!cargoSource(source).first.isEmpty()) {
                            auto *cargo=child->findChild<QTableWidget *>("cargoTable");
                            require(cargo && cargo->rowCount()>0 && child->findChild<QPushButton *>("cargoAdd")->isEnabled(),"Cargo missing from nested document editor");
                            require(!child->findChild<QPushButton *>("documentCargoLines"),"Cargo still requires a separate window from the editor");
                        }
                    });
                    require(store.record(newId).values[key]=="NESTED-123","Nested Edit did not persist");
                    bool previewed=false;
                    QTimer::singleShot(500,parent,[&] {
                        auto *dialog=dynamic_cast<DocumentPreview *>(QApplication::activeModalWidget());
                        if(dialog) { auto *pdf=dialog->findChild<QPdfView *>("previewView"); previewed=pdf && pdf->document()->status()==QPdfDocument::Status::Ready && pdf->document()->getAllText(0).text().contains("NESTED-123"); dialog->reject(); }
                        else if(auto *box=qobject_cast<QDialog *>(QApplication::activeModalWidget())) box->reject();
                    });
                    viewDoc->click(); require(previewed,"Nested View did not preview selected document");
                }
                auto *deleteDoc=parent->findChild<QPushButton *>("editorDocumentDelete"); require(deleteDoc && deleteDoc->isEnabled(),"Nested Delete unavailable");
                const auto lastDoc=store.record(nestedIds.last());
                QTimer::singleShot(0,parent,[&] { if(auto *box=parent->findChild<QMessageBox *>("confirmDocumentDelete")) box->button(QMessageBox::Yes)->click(); }); deleteDoc->click();
                require(versions->findData(lastDoc.id)<0 && !store.record(lastDoc.id).values["NATIVE:DELETED_AT"].toString().isEmpty(),"Nested Delete did not remove selected document");
                store.setDocumentDeleted(store.record(lastDoc.id),false); type->setCurrentIndex(0); type->setCurrentIndex(type->findData(lastDoc.source));
                // Editing another draft field must not discard documents when this editor is canceled.
                parent->findChild<QLineEdit *>("BOO:PO_NUMBER")->setText("UNSAVED-PARENT");
                app.processEvents(); app.processEvents();
                const auto *save=parent->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
                require(parent->rect().contains(save->mapTo(parent,save->rect().center())),"Shipment Save button outside window");
                if(argc==3) parent->grab().save(QString::fromLocal8Bit(argv[2])+"/shipment-with-documents.png");
                nestedComplete=true;
            } catch(const std::exception &e) { nestedFailure=e.what(); }
            parent->reject();
        });
        table->doubleClicked(table->currentIndex());
        if(!nestedComplete) throw std::runtime_error(nestedFailure.toStdString());
        require(store.record(id).values["BOO:PO_NUMBER"]!="UNSAVED-PARENT","Cancel saved pending shipment changes");
        auto *mainVersions=window.findChild<QComboBox *>("shipmentDocumentVersion");
        for(const auto docId : nestedIds) { const auto doc=store.record(docId); require(doc.values["NATIVE:BOOKING_ID"]==QString::number(id),"Nested document linked to wrong shipment"); }
        require(mainVersions->findData(nestedIds.first())>=0,"Closing shipment editor did not refresh main document list");
        // Delete each document independently; cancellation, restore, cargo and
        // optimistic concurrency must not affect its shipment or sibling forms.
        const auto beforeDeleteShipment=store.record(id);
        for(int docIndex=0;docIndex<nestedIds.size();++docIndex) {
            tabs->setCurrentIndex(docIndex+1); app.processEvents();
            auto *versions=window.findChild<QComboBox *>("shipmentDocumentVersion"); versions->setCurrentIndex(versions->findData(nestedIds[docIndex]));
            const auto original=store.record(nestedIds[docIndex]); const auto cargo=store.cargoLines(original); const auto siblings=store.linkedDocuments(id).size();
            auto *del=window.findChild<QPushButton *>("deleteDocumentButton"); require(del && del->isEnabled(),"Document Delete unavailable");
            QTimer::singleShot(0,&window,[&] { if(auto *box=window.findChild<QMessageBox *>("confirmDocumentDelete")) box->button(QMessageBox::Cancel)->click(); }); del->click();
            require(store.record(original.id).version==original.version,"Canceled document deletion changed data");
            QTimer::singleShot(0,&window,[&] { if(auto *box=window.findChild<QMessageBox *>("confirmDocumentDelete")) box->button(QMessageBox::Yes)->click(); }); del->click();
            require(versions->findData(original.id)<0 && store.linkedDocuments(id).size()==siblings-1,"Deleted document remains linked in active UI");
            require(store.record(id).values==beforeDeleteShipment.values && store.record(id).version==beforeDeleteShipment.version,"Document delete changed shipment");
            bool rejected=false; try { store.save(original); } catch(const std::exception &) { rejected=true; } require(rejected,"Stale editor resurrected deleted document");
            if(!cargo.isEmpty()) { rejected=false; try { store.save(cargo.first()); } catch(const std::exception &) { rejected=true; } require(rejected,"Deleted parent cargo remained editable"); }
            QTimer::singleShot(0,&window,[&] {
                auto *dialog=window.findChild<QDialog *>("deletedDocuments"); if(!dialog) return;
                auto *list=dialog->findChild<QListWidget *>("deletedDocumentsList"); for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toLongLong()==original.id) list->setCurrentRow(i);
                if(argc==3 && docIndex==0) dialog->grab().save(QString::fromLocal8Bit(argv[2])+"/deleted-documents.png");
                dialog->findChild<QPushButton *>("restoreDocument")->click();
            }); window.findChild<QAction *>("showDeletedDocuments")->trigger();
            const auto restored=store.record(original.id); require(restored.values==original.values && restored.version==original.version+2 && versions->findData(original.id)>=0,"Restore lost document fields or selection");
            const auto restoredCargo=store.cargoLines(restored); require(restoredCargo.size()==cargo.size(),"Restore lost cargo");
            for(int i=0;i<cargo.size();++i) require(restoredCargo[i].id==cargo[i].id && restoredCargo[i].values==cargo[i].values,"Document deletion changed cargo");
        }
        // A reusable company profile is stored in this database copy. It is
        // selected from existing address-book records and snapshots new forms.
        auto customer=store.blankRecord("CUSTOMER.TPS"); customer.values["CUS:NAME"]="Example Logistics"; customer.values["CUS:ADDRESS"]="456 Sample Road"; customer.values["CUS:CITY_STATE"]="Houston, TX"; customer.values["CUS:ZIP"]="77032"; const auto companyId=store.save(customer);
        auto contact=store.blankRecord("CONTACTS.TPS"); contact.values["CTA:FIRST_NAME"]="Example"; contact.values["CTA:LAST_NAME"]="Agent"; contact.values["CTA:PHONE"]="555-0100"; contact.values["CTA:EMAIL"]="agent@example.invalid"; const auto contactId=store.save(contact);
        const auto priorProfile=store.companyProfile(); bool profileSaved=false;
        QTimer::singleShot(0,&window,[&] {
            auto *dialog=dynamic_cast<CompanyProfileDialog *>(window.findChild<QDialog *>("companyProfile")); if(!dialog) return;
            auto *company=dialog->findChild<QComboBox *>("profileCustomer"), *person=dialog->findChild<QComboBox *>("profileContact");
            company->setCurrentIndex(company->findData(QString::number(companyId))); person->setCurrentIndex(person->findData(QString::number(contactId)));
            profileSaved=dialog->findChild<QLineEdit *>("profile_company_name")->text()=="Example Logistics" && dialog->findChild<QLineEdit *>("profile_email")->text()=="agent@example.invalid";
            if(argc==3) dialog->grab().save(QString::fromLocal8Bit(argv[2])+"/company-profile.png");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        }); window.findChild<QAction *>("showCompanyProfile")->trigger();
        const auto profile=store.companyProfile(); require(profileSaved && profile["contact_id"]==QString::number(contactId),"My company/contact profile not saved");
        bool conflict=false; try { store.saveCompanyProfile(priorProfile,priorProfile); } catch(const std::exception &) { conflict=true; } require(conflict,"Stale company profile overwrote settings");
        CompanyProfileDialog cancelProfile(store); cancelProfile.findChild<QLineEdit *>("profile_company_name")->setText("UNSAVED"); cancelProfile.reject(); require(store.companyProfile()==profile,"Cancel changed company profile");
        const auto prepared=prepareShipmentDocument(store,store.record(id),"AWB.TPS",&window); require(prepared && prepared->values["NATIVE:COMPANY_PROFILE"].toObject()==profile,"New document did not inherit company profile");
        const auto context=airWaybillContext(store,*prepared); require(context.agentAddress.first()=="Example Logistics","AWB issuing agent did not use profile");
        auto newDock=applyCompanyProfile(documentFromBooking(store.record(id),store.blankRecord("DODR.TPS")),profile); const auto savedDock=store.record(store.save(newDock));
        require(savedDock.values["NATIVE:FORWARDER_ADDRESS"].toString().contains("agent@example.invalid"),"Forwarder contact did not prepopulate");
        auto changedProfile=profile; changedProfile["company_name"]="Changed company"; store.saveCompanyProfile(changedProfile,profile);
        require(store.record(savedDock.id).values["NATIVE:COMPANY_PROFILE"].toObject()==profile,"Profile changes rewrote existing document");
        store.backup(tmp.filePath("profile-backup.sqlite")); Store backup(tmp.filePath("profile-backup.sqlite")); require(backup.companyProfile()==changedProfile,"Backup omitted company profile");
        QTextStream(stdout)<<"PASS: compact editor visibility; add/edit shipment; hidden Status preserved; column order persistence; Escape/mouse details close and popup/editor isolation; separate address book add/edit and workspace preservation; six document layouts and save/reopen; nested Add/Edit/View/Delete and save-before-create; unsaved previews and initial cargo; document delete/restore; company profile defaults, cancellation, conflicts and backup; delete cancellation, deletion and restore; documents preserved; deleted references reserved. Input database untouched.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
