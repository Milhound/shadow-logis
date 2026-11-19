#pragma once
// Entirely authored synthetic records. Never reads a local installation or database.
#include "store.h"
#include "domain.h"
#include "documents.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <stdexcept>

inline void createSyntheticWorkspace(const QString &path) {
    using namespace shadowlogis;
    createWorkspace(path);
    {
        Store store(path);
        for(int i=0;i<2;++i) {
            auto customer=store.blankRecord("CUSTOMER.TPS"); customer.values["CUS:NAME"]=i ? "Example Importer" : "Example Exporter";
            customer.values["CUS:ADDRESS"]="100 Example Street"; customer.values["CUS:CITY_STATE"]="Sample City"; customer.values["CUS:COUNTRY"]="United States"; store.save(customer);
            auto person=store.blankRecord("CONTACTS.TPS"); person.values["CTA:FIRST_NAME"]="Example"; person.values["CTA:LAST_NAME"]=QString("Person %1").arg(i+1); store.save(person);
        }
        for(const auto &reference : {"TEST-HISTORY","TEST-PREVIEW","TEST-GOODS"}) {
            auto booking=store.blankRecord("BOOKING.TPS"); booking.values["BOO:REFERENCE_NUMBER"]=reference;
            booking.values["BOO:QTY"]="2"; booking.values["BOO:KGS"]=QString(reference)=="TEST-GOODS" ? "11828.00" : "100.00";
            booking.values["BOO:SHIPPER_ARRAY"]=QJsonArray{"Example Exporter","100 Example Street","Sample City","United States",""};
            booking.values["BOO:CONSIGNEE_ARRAY"]=QJsonArray{"Example Importer","200 Example Street","Sample City","Singapore",""};
            booking=store.record(store.save(booking));
            auto awb=documentFromBooking(booking,store.blankRecord("AWB.TPS")); awb.values.remove("NATIVE:BOOKING_ID");
            awb.values["AWB:AWB_NUMBER"]="000-0000-0000"; awb.values["AWB:AIRPORT_OF_DESTINATION"]="Singapore (Changi In"; awb.values["AWB:AIRPORT_TO_1"]="SIN";
            awb.values["AWB:NATURE_OF_GOODS"]=QJsonArray{"Synthetic machine parts","For software testing only","","","","","","","",""};
            if(QString(reference)=="TEST-GOODS") {
                auto chargeable=awb.values["AWB:CHARGEABLE_WEIGHT"].toArray();
                chargeable[0]="11828.00"; awb.values["AWB:CHARGEABLE_WEIGHT"]=chargeable;
            }
            auto pieces=awb.values["AWB:NUMBER_OF_PIECES"].toArray(); pieces[5]="42"; awb.values["AWB:NUMBER_OF_PIECES"]=pieces;
            store.save(awb);
            store.save(documentFromBooking(booking,store.blankRecord("CERTORGN.TPS")));
        }
    }
    // Read-only directories and archive records are not exposed through Store::save.
    const auto connection=QUuid::createUuid().toString();
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",connection); db.setDatabaseName(path);
        if(!db.open()) throw std::runtime_error("Synthetic database could not open");
        QSqlQuery q(db); q.prepare("INSERT INTO records(source,table_id,values_json) VALUES('AIRPORTS.TPS',1,?)");
        q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"AIR:AP_NUMBER","SIN"},{"AIR:AP_NAME","Singapore (Changi International Airport)"}}).toJson()));
        if(!q.exec()) throw std::runtime_error("Synthetic airport could not be stored");
        q.prepare("INSERT INTO records(source,table_id,values_json) VALUES('AIRPORTS.TPS',1,?)");
        q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonObject{{"AIR:AP_NUMBER","IAH"},{"AIR:AP_NAME","George Bush Intercontinental Airport"}}).toJson()));
        if(!q.exec()) throw std::runtime_error("Synthetic origin airport could not be stored");
        QTemporaryDir tmp; const auto pdf=tmp.filePath("example.pdf");
        Record sample{0,0,"BOOKING.TPS",{{"BOO:REFERENCE_NUMBER","TEST-ARCHIVE"},{"BOO:COMMODITY_1","Synthetic archive document"}}}; exportSummary(sample,pdf,{});
        QFile file(pdf); if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Synthetic PDF could not open");
        q.prepare("INSERT INTO documents(path,sha256,pdf) VALUES('authored-example.pdf','synthetic',?)"); q.addBindValue(file.readAll());
        if(!q.exec()) throw std::runtime_error("Synthetic PDF could not be stored");
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}
