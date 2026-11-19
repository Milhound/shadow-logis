#include "store.h"
#include "domain.h"
#include "documents.h"
#include "preview.h"
#include "companyprofile.h"
#include "entrylogic.h"
#include "ratingdialog.h"
#include "dimensionsdialog.h"
#include "airlinelookup.h"
#include "reference.h"
#include "appearance.h"
#include "runtimecheck.h"
#include "workspaceui.h"
#include "preferences.h"
#include <QStandardPaths>
#include <QCalendarWidget>
#include <QCompleter>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QScrollArea>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QMenuBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QVBoxLayout>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#ifdef Q_OS_UNIX
#include <termios.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#include <io.h>
#endif

using namespace shadowlogis;
namespace {
void error(QWidget *parent, const std::exception &e) {
    QMessageBox::critical(parent, "Shadow Logis", QString::fromUtf8(e.what()));
}
const QList<QPair<QString, QString>> DocumentTypes{
    {"AWB.TPS", "Air waybills"}, {"OBL.TPS", "Ocean bills of lading"},
    {"DODR.TPS", "Dock receipts"}, {"CERTORGN.TPS", "Certificates of origin"}, {"PLIST.TPS", "Packing lists"}, {"SIGHT.TPS", "Sight drafts"}};
const QStringList ShipmentColumnKeys{"reference","shipper","consignee","bookingNumber"};

class MultilineDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &,const QModelIndex &) const override { return new QPlainTextEdit(parent); }
    void setEditorData(QWidget *editor,const QModelIndex &index) const override { static_cast<QPlainTextEdit *>(editor)->setPlainText(index.data(Qt::EditRole).toString()); }
    void setModelData(QWidget *editor,QAbstractItemModel *model,const QModelIndex &index) const override { model->setData(index,static_cast<QPlainTextEdit *>(editor)->toPlainText(),Qt::EditRole); }
};

bool deleteShippingDocument(Store &store,const Record &document,QWidget *parent) {
    QString label; for(const auto &[source,name] : DocumentTypes) if(source==document.source) label=name;
    if(label.isEmpty()) return false;
    const auto prefix=editableFields(document.source).first().first.section(':',0,0)+':';
    QMessageBox confirm(QMessageBox::Question,"Delete document", "Delete this document?",QMessageBox::Yes|QMessageBox::Cancel,parent);
    confirm.setObjectName("confirmDocumentDelete"); confirm.button(QMessageBox::Yes)->setText("Delete"); confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.setInformativeText(label+" · "+recordReference(document)+" · Sequence "+valueText(document.values[prefix+"ID_SEQUENCE"])+"\nThe shipment stays in place. Restore this document, its cargo and history from Workspace → Deleted documents.");
    if(confirm.exec()!=QMessageBox::Yes) return false;
    store.setDocumentDeleted(document,true); return true;
}

AirWaybillPrintOptions awbPrintProfile{true};
OceanBillPrintOptions oceanPrintProfile;
template<typename Options>
bool choosePrintRegistration(QWidget *parent,bool awb,Options &profile) {
    QDialog dialog(parent); dialog.setObjectName(awb ? "preprintedAwbSetup" : "preprintedBolSetup");
    dialog.setWindowTitle(awb ? "Dot Matrix · print alignment" : "Preprinted ocean bill of lading · Letter");
    auto *layout=new QVBoxLayout(&dialog);
    auto *note=new QLabel(awb ? "Print one colored copy at a time. Each job uses a single 9½ × 12-inch form from the continuous stock. Position the required copy at top of form before printing." : "Print shipment text only on preprinted Letter paper. Check the alignment against your stationery at actual size.");
    note->setWordWrap(true); note->setMinimumWidth(470); layout->addWidget(note);
    auto *form=new QFormLayout; layout->addLayout(form);
    const auto control=[&](const QString &name,double minimum,double maximum,double value,const QString &suffix) {
        auto *spin=new QDoubleSpinBox; spin->setObjectName(name); spin->setRange(minimum,maximum); spin->setDecimals(2); spin->setSingleStep(.25); spin->setValue(value); spin->setSuffix(suffix); return spin;
    };
    auto *x=control("printOffsetX",-25,25,profile.horizontalOffsetMm," mm");
    auto *y=control("printOffsetY",-25,25,profile.verticalOffsetMm," mm");
    auto *sx=control("printScaleX",95,105,profile.horizontalScalePercent," %");
    auto *sy=control("printScaleY",95,105,profile.verticalScalePercent," %");
    form->addRow("Horizontal offset (+ moves right)",x); form->addRow("Vertical offset (+ moves down)",y);
    form->addRow("Horizontal registration scale",sx); form->addRow("Vertical registration scale",sy);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Ok)->setText("Preview text only");
    layout->addWidget(buttons); QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted) return false;
    profile.horizontalOffsetMm=x->value(); profile.verticalOffsetMm=y->value();
    profile.horizontalScalePercent=sx->value(); profile.verticalScalePercent=sy->value();
    savePrintPreferences(awb,x->value(),y->value(),sx->value(),sy->value()); return true;
}

void previewShippingDocument(Store &store,const Record &record,QWidget *parent,const AirWaybillPrintOptions &options={},bool vics=false,const OceanBillPrintOptions &oceanOptions=oceanPrintProfile,bool pending=false) {
    QByteArray bytes; QString name,heading;
    const auto prefix = editableFields(record.source).first().first.section(':',0,0) + ':';
    const auto sequence = valueText(record.values.value(prefix + "ID_SEQUENCE"));
    heading = (record.source == "AWB.TPS" ? "Laser AWB · " : record.source=="OBL.TPS" ? (vics ? "VICS freight bill of lading · " : "Ocean bill of lading · ") : record.source=="DODR.TPS" ? "Dock receipt · " : record.source=="SIGHT.TPS" ? "Sight draft · " : record.source==DangerousGoodsSource ? "Dangerous goods declaration · " : "Shipment summary · ") + recordReference(record);
    if(record.source=="CERTORGN.TPS") heading="Certificate of origin · "+recordReference(record);
    if (options.preprintedContinuous) heading="Dot Matrix · "+recordReference(record);
    if (!sequence.isEmpty()) heading += " · Sequence " + sequence;
    auto suffix = recordReference(record); if (!sequence.isEmpty()) suffix += "-" + sequence;
    suffix.replace(QRegularExpression("[^A-Za-z0-9._-]"), "-");
    name = (record.source == "AWB.TPS" ? "draft-air-waybill-" : record.source=="OBL.TPS" ? (vics ? "draft-vics-bol-" : "draft-ocean-bol-") : record.source=="DODR.TPS" ? "draft-dock-receipt-" : record.source=="SIGHT.TPS" ? "sight-draft-" : record.source==DangerousGoodsSource ? "dangerous-goods-" : "shipment-summary-") + suffix + ".pdf";
    if(record.source=="CERTORGN.TPS") name="certificate-of-origin-"+suffix+".pdf";
    if (options.preprintedContinuous) name="text-only-awb-9.5x12-"+suffix+".pdf";
    if(record.source=="OBL.TPS" && !vics && oceanOptions.textOnly) { heading="Text-only ocean bill of lading · "+recordReference(record); name="text-only-ocean-bol-"+suffix+".pdf"; }
    if(pending) heading+=" · Unsaved changes";
    QTemporaryDir temporary;
    if (!temporary.isValid()) throw std::runtime_error("Could not create a temporary document preview.");
    const auto path = temporary.filePath("preview.pdf");
    if (record.source == "AWB.TPS") exportAirWaybill(record,path,airWaybillContext(store,record),options);
    else if (record.source == "OBL.TPS") { if(vics) exportVicsBillOfLading(record,path,store.cargoLines(record)); else exportOceanBillOfLading(record,path,store.cargoLines(record),oceanOptions); }
    else if (record.source=="DODR.TPS") exportDockReceipt(record,path,store.cargoLines(record));
    else if (record.source=="CERTORGN.TPS") exportCertificateOfOrigin(record,path,store.cargoLines(record),certificateForwarder(store,record));
    else if (record.source=="SIGHT.TPS") exportSightDraft(record,path);
    else if (record.source==DangerousGoodsSource) exportDangerousGoods(record,path);
    else exportSummary(record, path, store.cargoLines(record));
    QFile pdf(path);
    if (!pdf.open(QIODevice::ReadOnly)) throw std::runtime_error("Could not read the generated document preview.");
    bytes = pdf.readAll();
    DocumentPreview preview(std::move(bytes),heading,name,parent); preview.exec();
}

void previewShippingDocument(Store &store,qint64 id,QWidget *parent,const AirWaybillPrintOptions &options={},bool vics=false) {
    previewShippingDocument(store,store.record(id),parent,options,vics);
}

std::optional<Record> prepareShipmentDocument(Store &store,const Record &booking,const QString &source,QWidget *parent) {
    auto target=documentFromBooking(booking,store.blankRecord(source));
    if(source=="DODR.TPS" || source=="OBL.TPS") {
        const QString other=source=="DODR.TPS" ? "OBL.TPS" : "DODR.TPS";
        QStringList choices{"Use shipment details"}; QList<Record> documents;
        for(const auto &link : store.linkedDocuments(booking.id)) if(link.record.source==other && link.match!=DocumentMatch::AmbiguousReference) {
            documents<<store.record(link.record.id);
            const auto pp=other=="OBL.TPS" ? QString("OBL:") : QString("DOD:");
            const auto number=valueText(link.record.values[pp+(other=="OBL.TPS" ? "BL_NUMBER" : "DO_NUMBER")]);
            choices<<(number.isEmpty() ? recordReference(link.record) : number)+" · Sequence "+valueText(link.record.values[pp+"ID_SEQUENCE"]);
        }
        if(!documents.isEmpty()) {
            bool ok=false; const auto choice=QInputDialog::getItem(parent,"Reuse marine document details","Copy parties, routing and cargo from "+QString(other=="OBL.TPS" ? "ocean bill of lading" : "dock receipt"),choices,documents.size()==1 ? 1 : 0,false,&ok);
            if(!ok) return std::nullopt;
            const auto index=choices.indexOf(choice);
            if(index>0) target=marineDocumentFromDocument(documents[index-1],target);
        }
    }
    return applyCompanyProfile(target,store.companyProfile());
}

void showCargoDialog(Store &store,const Record &parentRecord,QWidget *parent);
QWidget *createCargoEditor(Store &store,const Record &parentRecord,QWidget *parent);

QString editorText(QString text,bool fixedLineArray) {
    // Fixed-size imported arrays reserve empty lines after the actual text.
    // Hide only that trailing padding; keep internal blank lines and memo text.
    if(fixedLineArray) while(text.endsWith('\n')) text.chop(1);
    return text;
}

class RecordEditor final : public QDialog {
public:
    Record value;
    explicit RecordEditor(Store &store, Record record, QWidget *parent = nullptr) : QDialog(parent), value(std::move(record)), openedValues(value.values) {
        setWindowTitle((value.id ? "Edit " : "New ") + QString(value.source == "AWB.TPS" ? "air waybill" : value.source==DangerousGoodsSource ? "dangerous goods declaration" : value.source=="BOOKING.TPS" ? "shipment" : value.source=="OBL.TPS" ? "ocean bill of lading" : value.source=="DODR.TPS" ? "dock receipt" : value.source=="CONTACTS.TPS" ? "contact" : value.source=="CUSTOMER.TPS" ? "customer" : value.source=="CERTORGN.TPS" ? "certificate of origin" : value.source=="PLIST.TPS" ? "packing list" : value.source=="SIGHT.TPS" ? "sight draft" : "record")); resize(820, 760);
        auto *layout = new QVBoxLayout(this);
        const bool document=QStringList{"AWB.TPS","OBL.TPS","DODR.TPS","CERTORGN.TPS","PLIST.TPS","SIGHT.TPS"}.contains(value.source);
        const bool compact=document || value.source=="BOOKING.TPS";
        if (value.source == DangerousGoodsSource) {
            auto *buttons = new QHBoxLayout;
            for (const QString &role : {QString("SHIPPER"), QString("CONSIGNEE")}) {
                auto *button = new QPushButton(role == "SHIPPER" ? "Choose shipper…" : "Choose consignee…");
                buttons->addWidget(button);
                connect(button, &QPushButton::clicked, this, [this, &store, role] {
                    try { chooseCustomer(store, role); } catch (const std::exception &e) { error(this, e); }
                });
            }
            layout->addLayout(buttons);
        }
        auto *tabs = compact ? nullptr : new QTabWidget(this);
        if(tabs) layout->addWidget(tabs);
        QMap<QString,QFormLayout *> forms;
        const auto formFor = [&](const QString &name) {
            if (!forms.contains(name)) {
                auto *scroll = new QScrollArea; scroll->setWidgetResizable(true);
                auto *body = new QWidget; auto *bodyLayout = new QVBoxLayout(body);
                auto *form = new QFormLayout; bodyLayout->addLayout(form); bodyLayout->addStretch();
                form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
                form->setFormAlignment(Qt::AlignTop);
                scroll->setWidget(body); tabs->addTab(scroll,name); forms[name] = form;
            }
            return forms[name];
        };
        if (value.source==DangerousGoodsSource) for (const QString &tab : {QString("Parties / routing"),QString("Dangerous goods"),QString("Handling / signing")}) formFor(tab);
        else if(!compact) formFor("Details");
        const auto schema = store.schema(value.source);
        const auto certificateAgent=value.source=="CERTORGN.TPS" ? certificateForwarder(store,value) : QString();
        for (const auto &item : schema.value("fields").toArray()) {
            const auto field = item.toObject(); definitions[field.value("name").toString()] = field;
        }
        for (const auto &item : schema.value("memos").toArray()) {
            const auto memo = item.toObject(); if (memo.value("flags").toInt() & 4) continue;
            const auto key = memo.value("name").toString(); definitions[key] = {{"memo", true}};
            if (!value.values.contains(key)) value.values[key] = key=="NATIVE:FORWARDER_ADDRESS" && value.source=="CERTORGN.TPS" ? certificateAgent : QString();
        }
        for (const auto &[key,label] : vicsFields(value.source)) {
            if (vicsOrderKeys().contains(key)) {
                if (!value.values.contains(key)) { QJsonArray rows; for (int i=0;i<8;++i) rows.append(""); value.values[key]=rows; }
                definitions[key]={{"elements",8}};
            } else {
                if (!value.values.contains(key)) value.values[key]="";
                if (key.endsWith("BILL_TO") || key.endsWith("INSTRUCTIONS")) definitions[key]={{"memo",true}};
            }
        }
        for (const auto &[key, label] : editableFields(value.source)) {
            if (key=="BOO:STATUS") continue; // Preserved in storage; retired from the UI.
            if (!value.values.contains(key) || vicsOrderKeys().contains(key) || (value.source == "AWB.TPS" && airCargoKeys().contains(key))) continue;
            QString tab = "Details";
            if (key.startsWith("NATIVE:VICS_")) tab="VICS form";
            if (value.source==DangerousGoodsSource) {
                tab="Parties / routing";
                if (key=="DGD:AIRCRAFT" || key=="DGD:SHIPMENT_TYPE") tab="Dangerous goods";
                if (QStringList{"DGD:HANDLING","DGD:EMERGENCY","DGD:SIGNATORY","DGD:DATE"}.contains(key)) tab="Handling / signing";
            }
            QWidget *editor;
            if(value.source=="SIGHT.TPS" && (key.startsWith("SIG:INST_") || QStringList{"SIG:COLLECTION","SIG:COLLECT_LC","SIG:TO_ACT","SIG:TO_ASSIST","SIG:REFER","SIG:REFER2"}.contains(key))) {
                auto *check=new QCheckBox; check->setChecked(valueText(value.values[key])=="1"); editor=check;
            } else if (key=="DGD:AIRCRAFT" || key=="DGD:SHIPMENT_TYPE") {
                auto *combo=new QComboBox; combo->addItem("Choose…",QString(""));
                const QStringList choices=key=="DGD:AIRCRAFT" ? QStringList{"Passenger and cargo aircraft","Cargo aircraft only"} : QStringList{"Non-radioactive","Radioactive"};
                for (const auto &choice : choices) combo->addItem(choice,choice);
                combo->setCurrentIndex(combo->findData(value.values[key].toString())); editor=combo;
            } else if (value.values[key].isArray() || definitions[key].value("memo").toBool()) {
                auto *text = new QPlainTextEdit(editorText(fieldText(key,value.values[key]),value.values[key].isArray()));
                text->setMinimumHeight(105); text->setMaximumHeight(170); editor = text;
            } else {
                auto *text = new QLineEdit(fieldText(key,value.values[key]));
                if (isDateField(key)) {
                    text->setPlaceholderText("YYYY-MM-DD");
                    auto *dateAction=text->addAction(style()->standardIcon(QStyle::SP_ArrowDown),QLineEdit::TrailingPosition);
                    dateAction->setObjectName("calendar_"+key); dateAction->setToolTip("Choose a date");
                    connect(dateAction,&QAction::triggered,this,[this,text,key] {
                        QDialog picker(this); picker.setObjectName("datePicker"); picker.setWindowTitle("Choose date");
                        auto *l=new QVBoxLayout(&picker); auto *calendar=new QCalendarWidget; calendar->setMinimumDate(QDate(1801,1,1)); calendar->setMaximumDate(QDate(9999,12,31));
                        const auto selected=QDate::fromString(text->text(),Qt::ISODate); calendar->setSelectedDate(selected.isValid() ? selected : QDate::currentDate()); l->addWidget(calendar);
                        auto *b=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); auto *clear=b->addButton("Clear",QDialogButtonBox::ActionRole); auto *today=b->addButton("Today",QDialogButtonBox::ActionRole); l->addWidget(b);
                        connect(today,&QPushButton::clicked,calendar,[calendar] { calendar->setSelectedDate(QDate::currentDate()); });
                        connect(clear,&QPushButton::clicked,&picker,[&] { text->clear(); picker.reject(); });
                        connect(b,&QDialogButtonBox::accepted,&picker,&QDialog::accept); connect(b,&QDialogButtonBox::rejected,&picker,&QDialog::reject);
                        connect(calendar,&QCalendarWidget::activated,&picker,&QDialog::accept);
                        if(picker.exec()==QDialog::Accepted) text->setText(calendar->selectedDate().toString(Qt::ISODate));
                    });
                }
                if (key == "AWB:PIECES_TOTAL" || key == "AWB:GROSS_WEIGHT_TOTAL") {
                    text->setReadOnly(true); text->setToolTip("Stored total; recalculated when cargo rows change.");
                }
                if (value.id && key.endsWith(":REFERENCE_NUMBER")) {
                    text->setReadOnly(true); text->setToolTip("Reference is fixed to preserve links to imported records.");
                }
                editor = text;
            }
            editor->setObjectName(key); editors[key] = editor;
            if(!compact) formFor(tab)->addRow(label, editor);
        }
        if(value.source=="BOOKING.TPS") { buildShipmentDocuments(store,layout); buildShipmentEditor(store,layout); }
        if(document) buildDocumentEditor(store,layout);
        if(QStringList{"DODRLINE.TPS","OBLLINE.TPS","PLLINE.TPS","CERTLINE.TPS"}.contains(value.source)) {
            const auto prefix=editableFields(value.source).first().first.section(':',0,0)+':';
            formFor("Details")->addRow("Weight entry",unitControl("weight",prefix+"WEIGHT_KGS",prefix+"WEIGHT_LBS"));
            formFor("Details")->addRow("Volume entry",unitControl("volume",prefix+"MEASURE_CBM",prefix+"MEASURE_CFT"));
        }
        installAirportCompletion(store);
        installReferenceLookups();
        if (value.source==DangerousGoodsSource) {
            validateDangerousGoods(value.values);
            resize(1100,800);
            auto *form=formFor("Dangerous goods");
            dgRows=new QTableWidget(0,7); dgRows->setObjectName("dangerousGoodsRows");
            const QStringList headers{"UN / ID no.","Proper shipping name","Class / division\n(subsidiary hazard)","Packing\ngroup","Quantity and type of packing","Packing\ninstruction","Authorization"};
            dgRows->setHorizontalHeaderLabels(headers); dgRows->horizontalHeader()->setFixedHeight(42); dgRows->horizontalHeader()->setStretchLastSection(true); dgRows->setWordWrap(true); dgRows->setItemDelegate(new MultilineDelegate(dgRows)); dgRows->setMinimumHeight(330);
            dgRows->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            const QList<int> widths{90,220,135,75,220,95,120}; for(int i=0;i<7;++i) dgRows->setColumnWidth(i,widths[i]);
            for(const auto &row : value.values["DGD:LINES"].toArray()) appendDangerousRow(row.toObject());
            if(!dgRows->rowCount()) appendDangerousRow({});
            form->addRow(dgRows);
            auto *rowActions=new QHBoxLayout; auto *add=new QPushButton("Add goods row"); auto *remove=new QPushButton("Remove selected row");
            add->setObjectName("addDangerousGoodsRow"); rowActions->addWidget(add); rowActions->addWidget(remove); rowActions->addStretch(); form->addRow(rowActions);
            connect(add,&QPushButton::clicked,this,[this] { if(dgRows->rowCount()<200) appendDangerousRow({}); });
            connect(remove,&QPushButton::clicked,this,[this] { if(dgRows->currentRow()>=0) dgRows->removeRow(dgRows->currentRow()); });
            auto *note=new QLabel("Enter the declared classification and packing details for each goods row. Double-click a cell to edit; Enter adds a line; Tab finishes editing. Blank drafts can be saved. The form does not determine classifications or sign the declaration.");
            note->setWordWrap(true); form->addRow(note);
            auto *signing=new QLabel("The signature area remains blank for signing after review. Use YYYY-MM-DD for the date."); signing->setWordWrap(true); formFor("Handling / signing")->addRow(signing);
        }
        saveError = new QLabel; saveError->setObjectName("saveError"); saveError->setWordWrap(true);
        saveError->hide(); layout->addWidget(saveError);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setProperty("primary",true);
        buttons->button(QDialogButtonBox::Save)->setText(value.source=="BOOKING.TPS" ? (value.id ? "Save" : "Add") : "Save");
        if(document) {
            QComboBox *mode=nullptr;
            if(value.source=="AWB.TPS" || value.source=="OBL.TPS") {
                auto *row=new QHBoxLayout; row->addWidget(new QLabel("Print format"));
                mode=new QComboBox; mode->setObjectName("documentPrintFormat");
                mode->addItem(value.source=="AWB.TPS" ? "Laser AWB" : "Full form",false);
                mode->addItem(value.source=="AWB.TPS" ? "Dot Matrix" : "Text Only",true);
                mode->setCurrentIndex(value.source=="OBL.TPS" ? 1 : 0); row->addWidget(mode); row->addStretch(); layout->addLayout(row);
            }
            auto *preview=buttons->addButton("Preview / Print…",QDialogButtonBox::ActionRole); preview->setObjectName("editorPreviewPrint"); preview->setAutoDefault(false);
            connect(preview,&QPushButton::clicked,this,[this,&store,mode] {
                const auto original=value;
                try {
                    readEditors(); auto candidate=value; value=original;
                    if(candidate.source=="AWB.TPS") {
                        bool changed=!candidate.id; for(const auto &key : airCargoKeys()) changed|=candidate.values[key]!=original.values[key];
                        if(changed) { const auto totals=airCargoTotals(candidate.values); candidate.values["AWB:PIECES_TOTAL"]=totals.pieces; candidate.values["AWB:GROSS_WEIGHT_TOTAL"]=totals.grossWeight; }
                    }
                    AirWaybillPrintOptions awb; OceanBillPrintOptions ocean;
                    if(mode && candidate.source=="AWB.TPS" && mode->currentData().toBool()) {
                        if(!choosePrintRegistration(this,true,awbPrintProfile)) return;
                        awb=awbPrintProfile;
                    }
                    if(mode && candidate.source=="OBL.TPS") {
                        ocean.textOnly=mode->currentData().toBool();
                        if(ocean.textOnly) { if(!choosePrintRegistration(this,false,oceanPrintProfile)) return; ocean=oceanPrintProfile; }
                    }
                    saveError->hide();
                    previewShippingDocument(store,candidate,this,awb,false,ocean,!candidate.id || candidate.values!=openedValues);
                } catch(const std::exception &e) { value=original; saveError->setText(QString::fromUtf8(e.what())); saveError->show(); }
            });
        }
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, [this, &store] {
            try {
                readEditors(); value = store.record(store.save(value)); accept();
            } catch (const std::exception &e) { saveError->setText(QString::fromUtf8(e.what())); saveError->show(); }
        });
    }
