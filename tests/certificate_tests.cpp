#include "documents.h"
#include "domain.h"
#include "privatefile.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QPdfDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>
using namespace shadowlogis;
namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QString extract(const QString &path) {
    QProcess p; p.start("pdftotext",{"-layout",path,"-"});
    check(p.waitForFinished(10000) && p.exitCode()==0,"Certificate extraction failed"); return QString::fromUtf8(p.readAllStandardOutput());
}
void rejects(const std::function<void()> &fn) { bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } check(failed,"Invalid certificate export accepted"); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir tmp; check(tmp.isValid(),"Temporary directory");
        const auto folder=argc==2 ? QString::fromLocal8Bit(argv[1]) : tmp.path(); check(QDir().mkpath(folder),"Output folder");
        Record cert{1,1,"CERTORGN.TPS",{
            {"CRO:REFERENCE_NUMBER","DEMO-CERT-001"},{"CRO:ID_SEQUENCE","7"},{"CRO:BL_NUMBER","DEMO-DOC-001"},{"CRO:BOOKING_NUMBER","BOOK-001"},
            {"CRO:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Road","Houston, TX 77001","United States"}},
            {"CRO:CONSIGNEE_ARRAY",QJsonArray{"Example Importer","456 Harbour Street","Rotterdam","Netherlands"}},
            {"CRO:NOTIFY_ARRAY",QJsonArray{"Example Destination Agent","Rotterdam, Netherlands"}},
            {"CRO:REFERENCE_ARRAY",QJsonArray{"Reference: DEMO-CERT-001","P.O. SAMPLE-PO","Invoice SAMPLE-INV","Letter of credit SAMPLE-LC"}},
            {"CRO:POINT_OF_ORIGIN","Texas"},{"CRO:PRECARRIAGE_BY","Truck"},{"CRO:PLACE_OF_RECEIPT","Houston"},
            {"CRO:VESSEL_NAME","Example Vessel"},{"CRO:VOYAGE_NUMBER","001E"},{"CRO:LOAD_PORT","Houston"},{"CRO:DISCHARGE_PORT","Rotterdam"},
            {"CRO:TYPE_MOVE","Ocean"},{"CRO:PIER_TERMINAL","Example Terminal"},{"CRO:CONTAINERIZED","Yes"},
            {"CRO:ROUTING_ARRAY",QJsonArray{"SYNTHETIC EXAMPLE - NOT FOR SHIPMENT"}},
            {"CRO:COUNTRY","USA AND CANADA"},{"CRO:UNDERSIGNED","Example Exporter"},{"CRO:DATED_AT","Houston, Texas"},
            {"CRO:DATE",dateStorage("2026-11-18")},{"CRO:SWORN_DATE",dateStorage("2026-11-19")},
            {"CRO:COMMISSION_EXPIRES_DATE",dateStorage("2028-03-02")},{"CRO:COC_AREA","Example Chamber of Commerce"},{"CRO:STATE","Texas"},
            {"NATIVE:FORWARDER_ADDRESS","Example Forwarder\n12 Sample Avenue\nHouston, TX\nUnited States"}}};
        Record goods{2,1,"CERTLINE.TPS",{{"CRL:REFERENCE_NUMBER","DEMO-CERT-001"},{"CRL:ID_SEQUENCE","7"},
            {"CRL:PCS","12"},{"CRL:MARKS_NUMBERS","DEMO MARKS\nCartons 1-12"},{"CRL:DESCRIPTION","Precision machine parts\nCountry details supplied by exporter"},
            {"CRL:WEIGHT_KGS","850.00"},{"CRL:WEIGHT_LBS","1873.93"},{"CRL:MEASURE_CBM","2.400"},{"CRL:MEASURE_CFT","84.75"}}};
        auto fraction=goods; fraction.values["CRL:PCS"]="1"; fraction.values["CRL:DESCRIPTION"]="Spare component"; fraction.values["CRL:WEIGHT_KGS"]="8.50";
        fraction.values["CRL:WEIGHT_LBS"]="18.74"; fraction.values["CRL:MEASURE_CBM"]="0"; fraction.values["CRL:MEASURE_CFT"]="0";
        const auto file=folder+"/certificate-of-origin-example.pdf";
        exportCertificateOfOrigin(cert,file,{goods,fraction}); auto text=extract(file);
        for(const auto &value : {"CERTIFICATE OF ORIGIN","DEMO-DOC-001","BOOK-001","Example Exporter","Example Importer","Example Forwarder", "SAMPLE-LC",
                                "Precision machine parts","850 kg","8.50 kg","USA AND CANADA","2026-11-18","2026-11-19","2028-03-02","Example Chamber of Commerce","Authorized Signature"})
            check(text.contains(value),qPrintable(QString("Missing certificate content: ")+value));
        check(!text.contains("Shipment summary") && !text.contains("Prepared with") && !text.contains("DRAFT") && !text.contains("850.00 kg"),"Certificate still contains summary content");
        check(text.count("USA AND CANADA")==2 && !text.contains("originated in the United States"),"Origin was silently replaced");
        check(hasPrivateFilePermissions(file),"Certificate file is not private");
        QPdfDocument pdf; check(pdf.load(file)==QPdfDocument::Error::None && pdf.pageCount()==1 && pdf.pagePointSize(0)==QSizeF(612,792),"Certificate page geometry"); pdf.close();
        auto many=goods; QStringList lines;
        for(int i=0;i<80;++i) lines<<QString("Long goods marker TOKEN%1").arg(i,3,10,QChar('0'));
        many.values["CRL:DESCRIPTION"]=lines.join('\n'); many.values["CRL:MARKS_NUMBERS"]="";
        const auto multi=folder+"/certificate-of-origin-continuation.pdf"; exportCertificateOfOrigin(cert,multi,{many}); text=extract(multi);
        for(int i=0;i<80;++i) check(text.count(QString("TOKEN%1").arg(i,3,10,QChar('0')))==1,"Cargo was lost or duplicated on continuation pages");
        check(text.count("850 kg")==1 && text.count("SIGNATURE OF OWNER OR AGENT")==1,"Continuation repeated quantities or signature block");
        check(pdf.load(multi)==QPdfDocument::Error::None && pdf.pageCount()>1 && text.count("CERTIFICATE OF ORIGIN")==pdf.pageCount(),"Continuation header/page count"); pdf.close();
        auto blank=cert; for(const auto &key : {"COUNTRY","UNDERSIGNED","COC_AREA","STATE","DATED_AT"}) blank.values["CRO:"+QString(key)]="";
        for(const auto &key : {"DATE","SWORN_DATE","COMMISSION_EXPIRES_DATE"}) blank.values["CRO:"+QString(key)]="0";
        exportCertificateOfOrigin(blank,tmp.filePath("blank.pdf")); text=extract(tmp.filePath("blank.pdf"));
        check(!text.contains("USA AND CANADA") && !text.contains("1800-") && !text.contains("expires on:"),"Blank declaration invents origin or dates");
        QFile previous(tmp.filePath("preserve.pdf")); check(previous.open(QIODevice::WriteOnly),"Previous output"); previous.write("sentinel"); previous.close();
        auto wrong=goods; wrong.values["CRL:ID_SEQUENCE"]="8"; rejects([&] { exportCertificateOfOrigin(cert,previous.fileName(),{wrong}); });
        wrong=goods; wrong.source="OBLLINE.TPS"; rejects([&] { exportCertificateOfOrigin(cert,previous.fileName(),{wrong}); });
        auto oversized=cert; oversized.values["CRO:SHIPPER_ARRAY"]=QJsonArray{QString("oversize ").repeated(1000)};
        rejects([&] { exportCertificateOfOrigin(oversized,previous.fileName(),{goods}); });
        check(previous.open(QIODevice::ReadOnly) && previous.readAll()=="sentinel","Failed certificate export replaced existing output");
        rejects([&] { exportCertificateOfOrigin(goods,tmp.filePath("wrong.pdf")); });
        QTextStream(stdout)<<"PASS: certificate form, country/date/party fields, weights, continuation, sequence checks and atomic private output.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
