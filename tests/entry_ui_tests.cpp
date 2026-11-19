// Real widgets and Store saves against a private snapshot, never the input DB.
#define SHADOW_LOGIS_GUI_TESTING
#include "../native/main.cpp"
#include "fixtures.h"
#include <QPdfDocument>
#include <QPdfSelection>
namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
void type(QLineEdit *field,const QString &text) { require(field,"Missing text field"); field->setText(text); QMetaObject::invokeMethod(field,"textEdited",Q_ARG(QString,text)); }
void save(RecordEditor &editor) {
    editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    if(editor.result()!=QDialog::Accepted) throw std::runtime_error(editor.findChild<QLabel *>("saveError")->text().toStdString());
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); if(argc!=1 && argc!=3) return 2;
    installAppearance(app);
    try {
        QTimer::singleShot(60000,&app,[] { std::exit(2); });
        QTemporaryDir temp; const auto db=temp.filePath("entry.sqlite"); if(argc==1) createSyntheticWorkspace(db); else { Store source(QString::fromLocal8Bit(argv[1])); source.backup(db); } Store store(db);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
        const auto output=argc==1 ? temp.filePath("artifacts") : QString::fromLocal8Bit(argv[2]); QDir().mkpath(output);
        auto blank=store.blankRecord("BOOKING.TPS"); blank.values["BOO:REFERENCE_NUMBER"]="UX-DEMO";
        RecordEditor booking(store,blank); booking.show(); app.processEvents();
        auto *kg=booking.findChild<QLineEdit *>("BOO:KGS"),*lb=booking.findChild<QLineEdit *>("BOO:LBS"); auto *units=booking.findChild<QComboBox *>("unitMode_weight");
        require(units && units->currentData()=="kg_to_lb","new shipment calculation default missing");
        type(kg,"8.50"); require(lb->text()=="18.74","weight conversion did not update live");
        type(lb,"20.25"); require(units->currentData()=="manual","typing override did not disable automatic weight"); type(kg,"10"); require(lb->text()=="20.25","manual override was overwritten");
        units->setCurrentIndex(units->findData("kg_to_lb")); require(lb->text()=="22.05","resume calculation failed");
        booking.findChild<QLineEdit *>("BOO:QTY")->setText("2"); type(kg,"100");
        booking.findChild<QLineEdit *>("BOO:CBM")->setText("2");
        booking.findChild<QPlainTextEdit *>("BOO:SHIPPER_ARRAY")->setPlainText("Example Exporter\n123 Sample Road\nHouston TX"); booking.findChild<QPlainTextEdit *>("BOO:CONSIGNEE_ARRAY")->setPlainText("Example Importer\n456 Harbour Street\nRotterdam");
        auto contact=store.blankRecord("CONTACTS.TPS"); contact.values["CTA:FIRST_NAME"]="Example"; contact.values["CTA:LAST_NAME"]="Contact"; contact.values["CTA:PHONE"]="555-0100"; contact.values["CTA:EMAIL"]="contact@example.invalid"; const auto contactId=store.save(contact);
        QTimer::singleShot(0,&booking,[&] {
            auto *dialog=booking.findChild<QDialog *>("contactPicker"); if(!dialog) return;
            auto *list=dialog->findChild<QListWidget *>("contactChoices"); for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole).toLongLong()==contactId) list->setCurrentRow(i);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }); booking.findChild<QPushButton *>("chooseContactCLIENT")->click();
        require(booking.findChild<QLineEdit *>("BOO:CLIENE_EMAIL")->text()=="contact@example.invalid","contact details not copied");
        auto customer=store.blankRecord("CUSTOMER.TPS"); customer.values["CUS:NAME"]="Lookup Example Company";
        customer.values["CUS:ADDRESS"]="123 Example Street"; customer.values["CUS:CITY_STATE"]="Sample City"; customer.values["CUS:COUNTRY"]="Canada"; customer.values["CUS:PH1_NUMBER"]="555-0199";
        const auto customerId=store.save(customer); customer=store.record(customerId);
        QTimer::singleShot(0,&booking,[&] {
            auto *dialog=booking.findChild<QDialog *>("customerPicker"); require(dialog,"Customer picker missing");
            auto *choices=dialog->findChild<QListWidget *>("customerChoices");
            for(int i=0;i<choices->count();++i) if(choices->item(i)->data(Qt::UserRole).toLongLong()==customerId) {
                const auto label=choices->item(i)->text(); require(label=="Lookup Example Company · Sample City · Canada · 555-0199","Customer picker exposed an ID or omitted useful details"); choices->setCurrentRow(i);
            }
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        }); booking.findChild<QPushButton *>("chooseSHIPPER")->click();
        require(booking.value.values["BOO:SHIPPER_NUMBER"]==customer.values["CUS:NUMBER"],"Hiding the customer number broke its stored link");
        RecordEditor customerEditor(store,customer);
        QTimer::singleShot(0,&customerEditor,[&] {
            auto *dialog=customerEditor.findChild<QDialog *>("referenceLookup"); require(dialog,"Country picker missing");
            dialog->findChild<QLineEdit *>("referenceSearch")->setText("United States"); auto *rows=dialog->findChild<QTableWidget *>("referenceTable");
            for(int i=0;i<rows->rowCount();++i) if(rows->item(i,0)->text()=="US") rows->selectRow(i);
            dialog->findChild<QPushButton *>("useReference")->click();
        }); customerEditor.findChild<QAction *>("lookup_CUS:COUNTRY")->trigger();
        require(customerEditor.findChild<QLineEdit *>("CUS:COUNTRY")->text()=="United States","Country picker did not fill the country name");
        customerEditor.reject(); require(store.record(customerId).values["CUS:COUNTRY"]=="Canada","Country picker saved a canceled edit");
        QTimer::singleShot(0,&booking,[&] {
            auto *dialog=booking.findChild<QDialog *>("datePicker"); if(!dialog) return;
            dialog->findChild<QCalendarWidget *>()->setSelectedDate(QDate(2027,1,2)); dialog->accept();
        }); booking.findChild<QAction *>("calendar_BOO:READY_DATE")->trigger();
        require(booking.findChild<QLineEdit *>("BOO:READY_DATE")->text()=="2027-01-02","date picker failed");
        booking.grab().save(output+"/entry-shipment.png"); save(booking);
        auto savedBooking=store.record(booking.value.id); require(savedBooking.values["BOO:KGS"]=="100" && savedBooking.values["BOO:LBS"]=="220.46","converted weights did not save");
        RecordEditor reopenBooking(store,savedBooking); require(reopenBooking.findChild<QComboBox *>("unitMode_weight")->currentData()=="kg_to_lb","conversion mode did not persist"); reopenBooking.reject();
        const auto awbId=store.save(documentFromBooking(savedBooking,store.blankRecord("AWB.TPS"))); const auto original=store.record(awbId);
        RecordEditor editor(store,original); editor.show(); app.processEvents(); auto *cargo=editor.findChild<QTableWidget *>("airCargo");
        bool dimensionApplied=false;
        QTimer::singleShot(0,&editor,[&] {
            auto *dialog=dynamic_cast<DimensionsDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
            auto *rows=dialog->findChild<QTableWidget *>("dimensionRows"); rows->item(0,0)->setText("2"); for(int col=1;col<4;++col) rows->item(0,col)->setText("100");
            auto *summary=dialog->findChild<QLabel *>("dimensionSummary"); require(summary->text().contains("334"),"dimension preview missing chargeable weight");
            auto *basis=dialog->findChild<QComboBox *>("volumetricBasis"); basis->setCurrentIndex(2);
            require(dialog->findChild<QLineEdit *>("volumetricDivisor")->text()=="167" && dialog->findChild<QTableWidget *>("cbmRows")->item(0,1)->text()=="2","CBM formula or shipment prefilling missing");
            require(summary->text().contains("Dimensional weight: 334 kg"),"CBM factor incorrect"); app.processEvents(); dialog->grab().save(output+"/entry-cbm.png");
            basis->setCurrentIndex(1); require(dialog->findChild<QLineEdit *>("volumetricDivisor")->text()=="166","imperial formula preset incorrect"); basis->setCurrentIndex(0);
            auto *unit=dialog->findChild<QComboBox *>("dimensionUnits"); unit->setCurrentIndex(1); require(rows->item(0,1)->text()=="39.370","dimension unit change reinterpreted lengths"); unit->setCurrentIndex(0);
            for(int col=1;col<4;++col) rows->item(0,col)->setText("100");
            app.processEvents(); dialog->grab().save(output+"/entry-dimensions.png"); dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); dimensionApplied=dialog->result()==QDialog::Accepted;
        }); editor.findChild<QPushButton *>("editDimensions")->click();
        require(dimensionApplied && cargo->item(0,3)->text()=="334","dimension result did not fill chargeable weight"); require(store.record(awbId).values==original.values,"dimension dialog saved document prematurely");
        cargo->item(0,1)->setText("500"); require(cargo->item(0,3)->text()=="500","gross weight change did not update chargeable weight");
        cargo->item(0,3)->setText("550"); cargo->item(0,1)->setText("600"); require(cargo->item(0,3)->text()=="550","manual chargeable override overwritten");
        QTimer::singleShot(0,&editor,[&] { auto *d=dynamic_cast<DimensionsDialog *>(QApplication::activeModalWidget()); if(d) { d->findChild<QCheckBox *>("autoChargeable0")->setChecked(true); d->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); } }); editor.findChild<QPushButton *>("editDimensions")->click(); require(cargo->item(0,3)->text()=="600","could not resume automatic chargeable weight");
        bool ratingApplied=false;
        QTimer::singleShot(0,&editor,[&] {
            auto *dialog=dynamic_cast<RatingDialog *>(QApplication::activeModalWidget()); if(!dialog) return;
            QTimer::singleShot(0,dialog,[&] {
                auto *picker=dialog->findChild<QDialog *>("referenceLookup"); require(picker,"Currency picker missing");
                picker->findChild<QLineEdit *>("referenceSearch")->setText("USD"); picker->findChild<QTableWidget *>("referenceTable")->selectRow(0); picker->findChild<QPushButton *>("useReference")->click();
            }); dialog->findChild<QAction *>("lookupCurrency")->trigger();
            require(dialog->findChild<QLineEdit *>("ratingCurrency")->text()=="USD","Currency picker failed to fill AWB currency");
            auto *rates=dialog->findChild<QTableWidget *>("ratingRows"); rates->item(0,1)->setText("2.50"); dialog->findChild<QCheckBox *>("calculateLine0")->setChecked(true);
            dialog->findChild<QComboBox *>("weightPayment")->setCurrentIndex(1); dialog->findChild<QCheckBox *>("calculateWeightCharges")->setChecked(true);
            auto *fees=dialog->findChild<QTableWidget *>("otherChargeRows"); fees->item(0,0)->setText("Handling"); fees->item(0,2)->setText("10.25"); static_cast<QComboBox *>(fees->cellWidget(0,3))->setCurrentIndex(1); static_cast<QComboBox *>(fees->cellWidget(0,4))->setCurrentIndex(1);
            dialog->findChild<QCheckBox *>("calculateOtherCharges")->setChecked(true); dialog->findChild<QCheckBox *>("calculateChargeTotals")->setChecked(true);
            require(dialog->findChild<QLineEdit *>("AWB:TOTAL_PPD")->text()=="1510.25","live fee or weight charge total incorrect");
            rates->item(0,2)->setText("1450"); require(!dialog->findChild<QCheckBox *>("calculateLine0")->isChecked() && dialog->findChild<QLineEdit *>("AWB:TOTAL_PPD")->text()=="1460.25","manual line override failed");
            dialog->findChild<QCheckBox *>("calculateLine0")->setChecked(true);
            type(dialog->findChild<QLineEdit *>("AWB:TOTAL_PPD"),"1500"); require(!dialog->findChild<QCheckBox *>("calculateChargeTotals")->isChecked(),"manual grand total did not disable calculation"); dialog->findChild<QCheckBox *>("calculateChargeTotals")->setChecked(true);
            app.processEvents(); dialog->grab().save(output+"/entry-rates.png"); dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); ratingApplied=dialog->result()==QDialog::Accepted;
        }); editor.findChild<QPushButton *>("editAirRates")->click();
        require(ratingApplied && store.record(awbId).values==original.values,"rates prematurely saved or failed to apply");
        // Fill current shipment details selectively, preserving custom text by default.
        savedBooking.values["BOO:ORIGIN_LOCATION"]="Houston"; savedBooking.values["BOO:DESTINATION_LOCATION"]="Rotterdam";
        auto changedAddress=savedBooking.values["BOO:SHIPPER_ARRAY"].toArray(); changedAddress[0]="Updated Exporter"; savedBooking.values["BOO:SHIPPER_ARRAY"]=changedAddress; store.save(savedBooking);
        editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->setText("Custom origin");
        QTimer::singleShot(0,&editor,[&] {
            auto *dialog=editor.findChild<QDialog *>("shipmentSuggestions"); if(!dialog) return;
            auto *rows=dialog->findChild<QTableWidget *>("suggestedFields");
            for(int row=0;row<rows->rowCount();++row) if(rows->item(row,0)->data(Qt::UserRole)=="AWB:AIRPORT_OF_ORIGIN") require(rows->item(row,0)->checkState()==Qt::Unchecked,"existing text selected for overwrite");
            dialog->grab().save(output+"/entry-reuse.png"); dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        }); editor.findChild<QPushButton *>("useShipmentDetails")->click();
        require(editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->text()=="Custom origin" && editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_DESTINATION")->text()=="Rotterdam","selective shipment filling lost override or blank field");
        auto *originCode=editor.findChild<QLineEdit *>("AWB:AIRPORT_FROM"); require(originCode->completer(),"airport completion missing");
        editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->clear();
        QMetaObject::invokeMethod(originCode->completer(),"activated",Q_ARG(QString,QString("IAH · George Bush Intercontinental Airport")));
        require(originCode->text()=="IAH" && !editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->text().isEmpty(),"airport code/name selection failed");
        editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->setText("Custom origin label");
        QTimer::singleShot(0,&editor,[&] {
            auto *picker=editor.findChild<QDialog *>("referenceLookup"); require(picker,"Airport lookup missing");
            picker->findChild<QLineEdit *>("referenceSearch")->setText("IAH"); auto *rows=picker->findChild<QTableWidget *>("referenceTable");
            require(rows->rowCount()>=1,"Imported airport search empty"); rows->selectRow(0); picker->findChild<QPushButton *>("useReference")->click();
        }); editor.findChild<QAction *>("lookup_AWB:AIRPORT_FROM")->trigger();
        require(originCode->text()=="IAH" && editor.findChild<QLineEdit *>("AWB:AIRPORT_OF_ORIGIN")->text()=="Custom origin label","Airport lookup overwrote a custom airport name");
        const auto awbNumber=editor.findChild<QLineEdit *>("AWB:AWB_NUMBER")->text();
        for(const auto &key : {QString("AWB:FIRST_CARRIER"),QString("AWB:SECOND_CARRIER"),QString("AWB:THIRD_CARRIER")}) {
            QTimer::singleShot(0,&editor,[&] {
                auto *picker=editor.findChild<QDialog *>("airlineLookup"); require(picker,"Airline picker missing");
                picker->findChild<QLineEdit *>("airlineSearch")->setText("Cathay Pacific"); auto *rows=picker->findChild<QTableWidget *>("airlineTable");
                require(rows->rowCount()==1,"Airline picker failed country/name search"); rows->selectRow(0); picker->findChild<QPushButton *>("useAirlineCode")->click();
            }); editor.findChild<QAction *>("lookup_"+key)->trigger();
            require(editor.findChild<QLineEdit *>(key)->text()=="CX","Carrier picker did not insert the selected IATA code");
        }
        QTimer::singleShot(0,&editor,[&] { editor.findChild<QDialog *>("airlineLookup")->reject(); }); editor.findChild<QAction *>("lookup_AWB:FIRST_CARRIER")->trigger();
        require(editor.findChild<QLineEdit *>("AWB:FIRST_CARRIER")->text()=="CX" && editor.findChild<QLineEdit *>("AWB:AWB_NUMBER")->text()==awbNumber,"Canceled carrier selection or numeric lookup changed unrelated fields");
        save(editor); const auto saved=store.record(awbId);
        require(saved.values["AWB:FIRST_CARRIER"]=="CX" && saved.values["AWB:SECOND_CARRIER"]=="CX" && saved.values["AWB:THIRD_CARRIER"]=="CX","Selected airline code did not persist");
        require(saved.values["AWB:TOTAL_PPD"]=="1510.25" && saved.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="600.00","save disagrees with calculated preview");
        auto changed=saved; auto weights=changed.values["AWB:GROSS_WEIGHT"].toArray(); weights[0]="700"; changed.values["AWB:GROSS_WEIGHT"]=weights; store.save(changed); const auto recalculated=store.record(awbId);
        require(recalculated.values["AWB:TOTAL_PPD"]=="1760.25","Store save did not calculate dependencies");
        exportAirWaybill(recalculated,output+"/calculated-awb.pdf",airWaybillContext(store,recalculated));
        QPdfDocument pdf; require(pdf.load(output+"/calculated-awb.pdf")==QPdfDocument::Error::None,"calculated PDF failed"); require(pdf.getAllText(0).text().contains("1760.25"),"PDF did not use calculated totals");
        auto cbmRecord=recalculated; auto cbmSettings=cbmRecord.values["NATIVE:DIMENSION_CALCULATION"].toObject(); cbmSettings["basis"]="kg/cbm"; cbmSettings["divisor"]="167"; cbmRecord.values["NATIVE:DIMENSION_CALCULATION"]=cbmSettings; cbmRecord=calculateEntry(cbmRecord);
        exportAirWaybill(cbmRecord,output+"/cbm-awb.pdf",airWaybillContext(store,cbmRecord));
        QPdfDocument cbmPdf; require(cbmPdf.load(output+"/cbm-awb.pdf")==QPdfDocument::Error::None,"CBM PDF failed"); const auto cbmText=cbmPdf.getAllText(0).text();
        require(cbmText.contains("2.000 m³") && cbmText.contains("334 kg") && !cbmText.contains("CFT"),"CBM PDF omitted volume or printed inactive dimension rows");
        RecordEditor canceled(store,recalculated); canceled.findChild<QTableWidget *>("airCargo")->item(0,1)->setText("800"); canceled.reject(); require(store.record(awbId).values==recalculated.values,"cancel saved automatic changes");
        QTextStream(stdout)<<"PASS: unit conversions and overrides; contact/date selection; dimensions, units, coverage and chargeable override; rating, fees and totals; shipment reuse; airport completion; calculation save/reopen/PDF consistency; cancel preserves DB. Input database untouched.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