private:
    QJsonObject openedValues;
    QMap<QString, QWidget *> editors;
    QMap<QString, QJsonObject> definitions;
    QWidget *unitControl(const QString &group,const QString &metricKey,const QString &imperialKey) {
        auto *metric=qobject_cast<QLineEdit *>(editors.value(metricKey)),*imperial=qobject_cast<QLineEdit *>(editors.value(imperialKey));
        auto *box=new QWidget; auto *row=new QHBoxLayout(box); row->setContentsMargins(0,0,0,0);
        auto *mode=new QComboBox; mode->setObjectName("unitMode_"+group); mode->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        mode->addItem("Manual values","manual");
        mode->addItem(group=="weight" ? "Enter kg → calculate lb" : "Enter m³ → calculate ft³",group=="weight" ? "kg_to_lb" : "cbm_to_cft");
        mode->addItem(group=="weight" ? "Enter lb → calculate kg" : "Enter ft³ → calculate m³",group=="weight" ? "lb_to_kg" : "cft_to_cbm");
        auto initial=value.values.value("NATIVE:UNIT_RULES").toObject()[group].toString("manual");
        if(!value.id && initial=="manual" && metric && imperial && metric->text().toDouble()==0 && imperial->text().toDouble()==0) initial=group=="weight" ? "kg_to_lb" : "cbm_to_cft";
        mode->setCurrentIndex(std::max(0,mode->findData(initial))); row->addWidget(mode);
        auto *note=new QLabel; note->setWordWrap(true); row->addWidget(note,1);
        if(!metric || !imperial) { box->hide(); return box; }
        auto busy=std::make_shared<bool>(false);
        const auto update=[=] {
            if(*busy) return;
            const auto rule=mode->currentData().toString();
            if(rule=="manual") { note->setText("Both values stay as entered."); return; }
            *busy=true;
            try {
                const bool fromMetric=mode->currentIndex()==1;
                auto *from=fromMetric ? metric : imperial,*to=fromMetric ? imperial : metric;
                const auto result=quantityConversion(from->text(),rule,group=="weight" ? 2 : 3);
                to->setText(group=="weight" ? weightText(result) : result);
                note->setText("Edit the result to keep an override.");
            } catch(const std::exception &e) { note->setText(QString::fromUtf8(e.what())); }
            *busy=false;
        };
        const auto remember=[=,this] {
            auto rules=value.values.value("NATIVE:UNIT_RULES").toObject(); rules[group]=mode->currentData().toString(); value.values["NATIVE:UNIT_RULES"]=rules;
        };
        connect(mode,&QComboBox::currentIndexChanged,this,[=] { remember(); update(); });
        connect(metric,&QLineEdit::textChanged,this,[=] { if(mode->currentIndex()==1) update(); });
        connect(imperial,&QLineEdit::textChanged,this,[=] { if(mode->currentIndex()==2) update(); });
        connect(metric,&QLineEdit::textEdited,this,[=] { if(mode->currentIndex()==2 && !*busy) mode->setCurrentIndex(0); });
        connect(imperial,&QLineEdit::textEdited,this,[=] { if(mode->currentIndex()==1 && !*busy) mode->setCurrentIndex(0); });
        if(initial!="manual") remember();
        update(); return box;
    }
    void installAirportCompletion(Store &store) {
        if(value.source!="AWB.TPS" || !store.sources().contains("AIRPORTS.TPS")) return;
        QMap<QString,QString> names; QSet<QString> ambiguous;
        for(const auto &airport : store.records({"AIRPORTS.TPS"})) {
            const auto code=valueText(airport.values["AIR:AP_NUMBER"]).trimmed().toUpper(),name=valueText(airport.values["AIR:AP_NAME"]).trimmed();
            if(code.isEmpty() || name.isEmpty()) continue;
            if(names.contains(code) && names[code]!=name) ambiguous.insert(code); else names[code]=name;
        }
        QStringList choices; for(auto it=names.begin();it!=names.end();++it) if(!ambiguous.contains(it.key())) choices<<it.key()+" · "+it.value();
        for(const auto &suffix : {QString("AIRPORT_FROM"),QString("AIRPORT_TO_1"),QString("AIRPORT_TO_2"),QString("AIRPORT_TO_3")}) {
            auto *field=qobject_cast<QLineEdit *>(editors.value("AWB:"+suffix)); if(!field) continue;
            auto *completer=new QCompleter(choices,field); completer->setCaseSensitivity(Qt::CaseInsensitive); completer->setFilterMode(Qt::MatchContains); field->setCompleter(completer); field->setPlaceholderText("Search code or airport");
            connect(completer,qOverload<const QString &>(&QCompleter::activated),this,[=,this](const QString &choice) {
                const auto code=choice.section(" · ",0,0); applyAirportChoice(suffix,code,names[code]);
            });
            auto *lookup=field->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),QLineEdit::TrailingPosition);
            lookup->setObjectName("lookup_"+field->objectName()); lookup->setToolTip("Find airport by city, country or code");
            connect(lookup,&QAction::triggered,this,[this,field,suffix,&store] {
                ReferenceDialog dialog(airportReference(store),true,this);
                dialog.findChild<QLineEdit *>("referenceSearch")->setText(field->text());
                if(dialog.exec()==QDialog::Accepted) {
                    const auto selected=dialog.selected();
                    applyAirportChoice(suffix,selected.code,selected.name);
                }
            });
        }
    }
    void applyAirportChoice(const QString &suffix,const QString &code,const QString &name) {
        if(auto *field=qobject_cast<QLineEdit *>(editors.value("AWB:"+suffix))) field->setText(code);
        if(suffix=="AIRPORT_FROM") {
            auto *origin=qobject_cast<QLineEdit *>(editors.value("AWB:AIRPORT_OF_ORIGIN")); if(origin && origin->text().trimmed().isEmpty()) origin->setText(name);
        } else {
            bool last=true;
            for(int i=suffix.right(1).toInt()+1;i<=3;++i) if(auto *later=qobject_cast<QLineEdit *>(editors.value("AWB:AIRPORT_TO_"+QString::number(i)))) last&=later->text().trimmed().isEmpty();
            auto *destination=qobject_cast<QLineEdit *>(editors.value("AWB:AIRPORT_OF_DESTINATION"));
            if(last && destination && destination->text().trimmed().isEmpty()) destination->setText(name);
        }
    }
    void installReferenceLookups() {
        for(const auto &key : {QString("AWB:FIRST_CARRIER"),QString("AWB:SECOND_CARRIER"),QString("AWB:THIRD_CARRIER")}) {
            auto *field=qobject_cast<QLineEdit *>(editors.value(key)); if(!field) continue;
            auto *action=field->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),QLineEdit::TrailingPosition);
            action->setObjectName("lookup_"+key); action->setToolTip("Find airline by country, name or code");
            connect(action,&QAction::triggered,this,[this,field] {
                AirlineLookupDialog dialog(true,field->text(),this);
                if(dialog.exec()==QDialog::Accepted) field->setText(dialog.selectedCode());
            });
        }
        if(auto *country=qobject_cast<QLineEdit *>(editors.value("CUS:COUNTRY"))) {
            auto *action=country->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),QLineEdit::TrailingPosition);
            action->setObjectName("lookup_CUS:COUNTRY"); action->setToolTip("Choose country or territory");
            connect(action,&QAction::triggered,this,[this,country] {
                ReferenceDialog dialog(countryReference(),true,this);
                if(dialog.exec()==QDialog::Accepted) country->setText(dialog.selected().name);
            });
        }
    }
    void fillFromShipment(Store &store) {
        Record booking;
        const auto id=value.values.value("NATIVE:BOOKING_ID").toString().toLongLong();
        if(id) booking=store.record(id);
        else {
            QList<Record> matches;
            for(const auto &r : store.records({"BOOKING.TPS"},true)) if(!recordReference(r).trimmed().isEmpty() && recordReference(r).trimmed().compare(recordReference(value).trimmed(),Qt::CaseInsensitive)==0) matches<<r;
            if(matches.size()!=1) throw std::runtime_error("This document does not have one unambiguous shipment. Open it from its shipment or review the duplicate references first.");
            booking=matches.first();
        }
        const auto original=value;
        Record current;
        try { readEditors(); current=value; value=original; } catch(...) { value=original; throw; }
        const auto suggestions=shipmentSuggestions(booking,current);
        QDialog dialog(this); dialog.setObjectName("shipmentSuggestions"); dialog.setWindowTitle("Use shipment details"); dialog.resize(900,600);
        auto *layout=new QVBoxLayout(&dialog); auto *note=new QLabel("Choose the fields to copy from the saved shipment. Empty fields are selected for you. Existing entries change only when you select them. Cargo and charges are kept separately."); note->setWordWrap(true); layout->addWidget(note);
        auto *table=new QTableWidget(0,3); table->setObjectName("suggestedFields"); table->setHorizontalHeaderLabels({"Copy field","Current document","Shipment"}); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setEditTriggers(QAbstractItemView::NoEditTriggers); layout->addWidget(table,1);
        for(const auto &[key,label] : editableFields(value.source)) if(editors.contains(key) && suggestions.contains(key)) {
            const int row=table->rowCount(); table->insertRow(row); auto *item=new QTableWidgetItem(label); item->setData(Qt::UserRole,key); item->setCheckState(valueText(current.values[key]).trimmed().isEmpty() ? Qt::Checked : Qt::Unchecked); table->setItem(row,0,item);
            table->setItem(row,1,new QTableWidgetItem(fieldText(key,current.values[key]))); table->setItem(row,2,new QTableWidgetItem(fieldText(key,suggestions[key]))); table->setRowHeight(row,72);
        }
        if(!table->rowCount()) note->setText("The available shipment details already match this document.");
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel); layout->addWidget(buttons); buttons->button(QDialogButtonBox::Apply)->setEnabled(table->rowCount()>0);
        connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted) return;
        for(int row=0;row<table->rowCount();++row) if(table->item(row,0)->checkState()==Qt::Checked) {
            const auto key=table->item(row,0)->data(Qt::UserRole).toString(); setText(key,fieldText(key,suggestions[key]));
            for(const auto &role : {QString("SHIPPER"),QString("CONSIGNEE")}) if(key.endsWith(':'+role+"_ARRAY")) {
                const auto number=key.section(':',0,0)+':'+role+"_NUMBER"; if(value.values.contains(number)) value.values[number]=booking.values["BOO:"+role+"_NUMBER"];
            }
        }
    }
    void buildShipmentDocuments(Store &store,QVBoxLayout *outer) {
        auto *row=new QHBoxLayout; row->addWidget(new QLabel("Documents")); outer->addLayout(row);
        auto *type=new QComboBox; type->setObjectName("editorDocumentType");
        for(const auto &[source,label] : DocumentTypes) type->addItem(label,source);
        auto *saved=new QComboBox; saved->setObjectName("editorDocumentVersion");
        for(auto *combo : {type,saved}) { combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(12); combo->setMinimumWidth(100); }
        row->addWidget(type); row->addWidget(saved,1);
        auto *add=new QPushButton("Add"); add->setObjectName("editorDocumentAdd"); auto *edit=new QPushButton("Edit"); edit->setObjectName("editorDocumentEdit"); auto *view=new QPushButton("View"); view->setObjectName("editorDocumentView");
        auto *remove=new QPushButton("Delete"); remove->setObjectName("editorDocumentDelete");
        for(auto *button : {add,edit,view,remove}) row->addWidget(button);
        if(!value.id) { auto *hint=new QLabel("Save the new shipment before adding documents."); hint->setWordWrap(true); outer->addWidget(hint); }
        const auto refresh=[this,&store,type,saved,add,edit,view,remove](qint64 selected=0) {
            QSignalBlocker block(saved); saved->clear();
            if(value.id) for(const auto &link : store.linkedDocuments(value.id)) if(link.record.source==type->currentData().toString()) {
                const auto &record=link.record; const auto pp=editableFields(record.source).first().first.section(':',0,0)+':';
                auto number=valueText(record.values[pp+(record.source=="AWB.TPS" ? "AWB_NUMBER" : record.source=="DODR.TPS" ? "DO_NUMBER" : record.source=="SIGHT.TPS" ? "SD_NUMBER" : "BL_NUMBER")]);
                QString label=(number.isEmpty() ? recordReference(record) : number)+" · Sequence "+valueText(record.values[pp+"ID_SEQUENCE"]);
                if(link.match==DocumentMatch::AmbiguousReference) label+=" · Shared reference: verify shipment";
                saved->addItem(label,record.id);
            }
            if(saved->findData(selected)>=0) saved->setCurrentIndex(saved->findData(selected));
            if(!saved->count()) saved->addItem("No saved document",QVariant::fromValue<qint64>(0));
            add->setEnabled(value.id && store.sources().contains(type->currentData().toString()));
            edit->setEnabled(saved->currentData().toLongLong()); view->setEnabled(saved->currentData().toLongLong()); remove->setEnabled(saved->currentData().toLongLong());
        };
        connect(type,&QComboBox::currentIndexChanged,this,[this,refresh] { try { refresh(); } catch(const std::exception &e) { error(this,e); } });
        connect(saved,&QComboBox::currentIndexChanged,this,[saved,edit,view,remove] { edit->setEnabled(saved->currentData().toLongLong()); view->setEnabled(saved->currentData().toLongLong()); remove->setEnabled(saved->currentData().toLongLong()); });
        connect(add,&QPushButton::clicked,this,[this,&store,type,refresh] {
            try {
                const auto original=value;
                Record candidate;
                try { readEditors(); candidate=value; } catch(...) { value=original; throw; }
                value=original;
                const auto persisted=store.record(value.id);
                if(persisted.version!=value.version) throw std::runtime_error("This shipment changed. Reopen it before adding a document.");
                if(candidate.values!=persisted.values) {
                    QMessageBox confirm(QMessageBox::Question,"Save shipment changes","Save shipment changes before adding a document?",QMessageBox::Save|QMessageBox::Cancel,this);
                    confirm.setObjectName("saveShipmentForDocument"); confirm.setInformativeText("The document will use the saved shipment details. These changes stay saved when you close this editor.");
                    if(confirm.exec()!=QMessageBox::Save) return;
                    value=store.record(store.save(candidate));
                    saveError->clear(); saveError->hide();
                }
                const auto current=store.record(value.id);
                if(current.version!=value.version) throw std::runtime_error("This shipment changed. Reopen it before adding a document.");
                const auto target=prepareShipmentDocument(store,current,type->currentData().toString(),this); if(!target) return;
                RecordEditor editor(store,*target,this);
                if(editor.exec()==QDialog::Accepted) refresh(editor.value.id);
            } catch(const std::exception &e) { error(this,e); }
        });
        connect(remove,&QPushButton::clicked,this,[this,&store,saved,refresh] {
            try { if(saved->currentData().toLongLong() && deleteShippingDocument(store,store.record(saved->currentData().toLongLong()),this)) refresh(); }
            catch(const std::exception &e) { error(this,e); }
        });
        connect(edit,&QPushButton::clicked,this,[this,&store,saved,refresh] {
            try { if(!saved->currentData().toLongLong()) return; RecordEditor editor(store,store.record(saved->currentData().toLongLong()),this); if(editor.exec()==QDialog::Accepted) refresh(editor.value.id); }
            catch(const std::exception &e) { error(this,e); }
        });
        connect(view,&QPushButton::clicked,this,[this,&store,saved] { try { if(saved->currentData().toLongLong()) previewShippingDocument(store,saved->currentData().toLongLong(),this); } catch(const std::exception &e) { error(this,e); } });
        refresh();
    }
    void buildDocumentEditor(Store &store,QVBoxLayout *outer) {
        setObjectName("documentEditor"); resize(1160,900); setMinimumSize(720,500);
        auto *tools=new QHBoxLayout;
        if(value.source!="SIGHT.TPS") {
            auto *fill=new QPushButton("Use shipment details…"); fill->setObjectName("useShipmentDetails"); fill->setAutoDefault(false); tools->addWidget(fill);
            connect(fill,&QPushButton::clicked,this,[this,&store] { try { fillFromShipment(store); } catch(const std::exception &e) { error(this,e); } });
        }
        if(value.source=="AWB.TPS") {
            auto *dimensions=new QPushButton("Dimensions / weight…"); dimensions->setObjectName("editDimensions"); dimensions->setAutoDefault(false); tools->addWidget(dimensions);
            connect(dimensions,&QPushButton::clicked,this,[this] {
                const auto original=value;
                try {
                    // Open even when a previous automatic rule needs new dimensions.
                    const auto priorRule=value.values.take("NATIVE:DIMENSION_CALCULATION");
                    const auto priorRates=value.values.take("NATIVE:AWB_CALCULATIONS");
                    readEditors(); auto current=value; current.values["NATIVE:DIMENSION_CALCULATION"]=priorRule; current.values["NATIVE:AWB_CALCULATIONS"]=priorRates; value=original;
                    DimensionsDialog dialog(current,this);
                    if(dialog.exec()==QDialog::Accepted) {
                        value=dialog.value;
                        QSignalBlocker guard(airCargo);
                        for(int row=0;row<AirCargoRows;++row) airCargo->item(row,3)->setText(weightText(valueText(value.values["AWB:CHARGEABLE_WEIGHT"].toArray()[row])));
                        updateAirTotals(true);
                    }
                } catch(const std::exception &e) { value=original; error(this,e); }
            });
            auto *rates=new QPushButton("Rates / charges…"); rates->setObjectName("editAirRates"); rates->setAutoDefault(false); tools->addWidget(rates);
            connect(rates,&QPushButton::clicked,this,[this] {
                const auto original=value;
                try {
                    const auto priorRates=value.values.take("NATIVE:AWB_CALCULATIONS");
                    readEditors(); auto current=value; current.values["NATIVE:AWB_CALCULATIONS"]=priorRates; value=original;
                    RatingDialog dialog(current,this);
                    if(dialog.exec()==QDialog::Accepted) value=dialog.value;
                } catch(const std::exception &e) { value=original; error(this,e); }
            });
        }
        tools->addStretch(); outer->addLayout(tools);
        auto *scroll=new QScrollArea; scroll->setObjectName("documentEditorScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *body=new QWidget; auto *layout=new QVBoxLayout(body); layout->setContentsMargins(0,0,8,0); layout->setSpacing(10);
        scroll->setWidget(body); outer->addWidget(scroll,1);
        const auto prefix=editableFields(value.source).first().first.section(':',0,0)+':';
        using Fields=QList<QPair<QString,QString>>;
        QSet<QString> placed; QList<QWidget *> order;
        const auto fullKey=[&](const QString &key) { return key.contains(':') ? key : prefix+key; };
        const auto grid=[](QWidget *parent) { auto *g=new QGridLayout(parent); g->setContentsMargins(12,17,12,12); g->setHorizontalSpacing(10); g->setVerticalSpacing(7); return g; };
        const auto field=[&](QGridLayout *g,int row,int col,const QString &key,const QString &label,bool stacked) {
            auto *editor=editors.value(fullKey(key)); if(!editor) return;
            placed.insert(fullKey(key)); editor->setMinimumWidth(70); order<<editor;
            auto *caption=new QLabel(label); caption->setBuddy(editor); caption->setWordWrap(true);
            if(auto *multi=qobject_cast<QPlainTextEdit *>(editor)) { multi->setMinimumHeight(85); multi->setMaximumHeight(105); if(fullKey(key)=="NATIVE:CARRIER_NAME") multi->setFixedHeight(44); }
            if(stacked) {
                auto *cell=new QWidget; auto *l=new QVBoxLayout(cell); l->setContentsMargins(0,0,0,0); l->setSpacing(3); l->addWidget(caption); l->addWidget(editor);
                g->addWidget(cell,row,col,Qt::AlignTop); g->setColumnStretch(col,1);
            } else { g->addWidget(caption,row,col*2); g->addWidget(editor,row,col*2+1); g->setColumnStretch(col*2+1,1); }
        };
        const auto group=[&](const QString &title,const Fields &fields,int columns=2,bool stacked=false) -> QGroupBox * {
            auto *box=new QGroupBox(title); auto *g=grid(box); int n=0;
            for(const auto &[key,label] : fields) if(editors.contains(fullKey(key))) { field(g,n/columns,n%columns,key,label,stacked); ++n; }
            if(!n) { delete box; return nullptr; } return box;
        };
        const auto section=[&](const QString &title,const Fields &fields,int columns=2,bool stacked=false) {
            if(auto *box=group(title,fields,columns,stacked)) layout->addWidget(box);
        };
        if(value.source=="SIGHT.TPS") {
            section("Draft",{{"REFERENCE_NUMBER","Shipment reference"},{"SD_NUMBER","Draft number"},{"DATE1","Draft date"},{"USAMOUNT","Amount (USD)"},{"HOW_PAID","Payment terms"},{"DATE2","Transmittal date"}},3,true);
            section("Payment",{{"TO_ORDER","Pay to the order of"},{"IN_DOLLARS","Amount in words (blank = calculate from USD amount)"},{"ACCOUNT_OF_1","Account of"},{"ACCOUNT_OF_2","Account of (continued)"}},2,true);
            section("Banks / contact",{{"B_NAME","Drawee / bank address"},{"GENT1","Remitting bank / references"},{"F_NAME","Contact in case of need"}},3,true);
            section("Collection",{{"LC_NO","Letter of credit number"},{"FOR_FIELD","Collection reference"},{"COLLECTION","For collection"},{"COLLECT_LC","For payment / negotiation under L/C"}});
            section("Enclosed documents",{{"B_L","B/L originals"},{"B_LCOPY","B/L copies"},{"COM_INV","Commercial invoices"},{"INS_CER","Insurance certificates"},{"CER_ORI","Certificates of origin"},{"CONS_INV","Consular invoices"},{"L_C","Letters of credit"},{"PCK_LIST","Packing lists"},{"WGHT_CER","Weight certificates"}},5,true);
            section("Other enclosures / instructions",{{"OTHER_DOCS","Other documents"},{"OTHER_DOCS2","Other documents (continued)"},{"OTHER_INSTRUCTIONS","Other instructions"}},3,true);
            Fields instructions; for(const auto &f : sightInstructionFields()) if(!placed.contains(f.first)) instructions<<f;
            section("Handling instructions",instructions,2);
            section("Signatory names",{{"SIGHT_SIG","Drawer name"},{"SIGHT_SIG2","Transmittal signatory name"}},2,true);
            section("Additional draft text",{{"DESC1","Additional draft text"},{"DESC2","Continued"},{"VALUE1","Additional value text"},{"VALUE2","Continued"}});
            auto *note=new QLabel("Preview includes the First and Second of Exchange. Print both, or choose a single page in the print dialog. Signature lines remain blank for signing."); note->setWordWrap(true); layout->addWidget(note); layout->addStretch();
            for(int i=1;i<order.size();++i) QWidget::setTabOrder(order[i-1],order[i]);
            if(!order.isEmpty()) order.first()->setFocus();
            return;
        }
        if(value.source=="AWB.TPS") section("Document",{{"REFERENCE_NUMBER","Shipment reference"},{"AWB_NUMBER","Air waybill"},{"MAWB_NUMBER","Master air waybill"}},3,true);
        else section("Document",{{"REFERENCE_NUMBER","Shipment reference"},{"BL_NUMBER","Bill of lading"},{"BOOKING_NUMBER","Booking number"},{"DOCUMENT_NUMBER","Document number"},{"DO_NUMBER","Dock receipt number"},{"MBL_NUMBER","Master bill of lading"},{"PO_NUMBER","Purchase order"},{"PROJECT_NUMBER","Project number"}},3,true);
        auto *parties=new QHBoxLayout; parties->setSpacing(12); layout->addLayout(parties);
        for(const auto &role : {QString("SHIPPER"),QString("CONSIGNEE")}) {
            auto *box=new QGroupBox(role=="SHIPPER" ? "Shipper" : "Consignee"); auto *g=grid(box); parties->addWidget(box,1);
            const auto key=prefix+role+"_ARRAY"; auto *address=qobject_cast<QPlainTextEdit *>(editors.value(key));
            if(address) { address->setMinimumHeight(90); address->setMaximumHeight(105); address->setPlaceholderText("Company name and full address"); g->addWidget(address,0,0,1,4); placed.insert(key); order<<address; }
            field(g,1,0,role+"_CONTACT","Contact",false); field(g,1,1,role+"_PHONE","Phone",false);
            auto *choose=new QPushButton("Choose customer…"); choose->setObjectName("choose"+role); g->addWidget(choose,2,0,1,4,Qt::AlignRight); order<<choose;
            connect(choose,&QPushButton::clicked,this,[this,&store,role] { try { chooseCustomer(store,role); } catch(const std::exception &e) { error(this,e); } });
            if(editors.contains(prefix+role+"_CONTACT")) {
                auto *contact=new QPushButton("Choose contact…"); contact->setObjectName("chooseContact"+role); g->addWidget(contact,2,0,1,2,Qt::AlignLeft); order<<contact;
                connect(contact,&QPushButton::clicked,this,[this,&store,role] { chooseContact(store,role); });
                g->removeWidget(choose); g->addWidget(choose,2,2,1,2,Qt::AlignRight);
            }
        }
        section("Additional parties",{{"NOTIFY_ARRAY","Notify party"},{"NATIVE:FORWARDER_ADDRESS","Forwarding agent / references"}},2,true);
        if(value.source=="AWB.TPS") {
            section("Agent / contact",{{"AWB_ACCOUNTING_INFO","Agent accounting / contact information"}},1,true);
            section("Routing",{{"AIRPORT_OF_ORIGIN","Origin airport"},{"AIRPORT_OF_DESTINATION","Destination airport"},
                {"AIRPORT_FROM","Origin code"},{"ULTIMATE_DESTINATION","Ultimate destination"},{"FIRST_CARRIER","First carrier"},{"AIRPORT_TO_1","First destination code"},
                {"SECOND_CARRIER","Second carrier"},{"AIRPORT_TO_2","Second destination code"},{"THIRD_CARRIER","Third carrier"},{"AIRPORT_TO_3","Third destination code"},
                {"FLIGHT_DATE1","Flight / date"},{"FLIGHT_DATE2","Second flight / date"}});
            auto *cargo=new QGroupBox("Cargo"); auto *row=new QHBoxLayout(cargo); row->setContentsMargins(12,17,12,12); row->setSpacing(12); layout->addWidget(cargo);
            auto *tableBox=new QWidget; auto *form=new QFormLayout(tableBox); form->setContentsMargins(0,0,0,0); addAirCargo(form); row->addWidget(tableBox,3); order<<airCargo;
            auto *description=new QWidget; auto *dg=grid(description); dg->setContentsMargins(0,0,0,0); row->addWidget(description,2);
            field(dg,0,0,"NATURE_OF_GOODS","Nature and quantity of goods",true);
            if(auto *cell=dg->itemAtPosition(0,0)) dg->addWidget(cell->widget(),0,0,1,2,Qt::AlignTop);
            field(dg,1,0,"PIECES_TOTAL","Total pieces",false); field(dg,2,0,"GROSS_WEIGHT_TOTAL","Gross weight",false);
            section("Handling / notes",{{"HANDLING_INFORMATION","Handling information"},{"EXTENDED_DESCRIPTION","Additional description"}},2,true);
            section("Service / execution",{{"TYPE_OF_SERVICE","Type of service"},{"SERVICE_LEVEL","Service level"},{"EXECUTION_DATE","Execution date"},{"EXECUTION_PLACE","Execution place"}});
        } else {
            section("Routing",{{"NATIVE:CARRIER_NAME","Ocean carrier"},{"PRECARRIAGE_BY","Pre-carriage by"},{"VESSEL_NAME","Vessel"},{"VOYAGE_NUMBER","Voyage"},
                {"LOAD_PORT","Loading port"},{"DISCHARGE_PORT","Discharge port"},{"PLACE_OF_RECEIPT","Place of receipt"},{"PLACE_OF_DELIVERY","Place of delivery"},
                {"PIER_TERMINAL","Loading terminal"},{"POINT_OF_ORIGIN","Origin / country"},{"TYPE_MOVE","Type of move"},{"DEL_CAR","Delivering carrier"},{"PRO_NUM","Truck / pro number"},{"TEL_NO","Delivery phone"}});
            section("Cargo amounts",{{"PCS","Packages"},{"KGS","Weight (kg)"},{"LBS","Weight (lb)"},{"CBM","Volume (m³)"},
                {"TOTAL_KGS","Total weight (kg)"},{"TOTAL_LBS","Total weight (lb)"},{"TOTAL_CBM","Total volume (m³)"}},4,true);
            if(!cargoSource(value.source).first.isEmpty()) {
                auto *cargo=new QGroupBox("Cargo / document notes"); cargo->setObjectName("documentCargoSection");
                auto *cargoLayout=new QVBoxLayout(cargo); cargoLayout->setContentsMargins(12,17,12,12);
                auto *editor=createCargoEditor(store,value,cargo); editor->setMinimumHeight(300); cargoLayout->addWidget(editor); layout->addWidget(cargo);
                order<<editor->findChild<QTableWidget *>("cargoTable");
            }
            section("References / onward routing",{{"REFERENCE_ARRAY","Export references"},{"ROUTING_ARRAY","Also notify / domestic routing"},{"ONWARD_ARRAY","Onward inland routing"},{"ON_NAME","Onward inland routing"}},2,true);
            section("Delivery addresses",{{"TO_NAME","Deliver to / document recipient"},{"PD_NAME","Delivery location"}},2,true);
            section("Instructions / notes",{{"BL_INSTRUCTIONS","Instructions"},{"NOTES","Notes"},{"INSTRUCTIONS_COMMENTS","Routing / delivery instructions"},{"HBL_RATING_NOTES","Freight / charge notes"}},2,true);
            section("Dates / issue",{{"DATE","Document date"},{"DO_DATE","Document date"},{"CUTOFF","Cutoff date"},{"SAILING","Sailing date"},
                {"ISSUED_AT","Place of issue"},{"PAYABLE_AT","Freight payable at"},{"NUMBER_OF_ORIGINALS","Number of originals"}});
            if(value.source=="CERTORGN.TPS") {
                section("Origin declaration",{{"COUNTRY","Country of origin"},{"UNDERSIGNED","Declarant name / company"},{"OWNER_AGENT","Owner / agent capacity"},{"DATED_AT","Declaration place"}});
                section("Chamber / notarization",{{"COC_AREA","Chamber of commerce"},{"STATE","Chamber state"},{"SWORN_DATE","Sworn date"},{"COMMISSION_EXPIRES_DATE","Commission expiry date"}});
            }
        }
        // Keep every supported editor reachable if a source has additional fields.
        Fields extra;
        for(const auto &[key,label] : editableFields(value.source)) if(editors.contains(key) && !placed.contains(key) && !key.startsWith("NATIVE:VICS_")) extra.append({key,label});
        section("Additional details",extra);
        if(value.source=="OBL.TPS") {
            auto *toggle=new QCheckBox("Show truck freight (VICS) fields"); toggle->setObjectName("showTruckFields"); layout->addWidget(toggle); order<<toggle;
            auto *truck=new QWidget; auto *tl=new QVBoxLayout(truck); tl->setContentsMargins(0,0,0,0); layout->addWidget(truck); truck->hide();
            Fields fields; for(const auto &[key,label] : vicsFields(value.source)) if(!vicsOrderKeys().contains(key)) fields.append({key,label});
            if(auto *box=group("Truck freight",fields)) tl->addWidget(box);
            vicsOrders=new QTableWidget(8,5); vicsOrders->setObjectName("vicsOrders"); vicsOrders->setHorizontalHeaderLabels({"Order number","Packages","Weight (lb)","Pallet/slip Y/N","Shipper info"});
            vicsOrders->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); vicsOrders->setMinimumHeight(310); tl->addWidget(vicsOrders); order<<vicsOrders;
            for(int col=0;col<5;++col) {
                const auto array=value.values[vicsOrderKeys()[col]].toArray();
                if(array.size()!=8) throw std::runtime_error("Invalid saved VICS order table; no values were discarded.");
                for(int row=0;row<8;++row) vicsOrders->setItem(row,col,new QTableWidgetItem(valueText(array[row])));
            }
            auto *note=new QLabel("Review truck carrier, freight terms and cargo before printing the VICS form. Signatures remain blank for signing."); note->setWordWrap(true); tl->addWidget(note);
            connect(toggle,&QCheckBox::toggled,truck,&QWidget::setVisible);
        }
        layout->addStretch();
        for(int i=1;i<order.size();++i) QWidget::setTabOrder(order[i-1],order[i]);
        if(!order.isEmpty()) order.first()->setFocus();
    }
    void buildShipmentEditor(Store &store,QVBoxLayout *outer) {
        setObjectName("shipmentEditor"); resize(1160,900); setMinimumSize(720,500);
        auto *scroll=new QScrollArea; scroll->setObjectName("shipmentEditorScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
        auto *body=new QWidget; auto *layout=new QVBoxLayout(body); layout->setContentsMargins(0,0,8,0); layout->setSpacing(10);
        scroll->setWidget(body); outer->addWidget(scroll,1);
        QList<QWidget *> tabOrder;
        const auto grid=[](QWidget *parent) { auto *g=new QGridLayout(parent); g->setHorizontalSpacing(10); g->setVerticalSpacing(7); g->setContentsMargins(12,17,12,12); return g; };
        const auto field=[&](QGridLayout *g,int row,int col,const QString &suffix,const QString &label,bool stacked=false) {
            auto *editor=editors.value("BOO:"+suffix); if(!editor) return;
            auto *caption=new QLabel(label); caption->setBuddy(editor);
            if(stacked) { auto *cell=new QWidget; auto *l=new QVBoxLayout(cell); l->setContentsMargins(0,0,0,0); l->setSpacing(3); l->addWidget(caption); l->addWidget(editor); g->addWidget(cell,row,col,Qt::AlignTop); g->setColumnStretch(col,1); }
            else { g->addWidget(caption,row,col); g->addWidget(editor,row,col+1); g->setColumnStretch(col+1,1); }
            editor->setMinimumWidth(70); tabOrder<<editor;
        };
        auto *identity=new QGroupBox("Shipment"); auto *idGrid=grid(identity);
        field(idGrid,0,0,"REFERENCE_NUMBER","Reference",true); field(idGrid,0,1,"BOOKING_NUMBER","Booking number",true); field(idGrid,0,2,"PO_NUMBER","Purchase order",true); layout->addWidget(identity);
        auto *parties=new QHBoxLayout; parties->setSpacing(12); layout->addLayout(parties);
        for(const auto &role : {QString("SHIPPER"),QString("CONSIGNEE")}) {
            auto *card=new QGroupBox(role=="SHIPPER" ? "Shipper" : "Consignee"); auto *g=grid(card); parties->addWidget(card,1);
            auto *address=qobject_cast<QPlainTextEdit *>(editors.value("BOO:"+role+"_ARRAY")); address->setMinimumHeight(92); address->setMaximumHeight(110); address->setPlaceholderText("Company name and full address");
            g->addWidget(address,0,0,1,4); tabOrder<<address;
            field(g,1,0,role+"_CONTACT","Contact"); field(g,1,2,role+"_PHONE","Phone");
            auto *choose=new QPushButton("Choose customer…"); choose->setObjectName("choose"+role); g->addWidget(choose,2,2,1,2,Qt::AlignRight); tabOrder<<choose;
            connect(choose,&QPushButton::clicked,this,[this,&store,role] { try { chooseCustomer(store,role); } catch(const std::exception &e) { error(this,e); } });
            auto *contact=new QPushButton("Choose contact…"); contact->setObjectName("chooseContact"+role); g->addWidget(contact,2,0,1,2,Qt::AlignLeft); tabOrder<<contact;
            connect(contact,&QPushButton::clicked,this,[this,&store,role] { chooseContact(store,role); });
        }
        auto *routeRow=new QHBoxLayout; routeRow->setSpacing(12); layout->addLayout(routeRow);
        auto *route=new QGroupBox("Routing"); auto *g=grid(route); routeRow->addWidget(route,2);
        field(g,0,0,"ORIGIN_LOCATION","Origin"); field(g,0,2,"DESTINATION_LOCATION","Destination");
        field(g,1,0,"LOAD_PORT","Loading port"); field(g,1,2,"DISCHARGE_PORT","Discharge port");
        field(g,2,0,"CARRIER_INDEX","Carrier"); field(g,2,2,"PLACE_OF_DELIVERY","Delivery place");
        field(g,3,0,"VESSEL_NAME","Vessel"); field(g,3,2,"VOYAGE_NUMBER","Voyage");
        auto *dates=new QGroupBox("Dates"); auto *dg=grid(dates); routeRow->addWidget(dates,1);
        field(dg,0,0,"READY_DATE","Ready"); field(dg,1,0,"CUTOFF_DATE","Cutoff"); field(dg,2,0,"SAILING_DATE","Sailing");
        field(dg,3,0,"ETD","Departure"); field(dg,4,0,"ETA","Arrival");
        auto *cargo=new QGroupBox("Cargo"); auto *cg=grid(cargo); layout->addWidget(cargo);
        field(cg,0,0,"COMMODITY_1","Goods description"); field(cg,0,2,"COMMODITY_2","Additional description");
        field(cg,1,0,"EQUIPMENT_DESCRIPTION","Equipment");
        auto *amounts=new QWidget; auto *ag=new QGridLayout(amounts); ag->setContentsMargins(0,0,0,0); ag->setHorizontalSpacing(12);
        field(ag,0,0,"QTY","Packages",true); field(ag,0,1,"KGS","Weight (kg)",true); field(ag,0,2,"LBS","Weight (lb)",true); field(ag,0,3,"CBM","Volume (m³)",true);
        cg->addWidget(amounts,2,0,1,4);
        cg->addWidget(unitControl("weight","BOO:KGS","BOO:LBS"),3,0,1,4);
        auto *notesRow=new QHBoxLayout; notesRow->setSpacing(12); layout->addLayout(notesRow);
        auto *notes=new QGroupBox("Instructions / notes"); auto *ng=grid(notes); notesRow->addWidget(notes,2);
        field(ng,0,0,"HAZARDOUS_COMMENTS","Handling notes",true); field(ng,0,1,"BOOKING_COMMENTS","Booking notes",true);
        for(const auto &key : {"BOO:HAZARDOUS_COMMENTS","BOO:BOOKING_COMMENTS"}) if(auto *text=qobject_cast<QPlainTextEdit *>(editors.value(key))) { text->setMinimumHeight(70); text->setMaximumHeight(95); }
        auto *contact=new QGroupBox("Customer contact"); auto *cc=grid(contact); notesRow->addWidget(contact,1);
        field(cc,0,0,"CLIENT_CONTACT","Name"); field(cc,1,0,"CLIENT_PHONE","Phone"); field(cc,2,0,"CLIENE_EMAIL","Email");
        auto *chooseContactButton=new QPushButton("Choose contact…"); chooseContactButton->setObjectName("chooseContactCLIENT"); cc->addWidget(chooseContactButton,3,0,1,2);
        connect(chooseContactButton,&QPushButton::clicked,this,[this,&store] { chooseContact(store,"CLIENT"); });
        layout->addStretch();
        for(int i=1;i<tabOrder.size();++i) QWidget::setTabOrder(tabOrder[i-1],tabOrder[i]);
        if(!tabOrder.isEmpty()) tabOrder.first()->setFocus();
    }
    QTableWidget *dgRows = nullptr;
    void appendDangerousRow(const QJsonObject &values) {
        const auto row=dgRows->rowCount(); dgRows->insertRow(row); dgRows->setRowHeight(row,64);
        for(int col=0;col<7;++col) dgRows->setItem(row,col,new QTableWidgetItem(values[dangerousGoodsColumns()[col].first].toString()));
    }
    QTableWidget *airCargo = nullptr;
    QTableWidget *vicsOrders = nullptr;
    QLabel *cargoTotal = nullptr, *saveError = nullptr;
    QJsonObject airValues() const {
        auto result = value.values;
        for (int col=0;col<4;++col) {
            auto array = value.values.value(airCargoKeys()[col]).toArray();
            for (int row=0;row<AirCargoRows;++row) {
                if (col == 2) array[row] = qobject_cast<QComboBox *>(airCargo->cellWidget(row,col))->currentData().toString();
                else {
                    auto text = airCargo->item(row,col)->text().trimmed();
                    // A display-only removal of .00 must not rewrite imported
                    // weights or trigger recalculation when another field saves.
                    const auto original=valueText(array[row]);
                    if ((col==1 || col==3) && text==weightText(original)) array[row]=original;
                    else array[row] = text.isEmpty() ? "0" : text;
                }
            }
            result[airCargoKeys()[col]] = array;
        }
        return result;
    }
    void updateAirTotals(bool changed=false) {
        try {
            auto candidate=airValues();
            if(changed && candidate.value("NATIVE:DIMENSION_CALCULATION").toObject()["enabled"].toBool()) {
                const auto calculated=calculateDimensions(Record{value.id,value.version,value.source,candidate}); candidate=calculated.record.values;
                QSignalBlocker guard(airCargo);
                for(int row=0;row<AirCargoRows;++row) airCargo->item(row,3)->setText(weightText(valueText(candidate["AWB:CHARGEABLE_WEIGHT"].toArray()[row])));
            }
            const auto totals = airCargoTotals(candidate);
            QStringList chargeable; for(int row=0;row<AirCargoRows;++row) chargeable<<candidate["AWB:CHARGEABLE_WEIGHT"].toArray()[row].toString();
            const auto unit=totals.unit=="K" ? " kg" : totals.unit=="L" ? " lb" : "";
            cargoTotal->setText("Calculated: " + totals.pieces + " pieces · " + weightText(totals.grossWeight)+unit+" gross · "+weightText(sumAmounts(chargeable))+unit+" chargeable");
            if(changed) { setText("AWB:PIECES_TOTAL",totals.pieces); setText("AWB:GROSS_WEIGHT_TOTAL",weightText(totals.grossWeight)); }
        } catch (const std::exception &e) { cargoTotal->setText(QString::fromUtf8(e.what())); }
    }
    void addAirCargo(QFormLayout *form) {
        for (const auto &key : airCargoKeys())
            if (value.values.value(key).toArray().size() != AirCargoStorageSlots) throw std::runtime_error("Invalid stored cargo array");
        airCargo = new QTableWidget(AirCargoRows,4); airCargo->setObjectName("airCargo");
        airCargo->setHorizontalHeaderLabels({"Pieces","Gross weight","Unit","Chargeable weight"});
        airCargo->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        airCargo->verticalHeader()->setDefaultSectionSize(29);
        airCargo->setFixedHeight(180); form->insertRow(0,airCargo);
        cargoTotal = new QLabel; cargoTotal->setWordWrap(true); form->insertRow(1,cargoTotal);
        auto *hint = new QLabel("Totals update as you edit. Use Dimensions / weight for volumetric calculations and Rates / charges for pricing.");
        hint->setWordWrap(true); form->insertRow(2,hint);
        auto *gross=new QPushButton("Use gross for empty chargeable weights"); gross->setObjectName("useGrossWeight"); gross->setAutoDefault(false); form->insertRow(3,gross);
        connect(gross,&QPushButton::clicked,this,[this] {
            for(int row=0;row<AirCargoRows;++row) if(airCargo->item(row,3)->text().trimmed().isEmpty() || airCargo->item(row,3)->text().toDouble()==0) airCargo->item(row,3)->setText(airCargo->item(row,1)->text());
            updateAirTotals(true);
        });
        for (int row=0;row<AirCargoRows;++row) for (int col=0;col<4;++col) {
            const auto stored = valueText(value.values[airCargoKeys()[col]].toArray().at(row));
            const auto text = col==1 || col==3 ? weightText(stored) : stored;
            if (col == 2) {
                auto *unit = new QComboBox; unit->addItem("—",QString("")); unit->addItem("kg","K"); unit->addItem("lb","L");
                if (unit->findData(text) < 0) unit->addItem(text,text);
                unit->setCurrentIndex(unit->findData(text)); airCargo->setCellWidget(row,col,unit);
                connect(unit,&QComboBox::currentIndexChanged,this,[this] { updateAirTotals(true); });
            } else airCargo->setItem(row,col,new QTableWidgetItem(text));
        }
        connect(airCargo,&QTableWidget::itemChanged,this,[this](QTableWidgetItem *item) {
            if(item->column()==3) {
                auto rule=value.values.value("NATIVE:DIMENSION_CALCULATION").toObject(); auto modes=rule["auto_chargeable"].toArray();
                if(modes.size()==AirCargoRows) { modes[item->row()]=false; rule["auto_chargeable"]=modes; value.values["NATIVE:DIMENSION_CALCULATION"]=rule; }
            }
            auto *unit=qobject_cast<QComboBox *>(airCargo->cellWidget(item->row(),2));
            if((item->column()==0 || item->column()==1) && item->text().toDouble()>0 && unit->currentData().toString().isEmpty()) {
                QString selected="K"; for(int row=0;row<AirCargoRows;++row) { const auto u=qobject_cast<QComboBox *>(airCargo->cellWidget(row,2))->currentData().toString(); if(!u.isEmpty()) { selected=u; break; } }
                QSignalBlocker guard(unit); unit->setCurrentIndex(unit->findData(selected));
            }
            updateAirTotals(true);
        }); updateAirTotals();
    }
    void setText(const QString &key, const QString &text) {
        auto *widget = editors.value(key);
        if (auto *line = qobject_cast<QLineEdit *>(widget)) line->setText(text);
        if (auto *multi = qobject_cast<QPlainTextEdit *>(widget)) multi->setPlainText(editorText(text,value.values[key].isArray()));
    }
    void chooseContact(Store &store,const QString &role) {
        try {
            QDialog picker(this); picker.setObjectName("contactPicker"); picker.setWindowTitle("Choose contact"); picker.resize(640,480);
            auto *layout=new QVBoxLayout(&picker); auto *search=new QLineEdit; search->setPlaceholderText("Search name, phone or email"); auto *list=new QListWidget; list->setObjectName("contactChoices"); layout->addWidget(search); layout->addWidget(list);
            for(const auto &contact : store.records({"CONTACTS.TPS"})) {
                auto *item=new QListWidgetItem(recordTitle(contact)+" · "+valueText(contact.values["CTA:PHONE"])+" · "+valueText(contact.values["CTA:EMAIL"]),list); item->setData(Qt::UserRole,contact.id);
            }
            connect(search,&QLineEdit::textChanged,&picker,[list](const QString &text) { for(int i=0;i<list->count();++i) list->item(i)->setHidden(!list->item(i)->text().contains(text,Qt::CaseInsensitive)); });
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::accepted,&picker,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&picker,&QDialog::reject); connect(list,&QListWidget::itemDoubleClicked,&picker,&QDialog::accept);
            if(picker.exec()!=QDialog::Accepted || !list->currentItem() || list->currentItem()->isHidden()) return;
            const auto contact=store.record(list->currentItem()->data(Qt::UserRole).toLongLong()); const auto prefix=editableFields(value.source).first().first.section(':',0,0)+':';
            setText(prefix+role+"_CONTACT",recordTitle(contact)); setText(prefix+role+"_PHONE",valueText(contact.values["CTA:PHONE"]));
            if(role=="CLIENT") setText("BOO:CLIENE_EMAIL",valueText(contact.values["CTA:EMAIL"]));
        } catch(const std::exception &e) { error(this,e); }
    }
    void chooseCustomer(Store &store, const QString &role) {
        QDialog picker(this); picker.setObjectName("customerPicker"); picker.setWindowTitle("Choose customer"); picker.resize(640, 480);
        auto *layout = new QVBoxLayout(&picker); auto *search = new QLineEdit; search->setPlaceholderText("Search customers");
        auto *list = new QListWidget; list->setObjectName("customerChoices"); layout->addWidget(search); layout->addWidget(list);
        const auto customers = store.records({"CUSTOMER.TPS"});
        for (const auto &customer : customers) {
            QStringList parts{recordTitle(customer)};
            for(const auto &key : {"CUS:CITY_STATE","CUS:COUNTRY","CUS:PH1_NUMBER"}) {
                const auto detail=valueText(customer.values.value(key)).trimmed(); if(!detail.isEmpty()) parts<<detail;
            }
            auto *item = new QListWidgetItem(parts.join(" · "), list);
            item->setData(Qt::UserRole, customer.id);
        }
        connect(search, &QLineEdit::textChanged, &picker, [list](const QString &text) {
            for (int i = 0; i < list->count(); ++i) list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &picker, [&picker] { picker.accept(); });
        if (picker.exec() != QDialog::Accepted || !list->currentItem() || list->currentItem()->isHidden()) return;
        const auto customer = store.record(list->currentItem()->data(Qt::UserRole).toLongLong());
        const auto prefix = editableFields(value.source).first().first.section(':', 0, 0) + ':';
        QStringList address;
        for (const QString &key : {QString("CUS:NAME"), QString("CUS:ADDRESS"), QString("CUS:ADDRESS2"), QString("CUS:SUITE")}) {
            const auto part = valueText(customer.values[key]).trimmed(); if (!part.isEmpty()) address << part;
        }
        const auto city = (valueText(customer.values["CUS:CITY_STATE"]) + ' ' + valueText(customer.values["CUS:ZIP"])).trimmed();
        if (!city.isEmpty()) address << city;
        const auto country = valueText(customer.values["CUS:COUNTRY"]).trimmed(); if (!country.isEmpty()) address << country;
        const auto arrayKey = prefix + role + "_ARRAY";
        const int lineCount = definitions.value(arrayKey).value("elements").toInt();
        if (lineCount && address.size() > lineCount) {
            QMessageBox::information(this, "Address needs review", "This customer has more address lines than the document. Combine lines in the address box before saving.");
        }
        setText(arrayKey, address.join('\n'));
        setText(prefix + role + "_CONTACT", valueText(customer.values["CUS:CONTACT"]));
        setText(prefix + role + "_PHONE", valueText(customer.values["CUS:PH1_NUMBER"]));
        value.values[prefix + role + "_NUMBER"] = customer.values["CUS:NUMBER"];
    }
    void readEditors() {
        if(dgRows) {
            QJsonArray rows;
            for(int row=0;row<dgRows->rowCount();++row) {
                QJsonObject cells; bool any=false;
                for(int col=0;col<7;++col) { const auto text=dgRows->item(row,col)->text().trimmed(); any|=!text.isEmpty(); cells[dangerousGoodsColumns()[col].first]=text; }
                if(any) rows.append(cells);
            }
            value.values["DGD:LINES"]=rows;
        }
        if (vicsOrders) for (int col=0;col<5;++col) {
            QJsonArray rows; for (int row=0;row<8;++row) rows.append(vicsOrders->item(row,col)->text().trimmed());
            value.values[vicsOrderKeys()[col]]=rows;
        }
        if (airCargo) {
            const auto candidate = airValues();
            bool changed = !value.id;
            for (const auto &key : airCargoKeys()) changed |= candidate[key] != value.values[key];
            if (changed) {
                airCargoTotals(candidate);
                for (const auto &key : airCargoKeys()) value.values[key] = candidate[key];
            }
        }
        for (auto it = editors.begin(); it != editors.end(); ++it) {
            QString text;
            if (const auto *check = qobject_cast<QCheckBox *>(it.value())) text=check->isChecked() ? "1" : "0";
            if (const auto *combo = qobject_cast<QComboBox *>(it.value())) text=combo->currentData().toString();
            if (const auto *line = qobject_cast<QLineEdit *>(it.value())) text = line->text();
            if (const auto *multi = qobject_cast<QPlainTextEdit *>(it.value())) text = multi->toPlainText();
            if (text == editorText(fieldText(it.key(), value.values[it.key()]),value.values[it.key()].isArray())) continue;
            if (isDateField(it.key())) { value.values[it.key()] = dateStorage(text); continue; }
            const auto def = definitions.value(it.key());
            const int type = def.value("type").toInt(), count = def.value("elements").toInt(1);
            auto parts = count > 1 ? text.split('\n') : QStringList{text};
            if (parts.size() > count) throw std::runtime_error(QString("%1 allows %2 lines; no lines have been discarded.").arg(it.key()).arg(count).toStdString());
            while (parts.size() < count) parts << "";
            for (auto &part : parts) {
                if (type >= 1 && type <= 10 && type != 4) {
                    part = part.trimmed(); if (part.isEmpty()) part = "0";
                    const QString pattern = type <= 7 ? "^[0-9]+$" : "^[0-9]+(?:\\.[0-9]+)?$";
                    if (!QRegularExpression(pattern).match(part).hasMatch())
                        throw std::runtime_error("Package counts, weights and volumes must be nonnegative numbers.");
                }
            }
            if (count > 1) { QJsonArray array; for (const auto &part : parts) array.append(part); value.values[it.key()] = array; }
            else value.values[it.key()] = parts.first();
        }
        value=calculateEntry(value);
    }
};

