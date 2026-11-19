#include "documents.h"
#include "domain.h"
#include <QApplication>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QPdfDocument>
#include <QProcess>
#include <QPalette>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QTextStream>
#include <functional>
using namespace shadowlogis;
namespace {
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
void rejects(const std::function<void()> &fn) { bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } check(failed,"Invalid declaration accepted"); }
QString extract(const QString &path) { QProcess p; p.start("pdftotext",{"-layout",path,"-"}); check(p.waitForFinished(10000) && !p.exitCode(),"Text extraction failed"); return QString::fromUtf8(p.readAllStandardOutput()); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir temp; check(temp.isValid(),"No temporary directory");
        auto blank=blankDangerousGoods(); validateDangerousGoods(blank.values);
        Record booking{1,1,"BOOKING.TPS",{{"BOO:REFERENCE_NUMBER","DEMO-DG-01"},
            {"BOO:SHIPPER_ARRAY",QJsonArray{"Example Exporter","123 Sample Road","Chicago, IL","United States",""}},
            {"BOO:CONSIGNEE_ARRAY",QJsonArray{"Example Importer","456 Sample Road","Montréal, QC","Canada",""}},
            {"BOO:ORIGIN_LOCATION","Chicago O'Hare (ORD)"},{"BOO:DESTINATION_LOCATION","Montréal (YUL)"},
            {"BOO:HAZARDOUS_COMMENTS","Keep upright."},{"BOO:COMMODITY_1","Do not infer a classification"}}};
        auto dg=documentFromBooking(booking,blank); validateDangerousGoods(dg.values);
        check(dg.values["DGD:SHIPPER_ARRAY"].toArray().size()==7 && dg.values["DGD:SHIPPER_ARRAY"].toArray()[0]=="Example Exporter","Full party transfer");
        check(dg.values["DGD:LINES"].toArray().isEmpty() && dg.values["DGD:AIRCRAFT"]=="" && dg.values["DGD:SHIPMENT_TYPE"]=="","DG data guessed from booking");
        Record awb{3,1,"AWB.TPS",{{"AWB:AWB_NUMBER","000-12345675"},{"AWB:SHIPPER_ARRAY",dg.values["DGD:SHIPPER_ARRAY"]},
            {"AWB:CONSIGNEE_ARRAY",dg.values["DGD:CONSIGNEE_ARRAY"]},{"AWB:AIRPORT_OF_ORIGIN","ORD - Chicago"},
            {"AWB:AIRPORT_OF_DESTINATION","YUL - Montréal"},{"AWB:HANDLING_INFORMATION",QJsonArray{"Review carrier requirements"}}}};
        dg=dangerousGoodsFromAirWaybill(awb,dg);
        check(dg.values["DGD:AWB_NUMBER"]=="000-12345675" && dg.values["NATIVE:AWB_ID"]=="3" && dg.values["NATIVE:BOOKING_ID"]=="1","AWB transfer lost identity");
        check(dg.values["DGD:SIGNATORY"]=="" && dg.values["DGD:DATE"]=="","Signature details inferred");
        QJsonObject row{{"un","UN 1845"},{"name","Carbon dioxide, solid"},{"class","9"},{"group",""},{"quantity","1 fibreboard box x\n8.50 kg"},{"instruction","954"},{"authorization",""}};
        dg.values["DGD:LINES"]=QJsonArray{row}; dg.values["DGD:AIRCRAFT"]="Passenger and cargo aircraft"; dg.values["DGD:SHIPMENT_TYPE"]="Non-radioactive";
        dg.values["DGD:HANDLING"]="SYNTHETIC TEST DATA - NOT FOR SHIPMENT"; dg.values["DGD:EMERGENCY"]="Example contact / +1 202 555 0100";
        dg.values["DGD:SIGNATORY"]="Example Signatory"; dg.values["DGD:DATE"]="2026-11-18";
        validateDangerousGoods(dg.values);
        auto bad=dg.values; bad["DGD:AIRCRAFT"]="Unknown"; rejects([&] { validateDangerousGoods(bad); });
        bad=dg.values; bad["DGD:DATE"]="2026-02-30"; rejects([&] { validateDangerousGoods(bad); });
        bad=dg.values; bad["DGD:LINES"]=QJsonArray{QJsonObject{{"un","UN 1845"}}}; rejects([&] { validateDangerousGoods(bad); });
        // Persistence uses a native schema even when no TPS DG datasets exist.
        const auto dbPath=temp.filePath("native.sqlite");
        { auto db=QSqlDatabase::addDatabase("QSQLITE","dg-fixture"); db.setDatabaseName(dbPath); check(db.open(),"Open fixture"); QSqlQuery q(db);
          for(const auto &sql : {"PRAGMA application_id=1329681230","PRAGMA user_version=1", "CREATE TABLE datasets(source TEXT,table_id INTEGER,schema_json TEXT)",
            "CREATE TABLE records(id INTEGER PRIMARY KEY,version INTEGER DEFAULT 1,source TEXT,table_id INTEGER,values_json TEXT)",
            "CREATE TABLE record_history(record_id INTEGER,version INTEGER,values_json TEXT,saved_at TEXT)",
            "INSERT INTO records(id,source,values_json) VALUES(1,'BOOKING.TPS','{\"BOO:REFERENCE_NUMBER\":\"DEMO-DG-01\"}')"}) check(q.exec(sql),"Fixture schema"); db.close(); }
        QSqlDatabase::removeDatabase("dg-fixture");
        { Store store(dbPath); check(store.sources().contains(DangerousGoodsSource),"Native DG source missing");
          const auto id=store.save(dg); auto saved=store.record(id); check(saved.values["DGD:LINES"]==dg.values["DGD:LINES"],"Goods rows lost on save");
          check(store.linkedDocuments(1).size()==1,"DG link missing");
          saved.values["DGD:HANDLING"]="Revised handling"; store.save(saved); check(store.history(id).size()==1,"DG history missing");
          rejects([&] {store.save(saved);});
          check(store.save(dg)>id && store.linkedDocuments(1).size()==2,"Separate declarations need separate sequences");
        }
        { Store reopen(dbPath); check(reopen.records({DangerousGoodsSource}).size()==2,"Saved declarations not reopened"); }
        const auto path=temp.filePath("declaration.pdf"); exportDangerousGoods(dg,path);
        QPdfDocument pdf; check(pdf.load(path)==QPdfDocument::Error::None && pdf.pageCount()==1,"DG PDF load");
        check(qAbs(pdf.pagePointSize(0).width()-595)<1 && qAbs(pdf.pagePointSize(0).height()-842)<1,"DG A4 form geometry");
        QPalette dark=app.palette(); dark.setColor(QPalette::Text,Qt::white); dark.setColor(QPalette::WindowText,Qt::white); dark.setColor(QPalette::Base,Qt::black); app.setPalette(dark);
        const auto darkPath=temp.filePath("dark.pdf"); exportDangerousGoods(dg,darkPath);
        QPdfDocument darkPdf; check(darkPdf.load(darkPath)==QPdfDocument::Error::None,"Dark-theme declaration load");
        check(pdf.render(0,{595,842})==darkPdf.render(0,{595,842}),"Declaration changed with desktop palette");
        const auto text=extract(path);
        for(const auto &field : {"SHIPPER'S DECLARATION","UN 1845","Carbon dioxide, solid","8.50 kg","954","Example Signatory","2026-11-18","SYNTHETIC TEST DATA","DRAFT"}) if(!text.contains(field)) throw std::runtime_error((QString("DG form field missing: ")+field).toStdString());
        check(text.count("Example Signatory")==1,"Name duplicated as signature");
        QJsonArray rows; for(int i=0;i<25;++i) { auto next=row; next["name"]=QString("ROW-%1: synthetic goods row").arg(i,2,10,QChar('0')); rows.append(next); }
        auto multi=dg; multi.values["DGD:LINES"]=rows; const auto multiPath=temp.filePath("multipage.pdf"); exportDangerousGoods(multi,multiPath);
        QPdfDocument multipage; check(multipage.load(multiPath)==QPdfDocument::Error::None && multipage.pageCount()>1,"DG rows must paginate");
        const auto all=extract(multiPath); for(int i=0;i<25;++i) check(all.count(QString("ROW-%1:").arg(i,2,10,QChar('0')))==1,"Row lost or repeated during pagination");
        check(all.count("SHIPPER'S DECLARATION")==multipage.pageCount(),"Headers not repeated");
        QFile before(path); check(before.open(QIODevice::ReadOnly),"Read saved PDF"); const auto original=before.readAll(); before.close();
        auto huge=dg; auto longRow=row; longRow["name"]=QString("Long goods description ").repeated(500); huge.values["DGD:LINES"]=QJsonArray{longRow}; rejects([&]{exportDangerousGoods(huge,path);});
        check(before.open(QIODevice::ReadOnly) && before.readAll()==original,"Failed export replaced existing PDF");
        const auto blankPath=temp.filePath("blank.pdf"); exportDangerousGoods(blank,blankPath);
        if(argc==2) { QDir().mkpath(QString::fromLocal8Bit(argv[1])); const auto output=QString::fromLocal8Bit(argv[1]);
            check(QFile::copy(path,output+"/dangerous-goods-example.pdf"),"Copy example"); check(QFile::copy(blankPath,output+"/dangerous-goods-blank.pdf"),"Copy blank"); check(QFile::copy(multiPath,output+"/dangerous-goods-multipage.pdf"),"Copy multipage"); }
        QTextStream(stdout)<<"PASS: DG party transfer, explicit choices, native persistence/reopen/history/conflicts, sequence links, A4 template, all fields, multipage rows, blank form, atomic overflow rejection.\n"; return 0;
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
