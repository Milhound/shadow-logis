#include "documents.h"
#include "domain.h"
#include "preview.h"
#include "appearance.h"
#include <QApplication>
#include <QBuffer>
#include <QJsonArray>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QPdfDocument>
#include <QPrinter>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <QXmlStreamReader>
#include <stdexcept>
using namespace shadowlogis;
namespace {
void require(bool ok,const char *message) { if (!ok) throw std::runtime_error(message); }
QString extracted(const QString &tool,const QString &path) {
    QProcess process; process.start(tool,{"-layout",path,"-"});
    require(process.waitForFinished(30000) && process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,"PDF text extraction failed");
    return QString::fromUtf8(process.readAllStandardOutput());
}
QImage rendered(const QString &tool,const QString &path,const QString &prefix) {
    QProcess process; process.start(tool,{"-f","1","-singlefile","-scale-to","1000","-png",path,prefix});
    require(process.waitForFinished(30000) && process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,"PDF rasterization failed");
    QImage image(prefix+".png"); require(!image.isNull(),"PDF raster missing"); return image;
}
struct PdfWord { QString text; QRectF box; };
QList<PdfWord> positionedWords(const QString &tool,const QString &path) {
    QProcess process; process.start(tool,{"-bbox",path,"-"});
    require(process.waitForFinished(30000) && process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,"PDF position extraction failed");
    QXmlStreamReader xml(process.readAllStandardOutput()); QList<PdfWord> words;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name()!=QStringLiteral("word")) continue;
        const auto a=xml.attributes();
        const QPointF start(a.value("xMin").toDouble(),a.value("yMin").toDouble());
        const QPointF end(a.value("xMax").toDouble(),a.value("yMax").toDouble());
        words.append({xml.readElementText(),QRectF(start,end)});
    }
    require(!xml.hasError(),"Malformed PDF positions"); return words;
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    installAppearance(app);
    try {
        require(argc==2 || argc==3,"Pass pdftotext and optionally pdftoppm executables"); QTemporaryDir temp; require(temp.isValid(),"Temporary directory unavailable");
        Record awb; awb.source="AWB.TPS";
        awb.values={{"AWB:REFERENCE_NUMBER","SYNTHETIC-AWB"},{"AWB:AWB_NUMBER","TEST-001"},
            {"AWB:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Road"}},
            {"AWB:CONSIGNEE_ARRAY",QJsonArray{"Café Importer","Montréal"}},
            {"AWB:NATURE_OF_GOODS",QJsonArray{"Synthetic machinery"}},
            {"AWB:PIECES_TOTAL","5"},{"AWB:GROSS_WEIGHT_TOTAL","0.30"}};
        for (const auto &key : airCargoKeys()) awb.values[key]=QJsonArray{"0","0","0","0","0","0"};
        awb.values["AWB:KG_LB"]=QJsonArray{"K","K","","","","L"};
        awb.values["AWB:NUMBER_OF_PIECES"]=QJsonArray{"2","3","0","0","0","99"};
        awb.values["AWB:GROSS_WEIGHT"]=QJsonArray{"0.10","0.20","0","0","0","444.25"};
        awb.values["AWB:RATE_CHARGE"]=QJsonArray{"1.50","2.00","0","0","0","9999.99"};
        awb.values["AWB:LINE_TOTAL"]=QJsonArray{"3.00","6.00","0","0","0","9999.99"};
        awb.values["AWB:CHARGE_TOTAL"]="9.00"; awb.values["AWB:WEIGHT_CHARGE_PPD"]="9.00";
        awb.values["AWB:TOTAL_PPD"]="10.00"; awb.values["AWB:AWB_CURRENCY"]="USD";
        awb.values["AWB:WEIGHT_VALUE_PPD_COL"]="P"; awb.values["AWB:OTHER_PPD_COL"]="C";
        awb.values["AWB:DECLARED_VALUE_CARRIAGE"]="NVD";
        awb.values["AWB:OTHER_CHARGE_DESCRIPTIONS"]=QJsonArray{"Document fee"};
        awb.values["AWB:OTHER_CHARGE_AMOUNTS"]=QJsonArray{"1.00"};
        awb.values["AWB:EXTENDED_DESCRIPTION"]="NOTE-001: Keep dry.\nNOTE-002: Keep upright.";
        AirWaybillContext context{"Example Airline",{"Example Forwarder","Sample Street"},"1234567","PO-123",{}};
        exportAirWaybill(awb,temp.path()+"/form.pdf",context);
        const auto text=extracted(QString::fromLocal8Bit(argv[1]),temp.path()+"/form.pdf");
        QPdfDocument form; require(form.load(temp.path()+"/form.pdf")==QPdfDocument::Error::None,"Native form PDF loads");
        require(form.pageCount()==1 && form.pagePointSize(0)==QSizeF(612,792),"Standard AWB must keep its exact Letter-size geometry");
        require(text.count("NOTE-001:")==1 && text.count("NOTE-002:")==1,"Shipment notes lost or duplicated");
        require(text.contains("Example Airline") && text.contains("Example Forwarder") && text.contains("1234567") && text.contains("PO-123"),"Issuing parties and reference data missing");
        require(text.contains("Café Importer") && text.contains("Montréal"),"Unicode PDF text lost");
        require(text.contains("0.30") && text.contains("USD") && text.contains("1.50") && text.contains("Document fee") && text.contains("10.00"),"Standard AWB fields missing");
        require(!text.contains("444.25") && !text.contains("9999.99"),"Legacy working slot printed as cargo or charges");
        require(!text.contains(QRegularExpression("[\\x{e000}-\\x{f8ff}]")),"Unexpected private-use font characters");
        require(text.contains("DRAFT") && text.contains("SYNTHETIC-AWB"),"Review identity missing");
        require(!text.contains("Laser AWB",Qt::CaseInsensitive) && !text.contains("(c)"),"Former product branding remains in generated text");
        for(const auto &label : {"Air Waybill","Shipper's Name and Address","Chargeable","Currency","Handling Information","Signature of Shipper"})
            require(text.contains(label),"Native AWB form wording is missing");
        QFile nativeForm(temp.path()+"/form.pdf"); require(nativeForm.open(QIODevice::ReadOnly),"Read native form");
        require(!QString::fromLatin1(nativeForm.readAll()).contains(QRegularExpression("/Subtype\\s*/Image")),"Native form contains a legacy raster background");
        auto whole=awb;
        whole.values["AWB:GROSS_WEIGHT"]=QJsonArray{"11828.00","0.00","0","0","0","444.25"};
        whole.values["AWB:CHARGEABLE_WEIGHT"]=QJsonArray{"11828.00","0.00","0","0","0","999.00"};
        whole.values["AWB:GROSS_WEIGHT_TOTAL"]="11828.00";
        whole.values["AWB:AIRPORT_OF_DESTINATION"]="Singapore (Changi In";
        auto destinationContext=context; destinationContext.destination="SIN - Singapore (Changi)";
        for (bool stock : {false,true}) {
            AirWaybillPrintOptions profile; profile.preprintedContinuous=stock;
            const auto path=temp.filePath(stock ? "whole-stock.pdf" : "whole-form.pdf");
            exportAirWaybill(whole,path,destinationContext,profile);
            const auto words=positionedWords(QString::fromLocal8Bit(argv[1]),path);
            int weights=0, destinations=0;
            for (const auto &word : words) {
                if (word.text=="11828") ++weights;
                require(word.text!="11828.00","Whole shipment weights still print decimals");
                if (word.text=="SIN" || word.text=="Singapore" || word.text=="(Changi)") {
                    ++destinations; const int copy=stock && word.box.top()>864 ? 1 : 0;
                    const auto bounds=stock ? QRectF(92,374+copy*864,129,17) : QRectF(39,295,102,14);
                    require(bounds.contains(word.box),"Destination label exceeds its printed box");
                }
            }
            require(weights==3 && destinations==3,"Whole weights or complete airport label missing from a copy");
            require(extracted(QString::fromLocal8Bit(argv[1]),path).contains("1.50"),"Rate precision changed with weight display");
        }
        AirWaybillPrintOptions continuous; continuous.preprintedContinuous=true;
        exportAirWaybill(awb,temp.filePath("text-only.pdf"),context,continuous);
        QPdfDocument preprinted; require(preprinted.load(temp.filePath("text-only.pdf"))==QPdfDocument::Error::None,"Text-only PDF loads");
        require(preprinted.pageCount()==1 && preprinted.pagePointSize(0)==QSizeF(684,864),"Single continuous form must be exactly 9.5 x 12 inches");
        const auto textOnly=extracted(QString::fromLocal8Bit(argv[1]),temp.filePath("text-only.pdf"));
        require(textOnly.count("NOTE-001:")==1 && textOnly.count("Example Airline")==1 && textOnly.count("PO-123")==1,"Continuous output must contain exactly one complete copy");
        require(!textOnly.contains("DRAFT") && !textOnly.contains("Signature of") && !textOnly.contains("Shipper's Name"),"Preprinted mode printed template labels");
        QFile textOnlyFile(temp.filePath("text-only.pdf")); require(textOnlyFile.open(QIODevice::ReadOnly),"Read text-only PDF");
        const auto textBytes=textOnlyFile.readAll();
        require(!QString::fromLatin1(textBytes).contains(QRegularExpression("/Subtype\\s*/Image")),"Text-only PDF contains a template image");
        // Charge rows must be measured in physical form coordinates, with the
        // same wrapping used for drawing, and keep amounts beside descriptions.
        for (bool stock : {false,true}) {
            auto charges=awb;
            charges.values["AWB:OTHER_CHARGE_DESCRIPTIONS"]=QJsonArray{"CHARGE-A\nCONTINUED", "CHARGE-B"};
            charges.values["AWB:OTHER_CHARGE_AMOUNTS"]=QJsonArray{"123.45", "678.90"};
            AirWaybillPrintOptions profile; profile.preprintedContinuous=stock;
            const auto path=temp.filePath(QString("charges-%1.pdf").arg(stock));
            exportAirWaybill(charges,path,{},profile);
            const auto words=positionedWords(QString::fromLocal8Bit(argv[1]),path);
            const auto bounds=[&](const QString &needle) {
                for (const auto &word : words) if (word.text==needle) return word.box;
                throw std::runtime_error(("Missing charge field: "+needle).toStdString());
            };
            const auto first=bounds("CHARGE-A"), continuation=bounds("CONTINUED"), second=bounds("CHARGE-B");
            require(std::abs(first.top()-bounds("123.45").top())<.1 && std::abs(second.top()-bounds("678.90").top())<.1,
                    "Charge amount no longer aligns with its description");
            if (!(first.bottom()<=continuation.top()+.1 && continuation.bottom()<=second.top()+.1))
                throw std::runtime_error(QString("Wrapped charge rows overlap: stock=%1 first bottom=%2 continuation=%3..%4 second top=%5")
                    .arg(stock).arg(first.bottom()).arg(continuation.top()).arg(continuation.bottom()).arg(second.top()).toStdString());
            QFile file(path); require(file.open(QIODevice::ReadOnly),"Read charge PDF");
            const auto saved=file.readAll(); file.close();
            charges.values["AWB:OTHER_CHARGE_DESCRIPTIONS"]=QJsonArray{QString("TOO MANY LINES\n").repeated(100)};
            bool rejected=false;
            try { exportAirWaybill(charges,path,{},profile); }
            catch (const std::exception &e) { rejected=QString::fromUtf8(e.what()).contains("Other charges"); }
            require(rejected && file.open(QIODevice::ReadOnly) && file.readAll()==saved,
                    "Charge overflow must preserve the existing PDF");
        }
        const auto inkBounds=[](const QImage &image) {
            QRect bounds;
            for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x)
                if (qAlpha(image.pixel(x,y))>128 && qGray(image.pixel(x,y))<128) bounds|=QRect(x,y,1,1);
            return bounds;
        };
        const auto originalBounds=inkBounds(preprinted.render(0,{684,864}));
        continuous.horizontalOffsetMm=5*25.4/72; continuous.verticalOffsetMm=7*25.4/72;
        exportAirWaybill(awb,temp.filePath("shifted.pdf"),context,continuous);
        QPdfDocument shifted; shifted.load(temp.filePath("shifted.pdf"));
        const auto shiftedBounds=inkBounds(shifted.render(0,{684,864}));
        require(shiftedBounds.topLeft()-originalBounds.topLeft()==QPoint(5,7),"Registration offset was not applied in physical units");
        DocumentPreview printPreview(textBytes,"Text-only print test","text-only.pdf");
        QPrinter printer(QPrinter::HighResolution); printer.setOutputFormat(QPrinter::PdfFormat); printer.setResolution(144);
        printer.setPageSize(QPageSize(QSizeF(9.5,12),QPageSize::Inch,"Continuous",QPageSize::ExactMatch));
        printer.setOutputFileName(temp.filePath("printed-continuous.pdf")); printPreview.printTo(printer);
        QPdfDocument printed; require(printed.load(temp.filePath("printed-continuous.pdf"))==QPdfDocument::Error::None && printed.pagePointSize(0)==QSizeF(684,864),"Print path changed continuous paper size");
        const auto printedBounds=inkBounds(printed.render(0,{684,864}));
        require((printedBounds.topLeft()-originalBounds.topLeft()).manhattanLength()<=2 && (printedBounds.bottomRight()-originalBounds.bottomRight()).manhattanLength()<=2,"Actual-size printing scaled or shifted the text");
        // Imported AWBs often have a multi-line goods description and notify
        // memo but only one dimension row. All data must share the full panel,
        // without reserving five unused rows or treating CRLF as two breaks.
        for (bool stock : {false,true}) for (int rows=1;rows<=5;++rows) {
            auto detailed=awb;
            detailed.values["AWB:NATURE_OF_GOODS"]=QJsonArray{"GOODS-A","GOODS-B","","","",""};
            detailed.values["AWB:EXTENDED_DESCRIPTION"]="NOTE-A\r\nNOTE-B\rNOTE-C\nNOTE-D";
            if (rows==1) detailed.values["AWB:EXTENDED_DESCRIPTION"]="NOTE-A\r\nNOTE-B\rNOTE-C\nNOTE-D\nNOTE-E\nNOTE-F\nNOTE-G\nNOTE-H\nNOTE-I\nNOTE-J";
            if (rows==5) detailed.values["AWB:NATURE_OF_GOODS"]=QJsonArray{"GOODS-A",QString(20,'\n'),"GOODS-B"}; // spacer fallback
            detailed.values["AWB:DIMS_STD_METRIC"]=rows%2 ? "S" : "M";
            detailed.values["AWB:DIMENSIONED_WEIGHT"]="917.29";
            QStringList needles{"GOODS-A","GOODS-B","NOTE-A","NOTE-B","NOTE-C","NOTE-D","917.29"};
            if (rows==1) needles << "NOTE-E" << "NOTE-F" << "NOTE-G" << "NOTE-H" << "NOTE-I" << "NOTE-J";
            int column=0;
            for (const auto &key : {"QUANTITY","LENGTH","WIDTH","HEIGHT","CFT"}) {
                QJsonArray values;
                for (int row=0;row<5;++row) {
                    const auto number=QString::number(101+row+column*100);
                    values.append(row<rows ? number : "0");
                    if (row<rows) needles << number;
                }
                detailed.values["AWB:DESCRIPTION_"+QString(key)]=values; ++column;
            }
            const auto saved=detailed.values;
            AirWaybillPrintOptions profile; profile.preprintedContinuous=stock;
            const auto path=temp.filePath(QString("goods-%1-%2.pdf").arg(stock).arg(rows));
            exportAirWaybill(detailed,path,{},profile);
            require(detailed.values==saved,"Printing changed stored description spacing or dimensions");
            const auto words=positionedWords(QString::fromLocal8Bit(argv[1]),path);
            const int copies=1;
            for (const auto &needle : needles) {
                int count=0;
                for (const auto &word : words) if (word.text==needle) {
                    ++count; const int copy=stock && word.box.top()>864 ? 1 : 0;
                    const QRectF panel=stock ? QRectF(468,463+copy*864,155,162) : QRectF(405,390,157,134);
                    // Poppler and Qt round font bounds independently (<0.05pt).
                    if (!panel.adjusted(-.05,-.05,.05,.05).contains(word.box)) throw std::runtime_error(QString("Goods panel bounds: stock=%1 rows=%2 word=%3 at %4,%5 %6x%7")
                        .arg(stock).arg(rows).arg(word.text).arg(word.box.x()).arg(word.box.y()).arg(word.box.width()).arg(word.box.height()).toStdString());
                }
                require(count==copies,"Goods, memo or dimension text missing or duplicated on a copy");
            }
            for (int copy=0;copy<copies;++copy) {
                double descriptionBottom=0, dimensionsTop=2000;
                for (const auto &word : words) {
                    if ((stock && word.box.top()>864 ? 1 : 0)!=copy) continue;
                    if (word.text.startsWith("GOODS-") || word.text.startsWith("NOTE-")) descriptionBottom=std::max(descriptionBottom,word.box.bottom());
                    if (word.text=="PCS") dimensionsTop=std::min(dimensionsTop,word.box.top());
                }
                require(descriptionBottom+1<dimensionsTop && dimensionsTop<2000,"Description overlaps the dimension table");
            }
        }
        // A standard form has fixed boxes. Oversize data must not silently clip,
        // repaginate the face, or replace an already saved PDF with a partial one.
        auto overflow=awb;
        QStringList notes;
        for (int i=1;i<=80;++i) notes << QString("NOTE-%1: Synthetic shipping instructions for pagination validation.").arg(i,3,10,QChar('0'));
        overflow.values["AWB:EXTENDED_DESCRIPTION"]=notes.join('\n');
        QFile before(temp.path()+"/form.pdf"); require(before.open(QIODevice::ReadOnly),"Read saved form"); const auto original=before.readAll(); before.close();
        bool rejected=false;
        try { exportAirWaybill(overflow,temp.path()+"/form.pdf"); } catch (const std::exception &e) { rejected=QString::fromUtf8(e.what()).contains("does not fit"); }
        require(rejected && before.open(QIODevice::ReadOnly) && before.readAll()==original,"Oversize text must preserve the prior PDF and report its field");
        before.close();
        QFile stockBefore(temp.filePath("text-only.pdf")); require(stockBefore.open(QIODevice::ReadOnly),"Read saved continuous form");
        const auto originalStock=stockBefore.readAll(); stockBefore.close();
        AirWaybillPrintOptions overflowProfile; overflowProfile.preprintedContinuous=true;
        rejected=false;
        try { exportAirWaybill(overflow,temp.filePath("text-only.pdf"),{},overflowProfile); } catch (const std::exception &e) { rejected=QString::fromUtf8(e.what()).contains("does not fit"); }
        require(rejected && stockBefore.open(QIODevice::ReadOnly) && stockBefore.readAll()==originalStock,"Continuous overflow clipped text or replaced the prior PDF");
        stockBefore.close();
        auto mismatch=awb; mismatch.values["AWB:PIECES_TOTAL"]="99"; rejected=false;
        try { exportAirWaybill(mismatch,temp.path()+"/invalid.pdf"); } catch (const std::exception &) { rejected=true; }
        require(rejected && !QFile::exists(temp.path()+"/invalid.pdf"),"Inconsistent shipment totals printed on the form");
        Record bill; bill.source="OBL.TPS"; bill.values={{"OBL:REFERENCE_NUMBER","CARGO-TEST"}};
        QList<Record> cargo;
        for (int i=1;i<=40;++i) cargo.append({0,0,"OBLLINE.TPS",{{"OLI:MARKS_NUMBERS",QString("BOX-%1").arg(i,3,10,QChar('0'))},
            {"OLI:PCS","2"},{"OLI:DESCRIPTION","Synthetic cargo description"},{"OLI:WEIGHT_KGS","20.5"},{"OLI:MEASURE_CBM","0.25"}}});
        exportSummary(bill,temp.path()+"/cargo.pdf",cargo);
        const auto summary=extracted(QString::fromLocal8Bit(argv[1]),temp.path()+"/cargo.pdf");
        for (int i=1;i<=40;++i) require(summary.count(QString("BOX-%1").arg(i,3,10,QChar('0')))==1,"Cargo row lost or duplicated");
        Record vics; vics.source="OBL.TPS";
        vics.values={{"OBL:REFERENCE_NUMBER","VICS-REVIEW"},{"OBL:BL_NUMBER","12345678901234567"},{"OBL:DATE",dateStorage("2026-11-18")},
            {"OBL:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Road","Houston, TX 77032"}},
            {"OBL:CONSIGNEE_ARRAY",QJsonArray{"Example Receiver","456 Test Avenue","Dallas, TX 75201"}},
            {"OBL:SIGNATURE_LINE_1","DO-NOT-REUSE-OCEAN-SIGNATURE"},
            {"NATIVE:VICS_CARRIER","Example Motor Carrier"},{"NATIVE:VICS_TRAILER","TR-123"},{"NATIVE:VICS_SEAL","SEAL-456"},
            {"NATIVE:VICS_SCAC","TEST"},{"NATIVE:VICS_PRO","PRO-789"},{"NATIVE:VICS_TERMS","P"},
            {"NATIVE:VICS_INSTRUCTIONS","Call before delivery. Keep dry."},{"NATIVE:VICS_BILL_TO","Example Freight Payer\n789 Sample Blvd\nHouston, TX 77032"},
            {"NATIVE:VICS_LOADED","S"},{"NATIVE:VICS_COUNTED","S"}};
        for (const auto &key : vicsOrderKeys()) { QJsonArray a; for (int i=0;i<8;++i) a.append(""); vics.values[key]=a; }
        const QStringList order{"PO-1001","12","850.25","Y","Appointment required"};
        for (int i=0;i<5;++i) { auto a=vics.values[vicsOrderKeys()[i]].toArray(); a[0]=order[i]; vics.values[vicsOrderKeys()[i]]=a; }
        Record freight; freight.source="OBLLINE.TPS";
        freight.values={{"OLI:PCS","12"},{"OLI:WEIGHT_LBS","850.25"},{"OLI:WEIGHT_KGS","0"},
            {"OLI:MARKS_NUMBERS","LOT-1001"},{"OLI:DESCRIPTION","Machine parts - keep upright"},
            {"NATIVE:VICS_HANDLING_QTY","2"},{"NATIVE:VICS_HANDLING_TYPE","PLT"},{"NATIVE:VICS_PACKAGE_TYPE","CTN"},
            {"NATIVE:VICS_NMFC","12345"},{"NATIVE:VICS_CLASS","70"}};
        const QList<Record> freightRows{freight};
        exportVicsBillOfLading(vics,temp.filePath("vics.pdf"),freightRows);
        const auto bolText=extracted(QString::fromLocal8Bit(argv[1]),temp.filePath("vics.pdf"));
        for (const auto &needle : {"SHIP FROM","SHIP TO","CUSTOMER ORDER INFORMATION","CARRIER INFORMATION","SHIPPER SIGNATURE / DATE",
                "CARRIER SIGNATURE / PICKUP DATE","Example Exporter","Example Receiver","Example Freight Payer","12345678901234567",
                "PO-1001","850.25 lb","LOT-1001","12345","DRAFT"}) require(bolText.contains(needle),"VICS wording or shipment field missing");
        require(!bolText.contains("DO-NOT-REUSE"),"Ocean signature was reused as a freight attestation");
        QPdfDocument bolPdf; require(bolPdf.load(temp.filePath("vics.pdf"))==QPdfDocument::Error::None && bolPdf.pageCount()==1 && bolPdf.pagePointSize(0)==QSizeF(612,792),"VICS Letter geometry changed");
        rejected=false; try { exportVicsBillOfLading(vics,temp.filePath("vics-overflow.pdf"),cargo); } catch (const std::exception &e) { rejected=QString::fromUtf8(e.what()).contains("supplement"); }
        require(rejected && !QFile::exists(temp.filePath("vics-overflow.pdf")),"VICS overflow lost cargo or altered the supplied layout");
        auto invalidVics=vics; invalidVics.values["NATIVE:VICS_TERMS"]="invalid";
        rejected=false; try { exportVicsBillOfLading(invalidVics,temp.filePath("vics-invalid.pdf"),freightRows); } catch (const std::exception &) { rejected=true; }
        require(rejected && !QFile::exists(temp.filePath("vics-invalid.pdf")),"Unknown freight terms were guessed");
        const auto artifacts=qEnvironmentVariable("SHADOW_LOGIS_PDF_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) { require(QDir().mkpath(artifacts),"PDF artifact directory"); exportVicsBillOfLading(vics,artifacts+"/vics-bill-of-lading-sample.pdf",freightRows); }
        if (argc==3) {
            QPalette light; light.setColor(QPalette::Text,Qt::black); light.setColor(QPalette::Base,Qt::white);
            QApplication::setPalette(light);
            exportAirWaybill(awb,temp.path()+"/light-awb.pdf"); exportSummary(bill,temp.path()+"/light-summary.pdf",cargo);
            exportVicsBillOfLading(vics,temp.filePath("light-vics.pdf"),freightRows);
            const auto uiFont=QApplication::font(); QApplication::setFont(QFont("monospace",24));
            exportAirWaybill(awb,temp.path()+"/other-ui-font.pdf"); QApplication::setFont(uiFont);
            require(rendered(QString::fromLocal8Bit(argv[2]),temp.path()+"/light-awb.pdf",temp.path()+"/ui-font-before")==
                    rendered(QString::fromLocal8Bit(argv[2]),temp.path()+"/other-ui-font.pdf",temp.path()+"/ui-font-after"),
                    "Changing the UI font changed the printed AWB");
            QPalette dark; dark.setColor(QPalette::Text,Qt::white); dark.setColor(QPalette::WindowText,Qt::white);
            dark.setColor(QPalette::Base,Qt::black); dark.setColor(QPalette::Window,Qt::black); QApplication::setPalette(dark);
            exportAirWaybill(awb,temp.path()+"/dark-awb.pdf"); exportSummary(bill,temp.path()+"/dark-summary.pdf",cargo);
            exportVicsBillOfLading(vics,temp.filePath("dark-vics.pdf"),freightRows);
            for (const auto &name : {QString("awb"),QString("summary"),QString("vics")}) {
                const auto a=rendered(QString::fromLocal8Bit(argv[2]),temp.path()+"/light-"+name+".pdf",temp.path()+"/light-"+name);
                const auto b=rendered(QString::fromLocal8Bit(argv[2]),temp.path()+"/dark-"+name+".pdf",temp.path()+"/dark-"+name);
                require(a==b,"PDF text visibility changes with desktop theme");
                int ink=0;
                for (int y=a.height()/4;y<a.height()/2;++y) for (int x=0;x<a.width();++x) if (qGray(a.pixel(x,y))<128) ++ink;
                require(ink>500,"PDF body text has insufficient visible ink");
                if (name=="awb") {
                    int black=0, pixels=0;
                    for (int y=qRound(a.height()*98.0/792);y<qRound(a.height()*143.0/792);++y)
                        for (int x=qRound(a.width()*306.0/612);x<qRound(a.width()*553.0/612);++x) { ++pixels; if (qGray(a.pixel(x,y))<128) ++black; }
                    const double fraction=double(black)/pixels;
                    require(fraction>0.02 && fraction<0.35,"Native form wording is missing or its vector glyphs were incorrectly stroked");
                }
            }
        }
        QTextStream(stdout)<<"PASS: standard AWB and supplied VICS form geometry, shipping/charge fields, overflow refusal without data loss, working-slot exclusion, Unicode, summary pagination";
        QTextStream(stdout)<<(argc==3 ? "; identical visible output under light/dark themes\n" : "; theme raster check skipped (pdftoppm unavailable)\n"); return 0;
    } catch (const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