bool blankCargoLine(const Record &line,const QString &prefix) {
    const QStringList numeric{"PCS","QTY","WEIGHT_KGS","WEIGHT_LBS","MEASURE_CBM","MEASURE_CFT"};
    for(auto it=line.values.begin();it!=line.values.end();++it) {
        if(it.key()==prefix+"REFERENCE_NUMBER" || it.key()==prefix+"ID_SEQUENCE" || it.key()==cargoLineNumberKey(line.source) || it.key()=="NATIVE:UNIT_RULES") continue;
        const auto text=valueText(it.value()).trimmed(); if(text.isEmpty()) continue;
        bool number=false; const auto amount=text.toDouble(&number);
        if(it.key().startsWith(prefix) && numeric.contains(it.key().mid(prefix.size())) && number && amount==0) continue;
        return false; // Preserve notes, marks, overrides and any unfamiliar data.
    }
    return true;
}

class CargoEditor final : public QWidget {
public:
    CargoEditor(Store &data,const Record &document,QWidget *parent) : QWidget(parent),store(data),parentDocument(document),prefix(cargoSource(document.source).second) {
        setObjectName("cargoEditor");
        auto *layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
        auto *note=new QLabel(parentDocument.id ? "Goods and document notes, in their saved order. Each row is saved separately." : "Save this document before adding or editing cargo."); note->setWordWrap(true); layout->addWidget(note);
        auto *filters=new QHBoxLayout; summary=new QLabel; summary->setObjectName("cargoSummary"); filters->addWidget(summary); filters->addStretch();
        showBlank=new QCheckBox("Show blank rows"); showBlank->setObjectName("showBlankCargoRows"); filters->addWidget(showBlank);
        add=new QPushButton("Add"); add->setObjectName("cargoAdd"); add->setEnabled(parentDocument.id); filters->addWidget(add);
        editLine=new QPushButton("Edit"); editLine->setObjectName("cargoEdit"); filters->addWidget(editLine); layout->addLayout(filters);
        table=new QTableWidget(0,6); table->setObjectName("cargoTable"); table->setHorizontalHeaderLabels({"Packages","Description / notes","Marks / numbers","Weight","Volume","Quantity"});
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setWordWrap(true); table->setAlternatingRowColors(true); table->setSortingEnabled(false); table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(const auto &[column,width] : QList<QPair<int,int>>{{0,85},{2,135},{3,105},{4,105},{5,100}}) table->setColumnWidth(column,width);
        layout->addWidget(table,1);
        connect(showBlank,&QCheckBox::toggled,this,[this] { try { refreshLines(); } catch(const std::exception &e) { error(this,e); } });
        connect(table,&QTableWidget::itemSelectionChanged,this,[this] { editLine->setEnabled(parentDocument.id && selected()); });
        connect(add,&QPushButton::clicked,this,[this] { edit(true); });
        connect(editLine,&QPushButton::clicked,this,[this] { edit(false); });
        connect(table,&QTableWidget::cellDoubleClicked,this,[this] { edit(false); });
        connect(table->horizontalHeader(),&QHeaderView::sectionResized,this,[this] { table->resizeRowsToContents(); });
        refreshLines();
    }
private:
    Store &store;
    Record parentDocument;
    QString prefix;
    QTableWidget *table;
    QLabel *summary;
    QCheckBox *showBlank;
    QPushButton *add,*editLine;
    qint64 selected() const {
        const auto *item=table->currentRow()<0 ? nullptr : table->item(table->currentRow(),0);
        return item ? item->data(Qt::UserRole).toLongLong() : 0;
    }
    void refreshLines(qint64 preferred=0) {
        const auto selectedId=preferred ? preferred : selected();
        const QSignalBlocker guard(table); table->setRowCount(0);
        int blankCount=0; bool hasQuantity=false;
        for(const auto &line : store.cargoLines(parentDocument)) {
            const bool blank=blankCargoLine(line,prefix); if(blank) ++blankCount;
            if(blank && !showBlank->isChecked()) continue;
            const auto text=[&](const QString &suffix) { return valueText(line.values[prefix+suffix]).trimmed(); };
            const auto measure=[&](const QString &suffix,const QString &unit) {
                const auto value=text(suffix); bool number=false; const auto amount=value.toDouble(&number);
                return value.isEmpty() || (number && amount==0) ? QString() : weightText(value)+unit;
            };
            const auto weight=QStringList{measure("WEIGHT_KGS"," kg"),measure("WEIGHT_LBS"," lb")}.filter(QRegularExpression("\\S")).join('\n');
            const auto volume=QStringList{measure("MEASURE_CBM"," m³"),measure("MEASURE_CFT"," ft³")}.filter(QRegularExpression("\\S")).join('\n');
            const auto quantity=QStringList{measure("QTY",{}),text("UNIT")}.filter(QRegularExpression("\\S")).join(' '); hasQuantity|=!quantity.isEmpty();
            const int row=table->rowCount(); table->insertRow(row);
            QString description=text("DESCRIPTION");
            if(blank) description="(Blank row)";
            else if(description.isEmpty() && text("PCS").isEmpty() && text("MARKS_NUMBERS").isEmpty() && weight.isEmpty() && volume.isEmpty() && quantity.isEmpty()) description="(Additional saved details)";
            const QStringList cells{blank ? QString() : weightText(text("PCS")),description,text("MARKS_NUMBERS"),weight,volume,quantity};
            for(int col=0;col<cells.size();++col) { auto *item=new QTableWidgetItem(cells[col]); item->setToolTip(cells[col]); item->setData(Qt::UserRole,line.id); item->setTextAlignment(Qt::AlignLeft|Qt::AlignTop); table->setItem(row,col,item); }
            if(selectedId && line.id==selectedId) table->selectRow(row);
        }
        table->setColumnHidden(5,!hasQuantity); table->resizeRowsToContents();
        summary->setText(QString("%1 document lines").arg(table->rowCount())+(blankCount ? (showBlank->isChecked() ? QString(" · %1 blank").arg(blankCount) : QString(" · %1 blank rows hidden").arg(blankCount)) : QString()));
        showBlank->setVisible(blankCount>0); editLine->setEnabled(parentDocument.id && selected());
    }
    void edit(bool create) {
        try {
            if(!parentDocument.id || (!create && !selected())) return;
            RecordEditor editor(store,create ? store.blankCargo(parentDocument) : store.record(selected()),this);
            if(editor.exec()==QDialog::Accepted) {
                if(blankCargoLine(editor.value,prefix)) { const QSignalBlocker block(showBlank); showBlank->setChecked(true); }
                refreshLines(editor.value.id);
            }
        } catch(const std::exception &e) { error(this,e); }
    }
};

