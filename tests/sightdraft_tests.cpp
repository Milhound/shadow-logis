#include "documents.h"
#include "domain.h"
#include "companyprofile.h"
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QPdfDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>
using namespace shadowlogis;
namespace {
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
void rejects(const std::function<void()> &fn) { bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } check(failed,"Invalid export accepted"); }
QString extract(const QString &path) { QProcess p; p.start("pdftotext",{"-layout",path,"-"}); check(p.waitForFinished(10000) && !p.exitCode(),"PDF extraction"); return QString::fromUtf8(p.readAllStandardOutput()); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir temp; const QString folder=argc==2 ? QString::fromLocal8Bit(argv[1]) : temp.path(); check(QDir().mkpath(folder),"Output directory");
        check(sightDraftAmount("353,437.00")=="353,437.00" && sightDraftAmount("0.1")=="0.10","Exact USD formatting");
        check(sightDraftAmountWords("353437.00")=="THREE HUNDRED FIFTY-THREE THOUSAND FOUR HUNDRED THIRTY-SEVEN AND 00/100","Amount in words");
        check(sightDraftAmountWords("0.01")=="ZERO AND 01/100","Cent precision");
        for(const auto &bad : {"-1","1,23","1e5","1.001","NaN"}) rejects([&] { sightDraftAmount(bad); });
        Record sight{0,0,"SIGHT.TPS",{{"SIG:REFERENCE_NUMBER","DEMO-SIGHT-001"},{"SIG:SD_NUMBER","DEMO-123"},{"SIG:USAMOUNT","353437.00"},
            {"SIG:DATE1","2026-11-18"},{"SIG:DATE2",dateStorage("2026-11-18")},{"SIG:HOW_PAID","AT SIGHT"},{"SIG:TO_ORDER","Example Exporter Inc."},
            {"SIG:ACCOUNT_OF_1","Example Importer Ltd."},{"SIG:B_NAME",QJsonArray{"Example Drawee Bank","123 Sample Street","Example City","Canada"}},
            {"SIG:GENT1",QJsonArray{"Example Remitting Bank","Reference DEMO-REF","Documentary Credit DEMO-LC"}},
            {"SIG:F_NAME",QJsonArray{"Example Logistics","456 Sample Road","Houston, TX 77032","Example Contact","Phone 555-0100","contact@example.invalid"}},
            {"SIG:COLLECT_LC","1"},{"SIG:B_L","3"},{"SIG:B_LCOPY","2"},{"SIG:COM_INV","3/2"},{"SIG:CER_ORI","1/2"},{"SIG:PCK_LIST","3/2"},{"SIG:WGHT_CER","1/2"},
            {"SIG:OTHER_DOCS","Certificate of quality"},{"SIG:OTHER_DOCS2","Shipment advice 1/1"},{"SIG:OTHER_INSTRUCTIONS","SYNTHETIC EXAMPLE - NOT FOR SHIPMENT\nNo payment or bank details are real."},
            {"SIG:SIGHT_SIG","Example Drawer"},{"SIG:SIGHT_SIG2","Example Signatory"}}};
        for(const auto &[key,label] : sightInstructionFields()) if(!sight.values.contains(key)) sight.values[key]="0";
        for(const auto &key : {"SIG:INST_1","SIG:INST_4","SIG:INST_5","SIG:INST_9B","SIG:TO_ASSIST","SIG:REFER2"}) sight.values[key]="1";
        const auto path=folder+"/sight-draft-example.pdf"; exportSightDraft(sight,path);
        QPdfDocument pdf; check(pdf.load(path)==QPdfDocument::Error::None && pdf.pageCount()==2,"Two sight draft exchange copies");
        const auto first=pdf.getAllText(0).text(), second=pdf.getAllText(1).text();
        check(first.contains("First of Exchange (Second Unpaid)") && !first.contains("Second of Exchange (First Unpaid)") && second.contains("Second of Exchange (First Unpaid)"),"Exchange copy wording");
        check(pdf.pagePointSize(0)==QSizeF(612,792) && pdf.pagePointSize(1)==QSizeF(612,792),"Sight draft Letter size"); pdf.close();
        const auto text=extract(path);
        for(const auto &needle : {"353,437.00","Example Drawee Bank","DEMO-123","Example Logistics","Example Drawer","Example Signatory","SYNTHETIC EXAMPLE","THREE HUNDRED"}) check(text.count(needle)>=2,"Sight draft field omitted on an exchange copy");
        check(!text.contains("(c)") && !text.contains("Shadow Logis"),"Product footer printed");
        QFile prior(path); check(prior.open(QIODevice::ReadOnly),"Read output"); const auto bytes=prior.readAll(); prior.close();
        auto overflow=sight; overflow.values["SIG:OTHER_INSTRUCTIONS"]=QString("Too much detail ").repeated(1000); rejects([&] { exportSightDraft(overflow,path); });
        check(prior.open(QIODevice::ReadOnly) && prior.readAll()==bytes,"Failed export replaced prior sight draft"); prior.close();
        auto invalid=sight; invalid.values["SIG:INST_1"]="2"; rejects([&] { exportSightDraft(invalid,path); });
        // Logo normalization, portable embedded bytes, and a branded dock header.
        QImage logo(400,120,QImage::Format_ARGB32); logo.fill(QColor("#16344b")); QPainter paint(&logo); paint.setPen(Qt::white); paint.setFont(QFont("sans-serif",32)); paint.drawText(logo.rect(),Qt::AlignCenter,"EXAMPLE"); paint.end();
        const auto logoPath=temp.filePath("logo.png"); check(logo.save(logoPath),"Synthetic logo"); const auto png=companyLogo(logoPath); QFile::remove(logoPath);
        QJsonObject profile{{"company_name","Example Logistics"},{"address","456 Sample Road\nHouston, TX 77032"},{"contact_name","Example Contact"},{"phone","555-0100"},{"email","contact@example.invalid"},{"iata","TEST-IATA"},{"logo_png",QString::fromLatin1(png.toBase64())}};
        Record dock{0,0,"DODR.TPS",{{"DOD:REFERENCE_NUMBER","DEMO-DOCK-001"},{"DOD:DO_NUMBER","DR-123"},{"DOD:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Street"}}}};
        dock=applyCompanyProfile(dock,profile); exportDockReceipt(dock,folder+"/branded-dock-receipt-example.pdf");
        const auto dockText=extract(folder+"/branded-dock-receipt-example.pdf"); check(dockText.contains("Example Logistics") && dockText.contains("contact@example.invalid") && dockText.contains("DR-123"),"Company header details omitted");
        QFile doc(folder+"/branded-dock-receipt-example.pdf"); check(doc.open(QIODevice::ReadOnly) && doc.readAll().contains("/Subtype /Image"),"Logo was not embedded");
        auto saved=dock; saved.id=1; auto other=profile; other["company_name"]="Changed"; check(applyCompanyProfile(saved,other).values==saved.values,"Default profile rewrote saved document");
        auto air=applyCompanyProfile(Record{0,0,"AWB.TPS",{}},profile); check(air.values["NATIVE:COMPANY_PROFILE"].toObject()==profile,"Company profile snapshot missing");
        rejects([&] { companyLogo(temp.filePath("missing.png")); });
        QTextStream(stdout)<<"PASS: exact amounts, two exchange copies, field preservation, atomic overflow, instruction flags, portable logo and profile snapshots.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
