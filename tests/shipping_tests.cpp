#include "domain.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>
#include <QSet>
#include <QTextStream>
#include <functional>
#include <stdexcept>
using namespace shadowlogis;
namespace {
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
void rejects(const std::function<void()> &action, const char *label) {
    bool caught = false; try { action(); } catch (const std::exception &) { caught = true; } check(caught,label);
}
QJsonObject blankCargo() {
    QJsonObject values;
    for (const auto &key : airCargoKeys()) {
        QJsonArray rows; for (int i=0;i<6;++i) rows.append(key == "AWB:KG_LB" ? "" : "0"); values[key] = rows;
    }
    return values;
}
void set(QJsonObject &v, const QString &key, int row, const QString &text) {
    auto list = v["AWB:"+key].toArray(); list[row] = text; v["AWB:"+key] = list;
}
void cargoTests() {
    check(weightText("11828.00")=="11828","whole kilograms display without decimals");
    check(weightText("8.50")=="8.50" && weightText("0.01")=="0.01","genuine fractional weights retain precision");
    check(weightText("0.00")=="0" && weightText("999999999999.00")=="999999999999","zero and large weights are formatted without floating point");
    check(fieldText("AWB:GROSS_WEIGHT_TOTAL","11828.00")=="11828","stored total display");
    check(fieldText("AWB:WEIGHT_CHARGE_PPD","11828.00")=="11828.00","money precision is unaffected");
    auto v = blankCargo(); auto empty = airCargoTotals(v);
    check(empty.pieces=="0" && empty.grossWeight=="0.00" && empty.unit.isEmpty(),"empty cargo");
    set(v,"NUMBER_OF_PIECES",0,"2"); set(v,"NUMBER_OF_PIECES",1,"3");
    set(v,"GROSS_WEIGHT",0,"0.10"); set(v,"GROSS_WEIGHT",1,"0.20");
    set(v,"KG_LB",0,"K"); set(v,"KG_LB",1,"K");
    auto total = airCargoTotals(v); check(total.pieces=="5" && total.grossWeight=="0.30" && total.unit=="K","exact decimal sum");
    auto legacy = v;
    set(legacy,"NUMBER_OF_PIECES",5,"99"); set(legacy,"GROSS_WEIGHT",5,"444.25");
    set(legacy,"KG_LB",5,"L"); set(legacy,"CHARGEABLE_WEIGHT",5,"999.00");
    const auto actual = airCargoTotals(legacy);
    check(actual.pieces=="5" && actual.grossWeight=="0.30" && actual.unit=="K","legacy working slot is not cargo or a unit conflict");
    for (const auto &key : airCargoKeys()) {
        auto visible = legacy[key].toArray(); visible.removeLast();
        if (key=="AWB:CHARGEABLE_WEIGHT") check(fieldText(key,legacy[key])=="0\n0\n0\n0\n0","whole chargeable weights and working-slot exclusion");
        else check(fieldText(key,legacy[key])==valueText(visible),"details and history must not display the working slot as cargo");
    }
    for (const auto &bad : QStringList{"-1","1e3","nan","9999999999999","0.001"}) {
        auto trial = v; set(trial,"GROSS_WEIGHT",0,bad); rejects([&]{airCargoTotals(trial);},"bad weight rejected");
    }
    auto trial = v; set(trial,"NUMBER_OF_PIECES",0,"1.5"); rejects([&]{airCargoTotals(trial);},"fractional pieces rejected");
    trial = v; set(trial,"KG_LB",1,"L"); rejects([&]{airCargoTotals(trial);},"mixed units rejected");
    trial = v; set(trial,"KG_LB",1,""); rejects([&]{airCargoTotals(trial);},"missing units rejected");
    trial = v; trial["AWB:GROSS_WEIGHT"]=QJsonArray{"1"}; rejects([&]{airCargoTotals(trial);},"short array rejected");
    trial = v; set(trial,"KG_LB",0,"L"); set(trial,"KG_LB",1,"L"); check(airCargoTotals(trial).unit=="L","pound weights supported");
    trial = v; set(trial,"GROSS_WEIGHT",0,"999999999999.99"); set(trial,"GROSS_WEIGHT",1,"0.01");
    check(airCargoTotals(trial).grossWeight=="1000000000000.00","large exact sum without overflow");
    trial = v; set(trial,"CHARGEABLE_WEIGHT",2,"4"); rejects([&]{airCargoTotals(trial);},"chargeable-only row needs unit");
}
void destinationTests() {
    Record awb{0,0,"AWB.TPS",{{"AWB:AIRPORT_OF_DESTINATION","Singapore (Changi In"},
        {"AWB:AIRPORT_TO_1","HKG"},{"AWB:AIRPORT_TO_2","SIN"},{"AWB:AIRPORT_TO_3",""}}};
    const Record airport{0,0,"AIRPORTS.TPS",{{"AIR:AP_NUMBER","SIN"},{"AIR:AP_NAME","Singapore (Changi International Airport), Singapore"}}};
    const auto original=awb.values;
    check(airDestinationLabel(awb,{airport})=="SIN - Singapore (Changi)","complete truncated destination using final routing airport");
    check(awb.values==original,"airport formatting leaves imported fields unchanged");
    check(airDestinationLabel(awb,{})=="Singapore (Changi In","missing airport directory preserves source");
    auto conflict=airport; conflict.values["AIR:AP_NAME"]="Different airport";
    check(airDestinationLabel(awb,{airport,conflict})=="Singapore (Changi In","ambiguous airport directory is not guessed");
    awb.values["AWB:AIRPORT_OF_DESTINATION"]="Custom delivery point";
    check(airDestinationLabel(awb,{airport})=="Custom delivery point","custom destination is not replaced by directory");
    awb.values["AWB:AIRPORT_OF_DESTINATION"]="Singapore (Changi In"; awb.values["AWB:AIRPORT_TO_3"]="LHR";
    check(airDestinationLabel(awb,{airport})=="Singapore (Changi In","transit airport is not mistaken for final destination");
}
void bookingTests() {
    Record blank{0,0,"AWB.TPS",blankCargo()};
    for (const auto &key : {"REFERENCE_NUMBER","SHIPPER_NUMBER","CONSIGNEE_NUMBER","SHIPPER_CONTACT","SHIPPER_PHONE","CONSIGNEE_CONTACT","CONSIGNEE_PHONE"}) blank.values["AWB:"+QString(key)]="";
    blank.values["AWB:SHIPPER_ARRAY"]=QJsonArray{"","","","","","",""};
    blank.values["AWB:CONSIGNEE_ARRAY"]=QJsonArray{"","","","","","",""};
    blank.values["AWB:NATURE_OF_GOODS"]=QJsonArray{"","","","","","","","","",""};
    blank.values["AWB:HANDLING_INFORMATION"]=QJsonArray{"","",""};
    blank.values["AWB:RATE_CHARGE"]=QJsonArray{"0","0","0","0","0","0"};
    Record booking{42,1,"BOOKING.TPS",{{"BOO:REFERENCE_NUMBER","DEMO-BOOK"},{"BOO:SHIPPER_ARRAY",QJsonArray{"Example Exporter","Street","City","Country","Last line"}},
        {"BOO:KGS","100"},{"BOO:LBS","220"},{"BOO:QTY","5"},{"BOO:COMMODITY_1","Machine parts"},{"BOO:COMMODITY_2","Fragile"},
        {"BOO:HAZARDOUS_COMMENTS","Keep upright"},{"BOO:BOOKING_COMMENTS","Delivery appointment required"},{"BOO:SECRET","Never copy"}}};
    const auto doc = documentFromBooking(booking,blank);
    check(doc.values["NATIVE:BOOKING_ID"]=="42","explicit link retained");
    check(doc.values["AWB:REFERENCE_NUMBER"]=="DEMO-BOOK","reference copied");
    check(doc.values["AWB:SHIPPER_ARRAY"].toArray().size()==7 && doc.values["AWB:SHIPPER_ARRAY"].toArray()[4]=="Last line","full address preserved and padded");
    check(doc.values["AWB:NATURE_OF_GOODS"].toArray()[1]=="Fragile","goods copied");
    check(doc.values["AWB:EXTENDED_DESCRIPTION"]=="Delivery appointment required","booking notes copied");
    check(doc.values["AWB:PIECES_TOTAL"]=="5" && doc.values["AWB:GROSS_WEIGHT_TOTAL"]=="100.00","booking cargo copied");
    check(!doc.values.contains("AWB:SECRET") && doc.values["AWB:RATE_CHARGE"]==blank.values["AWB:RATE_CHARGE"],"only shipping fields copied");
    booking.values["BOO:KGS"]="0"; const auto pounds = documentFromBooking(booking,blank);
    check(airCargoTotals(pounds.values).unit=="L" && pounds.values["AWB:GROSS_WEIGHT_TOTAL"]=="220.00","pounds retained without conversion");
    booking.id = 0; rejects([&]{documentFromBooking(booking,blank);},"unsaved booking rejected");
}
void linkTests() {
    QTemporaryDir temp; const auto path=temp.path()+"/links.sqlite";
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","link-fixture"); db.setDatabaseName(path); check(db.open(),"open link fixture");
        QSqlQuery q(db);
        check(q.exec("PRAGMA application_id=1329681230"),"application id");
        check(q.exec("PRAGMA user_version=1"),"schema version");
        check(q.exec("CREATE TABLE records(id INTEGER PRIMARY KEY,version INTEGER,source TEXT,values_json TEXT)"),"link records schema");
        const auto insert=[&](qint64 id,const QString &source,const QString &prefix,const QString &reference,qint64 parent=0) {
            QJsonObject values{{prefix+":REFERENCE_NUMBER",reference},{prefix+":ID_SEQUENCE",QString::number(id)}};
            if (parent) values["NATIVE:BOOKING_ID"]=QString::number(parent);
            q.prepare("INSERT INTO records VALUES(?,1,?,?)"); q.addBindValue(id); q.addBindValue(source);
            q.addBindValue(QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact))); check(q.exec(),"insert link fixture");
        };
        insert(1,"BOOKING.TPS","BOO","SHIP-001"); insert(2,"BOOKING.TPS","BOO"," ship-002 ");
        insert(3,"BOOKING.TPS","BOO",""); insert(4,"BOOKING.TPS","BOO","DUPE"); insert(5,"BOOKING.TPS","BOO"," dupe ");
        insert(10,"AWB.TPS","AWB"," ship-001 "); insert(11,"AWB.TPS","AWB","SHIP-001");
        insert(12,"OBL.TPS","OBL","SHIP-001"); insert(13,"PLIST.TPS","PLI","SHIP-001"); insert(14,"CERTORGN.TPS","CRO","SHIP-001");
        insert(15,"AWB.TPS","AWB","SHIP-001",2); insert(16,"AWB.TPS","AWB","ANOTHER-REFERENCE",1);
        insert(17,"AWB.TPS","AWB","SHIP-002"); insert(18,"AWB.TPS","AWB",""); insert(19,"AWB.TPS","AWB","DUPE");
        insert(20,"AWB.TPS","AWB","SHIP-001-EXTRA"); insert(21,"INVOICE.TPS","INV","SHIP-001");
        check(q.exec("CREATE TABLE record_history(record_id INTEGER,version INTEGER,values_json TEXT,saved_at TEXT)"),"history fixture");
        check(q.exec("UPDATE records SET values_json=json_set(values_json,'$.\"BOO:STATUS\"','Legacy status') WHERE id=1"),"status fixture");
        db.close();
    }
    QSqlDatabase::removeDatabase("link-fixture");
    QFile before(path); check(before.open(QIODevice::ReadOnly),"read link fixture"); const auto bytes=before.readAll(); before.close();
    Store store(path); const auto links=store.linkedDocuments(1); QSet<qint64> ids;
    for (const auto &link : links) {
        ids.insert(link.record.id);
        check(link.match==(link.record.id==16 ? DocumentMatch::NativeLink : DocumentMatch::ShipmentReference),"match provenance");
    }
    check(ids==QSet<qint64>{10,11,12,13,14,16},"all document types and sequences matched without unrelated references");
    const auto second=store.linkedDocuments(2); ids.clear(); for (const auto &link : second) ids.insert(link.record.id);
    check(ids==QSet<qint64>{15,17},"explicit link takes precedence over another reference");
    check(store.linkedDocuments(3).isEmpty(),"empty references never match each other");
    for (const auto booking : {4,5}) {
        const auto ambiguous=store.linkedDocuments(booking);
        check(ambiguous.size()==1 && ambiguous[0].record.id==19 && ambiguous[0].match==DocumentMatch::AmbiguousReference,"duplicate booking reference labeled for review");
    }
    rejects([&]{store.linkedDocuments(10);},"document is not a booking");
    check(before.open(QIODevice::ReadOnly) && before.readAll()==bytes,"reference lookup does not rewrite migrated records");
    before.close();
    const auto original=store.record(1); const auto document=store.record(16);
    store.setShipmentDeleted(original,true); const auto deleted=store.record(1);
    check(deleted.values["BOO:STATUS"]=="Legacy status" && deleted.version==original.version+1,"delete preserves status and increments version");
    check(store.deletedShipments().size()==1 && store.deletedShipments()[0].id==1,"deleted shipments list");
    check(store.records({"BOOKING.TPS"}).size()==4 && store.records({"BOOKING.TPS"},true).size()==5,"active filter retains physical rows");
    check(store.record(16).values==document.values && store.linkedDocuments(1).size()==links.size(),"delete keeps linked document data");
    rejects([&]{store.setShipmentDeleted(original,true);},"stale deletion rejected");
    rejects([&]{store.save(deleted);},"deleted shipment cannot be edited");
    auto bypass=deleted; bypass.values.remove("NATIVE:DELETED_AT"); rejects([&]{store.save(bypass);},"save cannot bypass restore");
    Record fresh{0,0,"AWB.TPS",blankCargo()}; fresh.values["NATIVE:BOOKING_ID"]="1"; fresh.values["AWB:REFERENCE_NUMBER"]="SHIP-001";
    rejects([&]{store.save(fresh);},"new document for deleted shipment rejected");
    rejects([&]{documentFromBooking(deleted,Record{0,0,"AWB.TPS",blankCargo()});},"copy from deleted shipment rejected");
    store.setShipmentDeleted(deleted,false);
    check(store.record(1).values==original.values && store.deletedShipments().isEmpty(),"restore recovers exact shipment fields");
    check(store.history(1).size()==2 && store.history(1)[0].values==deleted.values && store.history(1)[1].values==original.values,"delete and restore retain history");
    auto spoof=document; spoof.source="BOOKING.TPS"; rejects([&]{store.setShipmentDeleted(spoof,true);},"document cannot be deleted as shipment");
    // Deleted duplicate references must not reassign historical document ownership.
    store.setShipmentDeleted(store.record(4),true);
    check(store.linkedDocuments(5)[0].match==DocumentMatch::AmbiguousReference,"deletion changed ambiguous legacy ownership");
}

}
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    try { cargoTests(); bookingTests(); linkTests(); destinationTests(); QTextStream(stdout)<<checks<<" shipping checks passed\n"; return 0; }
    catch (const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