QWidget *createCargoEditor(Store &store,const Record &parentRecord,QWidget *parent) {
    return new CargoEditor(store,parentRecord,parent);
}

void showCargoDialog(Store &store,const Record &parentRecord,QWidget *parent) {
    QDialog dialog(parent); dialog.setObjectName("cargoLinesDialog"); dialog.setWindowTitle("Cargo · "+recordReference(parentRecord)); dialog.resize(1000,550);
    auto *layout=new QVBoxLayout(&dialog); layout->addWidget(createCargoEditor(store,parentRecord,&dialog),1);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
}

void showRecordHistory(Store &store,qint64 recordId,QWidget *parent) {
    const auto current = store.record(recordId); const auto versions = store.history(current.id);
    QDialog dialog(parent); dialog.setWindowTitle("Record history · " + recordReference(current)); dialog.resize(760,600);
    auto *layout = new QVBoxLayout(&dialog); auto *list = new QComboBox; auto *text = new QPlainTextEdit; text->setReadOnly(true);
    layout->addWidget(new QLabel("Prior saved versions compared with the current record.")); layout->addWidget(list); layout->addWidget(text);
    for (const auto &version : versions) list->addItem("Version " + QString::number(version.version));
    const auto show = [&](int index) {
        QStringList lines;
        if (index < 0) { text->setPlainText("No previous versions. History starts with the first native edit."); return; }
        const auto &version = versions[index];
        if(version.values["NATIVE:DELETED_AT"]!=current.values["NATIVE:DELETED_AT"])
            lines<<"Availability\nThen: "+(version.values["NATIVE:DELETED_AT"].toString().isEmpty() ? QString("Active") : "Deleted "+version.values["NATIVE:DELETED_AT"].toString())+"\nCurrent: "+(current.values["NATIVE:DELETED_AT"].toString().isEmpty() ? QString("Active") : "Deleted "+current.values["NATIVE:DELETED_AT"].toString());
        for (const auto &[key,label] : editableFields(version.source)) {
            const auto old = fieldText(key,version.values[key]);
            const auto now = fieldText(key,current.values[key]);
            if (old != now) lines << label + "\nThen: " + old + "\nCurrent: " + now;
        }
        if(current.source==DangerousGoodsSource && version.values["DGD:LINES"]!=current.values["DGD:LINES"])
            lines<<"Dangerous goods rows\nThen:\n"+dangerousGoodsText(version.values)+"\nCurrent:\n"+dangerousGoodsText(current.values);
        text->setPlainText(lines.isEmpty() ? "No shipping field differences from the current version." : lines.join("\n\n"));
    };
    QObject::connect(list,&QComboBox::currentIndexChanged,&dialog,show); show(list->currentIndex());
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
}

