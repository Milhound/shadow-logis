#include "entrylogic.h"
#include "domain.h"
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <limits>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace shadowlogis {
namespace {
void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
qint64 multiply(qint64 a,qint64 b) {
    if(b && a>std::numeric_limits<qint64>::max()/b) fail("Calculation is too large. Reduce the inputs or use a manual amount.");
    return a*b;
}
qint64 add(qint64 a,qint64 b) {
    if(a>std::numeric_limits<qint64>::max()-b) fail("Calculation total is too large.");
    return a+b;
}
qint64 power(int n) { qint64 v=1; while(n-->0) v*=10; return v; }
qint64 number(QString text,int decimals) {
    text=text.trimmed(); if(text.isEmpty()) text="0";
    if(!QRegularExpression("^[0-9]{1,12}(?:\\.[0-9]+)?$").match(text).hasMatch()) fail("Enter a nonnegative number without commas or units.");
    auto fraction=text.section('.',1,1); while(fraction.endsWith('0')) fraction.chop(1);
    if(fraction.size()>decimals) fail(QString("This calculation accepts up to %1 decimal places.").arg(decimals));
    bool ok=false; const auto v=(text.section('.',0,0)+fraction.leftJustified(decimals,'0')).toLongLong(&ok);
    if(!ok) fail("Calculation input is too large.");
    return v;
}
QString decimal(qint64 value,int decimals) {
    if(!decimals) return QString::number(value);
    const auto scale=power(decimals);
    return QString::number(value/scale)+'.'+QString::number(value%scale).rightJustified(decimals,'0');
}
qint64 roundedRatio(qint64 v,qint64 n,qint64 d) {
    auto divisor=std::gcd(v,d); v/=divisor; d/=divisor;
    divisor=std::gcd(n,d); n/=divisor; d/=divisor;
    const auto product=multiply(v,n);
    return add(product/d,product%d>=d/2+d%2 ? 1 : 0);
}
QString raw(const Record &r,const QString &suffix) { return valueText(r.values["AWB:"+suffix]); }
}
QString quantityConversion(const QString &text,const QString &conversion,int decimals) {
    if(decimals<0 || decimals>3) fail("Unsupported conversion precision.");
    // Exact international pound and foot definitions, not rounded reciprocal factors.
    qint64 n=0,d=0;
    if(conversion=="kg_to_lb") { n=100000000; d=45359237; }
    else if(conversion=="lb_to_kg") { n=45359237; d=100000000; }
    else if(conversion=="cbm_to_cft") { n=1000000000000; d=28316846592; }
    else if(conversion=="cft_to_cbm") { n=28316846592; d=1000000000000; }
    else if(conversion=="in_to_cm") { n=254; d=100; }
    else if(conversion=="cm_to_in") { n=100; d=254; }
    else fail("Unknown unit conversion.");
    return decimal(roundedRatio(number(text,3),multiply(n,power(decimals)),multiply(d,1000)),decimals);
}
QString ratedAmount(const QString &weight,const QString &rate) {
    return decimal(roundedRatio(number(weight,2),number(rate,4),10000),2);
}
QString sumAmounts(const QStringList &amounts) {
    qint64 total=0; for(const auto &v : amounts) total=add(total,number(v,2));
    return decimal(total,2);
}
Record calculateAirCharges(Record r) {
    if(r.source!="AWB.TPS") return r;
    const auto settings=r.values.value("NATIVE:AWB_CALCULATIONS").toObject();
    if(settings.isEmpty()) return r;
    const auto modes=settings["lines"].toArray();
    if(!modes.isEmpty() && modes.size()!=AirCargoRows) fail("Invalid AWB calculation settings.");
    auto lines=r.values["AWB:LINE_TOTAL"].toArray();
    const auto rates=r.values["AWB:RATE_CHARGE"].toArray(),weights=r.values["AWB:CHARGEABLE_WEIGHT"].toArray();
    if(lines.size()!=6 || rates.size()!=6 || weights.size()!=6) fail("Invalid AWB rating rows; no values were discarded.");
    for(int i=0;i<AirCargoRows;++i) if(!modes.isEmpty() && modes[i].toBool()) {
        const auto classes=r.values["AWB:RATE_CLASS"].toArray();
        const auto rateClass=(classes.size()>i ? classes[i].toString() : QString()).trimmed().toUpper();
        if(!QStringList{"","N","Q","K"}.contains(rateClass)) fail(QString("Cargo row %1 uses rate class %2. Use a manual line amount for special or minimum rates.").arg(i+1).arg(rateClass));
        lines[i]=ratedAmount(weights[i].toString(),rates[i].toString());
    }
    r.values["AWB:LINE_TOTAL"]=lines; // Sixth legacy working slot is untouched.
    if(settings["weight"].toBool()) {
        airCargoTotals(r.values);
        if(number(raw(r,"MINIMUM_CHARGE"),2)!=0) fail("A minimum charge is stored. Use manual weight charges or clear the minimum after reviewing the rate.");
        QStringList totals; for(int i=0;i<AirCargoRows;++i) {
            if(number(lines[i].toString(),2)>0 && number(weights[i].toString(),2)==0 &&
               number(r.values["AWB:GROSS_WEIGHT"].toArray()[i].toString(),2)==0 &&
               number(r.values["AWB:NUMBER_OF_PIECES"].toArray()[i].toString(),0)==0)
                fail(QString("Cargo row %1 has charges but no cargo. Review it before calculating weight charges.").arg(i+1));
            totals<<lines[i].toString();
        }
        const auto total=sumAmounts(totals),side=raw(r,"WEIGHT_VALUE_PPD_COL").trimmed().toUpper();
        if(side!="P" && side!="C") fail("Choose Prepaid or Collect for weight charges.");
        r.values["AWB:CHARGE_TOTAL"]=total;
        r.values["AWB:WEIGHT_CHARGE_PPD"]=side=="P" ? total : "0.00";
        r.values["AWB:WEIGHT_CHARGE_COL"]=side=="C" ? total : "0.00";
    }
    if(settings["other"].toBool()) {
        const auto amounts=r.values["AWB:OTHER_CHARGE_AMOUNTS"].toArray(),sides=r.values["AWB:OTHER_CHARGE_PPD_COL"].toArray(),accounts=r.values["AWB:OTHER_CHARGE_AGT_CAR"].toArray();
        if(amounts.size()!=12 || sides.size()!=12 || accounts.size()!=12) fail("Invalid other-charge rows.");
        QMap<QString,QStringList> groups;
        QString side;
        for(int i=0;i<12;++i) if(number(amounts[i].toString(),2)>0) {
            const auto s=sides[i].toString().trimmed().toUpper(),a=accounts[i].toString().trimmed().toUpper();
            if((s!="P" && s!="C") || (a!="A" && a!="C")) fail(QString("Other charge %1 needs Prepaid/Collect and Agent/Carrier selections.").arg(i+1));
            if(!side.isEmpty() && side!=s) fail("The AWB has one Other charges payment flag. Use one payment side or select manual other-charge totals.");
            side=s;
            groups["AWB:DUE_"+QString(a=="A" ? "AGENT_" : "CARRIER_")+(s=="P" ? "PPD" : "COL")]<<amounts[i].toString();
        }
        for(const auto &key : {"DUE_AGENT_PPD","DUE_AGENT_COL","DUE_CARRIER_PPD","DUE_CARRIER_COL"}) r.values["AWB:"+QString(key)]=sumAmounts(groups["AWB:"+QString(key)]);
        if(!side.isEmpty()) r.values["AWB:OTHER_PPD_COL"]=side;
        else r.values["AWB:OTHER_PPD_COL"]="";
    }
    if(settings["totals"].toBool()) for(const auto &side : {QString("PPD"),QString("COL")}) {
        QStringList amounts;
        for(const auto &key : {"WEIGHT_CHARGE_","VALUATION_CHARGE_","TAX_","DUE_AGENT_","DUE_CARRIER_","BLANK_"}) amounts<<raw(r,QString(key)+side);
        r.values["AWB:TOTAL_"+side]=sumAmounts(amounts);
    }
    return r;
}
Record calculateEntry(Record r) {
    const auto rules=r.values.value("NATIVE:UNIT_RULES").toObject();
    const auto prefix=editableFields(r.source).isEmpty() ? QString() : editableFields(r.source).first().first.section(':',0,0)+':';
    const bool line=QStringList{"DODRLINE.TPS","OBLLINE.TPS","PLLINE.TPS","CERTLINE.TPS"}.contains(r.source);
    if(r.source=="BOOKING.TPS" || line) {
        for(const auto &group : {QString("weight"),QString("volume")}) {
            const auto rule=rules[group].toString(); if(rule.isEmpty() || rule=="manual") continue;
            QString from,to;
            if(group=="weight" && rule=="kg_to_lb") { from=line ? "WEIGHT_KGS" : "KGS"; to=line ? "WEIGHT_LBS" : "LBS"; }
            else if(group=="weight" && rule=="lb_to_kg") { from=line ? "WEIGHT_LBS" : "LBS"; to=line ? "WEIGHT_KGS" : "KGS"; }
            else if(group=="volume" && line && rule=="cbm_to_cft") { from="MEASURE_CBM"; to="MEASURE_CFT"; }
            else if(group=="volume" && line && rule=="cft_to_cbm") { from="MEASURE_CFT"; to="MEASURE_CBM"; }
            else fail("Unsupported automatic unit conversion.");
            r.values[prefix+to]=quantityConversion(valueText(r.values[prefix+from]),rule,group=="volume" ? 3 : 2);
        }
    }
    if(r.source=="AWB.TPS" && r.values.value("NATIVE:DIMENSION_CALCULATION").toObject()["enabled"].toBool()) r=calculateDimensions(r).record;
    return calculateAirCharges(r);
}
DimensionResult calculateDimensions(Record r) {
    if(r.source!="AWB.TPS") fail("Dimensions require an air waybill.");
    const auto settings=r.values.value("NATIVE:DIMENSION_CALCULATION").toObject();
    const auto unit=raw(r,"DIMS_STD_METRIC");
    if(unit!="S" && unit!="M") fail("Choose inches or centimeters for dimensions.");
    const auto basis=settings["basis"].toString("cm3/kg");
    if(basis!="cm3/kg" && basis!="in3/lb" && basis!="kg/cbm") fail("Choose a volumetric calculation basis.");
    const bool fromCbm=basis=="kg/cbm";
    const auto divisor=number(settings["divisor"].toString(),3);
    if(divisor<=0 || divisor>1000000000) fail("Enter a positive divisor up to 1,000,000.");
    const auto rounding=settings["rounding"].toString("1");
    if(rounding!="1" && rounding!="0.5" && rounding!="0.01") fail("Choose chargeable-weight rounding.");
    const auto dimensionRounding=settings["dimension_rounding"].toString("as_entered");
    if(!QStringList{"as_entered","nearest","up"}.contains(dimensionRounding)) fail("Choose dimension rounding.");
    const auto mapping=settings["cargo_rows"].toArray(),automatic=settings["auto_chargeable"].toArray();
    if(mapping.size()!=5 || automatic.size()!=5) fail("Invalid dimension-to-cargo assignments.");
    auto cft=r.values["AWB:DESCRIPTION_CFT"].toArray();
    for(const auto &suffix : {"DESCRIPTION_QUANTITY","DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT","DESCRIPTION_CFT"})
        if(r.values["AWB:"+QString(suffix)].toArray().size()!=5) fail("Invalid dimension rows; no values were discarded.");
    const auto totals=airCargoTotals(r.values); const bool pounds=totals.unit=="L";
    long double cm3=0; long double volumes[5]={}; qint64 pieces[5]={};
    const auto format=[](long double v,int decimals) {
        if(!std::isfinite(v) || v<0 || v>999999999999.0L) fail("Calculated volume or weight is too large.");
        const auto scale=std::pow(10.0L,decimals);
        return decimal(static_cast<qint64>(std::floor(v*scale+0.5L)),decimals);
    };
    for(int row=0;row<5 && !fromCbm;++row) {
        const auto qty=number(r.values["AWB:DESCRIPTION_QUANTITY"].toArray()[row].toString(),0);
        long double dims[3]={}; bool any=qty>0;
        int i=0;
        for(const auto &suffix : {"DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT"}) {
            dims[i]=number(r.values["AWB:"+QString(suffix)].toArray()[row].toString(),3)/1000.0L; any|=dims[i]>0;
            if(dimensionRounding=="nearest") dims[i]=std::floor(dims[i]+.5L);
            else if(dimensionRounding=="up") dims[i]=std::ceil(dims[i]);
            ++i;
        }
        if(!any) { cft[row]="0.00"; continue; }
        if(!qty || dims[0]<=0 || dims[1]<=0 || dims[2]<=0) fail(QString("Dimension row %1 needs a whole package count and all three positive dimensions.").arg(row+1));
        long double volume=qty*dims[0]*dims[1]*dims[2];
        if(unit=="S") volume*=16.387064L;
        cm3+=volume; cft[row]=format(volume/28316.846592L,2);
        const auto mapped=mapping[row].toInt(-1);
        if(mapped<0 || mapped>=5) fail("Assign each dimension row to a cargo row.");
        volumes[mapped]+=volume; pieces[mapped]=add(pieces[mapped],qty);
    }
    if(fromCbm) {
        const auto cbm=settings["cbm_rows"].toArray(); if(cbm.size()!=5) fail("Enter CBM for each cargo row.");
        for(int row=0;row<5;++row) { volumes[row]=number(cbm[row].toString(),3)*1000.0L; cm3+=volumes[row]; }
    }
    const auto weight=[&](long double volume) {
        long double kg=fromCbm ? volume/1000000.0L*(divisor/1000.0L) : basis=="cm3/kg" ? volume/(divisor/1000.0L) : (volume/16.387064L)/(divisor/1000.0L)*.45359237L;
        return pounds ? kg/.45359237L : kg;
    };
    const auto dimensional=weight(cm3); auto chargeable=r.values["AWB:CHARGEABLE_WEIGHT"].toArray();
    for(int row=0;row<5;++row) if(automatic[row].toBool()) {
        const auto count=number(r.values["AWB:NUMBER_OF_PIECES"].toArray()[row].toString(),0);
        if(!count && !pieces[row] && volumes[row]==0) continue;
        if(fromCbm) {
            if(count<=0 || volumes[row]<=0) fail(QString("Cargo row %1 needs pieces and CBM, or a manual chargeable weight.").arg(row+1));
        } else if(count<=0 || count!=pieces[row]) fail(QString("Cargo row %1 has %2 pieces, but dimensions cover %3. Complete the dimensions or use a manual chargeable weight.").arg(row+1).arg(count).arg(pieces[row]));
        const auto gross=number(r.values["AWB:GROSS_WEIGHT"].toArray()[row].toString(),2)/100.0L;
        const auto greater=std::max(gross,weight(volumes[row])),step=number(rounding,2)/100.0L;
        // Remove only floating point noise at an exact rounding boundary.
        const auto quotient=greater/step;
        const auto tolerance=std::numeric_limits<long double>::epsilon()*std::max(1.0L,std::fabs(quotient))*16;
        chargeable[row]=format(std::ceil(quotient-tolerance)*step,2);
    }
    r.values["AWB:CHARGEABLE_WEIGHT"]=chargeable; r.values["AWB:DESCRIPTION_CFT"]=cft; r.values["AWB:DIMENSIONED_WEIGHT"]=format(dimensional,2);
    return {r,format(cm3/1000000.0L,3),format(cm3/28316.846592L,3),format(dimensional,2),pounds ? "lb" : "kg"};
}
QJsonObject shipmentSuggestions(const Record &booking,const Record &document) {
    if(booking.source!="BOOKING.TPS" || !booking.id || !booking.values["NATIVE:DELETED_AT"].toString().isEmpty()) fail("Choose an active shipment.");
    if((!document.values["NATIVE:BOOKING_ID"].toString().isEmpty() && document.values["NATIVE:BOOKING_ID"].toString()!=QString::number(booking.id)) ||
       recordReference(document).trimmed().compare(recordReference(booking).trimmed(),Qt::CaseInsensitive)!=0) fail("The document belongs to another shipment.");
    const auto prefix=editableFields(document.source).first().first.section(':',0,0)+':';
    QJsonObject out;
    const auto copy=[&](const QString &from,const QString &to) {
        if(!document.values.contains(to) || valueText(booking.values[from]).trimmed().isEmpty()) return;
        auto v=booking.values[from];
        if(document.values[to].isArray()) {
            auto lines=v.isArray() ? v.toArray() : QJsonArray{v}; const auto count=document.values[to].toArray().size();
            while(lines.size()>count && valueText(lines.last()).trimmed().isEmpty()) lines.removeLast();
            if(lines.size()>count) return; // Do not propose a truncated address.
            while(lines.size()<count) lines.append("");
            v=lines;
        }
        if(v!=document.values[to]) out[to]=v;
    };
    for(const auto &suffix : {"BOOKING_NUMBER","PO_NUMBER","SHIPPER_ARRAY","CONSIGNEE_ARRAY","SHIPPER_CONTACT","SHIPPER_PHONE","CONSIGNEE_CONTACT","CONSIGNEE_PHONE","VESSEL_NAME","VOYAGE_NUMBER","LOAD_PORT","DISCHARGE_PORT","PLACE_OF_DELIVERY"}) copy("BOO:"+QString(suffix),prefix+suffix);
    if(document.source=="AWB.TPS") {
        copy("BOO:ORIGIN_LOCATION","AWB:AIRPORT_OF_ORIGIN"); copy("BOO:DESTINATION_LOCATION","AWB:AIRPORT_OF_DESTINATION");
        copy("BOO:HAZARDOUS_COMMENTS","AWB:HANDLING_INFORMATION"); copy("BOO:BOOKING_COMMENTS","AWB:EXTENDED_DESCRIPTION");
    }
    if(document.source=="DODR.TPS") copy("BOO:BOOKING_COMMENTS","DOD:INSTRUCTIONS_COMMENTS");
    return out;
}
}
