#include "domain.h"
#include <QDate>
#include <QJsonArray>
#include <QRegularExpression>
#include <stdexcept>
namespace shadowlogis {
QStringList vicsOrderKeys() { return {"NATIVE:VICS_ORDER_NUMBER","NATIVE:VICS_ORDER_PACKAGES","NATIVE:VICS_ORDER_WEIGHT","NATIVE:VICS_ORDER_PALLET","NATIVE:VICS_ORDER_INFO"}; }
QList<QPair<QString,QString>> vicsFields(const QString &source) {
    if (source=="OBLLINE.TPS") return {
        {"NATIVE:VICS_HANDLING_QTY","Handling unit quantity"},{"NATIVE:VICS_HANDLING_TYPE","Handling unit type"},
        {"NATIVE:VICS_PACKAGE_TYPE","Package type"},{"NATIVE:VICS_HM","Hazardous material (X or blank)"},
        {"NATIVE:VICS_NMFC","NMFC number"},{"NATIVE:VICS_CLASS","Freight class"}};
    if (source!="OBL.TPS") return {};
    return {{"NATIVE:VICS_CARRIER","Carrier name"},{"NATIVE:VICS_TRAILER","Trailer number"},
        {"NATIVE:VICS_SEAL","Seal numbers"},{"NATIVE:VICS_SCAC","SCAC"},{"NATIVE:VICS_PRO","Pro number"},
        {"NATIVE:VICS_TERMS","Freight terms (P, C or 3)"},{"NATIVE:VICS_BILL_TO","Third-party freight bill-to address"},
        {"NATIVE:VICS_INSTRUCTIONS","Special instructions"},{"NATIVE:VICS_SID","Shipper SID"},
        {"NATIVE:VICS_CID","Consignee CID"},{"NATIVE:VICS_LOCATION","Delivery location number"},
        {"NATIVE:VICS_FROM_FOB","Ship-from FOB (X or blank)"},{"NATIVE:VICS_TO_FOB","Ship-to FOB (X or blank)"},
        {"NATIVE:VICS_MASTER","Master BOL (X or blank)"},{"NATIVE:VICS_VALUE","Declared value"},
        {"NATIVE:VICS_VALUE_PER","Declared value per"},{"NATIVE:VICS_COD","COD amount"},
        {"NATIVE:VICS_COD_TERMS","COD fee terms (P or C)"},{"NATIVE:VICS_COD_CHECK","Customer check acceptable (X or blank)"},
        {"NATIVE:VICS_LOADED","Trailer loaded by (S or D)"},{"NATIVE:VICS_COUNTED","Freight counted by (S, P or D)"},
        {"NATIVE:VICS_ORDER_NUMBER","Customer order number"},{"NATIVE:VICS_ORDER_PACKAGES","Order packages"},
        {"NATIVE:VICS_ORDER_WEIGHT","Order weight (lb)"},{"NATIVE:VICS_ORDER_PALLET","Pallet/slip (Y or N)"},
        {"NATIVE:VICS_ORDER_INFO","Additional shipper information"}};
}
namespace {
qint64 scaledNumber(const QJsonValue &value, int decimals) {
    const auto text = value.toString().trimmed();
    if (!value.isString() || !QRegularExpression("^[0-9]{1,12}(?:\\.[0-9]+)?$").match(text).hasMatch())
        throw std::runtime_error("Cargo numbers must be nonnegative, with at most 12 digits before the decimal point.");
    auto fraction = text.section('.',1,1);
    while (fraction.endsWith('0')) fraction.chop(1);
    if (fraction.size() > decimals)
        throw std::runtime_error(decimals ? "Cargo weights allow up to two decimal places." : "Pieces must be whole numbers.");
    return (text.section('.',0,0) + fraction.leftJustified(decimals,'0')).toLongLong();
}
QString decimalText(qint64 value) { return QString::number(value/100) + '.' + QString::number(value%100).rightJustified(2,'0'); }
}
QStringList airCargoKeys() { return {"AWB:NUMBER_OF_PIECES", "AWB:GROSS_WEIGHT", "AWB:KG_LB", "AWB:CHARGEABLE_WEIGHT"}; }
AirCargoTotals airCargoTotals(const QJsonObject &values) {
    for (const auto &key : airCargoKeys())
        if (!values[key].isArray() || values[key].toArray().size() != AirCargoStorageSlots)
            throw std::runtime_error("Invalid air waybill cargo storage; no values have been discarded.");
    qint64 pieces = 0, gross = 0; QString unit;
    for (int i=0;i<AirCargoRows;++i) {
        const auto p = scaledNumber(values["AWB:NUMBER_OF_PIECES"].toArray()[i],0);
        const auto w = scaledNumber(values["AWB:GROSS_WEIGHT"].toArray()[i],2);
        const auto c = scaledNumber(values["AWB:CHARGEABLE_WEIGHT"].toArray()[i],2);
        const auto u = values["AWB:KG_LB"].toArray()[i].toString();
        if (!u.isEmpty() && u != "K" && u != "L") throw std::runtime_error("Choose kg or lb for cargo weight units.");
        if (p || w || c) {
            if (u.isEmpty()) throw std::runtime_error("Choose a weight unit for every populated cargo row.");
            if (!unit.isEmpty() && unit != u) throw std::runtime_error("Use one weight unit across the air waybill. Weights are not converted automatically.");
            unit = u;
        }
        pieces += p; gross += w;
    }
    return {QString::number(pieces),decimalText(gross),unit};
}
QStringList companyAddress(const QJsonObject &profile,bool contactDetails) {
    QStringList lines;
    for(const auto &key : {"company_name","address"}) for(const auto &line : profile[key].toString().split('\n')) if(!line.trimmed().isEmpty()) lines<<line.trimmed();
    if(contactDetails) for(const auto &key : {"contact_name","phone","email"}) if(!profile[key].toString().trimmed().isEmpty()) lines<<profile[key].toString().trimmed();
    return lines;
}
Record applyCompanyProfile(Record r,const QJsonObject &profile) {
    if(r.id || profile.isEmpty()) return r;
    r.values["NATIVE:COMPANY_PROFILE"]=profile;
    const auto address=companyAddress(profile,true).join('\n');
    if(r.source=="OBL.TPS" || r.source=="DODR.TPS" || r.source=="CERTORGN.TPS") {
        if(valueText(r.values["NATIVE:FORWARDER_ADDRESS"]).trimmed().isEmpty()) r.values["NATIVE:FORWARDER_ADDRESS"]=r.source=="CERTORGN.TPS" ? companyAddress(profile).join('\n') : address;
    }
    if(r.source=="CERTORGN.TPS" && valueText(r.values["CRO:UNDERSIGNED"]).trimmed().isEmpty())
        r.values["CRO:UNDERSIGNED"]=profile["company_name"].toString();
    if(r.source=="SIGHT.TPS") {
        auto lines=companyAddress(profile,true); const int count=r.values["SIG:F_NAME"].toArray().size();
        if(lines.size()>count) throw std::runtime_error("Company profile exceeds the sight draft's seven contact lines. Combine address lines in Company profile.");
        while(lines.size()<count) lines<<"";
        r.values["SIG:F_NAME"]=QJsonArray::fromStringList(lines);
    }
    if(r.source=="AWB.TPS") {
        if(!profile["customer_number"].toString().isEmpty()) r.values["AWB:FORWARDER_NUMBER"]=profile["customer_number"];
        QStringList contact;
        for(const auto &key : {"contact_name","phone","email"}) if(!profile[key].toString().trimmed().isEmpty()) contact<<profile[key].toString().trimmed();
        if(valueText(r.values["AWB:AWB_ACCOUNTING_INFO"]).trimmed().isEmpty()) r.values["AWB:AWB_ACCOUNTING_INFO"]=contact.join('\n');
    }
    return r;
}
QList<QPair<QString,QString>> sightInstructionFields() {
    return {{"SIG:COLLECTION","For collection"},{"SIG:COLLECT_LC","For payment / negotiation under L/C"},
        {"SIG:INST_1","Deliver documents in one mailing"},{"SIG:INST_2","Deliver documents in two mailings"},
        {"SIG:INST_3","Deliver documents against payment only"},{"SIG:INST_4","Charges for account of drawee"},
        {"SIG:INST_5","Do not waive charges"},{"SIG:INST_6","Protest non-payment"},{"SIG:INST_6A","Protest non-acceptance"},
        {"SIG:INST_7","Do not protest"},{"SIG:INST_8","Present on arrival of goods"},
        {"SIG:INST_9A","Advise non-payment"},{"SIG:INST_9B","Advise non-acceptance"},{"SIG:INST_9C","Send non-payment / acceptance advice by airmail"},{"SIG:INST_9D","Send non-payment / acceptance advice by cable"},
        {"SIG:INST_10A","Advise payment"},{"SIG:INST_10B","Advise acceptance"},{"SIG:INST_10C","Send payment / acceptance advice by airmail"},{"SIG:INST_10D","Send payment / acceptance advice by fax"},
        {"SIG:TO_ACT","Authorize action to release goods"},{"SIG:TO_ASSIST","Authorize assistance without changing terms"},
        {"SIG:REFER","Refer questions to shipper"},{"SIG:REFER2","Refer questions to freight forwarder"}};
}
Record documentFromBooking(const Record &booking, Record target) {
    if (booking.source != "BOOKING.TPS" || !booking.id || target.id ||
        !QStringList{"AWB.TPS","OBL.TPS","DODR.TPS","PLIST.TPS","CERTORGN.TPS","SIGHT.TPS",DangerousGoodsSource}.contains(target.source))
        throw std::runtime_error("Create a document from a saved booking and a blank document.");
    if(!booking.values.value("NATIVE:DELETED_AT").toString().isEmpty()) throw std::runtime_error("Restore the shipment before creating documents.");
    const auto prefix = editableFields(target.source).first().first.section(':',0,0) + ':';
    for (const QString &suffix : {QString("REFERENCE_NUMBER"), QString("BOOKING_NUMBER"), QString("SHIPPER_NUMBER"),
            QString("CONSIGNEE_NUMBER"), QString("SHIPPER_ARRAY"), QString("CONSIGNEE_ARRAY"), QString("SHIPPER_CONTACT"),
            QString("SHIPPER_PHONE"), QString("CONSIGNEE_CONTACT"), QString("CONSIGNEE_PHONE"), QString("VESSEL_NAME"),
            QString("VOYAGE_NUMBER"), QString("LOAD_PORT"), QString("DISCHARGE_PORT"), QString("PLACE_OF_DELIVERY")}) {
        if (!target.values.contains(prefix+suffix) || !booking.values.contains("BOO:"+suffix)) continue;
        auto v = booking.values["BOO:"+suffix];
        if (v.isArray()) {
            auto array = v.toArray(); const auto count = target.values[prefix+suffix].toArray().size();
            if (array.size() > count) throw std::runtime_error("Booking address exceeds the document capacity.");
            while (array.size() < count) array.append("");
            v = array;
        }
        target.values[prefix+suffix] = v;
    }
    target.values["NATIVE:BOOKING_ID"] = QString::number(booking.id);
    if(target.source=="SIGHT.TPS") {
        target.values["SIG:TO_ORDER"]=valueText(booking.values["BOO:SHIPPER_ARRAY"]).section('\n',0,0);
        target.values["SIG:HOW_PAID"]="AT SIGHT";
        target.values["SIG:DATE1"]=QDate::currentDate().toString(Qt::ISODate);
        target.values["SIG:DATE2"]=dateStorage(QDate::currentDate().toString(Qt::ISODate));
    }
    if (target.source == "AWB.TPS") {
        if(booking.values.contains("BOO:CBM")) target.values["NATIVE:CARGO_VOLUME_CBM"]=booking.values["BOO:CBM"];
        const auto array = [&](const QString &key, const QStringList &items) {
            QJsonArray result; for (const auto &item : items) result.append(item);
            const auto count = target.values[key].toArray().size();
            if (result.size() > count) throw std::runtime_error("Booking text exceeds the document capacity.");
            while (result.size() < count) result.append("");
            target.values[key] = result;
        };
        array("AWB:NATURE_OF_GOODS", {valueText(booking.values["BOO:COMMODITY_1"]),valueText(booking.values["BOO:COMMODITY_2"])});
        array("AWB:HANDLING_INFORMATION", {valueText(booking.values["BOO:HAZARDOUS_COMMENTS"])});
        target.values["AWB:EXTENDED_DESCRIPTION"] = valueText(booking.values["BOO:BOOKING_COMMENTS"]);
        target.values["AWB:AIRPORT_OF_ORIGIN"] = valueText(booking.values["BOO:ORIGIN_LOCATION"]);
        target.values["AWB:AIRPORT_OF_DESTINATION"] = valueText(booking.values["BOO:DESTINATION_LOCATION"]);
        const auto setFirst = [&](const QString &key, const QString &v) {
            auto list = target.values[key].toArray(); list[0] = v; target.values[key] = list;
        };
        const auto kg = valueText(booking.values["BOO:KGS"]), lb = valueText(booking.values["BOO:LBS"]);
        const bool useKg = kg.toDouble() != 0 || lb.toDouble() == 0;
        setFirst("AWB:NUMBER_OF_PIECES", valueText(booking.values["BOO:QTY"]));
        setFirst("AWB:GROSS_WEIGHT", useKg ? kg : lb); setFirst("AWB:KG_LB", useKg ? "K" : "L");
        const auto totals = airCargoTotals(target.values);
        target.values["AWB:PIECES_TOTAL"] = totals.pieces; target.values["AWB:GROSS_WEIGHT_TOTAL"] = totals.grossWeight;
    }
    if (target.source == "DODR.TPS") {
        for (const auto &[from,to] : QList<QPair<QString,QString>>{{"PO_NUMBER","PO_NUMBER"},{"CUTOFF_DATE","CUTOFF"},{"SAILING_DATE","SAILING"},{"BOOKING_COMMENTS","INSTRUCTIONS_COMMENTS"}})
            if (booking.values.contains("BOO:"+from)) target.values["DOD:"+to]=booking.values["BOO:"+from];
    }
    if (target.source == DangerousGoodsSource) {
        target.values["DGD:DEPARTURE"] = valueText(booking.values["BOO:ORIGIN_LOCATION"]);
        target.values["DGD:DESTINATION"] = valueText(booking.values["BOO:DESTINATION_LOCATION"]);
        target.values["DGD:HANDLING"] = valueText(booking.values["BOO:HAZARDOUS_COMMENTS"]);
    }
    for (const auto &[from,to] : QList<QPair<QString,QString>>{{"QTY","PCS"},{"LBS","LBS"},{"KGS","KGS"},{"CBM","CBM"},
            {"LBS","TOTAL_LBS"},{"KGS","TOTAL_KGS"},{"CBM","TOTAL_CBM"}})
        if (target.values.contains(prefix+to) && booking.values.contains("BOO:"+from)) target.values[prefix+to]=booking.values["BOO:"+from];
    return target;
}
QPair<QString,QString> cargoSource(const QString &source) {
    if (source == "DODR.TPS") return {"DODRLINE.TPS", "DLI:"};
    if (source == "OBL.TPS") return {"OBLLINE.TPS", "OLI:"};
    if (source == "PLIST.TPS") return {"PLLINE.TPS", "PLL:"};
    if (source == "CERTORGN.TPS") return {"CERTLINE.TPS", "CRL:"};
    return {};
}
QString cargoLineNumberKey(const QString &source) {
    if (source=="DODRLINE.TPS") return "DLI:DO_LINENUMBER";
    if (source=="PLLINE.TPS") return "PLL:PL_LINENUMBER";
    if (source=="OBLLINE.TPS") return "OLI:BL_LINENUMBER";
    if (source=="CERTLINE.TPS") return "CRL:BL_LINENUMBER";
    throw std::runtime_error("Unsupported cargo source.");
}
QJsonObject marineSchema(const QString &source, QJsonObject schema) {
    if(QStringList{"BOOKING.TPS","OBL.TPS","DODR.TPS","PLIST.TPS","CERTORGN.TPS"}.contains(source)) {
        auto fields=schema["fields"].toArray();
        for(int i=0;i<fields.size();++i) {
            auto field=fields[i].toObject();
            const auto suffix=field["name"].toString().section(':',1);
            if(QStringList{"KGS","LBS","TOTAL_KGS","TOTAL_LBS"}.contains(suffix)) { field["type"]=10; field["decimals"]=2; fields[i]=field; }
        }
        schema["fields"]=fields;
    }
    if (source!="OBL.TPS" && source!="DODR.TPS" && source!="CERTORGN.TPS") return schema;
    auto fields=schema["fields"].toArray(), memos=schema["memos"].toArray();
    // Native extensions live in the JSON document only. The preserved TPS
    // schema, raw records and source files are never changed.
    for (const auto &name : {"NATIVE:FORWARDER_ADDRESS","NATIVE:CARRIER_NAME"})
        if(source!="CERTORGN.TPS" || QString(name)=="NATIVE:FORWARDER_ADDRESS") memos.append(QJsonObject{{"name",name},{"flags",0}});
    if (source=="DODR.TPS") {
        for (const auto &name : {"CONSIGNEE_ARRAY","NOTIFY_ARRAY","BL_NUMBER","PIER_TERMINAL","POINT_OF_ORIGIN","PRECARRIAGE_BY","PLACE_OF_RECEIPT","PLACE_OF_DELIVERY"})
            fields.append(QJsonObject{{"name","DOD:"+QString(name)},{"type",18},{"elements",QString(name).endsWith("_ARRAY") ? 7 : 1}});
    }
    schema["fields"]=fields; schema["memos"]=memos; return schema;
}
Record marineDocumentFromDocument(const Record &source, Record target) {
    if (!source.id || target.id || !QStringList{"DODR.TPS","OBL.TPS"}.contains(source.source) ||
        !QStringList{"DODR.TPS","OBL.TPS"}.contains(target.source) || source.source==target.source ||
        recordReference(source).trimmed().compare(recordReference(target).trimmed(),Qt::CaseInsensitive)!=0)
        throw std::runtime_error("Select a marine document belonging to this shipment.");
    const auto from=source.source=="OBL.TPS" ? QString("OBL:") : QString("DOD:");
    const auto to=target.source=="OBL.TPS" ? QString("OBL:") : QString("DOD:");
    for(const auto &suffix : {"SHIPPER_ARRAY","CONSIGNEE_ARRAY","NOTIFY_ARRAY","SHIPPER_NUMBER","CONSIGNEE_NUMBER","NOTIFY_NUMBER","BOOKING_NUMBER","BL_NUMBER","REFERENCE_ARRAY","VESSEL_NAME","VOYAGE_NUMBER","LOAD_PORT","DISCHARGE_PORT","PIER_TERMINAL","POINT_OF_ORIGIN","PRECARRIAGE_BY","PLACE_OF_RECEIPT","PLACE_OF_DELIVERY"}) {
        const auto key=from+suffix;
        if(!source.values.contains(key) || !target.values.contains(to+suffix)) continue;
        auto value=source.values[key];
        if(value.isArray()) {
            auto rows=value.toArray(); const int count=target.values[to+suffix].toArray().size();
            if(rows.size()>count) throw std::runtime_error("Address exceeds the destination form capacity.");
            while(rows.size()<count) rows.append("");
            value=rows;
        }
        target.values[to+suffix]=value;
    }
    for(const auto &key : {"NATIVE:FORWARDER_ADDRESS","NATIVE:CARRIER_NAME"})
        if(source.values.contains(key)) target.values[key]=source.values[key];
    const auto note=source.values[from+(source.source=="OBL.TPS" ? "BL_INSTRUCTIONS" : "INSTRUCTIONS_COMMENTS")];
    if(!note.isUndefined()) target.values[to+(target.source=="OBL.TPS" ? "BL_INSTRUCTIONS" : "INSTRUCTIONS_COMMENTS")]=note;
    target.values["NATIVE:MARINE_SOURCE_ID"]=QString::number(source.id);
    target.values["NATIVE:MARINE_SOURCE_VERSION"]=QString::number(source.version);
    return target;
}
bool isDateField(const QString &key) {
    return QStringList{"BOO:READY_DATE", "BOO:CUTOFF_DATE", "BOO:SAILING_DATE", "BOO:ETA", "BOO:ETD",
        "DOD:DO_DATE", "DOD:CUTOFF", "DOD:SAILING", "OBL:DATE", "PLI:DATE", "CRO:DATE", "CRO:SWORN_DATE", "CRO:COMMISSION_EXPIRES_DATE", "AWB:EXECUTION_DATE", "SIG:DATE2"}.contains(key);
}
QString fieldText(const QString &key, const QJsonValue &value) {
    if (airCargoKeys().contains(key) && value.isArray()) {
        auto rows = value.toArray();
        while (rows.size() > AirCargoRows) rows.removeLast();
        if (key=="AWB:GROSS_WEIGHT" || key=="AWB:CHARGEABLE_WEIGHT")
            for (int i=0;i<rows.size();++i) rows[i]=weightText(valueText(rows[i]));
        return valueText(rows); // Details and history also exclude legacy working slots.
    }
    const auto text = valueText(value);
    if (key=="AWB:GROSS_WEIGHT_TOTAL" || key=="AWB:DIMENSIONED_WEIGHT" || key=="BOO:KGS" || key=="BOO:LBS" ||
        key.endsWith(":WEIGHT_KGS") || key.endsWith(":WEIGHT_LBS")) return weightText(text);
    if (!isDateField(key)) return text;
    bool ok = false; const auto number = text.toLongLong(&ok);
    if (ok && number == 0) return {};
    if (ok && number >= 4 && number <= 2994626) return QDate(1800,12,28).addDays(number).toString(Qt::ISODate);
    return text; // Preserve unusual imported values until the user corrects them.
}
QString dateStorage(const QString &text) {
    if (text.trimmed().isEmpty()) return "0";
    const auto date = QDate::fromString(text, Qt::ISODate);
    if (!date.isValid() || date.year() < 1801 || date.year() > 9999)
        throw std::runtime_error("Enter dates as YYYY-MM-DD, or leave them empty.");
    return QString::number(QDate(1800,12,28).daysTo(date));
}
QString weightText(const QString &text) {
    const auto trimmed=text.trimmed();
    if (!QRegularExpression("^[0-9]+\\.0+$").match(trimmed).hasMatch()) return trimmed;
    return trimmed.section('.',0,0);
}
QString airDestinationLabel(const Record &record,const QList<Record> &airports) {
    const auto stored=valueText(record.values["AWB:AIRPORT_OF_DESTINATION"]).trimmed();
    QString code;
    for (int i=1;i<=3;++i) {
        const auto next=valueText(record.values["AWB:AIRPORT_TO_"+QString::number(i)]).trimmed().toUpper();
        if (!next.isEmpty()) code=next;
    }
    if (!QRegularExpression("^[A-Z]{3}$").match(code).hasMatch()) return stored;
    QString name;
    for (const auto &airport : airports) {
        if (airport.source!="AIRPORTS.TPS" || valueText(airport.values["AIR:AP_NUMBER"]).trimmed().toUpper()!=code) continue;
        const auto match=valueText(airport.values["AIR:AP_NAME"]).simplified();
        if (!name.isEmpty() && name!=match) return stored;
        name=match;
    }
    if (name.isEmpty()) return stored;
    // The old field is 20 bytes, including its string terminator. Do not
    // replace deliberately different destinations merely because codes match.
    if (!stored.isEmpty() && stored.compare(name,Qt::CaseInsensitive)!=0 &&
        !(stored.size()>=19 && name.startsWith(stored,Qt::CaseInsensitive))) return stored;
    const auto parts=QRegularExpression("^(.+?)\\s*\\(([^)]+)\\)(?:,.*)?$").match(name);
    if (parts.hasMatch()) {
        const auto city=parts.captured(1).trimmed();
        auto airport=parts.captured(2);
        airport.remove(QRegularExpression("\\b(?:International|Airport)\\b",QRegularExpression::CaseInsensitiveOption));
        airport=airport.simplified();
        name=city+(airport.isEmpty() || airport.compare(city,Qt::CaseInsensitive)==0 ? QString() : " ("+airport+")");
    }
    return code+" - "+name;
}
QList<QPair<QString,QString>> dangerousGoodsColumns() {
    return {{"un","UN or ID number"},{"name","Proper shipping name"},{"class","Class / division (subsidiary hazard)"},
        {"group","Packing group"},{"quantity","Quantity and type of packing"},{"instruction","Packing instruction"},{"authorization","Authorization"}};
}
QJsonObject dangerousGoodsSchema() {
    QJsonArray fields, memos;
    for (const auto &[key,label] : editableFields(DangerousGoodsSource)) {
        if (key=="DGD:HANDLING" || key=="DGD:EMERGENCY") memos.append(QJsonObject{{"name",key},{"flags",0}});
        else fields.append(QJsonObject{{"name",key},{"type",18},{"elements",key.endsWith("_ARRAY") ? 7 : 1}});
    }
    fields.append(QJsonObject{{"name","DGD:ID_SEQUENCE"},{"type",6},{"elements",1}});
    return {{"fields",fields},{"memos",memos}};
}
Record blankDangerousGoods() {
    Record r; r.source=DangerousGoodsSource;
    for (const auto &[key,label] : editableFields(r.source)) {
        if (key.endsWith("_ARRAY")) r.values[key]=QJsonArray{"","","","","","",""};
        else r.values[key]="";
    }
    r.values["DGD:ID_SEQUENCE"]="0"; r.values["DGD:LINES"]=QJsonArray{};
    return r;
}
QString dangerousGoodsText(const QJsonObject &values) {
    QStringList rows; int index=0;
    for(const auto &row : values["DGD:LINES"].toArray()) {
        QStringList cells;
        for(const auto &[key,label] : dangerousGoodsColumns()) {
            const auto text=row.toObject()[key].toString(); if(!text.isEmpty()) cells<<label+": "+text;
        }
        rows<<"Goods row "+QString::number(++index)+"\n"+cells.join('\n');
    }
    return rows.join("\n\n");
}
void validateDangerousGoods(const QJsonObject &v) {
    const auto fail=[](const QString &message) { throw std::runtime_error(message.toStdString()); };
    for (const auto &[key,label] : editableFields(DangerousGoodsSource)) {
        if (key.endsWith("_ARRAY")) {
            if (!v[key].isArray() || v[key].toArray().size()!=7) fail(label+": expected seven address lines.");
            for (const auto &line : v[key].toArray()) if (!line.isString()) fail(label+": text required.");
        } else if (!v[key].isString()) fail(label+": text required.");
    }
    if (!QStringList{"","Passenger and cargo aircraft","Cargo aircraft only"}.contains(v["DGD:AIRCRAFT"].toString())) fail("Choose an aircraft limitation.");
    if (!QStringList{"","Non-radioactive","Radioactive"}.contains(v["DGD:SHIPMENT_TYPE"].toString())) fail("Choose a shipment type.");
    const auto date=v["DGD:DATE"].toString();
    if (!date.isEmpty() && (!QDate::fromString(date,Qt::ISODate).isValid() || QDate::fromString(date,Qt::ISODate).toString(Qt::ISODate)!=date)) fail("Enter the declaration date as YYYY-MM-DD.");
    if (!v["DGD:LINES"].isArray() || v["DGD:LINES"].toArray().size()>200) fail("A declaration supports up to 200 goods rows.");
    for (const auto &row : v["DGD:LINES"].toArray()) {
        if (!row.isObject() || row.toObject().size()!=7) fail("Invalid dangerous goods row; no data was discarded.");
        for (const auto &[key,label] : dangerousGoodsColumns()) if (!row.toObject()[key].isString()) fail(label+": text required.");
    }
}
Record dangerousGoodsFromAirWaybill(const Record &awb, Record target) {
    if (awb.source!="AWB.TPS" || !awb.id || target.source!=DangerousGoodsSource || target.id)
        throw std::runtime_error("Copy air waybill details into a new declaration.");
    for (const auto &role : {QString("SHIPPER_ARRAY"),QString("CONSIGNEE_ARRAY")}) {
        auto address=awb.values["AWB:"+role].toArray();
        if (address.size()>7) throw std::runtime_error("Air waybill address exceeds declaration capacity.");
        while (address.size()<7) address.append("");
        target.values["DGD:"+role]=address;
    }
    target.values["DGD:AWB_NUMBER"]=valueText(awb.values["AWB:AWB_NUMBER"]);
    target.values["DGD:DEPARTURE"]=valueText(awb.values["AWB:AIRPORT_OF_ORIGIN"]);
    target.values["DGD:DESTINATION"]=valueText(awb.values["AWB:AIRPORT_OF_DESTINATION"]);
    target.values["DGD:HANDLING"]=valueText(awb.values["AWB:HANDLING_INFORMATION"]);
    target.values["NATIVE:AWB_ID"]=QString::number(awb.id);
    return target;
}
QList<QPair<QString, QString>> editableFields(const QString &source) {
    if(source=="SIGHT.TPS") return QList<QPair<QString,QString>>{
        {"SIG:REFERENCE_NUMBER","Shipment reference"},{"SIG:SD_NUMBER","Draft number"},{"SIG:USAMOUNT","Amount (USD)"},
        {"SIG:DATE1","Draft date"},{"SIG:HOW_PAID","Payment terms"},{"SIG:TO_ORDER","Pay to the order of"},
        {"SIG:IN_DOLLARS","Amount in words"},{"SIG:ACCOUNT_OF_1","Account of"},{"SIG:ACCOUNT_OF_2","Account of (continued)"},
        {"SIG:B_NAME","Drawee / bank address"},{"SIG:GENT1","Remitting bank / references"},{"SIG:F_NAME","Contact in case of need"},
        {"SIG:DATE2","Transmittal date"},{"SIG:LC_NO","Letter of credit number"},{"SIG:FOR_FIELD","Collection reference"},
        {"SIG:B_L","B/L originals"},{"SIG:B_LCOPY","B/L copies"},{"SIG:COM_INV","Commercial invoices"},{"SIG:INS_CER","Insurance certificates"},
        {"SIG:CER_ORI","Certificates of origin"},{"SIG:CONS_INV","Consular invoices"},{"SIG:L_C","Letters of credit"},{"SIG:PCK_LIST","Packing lists"},{"SIG:WGHT_CER","Weight certificates"},
        {"SIG:OTHER_DOCS","Other documents"},{"SIG:OTHER_DOCS2","Other documents (continued)"},{"SIG:OTHER_INSTRUCTIONS","Other instructions"},
        {"SIG:SIGHT_SIG","Drawer / authorized signatory name"},{"SIG:SIGHT_SIG2","Transmittal signatory name"},
        {"SIG:DESC1","Additional draft text"},{"SIG:DESC2","Additional draft text (continued)"},
        {"SIG:VALUE1","Additional value text"},{"SIG:VALUE2","Additional value text (continued)"}}+sightInstructionFields();
    if (source == DangerousGoodsSource) return {
        {"DGD:REFERENCE_NUMBER","Shipment reference"},{"DGD:SHIPPER_ARRAY","Shipper name and address"},
        {"DGD:CONSIGNEE_ARRAY","Consignee name and address"},{"DGD:AWB_NUMBER","Air waybill number"},
        {"DGD:DEPARTURE","Airport of departure"},{"DGD:DESTINATION","Airport of destination"},
        {"DGD:AIRCRAFT","Aircraft limitation"},{"DGD:SHIPMENT_TYPE","Shipment type"},
        {"DGD:HANDLING","Additional handling information"},{"DGD:EMERGENCY","Emergency contact / 24-hour number"},
        {"DGD:SIGNATORY","Name of signatory"},{"DGD:DATE","Date"}};
    QString linePrefix;
    if (source == "DODRLINE.TPS") linePrefix = "DLI:";
    if (source == "OBLLINE.TPS") linePrefix = "OLI:";
    if (source == "PLLINE.TPS") linePrefix = "PLL:";
    if (source == "CERTLINE.TPS") linePrefix = "CRL:";
    if (!linePrefix.isEmpty()) return QList<QPair<QString,QString>>{
        {linePrefix+"MARKS_NUMBERS", "Marks / numbers"}, {linePrefix+"PCS", "Packages"},
        {linePrefix+"DESCRIPTION", "Description"}, {linePrefix+"QTY", "Quantity"}, {linePrefix+"UNIT", "Unit"},
        {linePrefix+"WEIGHT_LBS", "Weight (lb)"}, {linePrefix+"WEIGHT_KGS", "Weight (kg)"},
        {linePrefix+"MEASURE_CFT", "Volume (ft3)"}, {linePrefix+"MEASURE_CBM", "Volume (m3)"}} + vicsFields(source);
    if (source == "CUSTOMER.TPS") return {
        {"CUS:NAME","Company name"}, {"CUS:CONTACT","Contact"},
        {"CUS:ADDRESS","Address"}, {"CUS:ADDRESS2","Address line 2"}, {"CUS:SUITE","Suite"},
        {"CUS:CITY_STATE","City / state"}, {"CUS:ZIP","Postal code"}, {"CUS:COUNTRY","Country"},
        {"CUS:PH1_NUMBER","Phone"}, {"CUS:PH2_NUMBER","Alternate phone"},
        {"CUS:EMAIL","Email"}, {"CUS:WEB_ADDRESS","Website"}, {"CUS:NOTES","Notes"}};
    if (source == "CONTACTS.TPS") return {
        {"CTA:FIRST_NAME","First name"}, {"CTA:LAST_NAME","Last name"},
        {"CTA:TITLE","Title"}, {"CTA:PHONE","Phone"}, {"CTA:EMAIL","Email"},
        {"CTA:LOCATION","Location"}, {"CTA:NOTES1","Notes"}, {"CTA:NOTES2","Additional notes"}};
    if (source == "BOOKING.TPS") return {
        {"BOO:REFERENCE_NUMBER","Reference"}, {"BOO:BOOKING_NUMBER","Booking number"},
        {"BOO:PO_NUMBER","Purchase order"}, {"BOO:STATUS","Status"},
        {"BOO:READY_DATE","Ready date"}, {"BOO:CUTOFF_DATE","Cutoff date"},
        {"BOO:SAILING_DATE","Sailing date"}, {"BOO:ETD","Estimated departure"}, {"BOO:ETA","Estimated arrival"},
        {"BOO:CLIENT_CONTACT","Customer contact"}, {"BOO:CLIENT_PHONE","Customer phone"},
        {"BOO:CLIENE_EMAIL","Customer email"},
        {"BOO:SHIPPER_ARRAY","Shipper name and address"}, {"BOO:SHIPPER_CONTACT","Shipper contact"}, {"BOO:SHIPPER_PHONE","Shipper phone"},
        {"BOO:CONSIGNEE_ARRAY","Consignee name and address"}, {"BOO:CONSIGNEE_CONTACT","Consignee contact"}, {"BOO:CONSIGNEE_PHONE","Consignee phone"},
        {"BOO:ORIGIN_LOCATION","Origin"}, {"BOO:DESTINATION_LOCATION","Destination"},
        {"BOO:CARRIER_INDEX","Carrier"}, {"BOO:VESSEL_NAME","Vessel"}, {"BOO:VOYAGE_NUMBER","Voyage"},
        {"BOO:LOAD_PORT","Loading port"}, {"BOO:DISCHARGE_PORT","Discharge port"},
        {"BOO:PLACE_OF_DELIVERY","Place of delivery"}, {"BOO:COMMODITY_1","Goods description"},
        {"BOO:COMMODITY_2","Additional goods description"}, {"BOO:QTY","Packages"},
        {"BOO:LBS","Weight (lb)"}, {"BOO:KGS","Weight (kg)"}, {"BOO:CBM","Volume (m3)"},
        {"BOO:EQUIPMENT_DESCRIPTION","Equipment"}, {"BOO:HAZARDOUS_COMMENTS","Handling notes"},
        {"BOO:BOOKING_COMMENTS","Booking comments"}};
    if (source=="DODR.TPS") return {
        {"DOD:REFERENCE_NUMBER","Shipment reference"},{"DOD:DO_NUMBER","Dock receipt number"},{"DOD:BL_NUMBER","Bill of lading number"},
        {"DOD:BOOKING_NUMBER","Booking number"},{"DOD:PO_NUMBER","Purchase order"},{"DOD:PROJECT_NUMBER","Project number"},
        {"DOD:SHIPPER_ARRAY","Shipper / exporter"},{"DOD:CONSIGNEE_ARRAY","Consignee"},{"DOD:NOTIFY_ARRAY","Notify party"},
        {"NATIVE:FORWARDER_ADDRESS","Forwarding agent / references"},{"DOD:REFERENCE_ARRAY","Export references"},
        {"DOD:TO_NAME","Deliver to / document recipient (legacy)"},{"DOD:PD_NAME","Delivery location (legacy)"},
        {"NATIVE:CARRIER_NAME","Ocean carrier"},{"DOD:PRECARRIAGE_BY","Pre-carriage by"},{"DOD:PLACE_OF_RECEIPT","Place of receipt"},
        {"DOD:VESSEL_NAME","Vessel"},{"DOD:VOYAGE_NUMBER","Voyage"},{"DOD:LOAD_PORT","Loading port"},
        {"DOD:PIER_TERMINAL","Loading pier / terminal"},{"DOD:DISCHARGE_PORT","Discharge port"},{"DOD:PLACE_OF_DELIVERY","Place of delivery"},
        {"DOD:POINT_OF_ORIGIN","Point and country of origin"},{"DOD:ON_NAME","Onward inland routing"},
        {"DOD:DEL_CAR","Delivering carrier / trucker"},{"DOD:PRO_NUM","Truck / pro number"},{"DOD:TEL_NO","Delivery contact phone"},
        {"DOD:CUTOFF","Cutoff date"},{"DOD:SAILING","Sailing date"},{"DOD:DO_DATE","Document date"},
        {"DOD:INSTRUCTIONS_COMMENTS","Routing / delivery instructions"}};
    QString prefix;
    if (source == "AWB.TPS") prefix = "AWB:";
    else if (source == "OBL.TPS") prefix = "OBL:";
    else if (source == "PLIST.TPS") prefix = "PLI:";
    else if (source == "CERTORGN.TPS") prefix = "CRO:";
    if (prefix.isEmpty()) return {};
    QList<QPair<QString, QString>> out{{prefix+"REFERENCE_NUMBER","Reference"},
        {prefix+"SHIPPER_ARRAY","Shipper name and address"}, {prefix+"CONSIGNEE_ARRAY","Consignee name and address"}};
    if (prefix == "AWB:") out += QList<QPair<QString, QString>>{
        {"AWB:AWB_NUMBER","Air waybill"}, {"AWB:MAWB_NUMBER","Master air waybill"},
        {"AWB:SHIPPER_CONTACT","Shipper contact"}, {"AWB:SHIPPER_PHONE","Shipper phone"},
        {"AWB:CONSIGNEE_CONTACT","Consignee contact"}, {"AWB:CONSIGNEE_PHONE","Consignee phone"},
        {"AWB:AIRPORT_OF_ORIGIN","Origin airport"}, {"AWB:AIRPORT_OF_DESTINATION","Destination airport"},
        {"AWB:FIRST_CARRIER","Carrier"}, {"AWB:FLIGHT_DATE1","Flight / date"},
        {"AWB:AIRPORT_FROM","Origin airport code"}, {"AWB:AIRPORT_TO_1","First destination code"},
        {"AWB:AIRPORT_TO_2","Second destination code"}, {"AWB:SECOND_CARRIER","Second carrier"},
        {"AWB:AIRPORT_TO_3","Third destination code"}, {"AWB:THIRD_CARRIER","Third carrier"},
        {"AWB:FLIGHT_DATE2","Second flight / date"}, {"AWB:ULTIMATE_DESTINATION","Ultimate destination"},
        {"AWB:NATURE_OF_GOODS","Nature and quantity of goods"}, {"AWB:HANDLING_INFORMATION","Handling information"},
        {"AWB:NUMBER_OF_PIECES","Pieces by cargo row"}, {"AWB:GROSS_WEIGHT","Gross weight by cargo row"},
        {"AWB:KG_LB","Weight units by cargo row (K=kg, L=lb)"}, {"AWB:CHARGEABLE_WEIGHT","Chargeable weight by cargo row"},
        {"AWB:PIECES_TOTAL","Total pieces"}, {"AWB:GROSS_WEIGHT_TOTAL","Gross weight"},
        {"AWB:EXECUTION_DATE","Execution date"}, {"AWB:EXECUTION_PLACE","Execution place"},
        {"AWB:TYPE_OF_SERVICE","Type of service"}, {"AWB:SERVICE_LEVEL","Service level"},
        {"AWB:EXTENDED_DESCRIPTION","Extended description"},{"AWB:AWB_ACCOUNTING_INFO","Agent accounting / contact information"}};
    else out += QList<QPair<QString, QString>>{
        {prefix+"BL_NUMBER","Bill of lading"}, {prefix+"BOOKING_NUMBER","Booking number"},
        {prefix+"DOCUMENT_NUMBER","Document number"}, {prefix+"VESSEL_NAME","Vessel"},
        {prefix+"VOYAGE_NUMBER","Voyage"}, {prefix+"LOAD_PORT","Loading port"},
        {prefix+"DISCHARGE_PORT","Discharge port"}, {prefix+"PLACE_OF_DELIVERY","Place of delivery"},
        {prefix+"DATE","Document date"}, {prefix+"PCS","Packages"}, {prefix+"LBS","Weight (lb)"},
        {prefix+"KGS","Weight (kg)"}, {prefix+"CBM","Volume (m3)"},
        {prefix+"TOTAL_LBS","Total weight (lb)"}, {prefix+"TOTAL_KGS","Total weight (kg)"},
        {prefix+"TOTAL_CBM","Total volume (m3)"}, {prefix+"BL_INSTRUCTIONS","Instructions"}, {prefix+"NOTES","Notes"}};
    if(source=="OBL.TPS") out += QList<QPair<QString,QString>>{
        {"OBL:MBL_NUMBER","Master bill of lading"},{"OBL:NOTIFY_ARRAY","Notify party"},{"OBL:REFERENCE_ARRAY","Export references"},
        {"NATIVE:FORWARDER_ADDRESS","Forwarding agent / references"},{"NATIVE:CARRIER_NAME","Ocean carrier"},
        {"OBL:ROUTING_ARRAY","Also notify / domestic routing"},{"OBL:ONWARD_ARRAY","Onward inland routing"},
        {"OBL:PRECARRIAGE_BY","Pre-carriage by"},{"OBL:PLACE_OF_RECEIPT","Place of receipt"},{"OBL:PIER_TERMINAL","Loading pier / terminal"},
        {"OBL:POINT_OF_ORIGIN","Point and country of origin"},{"OBL:TYPE_MOVE","Type of move"},
        {"OBL:NUMBER_OF_ORIGINALS","Number of originals"},{"OBL:ISSUED_AT","Place of issue"},{"OBL:PAYABLE_AT","Freight payable at"},
        {"OBL:HBL_RATING_NOTES","Freight / charge notes"}};
    if(source=="CERTORGN.TPS") out += QList<QPair<QString,QString>>{
        {"CRO:NOTIFY_ARRAY","Notify party"},{"CRO:REFERENCE_ARRAY","Export references"},{"NATIVE:FORWARDER_ADDRESS","Forwarding agent / references"},
        {"CRO:ROUTING_ARRAY","Domestic routing / export instructions"},{"CRO:ONWARD_ARRAY","Onward routing"},
        {"CRO:PRECARRIAGE_BY","Pre-carriage by"},{"CRO:PLACE_OF_RECEIPT","Place of receipt"},{"CRO:PIER_TERMINAL","Loading pier / terminal"},
        {"CRO:POINT_OF_ORIGIN","Point (state) of origin / FTZ"},{"CRO:TYPE_MOVE","Type of move"},{"CRO:CONTAINERIZED","Containerized"},
        {"CRO:ULTIMATE_DESTINATION","Ultimate destination"},{"CRO:CONSOLIDATION_NUMBER","Consolidation number"},
        {"CRO:COUNTRY","Country of origin"},{"CRO:UNDERSIGNED","Declarant name / company"},{"CRO:OWNER_AGENT","Owner / agent capacity"},
        {"CRO:DATED_AT","Declaration place"},{"CRO:SWORN_DATE","Sworn date"},{"CRO:COMMISSION_EXPIRES_DATE","Commission expiry date"},
        {"CRO:COC_AREA","Chamber of commerce"},{"CRO:STATE","Chamber state"}};
    return out + vicsFields(source);
}
bool isAddressArray(const QString &key) {
    return key.endsWith("_ARRAY") || key == "AWB:NATURE_OF_GOODS" || key == "AWB:HANDLING_INFORMATION";
}
QString recordReference(const Record &record) {
    if (record.source == "CUSTOMER.TPS") return valueText(record.values.value("CUS:NUMBER"));
    if (record.source == "CONTACTS.TPS") return valueText(record.values.value("CTA:NUMBER"));
    for (auto it = record.values.begin(); it != record.values.end(); ++it)
        if (it.key().endsWith(":REFERENCE_NUMBER")) return valueText(it.value());
    return QString::number(record.id);
}
QString recordTitle(const Record &record) {
    if (record.source == "CUSTOMER.TPS") return valueText(record.values.value("CUS:NAME"));
    if (record.source == "CONTACTS.TPS") return (valueText(record.values.value("CTA:FIRST_NAME"))+" "+valueText(record.values.value("CTA:LAST_NAME"))).trimmed();
    for (auto it = record.values.begin(); it != record.values.end(); ++it)
        if (it.key().endsWith(":SHIPPER_ARRAY")) return valueText(it.value()).section('\n',0,0);
    return recordReference(record);
}
}