class AddressBookDialog final : public QDialog {
public:
    AddressBookDialog(Store &store,int initialTab,QWidget *parent) : QDialog(parent), store(store) {
        setObjectName("addressBook"); setWindowTitle("Address book"); resize(1000,650); setMinimumSize(680,420);
        auto *layout=new QVBoxLayout(this); layout->setSpacing(10);
        tabs=new QTabBar; tabs->setObjectName("addressBookTabs"); tabs->addTab("Customers"); tabs->addTab("Contacts"); tabs->setExpanding(false); tabs->setCurrentIndex(initialTab); layout->addWidget(tabs);
        auto *actions=new QHBoxLayout; search=new QLineEdit; search->setObjectName("addressBookSearch"); search->setClearButtonEnabled(true); actions->addWidget(search,1);
        add=new QPushButton("Add"); add->setObjectName("addressBookAdd"); edit=new QPushButton("Edit"); edit->setObjectName("addressBookEdit"); history=new QPushButton("History");
        actions->addWidget(add); actions->addWidget(edit); actions->addWidget(history); layout->addLayout(actions);
        table=new QTableView; table->setObjectName("addressBookTable"); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setSortingEnabled(true); table->setAlternatingRowColors(true); table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(32); table->horizontalHeader()->setStretchLastSection(true);
        model=new QStandardItemModel(this); proxy=new QSortFilterProxyModel(this); proxy->setSourceModel(model); proxy->setFilterCaseSensitivity(Qt::CaseInsensitive); proxy->setFilterKeyColumn(-1); table->setModel(proxy); layout->addWidget(table,1);
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
        connect(search,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
        connect(tabs,&QTabBar::currentChanged,this,[this] { guarded([this] { search->clear(); refresh(); }); });
        connect(table->selectionModel(),&QItemSelectionModel::selectionChanged,this,[this] { updateActions(); });
        connect(add,&QPushButton::clicked,this,[this] { editRecord(true); });
        connect(edit,&QPushButton::clicked,this,[this] { editRecord(false); });
        connect(table,&QTableView::doubleClicked,this,[this] { editRecord(false); });
        connect(history,&QPushButton::clicked,this,[this] { if(selected()) guarded([this] { showRecordHistory(this->store,selected(),this); }); });
        refresh();
    }
private:
    Store &store;
    QTabBar *tabs;
    QLineEdit *search;
    QTableView *table;
    QStandardItemModel *model;
    QSortFilterProxyModel *proxy;
    QPushButton *add,*edit,*history;
    QString source() const { return tabs->currentIndex()==0 ? "CUSTOMER.TPS" : "CONTACTS.TPS"; }
    qint64 selected() const { return table->selectionModel()->hasSelection() ? proxy->index(table->currentIndex().row(),0).data(Qt::UserRole).toLongLong() : 0; }
    void guarded(const std::function<void()> &operation) { try { operation(); } catch(const std::exception &e) { error(this,e); } }
    void updateActions() { edit->setEnabled(selected()); history->setEnabled(selected()); }
    void refresh(qint64 id=0) {
        model->clear(); model->setHorizontalHeaderLabels({"Name","Contact / location","Phone","Email"});
        search->setPlaceholderText(tabs->currentIndex()==0 ? "Search customers…" : "Search contacts…");
        const bool customer=tabs->currentIndex()==0; const QString prefix=customer ? "CUS:" : "CTA:";
        add->setEnabled(store.sources().contains(source()));
        for(const auto &record : store.records({source()})) {
            auto *name=new QStandardItem(recordTitle(record)); name->setData(record.id,Qt::UserRole);
            model->appendRow({name,new QStandardItem(valueText(record.values[prefix+(customer ? "CONTACT" : "LOCATION")])),
                new QStandardItem(valueText(record.values[prefix+(customer ? "PH1_NUMBER" : "PHONE")])),new QStandardItem(valueText(record.values[prefix+"EMAIL"]))});
        }
        table->setColumnWidth(0,280); table->setColumnWidth(1,220); table->setColumnWidth(2,170); table->sortByColumn(0,Qt::AscendingOrder);
        for(int row=0;row<proxy->rowCount();++row) if(proxy->index(row,0).data(Qt::UserRole).toLongLong()==id) { table->selectRow(row); table->scrollTo(proxy->index(row,0)); break; }
        updateActions();
    }
    void editRecord(bool create) {
        if(!create && !selected()) return;
        guarded([&] {
            RecordEditor editor(store,create ? store.blankRecord(source()) : store.record(selected()),this);
            if(editor.exec()==QDialog::Accepted) { search->clear(); refresh(editor.value.id); }
        });
    }
};

class Window final : public QMainWindow {
public:
    explicit Window(const QString &path) {
        setWindowTitle("Shadow Logis"); resize(1280, 820);
        restoreGeometry(preferences.value("window/geometry").toByteArray());
        loadPrintPreferences(awbPrintProfile,oceanPrintProfile);
        shipmentColumnOrder=preferences.value("shipments/columnOrder",ShipmentColumnKeys).toStringList();
        auto *version=new QLabel(QStringLiteral("v" SHADOW_LOGIS_VERSION)); version->setObjectName("applicationVersion");
        auto versionFont=version->font(); versionFont.setPointSizeF(std::max(8.0,versionFont.pointSizeF()*.85)); version->setFont(versionFont);
        version->setToolTip(QStringLiteral("Shadow Logis " SHADOW_LOGIS_VERSION));
        version->setTextInteractionFlags(Qt::TextSelectableByMouse); statusBar()->addPermanentWidget(version);
        auto *bar = menuBar()->addMenu("File");
        auto *workspaceMenu=menuBar()->addMenu("Workspace");
        workspaceMenu->addAction("Bookings / shipments",this,[this] { chooseSection(2); })->setObjectName("showShipments");
        resetColumnsAction=workspaceMenu->addAction("Reset booking column order",this,[this] {
            shipmentColumnOrder=ShipmentColumnKeys; preferences.setValue("shipments/columnOrder",shipmentColumnOrder); applyColumnOrder();
        }); resetColumnsAction->setObjectName("resetShipmentColumnOrder");
        workspaceMenu->addAction("Deleted shipments…",this,[this] { showDeletedShipments(); })->setObjectName("showDeletedShipments");
        workspaceMenu->addAction("Deleted documents…",this,[this] { showDeletedDocuments(); })->setObjectName("showDeletedDocuments");
        auto *emptyRows=workspaceMenu->addAction("Show empty imported bookings"); emptyRows->setCheckable(true);
        includeEmptyBookings=preferences.value("shipments/includeEmpty",false).toBool(); emptyRows->setChecked(includeEmptyBookings);
        connect(emptyRows,&QAction::toggled,this,[this](bool checked) { includeEmptyBookings=checked; preferences.setValue("shipments/includeEmpty",checked); guarded([this] { refresh(); }); });
        auto *addressBook=menuBar()->addMenu("Address book");
        addressBook->addAction("Customers…",this,[this] { showAddressBook(0); })->setObjectName("showCustomers");
        addressBook->addAction("Contacts…",this,[this] { showAddressBook(1); })->setObjectName("showContacts");
        addressBook->addSeparator();
        addressBook->addAction("Company profile…",this,[this] { if(store) guarded([&] { CompanyProfileDialog dialog(*store,this); dialog.exec(); }); })->setObjectName("showCompanyProfile");
        auto *documentsMenu=menuBar()->addMenu("Documents");
        auto *referenceMenu=menuBar()->addMenu("Reference");
        referenceMenu->addAction("Airline codes…",this,[this] { guarded([this] {
            if(auto *existing=findChild<QDialog *>("airlineReferenceWindow")) { existing->show(); existing->raise(); existing->activateWindow(); return; }
            auto *dialog=new AirlineLookupDialog(false,{},this); dialog->setObjectName("airlineReferenceWindow"); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
        }); })->setObjectName("showAirlineCodes");
        const auto showReference=[this](const ReferenceTable &table) {
            const auto name="referenceWindow_"+table.title;
            if(auto *existing=findChild<QDialog *>(name)) { existing->show(); existing->raise(); existing->activateWindow(); return; }
            auto *dialog=new ReferenceDialog(table,false,this); dialog->setObjectName(name); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show();
        };
        referenceMenu->addAction("Airport codes…",this,[this,showReference] { guarded([&] {
            showReference(store ? airportReference(*store) : ReferenceTable{"Airport codes","Open a migrated database to browse its airport directory.",{},false});
        }); })->setObjectName("showAirportCodes");
        referenceMenu->addAction("Countries / territories…",this,[showReference] { showReference(countryReference()); })->setObjectName("showCountryCodes");
        referenceMenu->addAction("Currency codes…",this,[showReference] { showReference(currencyReference()); })->setObjectName("showCurrencyCodes");
        referenceMenu->addAction("Incoterms 2020…",this,[showReference] { showReference(incotermReference()); })->setObjectName("showIncoterms");
        vicsAction=documentsMenu->addAction("Preview truck bill of lading (VICS)…",this,[this] { if(source()=="OBL.TPS") previewDocument({},true); });
        vicsAction->setObjectName("previewVicsBol");
        documentsMenu->addAction("Ocean BoL print alignment…",this,[this] {
            if(!store || !selected() || source()!="OBL.TPS") return;
            if(choosePrintRegistration(this,false,oceanPrintProfile)) previewDocument();
        });
        auto *archive=menuBar()->addMenu("Archive");
        archive->addAction("All shipping documents…",this,[this] { chooseSection(3); })->setObjectName("showAllDocuments");
        archive->addAction("Original PDFs…",this,[this] { chooseSection(4); })->setObjectName("showOriginalPdfs");
        bar->addAction("New workspace…",this,[this] { newWorkspace(); })->setObjectName("newWorkspace");
        auto *openWorkspaceAction=bar->addAction("Open workspace…", this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, "Open shipping workspace", preferences.value("workspace/lastPath").toString(), "SQLite databases (*.sqlite *.db);;All files (*)");
            if (!path.isEmpty()) guarded([&] { open(path); });
        });
        openWorkspaceAction->setObjectName("openWorkspace");
        bar->addAction("Import legacy TPS installation…", this, [this] { migrateFolder(); })->setObjectName("importWorkspace");
        bar->addAction("Back up database…", this, [this] {
            if (!store) return;
            const auto path = QFileDialog::getSaveFileName(this,"Create database backup","shadow-logis-backup-" + QDate::currentDate().toString(Qt::ISODate) + ".sqlite","SQLite (*.sqlite)");
            if (path.isEmpty()) return;
            guarded([&] {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                try { store->backup(path); } catch (...) { QApplication::restoreOverrideCursor(); throw; }
                QApplication::restoreOverrideCursor(); statusBar()->showMessage("Verified backup saved to " + path,10000);
            });
        });
        bar->addSeparator(); bar->addAction("Migration report", this, [this] { if (store) showReport(); });
        auto *help=menuBar()->addMenu("Help");
        help->addAction("Application license…",this,[this] {
            QDialog dialog(this); dialog.setWindowTitle("Shadow Logis · AGPL-3.0-only"); dialog.resize(760,620);
            auto *layout=new QVBoxLayout(&dialog); auto *source=new QLabel("Shadow Logis is licensed under GNU AGPL version 3 only. <a href=\"https://github.com/Milhound/shadow-logis\">Corresponding source</a>. Third-party components retain their own licenses."); source->setWordWrap(true); source->setOpenExternalLinks(true); layout->addWidget(source);
            auto *text=new QPlainTextEdit; text->setReadOnly(true); QFile license(":/shadow-logis/LICENSE"); if(license.open(QIODevice::ReadOnly)) text->setPlainText(QString::fromUtf8(license.readAll())); layout->addWidget(text);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
        });
        help->addAction("Inter font license…",this,[this] {
            QDialog dialog(this); dialog.setWindowTitle("Inter font · SIL Open Font License"); dialog.resize(700,580);
            auto *layout=new QVBoxLayout(&dialog); auto *text=new QTextBrowser;
            QFile license(":/shadow-logis/fonts/OFL.txt");
            if(license.open(QIODevice::ReadOnly)) text->setPlainText(QString::fromUtf8(license.readAll()));
            layout->addWidget(text); auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
        });
        auto *root = new QWidget; auto *content = new QVBoxLayout(root); content->setContentsMargins(16,12,16,12); content->setSpacing(10); setCentralWidget(root);
        workspaceControls=new QWidget; auto *filters=new QHBoxLayout(workspaceControls); filters->setContentsMargins(0,0,0,0); filters->setSpacing(8);
        backButton=new QPushButton("← Shipments"); filters->addWidget(backButton); connect(backButton,&QPushButton::clicked,this,[this] { chooseSection(2); });
        title=new QLabel; auto titleFont=title->font(); titleFont.setBold(true); title->setFont(titleFont); filters->addWidget(title);
        search=new QLineEdit; search->setObjectName("shipmentSearch"); search->setClearButtonEnabled(true); filters->addWidget(search,1);
        kind=new QComboBox; kind->setObjectName("archiveDocumentType"); for(const auto &[source,label] : DocumentTypes) kind->addItem(label,source);
        filters->addWidget(kind);
        addShipmentButton=new QPushButton("Add"); addShipmentButton->setObjectName("newShipment"); addShipmentButton->setProperty("primary",true); filters->addWidget(addShipmentButton);
        editShipmentButton=new QPushButton("Edit"); editShipmentButton->setObjectName("editShipment"); filters->addWidget(editShipmentButton);
        deleteShipmentButton=new QPushButton("Delete"); deleteShipmentButton->setObjectName("deleteShipment"); filters->addWidget(deleteShipmentButton);
        connect(addShipmentButton,&QPushButton::clicked,this,[this] { editShipment(true); });
        connect(editShipmentButton,&QPushButton::clicked,this,[this] { editShipment(false); });
        connect(deleteShipmentButton,&QPushButton::clicked,this,[this] { deleteShipment(); });
        content->addWidget(workspaceControls);
        welcome=new QWidget; welcome->setObjectName("workspaceWelcome"); auto *welcomeLayout=new QVBoxLayout(welcome);
        auto *welcomeTitle=new QLabel("Welcome to Shadow Logis"); auto welcomeFont=welcomeTitle->font(); welcomeFont.setPointSize(18); welcomeFont.setBold(true); welcomeTitle->setFont(welcomeFont);
        welcomeLayout->addStretch(); welcomeLayout->addWidget(welcomeTitle);
        auto *welcomeText=new QLabel("Start a new shipping workspace, open an existing one, or import your legacy TPS installation. Import finds the data folder and lets you choose what to bring across."); welcomeText->setWordWrap(true); welcomeLayout->addWidget(welcomeText);
        auto *welcomeActions=new QHBoxLayout; welcomeLayout->addLayout(welcomeActions);
        for(const auto &[label,action] : QList<QPair<QString,QString>>{{"New workspace","newWorkspace"},{"Open workspace","openWorkspace"},{"Import legacy TPS installation","importWorkspace"}}) {
            auto *button=new QPushButton(label); button->setObjectName("welcome_"+action); welcomeActions->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,action] { findChild<QAction *>(action)->trigger(); });
        }
        welcomeActions->addStretch(); welcomeLayout->addStretch(); content->addWidget(welcome,1);
        auto *split = new QSplitter(Qt::Vertical); content->addWidget(split, 1);
        table = new QTableView; table->setObjectName("shipmentsTable"); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection); table->setSortingEnabled(true); table->setAlternatingRowColors(true); table->setShowGrid(false);
        table->verticalHeader()->hide(); table->horizontalHeader()->setStretchLastSection(true); table->verticalHeader()->setDefaultSectionSize(34);
        model = new QStandardItemModel(this); proxy = new QSortFilterProxyModel(this); proxy->setSourceModel(model);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive); proxy->setFilterKeyColumn(-1); table->setModel(proxy); split->addWidget(table);
        table->horizontalHeader()->setFirstSectionMovable(true);
        connect(table->horizontalHeader(),&QHeaderView::sectionMoved,this,[this] {
            auto *header=table->horizontalHeader();
            if(section!=2 || refreshing || header->count()!=ShipmentColumnKeys.size()) return;
            shipmentColumnOrder.clear();
            for(int visual=0;visual<header->count();++visual) shipmentColumnOrder<<ShipmentColumnKeys[header->logicalIndex(visual)];
            preferences.setValue("shipments/columnOrder",shipmentColumnOrder);
        });
        shipmentPanel=new QWidget; shipmentPanel->setObjectName("shipmentPanel"); auto *panelLayout=new QVBoxLayout(shipmentPanel); panelLayout->setContentsMargins(14,14,14,10); panelLayout->setSpacing(10); split->addWidget(shipmentPanel);
        auto *panelHeading=new QHBoxLayout; panelLayout->addLayout(panelHeading);
        shipmentHeading=new QLabel("Select a shipment"); auto hfont=shipmentHeading->font(); hfont.setPointSize(15); hfont.setBold(true); shipmentHeading->setFont(hfont); panelHeading->addWidget(shipmentHeading,1);
        closeShipmentButton=new QToolButton; closeShipmentButton->setObjectName("closeShipmentDetails"); closeShipmentButton->setText("×");
        closeShipmentButton->setToolTip("Close details (Esc)"); closeShipmentButton->setAccessibleName("Close shipment details");
        closeShipmentButton->setAutoRaise(true); closeShipmentButton->setFixedSize(28,28); panelHeading->addWidget(closeShipmentButton,0,Qt::AlignTop);
        connect(closeShipmentButton,&QToolButton::clicked,this,[this] { closeShipmentDetails(); });
        closeShipmentShortcut=new QShortcut(QKeySequence(Qt::Key_Escape),root);
        closeShipmentShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(closeShipmentShortcut,&QShortcut::activated,this,[this] { closeShipmentDetails(); });
        documentTabs=new QTabBar; documentTabs->setObjectName("shipmentDocumentTabs"); documentTabs->setExpanding(false); documentTabs->setDrawBase(false); documentTabs->addTab("Shipment details");
        for(const auto &[source,label] : DocumentTypes) {
            const int tab=documentTabs->addTab(label);
            // Retain the document type and archived records; hide only this tab.
            documentTabs->setTabVisible(tab,source!="PLIST.TPS");
        }
        panelLayout->addWidget(documentTabs);
        documentPicker=new QComboBox; documentPicker->setObjectName("shipmentDocumentVersion"); panelLayout->addWidget(documentPicker);
        auto *actions = new QHBoxLayout;
        newButton = new QPushButton("New…"); editButton = new QPushButton("Edit"); exportButton = new QPushButton("Preview");
        newButton->setObjectName("createDocumentButton"); editButton->setObjectName("editRecordButton"); exportButton->setObjectName("previewDocumentButton");
        cargoButton = new QPushButton("Cargo"); cargoButton->setToolTip("Edit this document's packages, goods descriptions, weights and measurements."); historyButton = new QPushButton("History");
        deleteDocumentButton=new QPushButton("Delete"); deleteDocumentButton->setObjectName("deleteDocumentButton");
        preprintedButton=new QPushButton("Dot Matrix"); preprintedButton->setObjectName("preprintedAwbButton");
        for (auto *button : {newButton, editButton, deleteDocumentButton, cargoButton, exportButton, preprintedButton, historyButton}) actions->addWidget(button);
        actions->addStretch(); panelLayout->addLayout(actions);
        detail = new QTextBrowser; detail->setObjectName("shipmentOverview"); detail->setOpenLinks(false); panelLayout->addWidget(detail,1); split->setSizes({280,460});
        connect(deleteDocumentButton,&QPushButton::clicked,this,[this] {
            if(!store || !selected()) return;
            guarded([&] { if(deleteShippingDocument(*store,store->record(selected()),this)) { if(documentMode()) { loadShipmentDocuments(); updateDetail(); } else refresh(); } });
        });
        connect(documentTabs,&QTabBar::currentChanged,this,[this] { guarded([this] { loadShipmentDocuments(); updateDetail(); }); });
        connect(documentPicker,&QComboBox::currentIndexChanged,this,[this] { guarded([this] { updateDetail(); }); });
        connect(search, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);
        connect(kind, &QComboBox::currentIndexChanged, this, [this] { guarded([this] { refresh(); }); });
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { guarded([this] { loadShipmentDocuments(); updateDetail(); }); });
        connect(table, &QTableView::doubleClicked, this, [this] { if(section==2) { editShipment(false); } else if (section != 4) edit(false); else previewDocument(); });
        connect(newButton, &QPushButton::clicked, this, [this] { if (documentMode()) makeDocument(); else edit(true); });
        connect(editButton, &QPushButton::clicked, this, [this] { edit(false); });
        connect(exportButton, &QPushButton::clicked, this, [this] { previewDocument(); });
        connect(preprintedButton,&QPushButton::clicked,this,[this] { previewPreprinted(); });
        connect(cargoButton, &QPushButton::clicked, this, [this] { showCargo(); });
        connect(historyButton, &QPushButton::clicked, this, [this] { showHistory(); });
        chooseSection(2);
        if (!path.isEmpty()) open(path); else refresh();
        const auto sortColumn=preferences.value("shipments/sortColumn",0).toInt();
        const auto sortOrder=preferences.value("shipments/sortOrder",int(Qt::AscendingOrder)).toInt();
        if(sortColumn>=0 && sortColumn<4 && (sortOrder==0 || sortOrder==1)) table->sortByColumn(sortColumn,static_cast<Qt::SortOrder>(sortOrder));
        connect(table->horizontalHeader(),&QHeaderView::sortIndicatorChanged,this,[this](int column,Qt::SortOrder order) {
            if(section==2 && !refreshing) { preferences.setValue("shipments/sortColumn",column); preferences.setValue("shipments/sortOrder",int(order)); }
        });
    }
    void smoke(const QString &imagePath) {
        if (!store || model->rowCount() == 0) throw std::runtime_error("GUI smoke check needs a database with shipments");
        table->selectRow(0); updateDetail();
        if (detail->toPlainText().isEmpty()) throw std::runtime_error("Shipment detail did not load");
        if (!grab().save(imagePath)) throw std::runtime_error("Could not save GUI screenshot");
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        preferences.setValue("window/geometry",saveGeometry());
        QMainWindow::closeEvent(event);
    }
