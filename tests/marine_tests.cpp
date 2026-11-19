#include "documents.h"
#include "domain.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QPdfDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>
using namespace shadowlogis;
namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
void rejects(const std::function<void()> &fn) { bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } check(failed,"Invalid marine export accepted"); }
QString extract(const QString &path) { QProcess p; p.start("pdftotext",{"-layout",path,"-"}); check(p.waitForFinished(10000) && p.exitCode()==0,"PDF extraction failed"); return QString::fromUtf8(p.readAllStandardOutput()); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir temp; check(temp.isValid(),"Temporary directory");
        const QString folder=argc==2 ? QString::fromLocal8Bit(argv[1]) : temp.path(); check(QDir().mkpath(folder),"Output folder");
        Record ocean{5,1,"OBL.TPS",{{"OBL:REFERENCE_NUMBER","DEMO-OCEAN-001"},{"OBL:ID_SEQUENCE","7"},{"OBL:BL_NUMBER","DEMO-BL-001"},
            {"OBL:MBL_NUMBER","DEMO-MASTER-001"},{"OBL:BOOKING_NUMBER","BOOK-001"},{"OBL:DOCUMENT_NUMBER","DOC-001"},
            {"OBL:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Road","Houston, TX 77001","United States"}},
            {"OBL:CONSIGNEE_ARRAY",QJsonArray{"Example Importer","456 Harbour Street","Rotterdam","Netherlands"}},
            {"OBL:NOTIFY_ARRAY",QJsonArray{"Example Destination Agent","789 Dock Road","Rotterdam, Netherlands"}},
            {"OBL:REFERENCE_ARRAY",QJsonArray{"DEMO-OCEAN-001","Purchase order DEMO-PO-001"}},
            {"OBL:VESSEL_NAME","Example Vessel"},{"OBL:VOYAGE_NUMBER","001E"},{"OBL:LOAD_PORT","Houston"},{"OBL:DISCHARGE_PORT","Rotterdam"},
            {"OBL:PIER_TERMINAL","Example Container Terminal"},{"OBL:PLACE_OF_RECEIPT","Houston, TX"},{"OBL:PLACE_OF_DELIVERY","Rotterdam CY"},
            {"OBL:PRECARRIAGE_BY","Truck"},{"OBL:POINT_OF_ORIGIN","Texas, United States"},{"OBL:TYPE_MOVE","CY / CY"},
            {"OBL:ISSUED_AT","Houston, TX"},{"OBL:DATE",dateStorage("2026-11-18")},{"OBL:PAYABLE_AT","Houston"},{"OBL:NUMBER_OF_ORIGINALS","3"},
            {"OBL:HBL_RATING_NOTES","Freight prepaid"},{"OBL:BL_INSTRUCTIONS","SYNTHETIC EXAMPLE - NOT FOR SHIPMENT"},
            {"OBL:SIGNATURE_LINE_1","LEGACY SIGNATURE MUST NOT PRINT"},
            {"NATIVE:FORWARDER_ADDRESS","Example Forwarder\n12 Sample Avenue\nHouston, TX, United States"},{"NATIVE:CARRIER_NAME","Example Ocean Carrier"}}};
        Record row{0,1,"OBLLINE.TPS",{{"OLI:REFERENCE_NUMBER","DEMO-OCEAN-001"},{"OLI:ID_SEQUENCE","7"},{"OLI:MARKS_NUMBERS","DEMO-CNTR-001\nSeal DEMO-123\nCartons 1-12"},
            {"OLI:PCS","12"},{"OLI:DESCRIPTION","Precision machine parts\nPacked in protective cartons"},{"OLI:WEIGHT_KGS","850.00"},{"OLI:MEASURE_CBM","5.25"}}};
        auto fractional=row; fractional.values["OLI:MARKS_NUMBERS"]="Carton 13"; fractional.values["OLI:PCS"]="1"; fractional.values["OLI:DESCRIPTION"]="Replacement seals"; fractional.values["OLI:WEIGHT_KGS"]="8.50"; fractional.values["OLI:MEASURE_CBM"]="0.25";
        const auto op=folder+"/ocean-bill-of-lading-example.pdf"; exportOceanBillOfLading(ocean,op,{row,fractional},{false});
        auto text=extract(op); for(const auto &value : {"OCEAN BILL OF LADING","DEMO-BL-001","Example Vessel","DEMO-123","850 kg","8.50 kg","5.25 m","Example Forwarder","SYNTHETIC EXAMPLE"}) check(text.contains(value),qPrintable(QString("Missing PDF field: ")+value));
        check(!text.contains("850.00") && !text.contains("LEGACY SIGNATURE"),"Display precision or signature regression");
        QPdfDocument pdf; check(pdf.load(op)==QPdfDocument::Error::None && pdf.pageCount()==1 && pdf.pagePointSize(0)==QSizeF(612,792),"Ocean page geometry"); pdf.close();
        const auto stock=folder+"/text-only-ocean-bol-example.pdf";
        exportOceanBillOfLading(ocean,stock,{row,fractional}); const auto stockText=extract(stock);
        for(const auto &needle : {"Example Exporter","Example Importer","DEMO-123","850 kg","8.50 kg","DEMO-BL-001"}) check(stockText.contains(needle),"Text-only BoL lost shipment data");
        for(const auto &label : {"OCEAN BILL OF LADING","SHIPPER / EXPORTER","GROSS WEIGHT","Received by Carrier","Agent for carrier","DRAFT"}) check(!stockText.contains(label),"Text-only BoL printed template wording");
        QFile stockFile(stock); check(stockFile.open(QIODevice::ReadOnly) && !stockFile.readAll().contains("/Subtype /Image"),"Text-only BoL contains a template image");
        check(pdf.load(stock)==QPdfDocument::Error::None && pdf.pagePointSize(0)==QSizeF(612,792),"Text-only BoL size");
        const auto unshifted=pdf.getAllText(0).boundingRectangle().topLeft(); pdf.close();
        OceanBillPrintOptions offset; offset.horizontalOffsetMm=5*25.4/72; offset.verticalOffsetMm=7*25.4/72;
        exportOceanBillOfLading(ocean,temp.filePath("shifted.pdf"),{row,fractional},offset);
        check(pdf.load(temp.filePath("shifted.pdf"))==QPdfDocument::Error::None,"Shifted BoL load");
        const auto delta=pdf.getAllText(0).boundingRectangle().topLeft()-unshifted; check(qAbs(delta.x()-5)<.1 && qAbs(delta.y()-7)<.1,"BoL alignment offset not physical units"); pdf.close();
        Record empty{0,0,"OBL.TPS",{{"OBL:REFERENCE_NUMBER",""}}}; exportOceanBillOfLading(empty,temp.filePath("empty.pdf"));
        check(pdf.load(temp.filePath("empty.pdf"))==QPdfDocument::Error::None,"Empty BoL load"); const auto blank=pdf.render(0,{612,792});
        for(int y=0;y<blank.height();++y) for(int x=0;x<blank.width();++x) check(qAlpha(blank.pixel(x,y))<128 || qGray(blank.pixel(x,y))>240,"Blank preprinted BoL contains template ink"); pdf.close();
        auto packages=row; packages.values["OLI:PCS"]="1234"; exportOceanBillOfLading(ocean,temp.filePath("packages.pdf"),{packages});
        check(extract(temp.filePath("packages.pdf")).contains("1234"),"Package count wrapped or split across lines");
        Record dock; dock.source="DODR.TPS"; dock.values={{"DOD:REFERENCE_NUMBER","DEMO-OCEAN-001"},{"DOD:ID_SEQUENCE","8"}};
        for(auto it=ocean.values.begin();it!=ocean.values.end();++it) if(it.key().startsWith("OBL:")) dock.values["DOD:"+it.key().mid(4)]=it.value();
        dock.values["DOD:ID_SEQUENCE"]="8"; dock=marineDocumentFromDocument(ocean,dock);
        dock.values["DOD:DO_NUMBER"]="DEMO-DR-001"; dock.values["DOD:DEL_CAR"]="Example Trucking";
        dock.values["DOD:CUTOFF"]=dateStorage("2026-11-20"); dock.values["DOD:SAILING"]=dateStorage("2026-11-23");
        QList<Record> dockCargo;
        for(const auto &original : {row,fractional}) { Record line; line.source="DODRLINE.TPS"; for(auto it=original.values.begin();it!=original.values.end();++it) line.values["DLI:"+it.key().mid(4)]=it.value(); line.values["DLI:ID_SEQUENCE"]="8"; dockCargo<<line; }
        check(dock.values["NATIVE:MARINE_SOURCE_ID"]=="5" && dock.values["DOD:BL_NUMBER"]=="DEMO-BL-001","Marine copy provenance");
        check(dock.values["DOD:ID_SEQUENCE"]=="8","Marine copy replaced target identity");
        const auto dp=folder+"/dock-receipt-example.pdf"; exportDockReceipt(dock,dp,dockCargo); text=extract(dp);
        for(const auto &value : {"DOCK RECEIPT","DEMO-DR-001","DEMO-BL-001","Example Trucking","2026-11-20","RECEIVING CLERK","850 kg","8.50 kg"}) check(text.contains(value),qPrintable(QString("Missing dock field: ")+value));
        check(pdf.load(dp)==QPdfDocument::Error::None && pdf.pageCount()==1 && pdf.pagePointSize(0)==QSizeF(612,792),"Dock page geometry"); pdf.close();
        // One unusually long cargo entry must continue rather than disappear.
        auto longRow=row; QStringList lines; for(int i=0;i<85;++i) lines<<QString("Cargo line %1 END%1").arg(i,3,10,QChar('0'));
        longRow.values["OLI:DESCRIPTION"]=lines.join('\n'); const auto multi=folder+"/ocean-bill-of-lading-multipage.pdf";
        exportOceanBillOfLading(ocean,multi,{longRow,fractional},{false}); text=extract(multi);
        for(int i=0;i<85;++i) check(text.count(QString("END%1").arg(i,3,10,QChar('0')))==1,"Cargo lost or duplicated across continuation pages");
        check(pdf.load(multi)==QPdfDocument::Error::None && pdf.pageCount()>1,"Cargo pagination");
        check(text.count("OCEAN BILL OF LADING")==pdf.pageCount(),"Missing repeated marine header"); pdf.close();
        exportOceanBillOfLading(ocean,temp.filePath("stock-multipage.pdf"),{longRow,fractional}); const auto stockMulti=extract(temp.filePath("stock-multipage.pdf"));
        for(int i=0;i<85;++i) check(stockMulti.count(QString("END%1").arg(i,3,10,QChar('0')))==1,"Text-only continuation lost cargo");
        auto notes=ocean; QStringList extra;
        for(int i=0;i<24;++i) extra<<QString("Instruction marker NOTE%1").arg(i,2,10,QChar('0'));
        notes.values["OBL:BL_INSTRUCTIONS"]=extra.join('\n'); notes.values["OBL:ONWARD_ARRAY"]=QJsonArray{"Long routing detail 1","Long routing detail 2","Long routing detail 3","Long routing detail 4"};
        const auto continued=folder+"/ocean-bill-of-lading-notes.pdf"; exportOceanBillOfLading(notes,continued,{row},{false}); text=extract(continued);
        for(int i=0;i<24;++i) check(text.count(QString("NOTE%1").arg(i,2,10,QChar('0')))==1,"Continuation lost or duplicated an instruction");
        check(text.simplified().contains("not additional cargo") && text.contains("Long routing detail 4"),"Unlabeled or missing document information continuation");
        auto totals=ocean; totals.values["OBL:PCS"]="17"; totals.values["OBL:KGS"]="48.50";
        exportOceanBillOfLading(totals,temp.filePath("header-only.pdf")); check(extract(temp.filePath("header-only.pdf")).contains("48.50 kg"),"Stored header-only cargo omitted");
        // Refuse a mismatched sequence and keep an existing output intact on failure.
        QFile prior(temp.filePath("preserve.pdf")); check(prior.open(QIODevice::WriteOnly),"Prior output"); prior.write("sentinel"); prior.close();
        auto wrong=row; wrong.values["OLI:ID_SEQUENCE"]="9"; rejects([&] { exportOceanBillOfLading(ocean,prior.fileName(),{wrong}); });
        auto overflow=ocean; overflow.values["OBL:SHIPPER_ARRAY"]=QJsonArray{QString("oversize ").repeated(1000)};
        rejects([&] { exportOceanBillOfLading(overflow,prior.fileName(),{row}); });
        check(prior.open(QIODevice::ReadOnly) && prior.readAll()=="sentinel","Failed export replaced existing output");
        rejects([&] { exportDockReceipt(ocean,temp.filePath("wrong.pdf")); });
        auto savedTarget=dock; savedTarget.id=12; rejects([&] { marineDocumentFromDocument(ocean,savedTarget); });
        auto other=dock; other.values["DOD:REFERENCE_NUMBER"]="OTHER"; rejects([&] { marineDocumentFromDocument(ocean,other); });
        QTextStream(stdout)<<"PASS: marine field placement, weights, provenance, cargo pagination, sequence isolation and atomic export failures.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