private:
    QWidget *welcome, *workspaceControls;
    std::unique_ptr<Store> store;
    Preferences preferences;
    QStringList shipmentColumnOrder;
    bool refreshing=false;
    int section = 2;
    bool includeEmptyBookings=false;
    QPushButton *backButton;
    QWidget *shipmentPanel;
    QToolButton *closeShipmentButton;
    QShortcut *closeShipmentShortcut;
    QTabBar *documentTabs;
    QComboBox *documentPicker;
    QLabel *shipmentHeading;
    QList<LinkedDocument> shipmentDocuments;
    bool documentMode() const { return section==2 && documentTabs->currentIndex()>0; }
    qint64 bookingSelected() const {
        const auto index=table->currentIndex(); return index.isValid() && table->selectionModel()->hasSelection() ? proxy->index(index.row(),0).data(Qt::UserRole).toLongLong() : 0;
    }
    void closeShipmentDetails() {
        if(section!=2 || !bookingSelected()) return;
        table->setFocus(Qt::OtherFocusReason);
        table->selectionModel()->clear();
    }
    void chooseSection(int next) {
        section=next;
        if(next==2) documentTabs->setCurrentIndex(0);
        const QStringList names{"Customers","Contacts","Bookings / shipments","Document archive","Original PDFs"};
        title->setText(names[next]); title->setVisible(next!=2); backButton->setVisible(next!=2); documentTabs->setVisible(next==2); kind->setVisible(next==3);
        search->setPlaceholderText(next==2 ? "Search shipments by reference, shipper or consignee…" : "Search…");
        search->clear(); guarded([this] { refresh(); });
    }
    void loadShipmentDocuments(qint64 selectId=0) {
        QSignalBlocker blocker(documentPicker); documentPicker->clear(); shipmentDocuments.clear();
        documentPicker->setVisible(documentMode());
        for(int i=0;i<DocumentTypes.size();++i) documentTabs->setTabText(i+1,DocumentTypes[i].second);
        if(section!=2 || !store || !bookingSelected()) { if(section==2) shipmentHeading->setText("Select a shipment"); return; }
        const auto booking=store->record(bookingSelected()); shipmentHeading->setText(recordReference(booking)+"  ·  "+recordTitle(booking));
        shipmentDocuments=store->linkedDocuments(booking.id);
        // Paused workflows keep their records but do not inflate visible counts.
        shipmentDocuments.removeIf([](const LinkedDocument &link) { for(const auto &type : DocumentTypes) if(type.first==link.record.source) return false; return true; });
        for(int i=0;i<DocumentTypes.size();++i) {
            int count=0; for(const auto &link : shipmentDocuments) if(link.record.source==DocumentTypes[i].first) ++count;
            documentTabs->setTabText(i+1,DocumentTypes[i].second+(count ? " ("+QString::number(count)+")" : ""));
        }
        if(!documentMode()) return;
        for(const auto &link : shipmentDocuments) if(link.record.source==source()) {
            const auto &r=link.record; const auto prefix=editableFields(r.source).first().first.section(':',0,0)+':';
            QString number=valueText(r.values[prefix+(r.source=="AWB.TPS" || r.source==DangerousGoodsSource ? "AWB_NUMBER" : r.source=="DODR.TPS" ? "DO_NUMBER" : r.source=="SIGHT.TPS" ? "SD_NUMBER" : "BL_NUMBER")]);
            QString label=number.isEmpty() ? recordReference(r) : number;
            label+=" · Sequence "+valueText(r.values[prefix+"ID_SEQUENCE"]);
            if(link.match==DocumentMatch::AmbiguousReference) label+=" · Shared reference: verify shipment";
            documentPicker->addItem(label,r.id);
        }
        if(selectId && documentPicker->findData(selectId)>=0) documentPicker->setCurrentIndex(documentPicker->findData(selectId));
        if(!documentPicker->count()) documentPicker->addItem("No saved document yet",QVariant::fromValue<qint64>(0));
    }
    QStandardItemModel *model;
    QSortFilterProxyModel *proxy;
    QTableView *table;
    QTextBrowser *detail;
    QLineEdit *search;
    QComboBox *kind;
    QLabel *title;
    QPushButton *addShipmentButton, *editShipmentButton, *deleteShipmentButton;
    QPushButton *newButton, *editButton, *exportButton, *cargoButton, *historyButton;
    QPushButton *preprintedButton, *deleteDocumentButton;
    QAction *vicsAction, *resetColumnsAction;
    void applyColumnOrder() {
        auto *header=table->horizontalHeader(); const QSignalBlocker block(header);
        header->setSectionsMovable(section==2);
        header->setToolTip(section==2 ? "Drag column headings to rearrange them. Restore the default order from Workspace → Reset booking column order." : QString());
        resetColumnsAction->setEnabled(section==2);
        QStringList order;
        if(section==2) {
            for(const auto &key : shipmentColumnOrder) if(ShipmentColumnKeys.contains(key) && !order.contains(key)) order<<key;
            for(const auto &key : ShipmentColumnKeys) if(!order.contains(key)) order<<key;
            shipmentColumnOrder=order;
        }
        for(int visual=0;visual<header->count();++visual) {
            const int logical=section==2 ? ShipmentColumnKeys.indexOf(order[visual]) : visual;
            header->moveSection(header->visualIndex(logical),visual);
        }
    }
    void guarded(const std::function<void()> &operation) { try { operation(); } catch (const std::exception &e) { error(this, e); } }
    QString source() const {
        switch (section) { case 0: return "CUSTOMER.TPS"; case 1: return "CONTACTS.TPS"; case 2: return documentMode() ? DocumentTypes[documentTabs->currentIndex()-1].first : QString("BOOKING.TPS");
        case 3: return kind->currentData().toString(); default: return {}; }
    }
    qint64 selected() const {
        if(documentMode()) return documentPicker->currentData().toLongLong();
        if(section==2) return bookingSelected();
        const auto index = table->currentIndex();
        return index.isValid() ? proxy->index(index.row(), 0).data(Qt::UserRole).toLongLong() : 0;
    }
    void open(const QString &path) {
        auto next = std::make_unique<Store>(path); store = std::move(next);
        rememberWorkspace(path); setWindowTitle("Shadow Logis · "+QFileInfo(path).completeBaseName()); refresh();
        statusBar()->showMessage("Shipping workspace ready",4000);
    }
    void refresh(qint64 selectId = 0) {
        const QScopedValueRollback<bool> refreshingGuard(refreshing,true);
        model->clear(); detail->clear();
        const QString listedSource=section==2 ? "BOOKING.TPS" : source();
        documentTabs->setVisible(section==2); documentPicker->setVisible(documentMode());
        shipmentHeading->setText(section==2 ? "Select a shipment" : title->text());
        auto columns = section>=2 ? QStringList{"Reference", "Shipper", "Consignee"} : QStringList{"Number", "Name", "Contact details"};
        if (section == 2) columns << "Booking number"; // BOO:STATUS is retained only in storage.
        if (section == 3) columns << "Document number";
        model->setHorizontalHeaderLabels(section == 4 ? QStringList{"Original document"} : columns);
        applyColumnOrder();
        if (!store) { updateDetail(); return; }
        if (section == 4) {
            for (const auto &[id, path] : store->documents()) {
                auto *item = new QStandardItem(path); item->setData(id, Qt::UserRole); model->appendRow(item);
            }
        } else {
            for (const auto &r : store->records({listedSource})) {
                QString additional;
                if (section == 0) additional = valueText(r.values["CUS:CONTACT"]) + "  " + valueText(r.values["CUS:EMAIL"]) + "  " + valueText(r.values["CUS:PH1_NUMBER"]);
                else if (section == 1) additional = valueText(r.values["CTA:EMAIL"]) + "  " + valueText(r.values["CTA:PHONE"]);
                else for (auto it = r.values.begin(); it != r.values.end(); ++it) if (it.key().endsWith(":CONSIGNEE_ARRAY")) additional = valueText(it.value()).section('\n',0,0);
                if(section==2 && !includeEmptyBookings && recordReference(r).trimmed().isEmpty() && recordTitle(r).trimmed().isEmpty() && additional.trimmed().isEmpty() && valueText(r.values["BOO:BOOKING_NUMBER"]).trimmed().isEmpty()) continue;
                auto *first = new QStandardItem(recordReference(r)); first->setData(r.id, Qt::UserRole);
                QList<QStandardItem *> items{first, new QStandardItem(recordTitle(r)), new QStandardItem(additional)};
                if (section == 2) items << new QStandardItem(valueText(r.values["BOO:BOOKING_NUMBER"]));
                if (section == 3) {
                    QStringList numbers;
                    for (const auto &[key,label] : editableFields(r.source))
                        if (key.endsWith(":AWB_NUMBER") || key.endsWith(":MAWB_NUMBER") || key.endsWith(":BL_NUMBER") || key.endsWith(":DOCUMENT_NUMBER") || key.endsWith(":DO_NUMBER")) numbers << valueText(r.values[key]);
                    items << new QStandardItem(numbers.join("  "));
                }
                model->appendRow(items);
            }
        }
        table->resizeColumnsToContents(); table->setColumnWidth(0, section == 4 ? 500 : 160);
        table->setColumnWidth(1,300); if(section!=4) table->setColumnWidth(2,280);
        for (int row = 0; row < proxy->rowCount(); ++row)
            if (proxy->index(row,0).data(Qt::UserRole).toLongLong() == selectId) { table->selectRow(row); break; }
        loadShipmentDocuments(); updateDetail();
    }
    void updateDetail() {
        welcome->setVisible(!store);
        workspaceControls->setVisible(bool(store));
        table->parentWidget()->setVisible(bool(store)); search->setEnabled(bool(store));
        const auto id=selected(); const bool enabled=store && id;
        shipmentPanel->setVisible(section!=2 || (store && bookingSelected()));
        closeShipmentButton->setVisible(section==2);
        closeShipmentShortcut->setEnabled(section==2 && store && bookingSelected());
        const bool available=store && store->sources().contains(source());
        newButton->setVisible(section!=4 && !(section==2 && !documentMode()));
        newButton->setText(documentMode() ? (id ? "Create another…" : "Create from shipment…") : section==1 ? "Add contact…" : section==0 ? "Add customer…" : "New document…");
        for(auto *button : {addShipmentButton,editShipmentButton,deleteShipmentButton}) button->setVisible(section==2);
        addShipmentButton->setEnabled(store && store->sources().contains("BOOKING.TPS"));
        editShipmentButton->setEnabled(store && bookingSelected()); deleteShipmentButton->setEnabled(store && bookingSelected());
        newButton->setEnabled(available && (!documentMode() || bookingSelected()));
        editButton->setVisible(section!=4 && !(section==2 && !documentMode())); editButton->setText("Edit");
        if(section<2) editButton->setText(section==1 ? "Edit contact…" : "Edit customer…");
        editButton->setEnabled(enabled && section!=4);
        historyButton->setVisible(section!=4); historyButton->setEnabled(enabled && section!=4);
        cargoButton->setVisible(!cargoSource(source()).first.isEmpty()); cargoButton->setEnabled(enabled);
        exportButton->setVisible(section>=2); exportButton->setEnabled(enabled);
        exportButton->setText(section!=4 && source()=="AWB.TPS" ? "Laser AWB" : "Preview");
        deleteDocumentButton->setVisible(documentMode() || section==3); deleteDocumentButton->setEnabled(enabled);
        vicsAction->setEnabled(enabled && source()=="OBL.TPS");
        preprintedButton->setVisible(source()=="AWB.TPS"); preprintedButton->setEnabled(enabled);
        if(!enabled) {
            if(section<2) { detail->setHtml(section==1 ? "<p>Select a contact to edit, or choose <b>Add contact</b>.</p>" : "<p>Select a customer to edit, or choose <b>Add customer</b>.</p>"); return; }
            detail->setHtml(documentMode() && bookingSelected() ? "<h2>Start this document from your shipment</h2><p>Choose <b>Create from shipment</b> to copy the parties and available routing and cargo information.</p><p>You can review and complete the document before saving or printing.</p>" : "<h2>Your shipment workspace</h2><p>Select a shipment above to see its details and documents, or choose <b>Add</b> to get started.</p>");
            return;
        }
        if(section==4) { detail->setHtml("<h2>Preserved original PDF</h2><p>Preview the original document, print it or save an exact copy.</p>"); return; }
        const auto r=store->record(id); QString html="<style>td{padding:7px 16px 7px 0;} th{text-align:left;} p{margin:8px 0;}</style>";
        if(documentMode()) {
            html+="<h3>"+DocumentTypes[documentTabs->currentIndex()-1].second.toHtmlEscaped()+"</h3>";
            if(r.source==DangerousGoodsSource) html+="<p>Air declaration · "+QString::number(r.values["DGD:LINES"].toArray().size())+" goods row(s) · Signature left blank for signing after review.</p>";
            if(r.source=="PLIST.TPS") html+="<p>The current packing-list preview is an information summary.</p>";
        }
        if(section==2 && !documentMode()) html+="<p><b>"+QString::number(shipmentDocuments.size())+"&nbsp;linked "+(shipmentDocuments.size()==1 ? "document" : "documents")+"</b> · Choose a tab above to open or create one.</p>";
        const auto prefix=editableFields(r.source).first().first.section(':',0,0)+':';
        if(r.values.contains(prefix+"SHIPPER_ARRAY")) {
            const auto address=[&](const QString &role) { return valueText(r.values[prefix+role+"_ARRAY"]).trimmed().toHtmlEscaped().replace('\n',"<br>"); };
            html+="<table width='100%' cellspacing='12'><tr><td width='50%' valign='top'><h3>Shipper</h3>"+address("SHIPPER")+"</td><td width='50%' valign='top'><h3>Consignee</h3>"+address("CONSIGNEE")+"</td></tr></table><hr>";
        }
        html+="<table width='100%'>";
        for(const auto &[key,label] : editableFields(r.source)) {
            if(key.endsWith("SHIPPER_ARRAY") || key.endsWith("CONSIGNEE_ARRAY") || key.endsWith(":REFERENCE_NUMBER") || key=="BOO:STATUS") continue;
            const auto text=fieldText(key,r.values[key]).trimmed(); if(text.isEmpty()) continue;
            html+="<tr><td width='25%' valign='top'><b>"+label.toHtmlEscaped()+"</b></td><td>"+text.toHtmlEscaped().replace('\n',"<br>")+"</td></tr>";
        }
        html+="</table>";
        if(r.source==DangerousGoodsSource) html+="<h3>Goods</h3>"+dangerousGoodsText(r.values).toHtmlEscaped().replace('\n',"<br>");
        if(!cargoSource(r.source).first.isEmpty()) html+="<p>Cargo lines: "+QString::number(store->cargoLines(r).size())+"</p>";
        detail->setHtml(html);
    }
    void showHistory() {
        if (!store || !selected() || section == 4) return;
        guarded([&] { showRecordHistory(*store,selected(),this); });
    }
    void showAddressBook(int initialTab) {
        if(!store) return;
        guarded([&] { AddressBookDialog dialog(*store,initialTab,this); dialog.exec(); });
    }
    void showCargo() {
        if (!store || !selected()) return;
        guarded([&] { showCargoDialog(*store,store->record(selected()),this); updateDetail(); });
    }
    void editShipment(bool create) {
        if(!store || section!=2 || (!create && !bookingSelected())) return;
        guarded([&] {
            const auto priorTab=documentTabs->currentIndex();
            const auto priorId=bookingSelected();
            const auto priorDocument=documentPicker->currentData().toLongLong();
            RecordEditor editor(*store,create ? store->blankRecord("BOOKING.TPS") : store->record(bookingSelected()),this);
            if(editor.exec()==QDialog::Accepted) {
                if(create) documentTabs->setCurrentIndex(0); else documentTabs->setCurrentIndex(priorTab);
                search->clear(); refresh(editor.value.id); statusBar()->showMessage(create ? "Shipment added" : "Shipment saved",4000);
            } else refresh(priorId); // Nested documents can be saved while shipment edits are canceled.
            if(documentMode()) { loadShipmentDocuments(priorDocument); updateDetail(); }
        });
    }
    void deleteShipment() {
        if(!store || section!=2 || !bookingSelected()) return;
        guarded([&] {
            const auto shipment=store->record(bookingSelected());
            const auto count=store->linkedDocuments(shipment.id).size();
            QMessageBox confirm(QMessageBox::Question,"Delete shipment", "Delete shipment "+recordReference(shipment)+"?",QMessageBox::Yes|QMessageBox::Cancel,this);
            confirm.setObjectName("confirmShipmentDelete"); confirm.button(QMessageBox::Yes)->setText("Delete"); confirm.setDefaultButton(QMessageBox::Cancel);
            confirm.setInformativeText("It will leave the shipment list. Its "+QString::number(count)+" linked document(s), cargo and history will be kept. Restore it from Workspace → Deleted shipments.");
            if(confirm.exec()!=QMessageBox::Yes) return;
            store->setShipmentDeleted(shipment,true); refresh(); statusBar()->showMessage("Shipment deleted. Restore it from Workspace → Deleted shipments.",8000);
        });
    }
    void showDeletedShipments() {
        if(!store) return;
        guarded([&] {
            QDialog dialog(this); dialog.setObjectName("deletedShipments"); dialog.setWindowTitle("Deleted shipments"); dialog.resize(760,460);
            auto *layout=new QVBoxLayout(&dialog); auto *filter=new QLineEdit; filter->setPlaceholderText("Search deleted shipments…"); layout->addWidget(filter);
            auto *list=new QListWidget; list->setObjectName("deletedShipmentsList"); layout->addWidget(list);
            const auto shipments=store->deletedShipments();
            for(const auto &r : shipments) { auto *item=new QListWidgetItem(recordReference(r)+"  ·  "+recordTitle(r),list); item->setData(Qt::UserRole,r.id); }
            auto *empty=new QLabel("No deleted shipments."); empty->setVisible(shipments.isEmpty()); layout->addWidget(empty);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); auto *restore=buttons->addButton("Restore",QDialogButtonBox::ActionRole); restore->setObjectName("restoreShipment"); restore->setEnabled(false); layout->addWidget(buttons);
            connect(list,&QListWidget::itemSelectionChanged,&dialog,[&] { restore->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); });
            connect(filter,&QLineEdit::textChanged,&dialog,[&](const QString &text) { for(int i=0;i<list->count();++i) list->item(i)->setHidden(!list->item(i)->text().contains(text,Qt::CaseInsensitive)); restore->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); });
            connect(restore,&QPushButton::clicked,&dialog,[&] { guarded([&] {
                if(!list->currentItem() || list->currentItem()->isHidden()) return;
                const auto id=list->currentItem()->data(Qt::UserRole).toLongLong();
                for(const auto &r : shipments) if(r.id==id) { store->setShipmentDeleted(r,false); break; }
                dialog.accept(); chooseSection(2); search->clear(); refresh(id); statusBar()->showMessage("Shipment restored",4000);
            }); });
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
        });
    }
    void showDeletedDocuments() {
        if(!store) return;
        guarded([&] {
            QDialog dialog(this); dialog.setObjectName("deletedDocuments"); dialog.setWindowTitle("Deleted documents"); dialog.resize(780,460);
            auto *layout=new QVBoxLayout(&dialog); auto *filter=new QLineEdit; filter->setPlaceholderText("Search deleted documents…"); layout->addWidget(filter);
            auto *list=new QListWidget; list->setObjectName("deletedDocumentsList"); layout->addWidget(list);
            const auto documents=store->deletedDocuments();
            for(const auto &r : documents) {
                QString type; for(const auto &[source,label] : DocumentTypes) if(source==r.source) type=label;
                const auto pp=editableFields(r.source).first().first.section(':',0,0)+':';
                auto *item=new QListWidgetItem(type+" · "+recordReference(r)+" · Sequence "+valueText(r.values[pp+"ID_SEQUENCE"]),list); item->setData(Qt::UserRole,r.id);
            }
            auto *empty=new QLabel("No deleted documents."); empty->setVisible(documents.isEmpty()); layout->addWidget(empty);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close); auto *restore=buttons->addButton("Restore",QDialogButtonBox::ActionRole); restore->setObjectName("restoreDocument"); restore->setEnabled(false); layout->addWidget(buttons);
            connect(list,&QListWidget::itemSelectionChanged,&dialog,[&] { restore->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); });
            connect(filter,&QLineEdit::textChanged,&dialog,[&](const QString &text) { for(int i=0;i<list->count();++i) list->item(i)->setHidden(!list->item(i)->text().contains(text,Qt::CaseInsensitive)); restore->setEnabled(list->currentItem() && !list->currentItem()->isHidden()); });
            connect(restore,&QPushButton::clicked,&dialog,[&] { guarded([&] {
                if(!list->currentItem() || list->currentItem()->isHidden()) return;
                const auto id=list->currentItem()->data(Qt::UserRole).toLongLong();
                for(const auto &r : documents) if(r.id==id) { store->setDocumentDeleted(r,false); break; }
                dialog.accept(); if(documentMode()) { loadShipmentDocuments(id); updateDetail(); } else refresh(id);
                statusBar()->showMessage("Document restored",4000);
            }); });
            connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
        });
    }
    void edit(bool create) {
        if (!store || (!create && !selected())) return;
        guarded([&] {
            RecordEditor editor(*store, create ? store->blankRecord(source()) : store->record(selected()), this);
            if (editor.exec() == QDialog::Accepted) {
                const auto id = editor.value.id; if(documentMode()) { loadShipmentDocuments(id); updateDetail(); } else { search->clear(); refresh(id); } statusBar()->showMessage("Record saved", 5000);
            }
        });
    }
    void makeDocument() {
        if(!store || !bookingSelected() || !documentMode()) return;
        guarded([&] {
            const auto target=prepareShipmentDocument(*store,store->record(bookingSelected()),source(),this); if(!target) return;
            RecordEditor editor(*store,*target,this);
            if(editor.exec()==QDialog::Accepted) { loadShipmentDocuments(editor.value.id); updateDetail(); statusBar()->showMessage("Document saved to this shipment",6000); }
        });
    }
    void previewPreprinted() {
        if(!store || !selected() || source()!="AWB.TPS") return;
        if(choosePrintRegistration(this,true,awbPrintProfile)) previewDocument(awbPrintProfile);
    }
    void previewDocument(const AirWaybillPrintOptions &options={},bool vics=false) {
        if (!store || !selected()) return;
        guarded([&] {
            QByteArray bytes; QString name, heading;
            if (section == 4) {
                bytes = store->document(selected());
                name = QFileInfo(table->currentIndex().data().toString()).fileName();
                heading = "Original PDF · " + name;
            } else {
                previewShippingDocument(*store,selected(),this,options,vics); return;
            }
            DocumentPreview preview(std::move(bytes), heading, name, this); preview.exec();
        });
    }
    void showReport() {
        guarded([&] {
            QDialog dialog(this); dialog.setWindowTitle("Migration report"); dialog.resize(820,600);
            auto *layout = new QVBoxLayout(&dialog); auto *text = new QPlainTextEdit;
            text->setReadOnly(true); text->setPlainText(QString::fromUtf8(QJsonDocument(store->migrationReport()).toJson())); layout->addWidget(text);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
        });
    }
    void newWorkspace() {
        QFileDialog dialog(this,"Create workspace",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/shadow-logis.sqlite","Shadow Logis workspace (*.sqlite)");
        dialog.setObjectName("newWorkspaceFile"); dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setDefaultSuffix("sqlite");
        if(dialog.exec()!=QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
        guarded([&] { const auto path=dialog.selectedFiles().first(); createWorkspace(path); chooseSection(2); open(path); });
    }
    void migrateFolder() {
        ImportWorkspaceDialog dialog(this);
        if(dialog.exec()!=QDialog::Accepted) return;
        guarded([&] {
            chooseSection(2); open(dialog.destination);
            statusBar()->showMessage(QString("Imported %1 records into a new workspace").arg(dialog.report["records"].toInt()),10000);
            if(dialog.report["archived_only_tps"].toInt()) QMessageBox::warning(this,"Import needs review",QString("%1 selected TPS files could not be decoded. Their originals were preserved in the workspace. See File → Migration report for details; the rest of the selected data was imported.").arg(dialog.report["archived_only_tps"].toInt()));
        });
    }

};

QByteArray promptPassword() {
    QTextStream(stderr) << "TPS owner password (optional): " << Qt::flush;
#ifdef Q_OS_UNIX
    termios original{}; const bool hidden = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original) == 0;
    if (isatty(STDIN_FILENO) && !hidden) throw std::runtime_error("Cannot disable password echo");
    if (hidden) { auto flags = original; flags.c_lflag &= ~ECHO; if (tcsetattr(STDIN_FILENO, TCSANOW, &flags)) throw std::runtime_error("Cannot disable password echo"); }
    const auto restoreEcho = qScopeGuard([&] {
        if (hidden) { tcsetattr(STDIN_FILENO, TCSANOW, &original); QTextStream(stderr) << '\n'; }
    });
#elif defined(Q_OS_WIN)
    const bool hidden = _isatty(_fileno(stdin));
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE); DWORD original = 0;
    if (hidden && (!GetConsoleMode(input, &original) || !SetConsoleMode(input, original & ~ENABLE_ECHO_INPUT)))
        throw std::runtime_error("Cannot disable password echo. Use the masked GUI migration dialog.");
    const auto restoreEcho = qScopeGuard([&] {
        if (hidden) { SetConsoleMode(input, original); QTextStream(stderr) << '\n'; }
    });
#endif
    std::string password;
    if (!std::getline(std::cin, password)) throw std::runtime_error("Could not read the owner password. Use the console executable or GUI migration dialog.");
    for (const auto c : password) if (static_cast<unsigned char>(c) >= 128) throw std::runtime_error("Only ASCII owner passwords are currently supported");
    return QByteArray::fromStdString(password);
}
}

#ifndef SHADOW_LOGIS_GUI_TESTING
int main(int argc, char **argv) {
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--create-workspace" || arg.startsWith("--create-workspace=") || arg == "--migrate" || arg.startsWith("--migrate=") || arg == "--help" || arg == "--version") headless = true;
    }
    std::unique_ptr<QCoreApplication> app;
    if (headless) app = std::make_unique<QCoreApplication>(argc, argv);
    else app = std::make_unique<QApplication>(argc, argv);
    if(auto *gui=qobject_cast<QApplication *>(app.get())) installAppearance(*gui);
    QCoreApplication::setApplicationName("Shadow Logis"); QCoreApplication::setApplicationVersion(QStringLiteral(SHADOW_LOGIS_VERSION));
    QCommandLineParser args; args.setApplicationDescription("Native shipping workspace and read-only TPS migration");
    args.addHelpOption(); args.addVersionOption();
    args.addOption({"create-workspace", "Create an empty shipping workspace (never replaces a file)", "file"});
    args.addOption({"database", "Open a migrated SQLite database", "file"});
    args.addOption({"migrate", "Import TPS files and historical PDFs into a NEW SQLite database", "directory"});
    args.addOption({"output", "Output path for migration or PDF export", "file"});
    args.addOption({"export-awb", "Export the air-waybill record ID to --output (requires --database)", "id"});
    args.addOption({"export-bol", "Export a VICS freight BOL record ID to --output (requires --database)", "id"});
    args.addOption({"export-ocean-bol", "Export a text-only ocean BOL record ID to --output", "id"});
    args.addOption({"export-sight-draft", "Export both sight draft exchange copies", "id"});
    args.addOption({"export-dock-receipt", "Export a dock receipt record ID to --output", "id"});
    args.addOption({"export-certificate", "Export a certificate of origin record ID to --output", "id"});
    args.addOption({"export-awb-text", "Export text-only AWB for one 9.5 x 12 inch continuous form", "id"});
    args.addOption({"print-offset-x", "Preprinted form horizontal offset in mm", "mm", "0"});
    args.addOption({"print-offset-y", "Preprinted form vertical offset in mm", "mm", "0"});
    args.addOption({"print-scale-x", "Preprinted form horizontal registration scale", "percent", "100"});
    args.addOption({"print-scale-y", "Preprinted form vertical registration scale", "percent", "100"});
    args.addOption({"password-prompt", "Prompt for an optional TPS owner password without terminal echo"});
    args.addOption({"self-test", "Check bundled runtime with synthetic data, then exit (no workspace opened)"});
    args.addOption({"smoke-test", "Load the GUI, save a screenshot, and exit", "png"});
    args.addOption({"sample-pdf", "Export a synthetic shipment summary and exit", "pdf"});
    args.addOption({"sample-cargo-pdf", "Export a synthetic multipage cargo summary and exit", "pdf"});
    args.process(*app);
    try {
        if (args.isSet("self-test")) {
            if (args.optionNames().size() != 1) throw std::runtime_error("Use --self-test without other application options.");
            auto *gui = qobject_cast<QApplication *>(app.get());
            if (!gui) throw std::runtime_error("Runtime check requires a GUI application.");
            checkRuntime(*gui);
            Window window({}); window.show(); gui->processEvents();
            if (window.grab().isNull()) throw std::runtime_error("Runtime check: workspace could not render.");
            QTextStream(stdout) << "PASS: application runtime (" << gui->platformName()
                               << "), embedded fonts/icon/references, SQLite, image codecs, PDF preview and print output.\n";
            return 0;
        }
        QStringList exports;
        for(const auto &action : {"export-awb","export-bol","export-awb-text","export-ocean-bol","export-dock-receipt","export-sight-draft","export-certificate"}) if(args.isSet(action)) exports<<action;
        if(!exports.isEmpty()) {
            if(args.isSet("migrate") || exports.size()!=1) throw std::runtime_error("Choose one document export or migration.");
            const auto action=exports.first();
            bool ok = false; const auto id = args.value(action).toLongLong(&ok);
            if (!ok || id <= 0 || !args.isSet("database") || !args.isSet("output"))
                throw std::runtime_error("PDF export requires a positive record ID, --database and --output");
            Store store(args.value("database")); const auto record = store.record(id);
            AirWaybillPrintOptions options; options.preprintedContinuous=args.isSet("export-awb-text");
            const auto number=[&](const QString &key) { bool valid=false; const auto v=args.value(key).toDouble(&valid); if (!valid) throw std::runtime_error("Invalid print registration number."); return v; };
            options.horizontalOffsetMm=number("print-offset-x"); options.verticalOffsetMm=number("print-offset-y");
            options.horizontalScalePercent=number("print-scale-x"); options.verticalScalePercent=number("print-scale-y");
            if (args.isSet("export-awb") || options.preprintedContinuous) exportAirWaybill(record,args.value("output"),airWaybillContext(store,record),options);
            else if(args.isSet("export-ocean-bol")) exportOceanBillOfLading(record,args.value("output"),store.cargoLines(record),{true,options.horizontalOffsetMm,options.verticalOffsetMm,options.horizontalScalePercent,options.verticalScalePercent});
            else if(args.isSet("export-sight-draft")) exportSightDraft(record,args.value("output"));
            else if(args.isSet("export-dock-receipt")) exportDockReceipt(record,args.value("output"),store.cargoLines(record));
            else if(args.isSet("export-certificate")) exportCertificateOfOrigin(record,args.value("output"),store.cargoLines(record),certificateForwarder(store,record));
            else exportVicsBillOfLading(record,args.value("output"),store.cargoLines(record));
            return 0;
        }
        if(args.isSet("create-workspace")) {
            if(args.isSet("migrate") || args.isSet("database") || !exports.isEmpty()) throw std::runtime_error("Choose workspace creation separately from migration/export");
            createWorkspace(args.value("create-workspace")); return 0;
        }
        if (args.isSet("migrate")) {
            if (!args.isSet("output")) throw std::runtime_error("--migrate requires --output");
            auto password = args.isSet("password-prompt") ? promptPassword() : QByteArray{};
            const auto report = migrate(args.value("migrate"), args.value("output"), password); password.fill('\0');
            QTextStream(stdout) << QJsonDocument(report).toJson(); return 0;
        }
        if (args.isSet("sample-pdf") || args.isSet("sample-cargo-pdf")) {
            Record sample; sample.source = "BOOKING.TPS";
            sample.values = {{"BOO:REFERENCE_NUMBER", "DEMO-001"}, {"BOO:BOOKING_NUMBER", "BOOK-001"},
                {"BOO:SHIPPER_ARRAY", QJsonArray{"Example Exporter", "123 Sample Road", "Chicago, IL", "United States", ""}},
                {"BOO:CONSIGNEE_ARRAY", QJsonArray{"Example Importer", "456 Harbour Street", "Vancouver, BC", "Canada", ""}},
                {"BOO:VESSEL_NAME", "Example Vessel"}, {"BOO:LOAD_PORT", "Seattle"}, {"BOO:DISCHARGE_PORT", "Vancouver"},
                {"BOO:COMMODITY_1", "Machine parts"}, {"BOO:QTY", "12"}, {"BOO:KGS", "850"}, {"BOO:CBM", "3.25"}};
            QList<Record> cargo;
            if (args.isSet("sample-cargo-pdf")) {
                sample.source = "OBL.TPS";
                QJsonObject fields;
                for (auto it = sample.values.begin(); it != sample.values.end(); ++it) fields["OBL:"+it.key().section(':',1)] = it.value();
                sample.values = fields;
                for (int i = 0; i < 40; ++i) {
                    Record line; line.source = "OBLLINE.TPS";
                    line.values = {{"OLI:MARKS_NUMBERS",QString("BOX-%1").arg(i+1)}, {"OLI:PCS","2"},
                        {"OLI:DESCRIPTION","Synthetic machine parts for layout validation"}, {"OLI:WEIGHT_KGS","20.5"}, {"OLI:MEASURE_CBM","0.25"}};
                    cargo.append(line);
                }
            }
            exportSummary(sample, args.value(args.isSet("sample-cargo-pdf") ? "sample-cargo-pdf" : "sample-pdf"), cargo); return 0;
        }
        auto path = args.value("database");
        if (path.isEmpty()) {
            const auto remembered=rememberedWorkspace();
            if(!remembered.isEmpty()) {
                try { Store check(remembered); path=remembered; } catch(const std::exception &) { Preferences().remove("workspace/lastPath"); }
            }
        }
        if (path.isEmpty()) {
            if (QFileInfo::exists("data/shadow-logis.sqlite")) path="data/shadow-logis.sqlite";
            else {
                // Discover a sole existing migration without renaming user data.
                const QDir data("data");
                const auto databases=data.entryList({"*.sqlite","*.db"},QDir::Files);
                if (databases.size()==1) path=data.filePath(databases.first());
            }
        }
        Window window(path); window.show();
        if (args.isSet("smoke-test")) QTimer::singleShot(200, &window, [&] {
            try { window.smoke(args.value("smoke-test")); app->exit(0); }
            catch (const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; app->exit(1); }
        });
        return app->exec();
    } catch (const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; return 1; }
}
#endif
