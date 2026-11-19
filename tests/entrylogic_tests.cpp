#include "entrylogic.h"
#include "domain.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTextStream>
#include <functional>
#include <stdexcept>
using namespace shadowlogis;
namespace {
int count=0;
void check(bool ok,const char *message) { ++count; if(!ok) throw std::runtime_error(message); }
void rejects(const std::function<void()> &f,const char *message) { bool threw=false; try { f(); } catch(const std::exception &) { threw=true; } check(threw,message); }
void cell(Record &r,const QString &suffix,int index,const QString &text) { auto a=r.values["AWB:"+suffix].toArray(); a[index]=text; r.values["AWB:"+suffix]=a; }
Record air() {
    Record r{1,1,"AWB.TPS",{}};
    for(const auto &key : airCargoKeys()+QStringList{"AWB:RATE_CHARGE","AWB:LINE_TOTAL","AWB:RATE_CLASS"}) {
        QJsonArray a; for(int i=0;i<6;++i) a.append(key=="AWB:KG_LB" || key=="AWB:RATE_CLASS" ? "" : "0"); r.values[key]=a;
    }
    for(const auto &key : {"DESCRIPTION_QUANTITY","DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT","DESCRIPTION_CFT"}) r.values["AWB:"+QString(key)]=QJsonArray{"0","0","0","0","0"};
    for(const auto &key : {"OTHER_CHARGE_AMOUNTS","OTHER_CHARGE_PPD_COL","OTHER_CHARGE_AGT_CAR"}) {
        QJsonArray a; for(int i=0;i<12;++i) a.append(QString(key)=="OTHER_CHARGE_AMOUNTS" ? "0" : ""); r.values["AWB:"+QString(key)]=a;
    }
    cell(r,"NUMBER_OF_PIECES",0,"2"); cell(r,"GROSS_WEIGHT",0,"100"); cell(r,"CHARGEABLE_WEIGHT",0,"100"); cell(r,"KG_LB",0,"K");
    r.values["AWB:WEIGHT_VALUE_PPD_COL"]="P"; r.values["AWB:DIMS_STD_METRIC"]="M";
    return r;
}
void mathTests() {
    check(ratedAmount("11828","9")=="106452.00","legacy example rate multiplication");
    check(ratedAmount("0.50","0.01")=="0.01","half-cent rounds up");
    check(ratedAmount("1.25","0.008")=="0.01","sub-cent rates are exact");
    check(sumAmounts({"0.10","0.20","10.25"})=="10.55","money sum is exact");
    check(quantityConversion("100","kg_to_lb",2)=="220.46","kg to lb");
    check(quantityConversion("1","lb_to_kg",2)=="0.45","lb to kg");
    check(quantityConversion("8.5","kg_to_lb",2)=="18.74","fractional kg is preserved");
    check(quantityConversion("1","cbm_to_cft",3)=="35.315","cbm to cft");
    check(quantityConversion("35.315","cft_to_cbm",3)=="1.000","cft to cbm");
    check(quantityConversion("47","in_to_cm",3)=="119.380","inches to cm");
    for(const auto &bad : {"-1","nan","1,000","1e3","1.001"}) rejects([&] { sumAmounts({bad}); },"invalid money rejected");
    rejects([] { ratedAmount("999999999999.99","999999999999.99"); },"money overflow rejected");
    auto r=air(); r.values["AWB:TOTAL_PPD"]="123.45"; auto original=r.values;
    check(calculateEntry(r).values==original,"imported values without modes remain unchanged");
    cell(r,"RATE_CHARGE",0,"2.50"); cell(r,"LINE_TOTAL",5,"999");
    r.values["NATIVE:AWB_CALCULATIONS"]=QJsonObject{{"lines",QJsonArray{true,false,false,false,false}},{"weight",true},{"other",true},{"totals",true}};
    cell(r,"OTHER_CHARGE_AMOUNTS",0,"10.25"); cell(r,"OTHER_CHARGE_PPD_COL",0,"P"); cell(r,"OTHER_CHARGE_AGT_CAR",0,"A"); r.values["AWB:TAX_PPD"]="1.25";
    auto result=calculateEntry(r);
    check(result.values["AWB:LINE_TOTAL"].toArray()[0]=="250.00" && result.values["AWB:LINE_TOTAL"].toArray()[5]=="999","line rating and sixth slot preserved");
    check(result.values["AWB:TOTAL_PPD"]=="261.50" && result.values["AWB:TOTAL_COL"]=="0.00","charge totals include fees and tax");
    r.values["AWB:WEIGHT_VALUE_PPD_COL"]="C"; result=calculateEntry(r); check(result.values["AWB:TOTAL_COL"]=="250.00" && result.values["AWB:TOTAL_PPD"]=="11.50","collect weight and prepaid fees separated");
    auto settings=r.values["NATIVE:AWB_CALCULATIONS"].toObject(); settings["lines"]=QJsonArray{false,false,false,false,false}; r.values["NATIVE:AWB_CALCULATIONS"]=settings; cell(r,"LINE_TOTAL",0,"200");
    check(calculateEntry(r).values["AWB:TOTAL_COL"]=="200.00","manual line amount participates in totals");
    r.values["AWB:MINIMUM_CHARGE"]="300"; rejects([&] { calculateEntry(r); },"minimum rate requires explicit manual handling"); r.values["AWB:MINIMUM_CHARGE"]="0";
    settings["lines"]=QJsonArray{true,false,false,false,false}; r.values["NATIVE:AWB_CALCULATIONS"]=settings; cell(r,"RATE_CLASS",0,"B"); rejects([&] { calculateEntry(r); },"basic charge is not mistaken for rate per unit");
    cell(r,"RATE_CLASS",0,"N"); cell(r,"OTHER_CHARGE_PPD_COL",0,""); rejects([&] { calculateEntry(r); },"fee payment classification required");
    Record booking{1,1,"BOOKING.TPS",{{"BOO:REFERENCE_NUMBER","T"},{"BOO:KGS","8.50"},{"BOO:LBS","999"},{"BOO:STATUS","old"}}};
    check(calculateEntry(booking).values==booking.values,"manual unit values preserved"); booking.values["NATIVE:UNIT_RULES"]=QJsonObject{{"weight","kg_to_lb"}};
    auto converted=calculateEntry(booking); check(converted.values["BOO:LBS"]=="18.74" && converted.values["BOO:KGS"]=="8.50" && converted.values["BOO:STATUS"]=="old","opt-in conversion preserves source and status");
}
void dimensionTests() {
    auto r=air(); cell(r,"DESCRIPTION_QUANTITY",0,"2"); cell(r,"DESCRIPTION_LENGTH",0,"100"); cell(r,"DESCRIPTION_WIDTH",0,"100"); cell(r,"DESCRIPTION_HEIGHT",0,"100");
    auto settings=QJsonObject{{"enabled",true},{"basis","cm3/kg"},{"divisor","6000"},{"rounding","1"},{"cargo_rows",QJsonArray{0,0,0,0,0}},{"auto_chargeable",QJsonArray{true,false,false,false,false}}}; r.values["NATIVE:DIMENSION_CALCULATION"]=settings;
    auto d=calculateDimensions(r);
    check(d.cubicMeters=="2.000" && d.cubicFeet=="70.629" && d.dimensionalWeight=="333.33","volumetric formula and volume units");
    check(d.record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="334.00","round chargeable weight up to whole kg");
    auto cbmSettings=settings; cbmSettings["basis"]="kg/cbm"; cbmSettings["divisor"]="167"; cbmSettings["cbm_rows"]=QJsonArray{"2","0","0","0","0"};
    auto cbmRecord=r; cbmRecord.values["NATIVE:DIMENSION_CALCULATION"]=cbmSettings;
    check(calculateDimensions(cbmRecord).dimensionalWeight=="334.00","direct CBM uses 167 rather than 1000000/6000");
    cbmSettings["cbm_rows"]=QJsonArray{"1.5","0","0","0","0"}; cbmSettings["rounding"]="0.5"; cbmRecord.values["NATIVE:DIMENSION_CALCULATION"]=cbmSettings;
    check(calculateDimensions(cbmRecord).dimensionalWeight=="250.50" && calculateDimensions(cbmRecord).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="250.50","fractional CBM and rounding");
    cell(cbmRecord,"KG_LB",0,"L"); check(calculateDimensions(cbmRecord).dimensionalWeight=="552.26","CBM kg converted to cargo pound unit");
    settings["rounding"]="0.5"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; check(calculateDimensions(r).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="333.50","half-unit rounding");
    settings["rounding"]="0.01"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; check(calculateDimensions(r).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="333.34","fractional rounding does not undercharge");
    cell(r,"GROSS_WEIGHT",0,"450.50"); check(calculateDimensions(r).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="450.50","larger actual weight wins and genuine fraction retained");
    settings["divisor"]="5000"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; check(calculateDimensions(r).dimensionalWeight=="400.00","divisor override");
    settings["divisor"]="6000"; settings["rounding"]="1"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings;
    cell(r,"GROSS_WEIGHT",0,"100"); cell(r,"DESCRIPTION_LENGTH",0,"50"); cell(r,"DESCRIPTION_WIDTH",0,"60"); cell(r,"DESCRIPTION_HEIGHT",0,"100");
    check(calculateDimensions(r).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="100.00","exact boundary does not round an extra kg");
    cell(r,"NUMBER_OF_PIECES",0,"3"); rejects([&] { calculateDimensions(r); },"incomplete dimension coverage blocked"); cell(r,"NUMBER_OF_PIECES",0,"2");
    settings["auto_chargeable"]=QJsonArray{false,false,false,false,false}; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; cell(r,"CHARGEABLE_WEIGHT",0,"123.45"); check(calculateDimensions(r).record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="123.45","manual chargeable override retained");
    r.values["AWB:DIMS_STD_METRIC"]="S"; cell(r,"DESCRIPTION_LENGTH",0,"10"); cell(r,"DESCRIPTION_WIDTH",0,"10"); cell(r,"DESCRIPTION_HEIGHT",0,"10"); check(calculateDimensions(r).cubicMeters=="0.033","inches converted to cubic meters");
    settings["basis"]="in3/lb"; settings["divisor"]="166"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; cell(r,"KG_LB",0,"L"); check(calculateDimensions(r).dimensionalWeight=="12.05","imperial divisor and pound cargo");
    // Separate sizes and rates have explicit assignments, never the whole volume twice.
    r=air(); cell(r,"NUMBER_OF_PIECES",0,"1"); cell(r,"DESCRIPTION_QUANTITY",0,"1"); cell(r,"DESCRIPTION_LENGTH",0,"100"); cell(r,"DESCRIPTION_WIDTH",0,"100"); cell(r,"DESCRIPTION_HEIGHT",0,"100");
    cell(r,"NUMBER_OF_PIECES",1,"1"); cell(r,"KG_LB",1,"K"); cell(r,"GROSS_WEIGHT",1,"200"); cell(r,"DESCRIPTION_QUANTITY",1,"1"); cell(r,"DESCRIPTION_LENGTH",1,"60"); cell(r,"DESCRIPTION_WIDTH",1,"50"); cell(r,"DESCRIPTION_HEIGHT",1,"40");
    r.values["NATIVE:DIMENSION_CALCULATION"]=QJsonObject{{"enabled",true},{"basis","cm3/kg"},{"divisor","6000"},{"rounding","1"},{"cargo_rows",QJsonArray{0,1,0,0,0}},{"auto_chargeable",QJsonArray{true,true,false,false,false}}};
    r.values["NATIVE:AWB_CALCULATIONS"]=QJsonObject{{"lines",QJsonArray{true,true,false,false,false}},{"weight",true},{"totals",true}}; cell(r,"RATE_CHARGE",0,"2"); cell(r,"RATE_CHARGE",1,"3");
    const auto full=calculateEntry(r); check(full.values["AWB:CHARGEABLE_WEIGHT"].toArray()[0]=="167.00" && full.values["AWB:CHARGEABLE_WEIGHT"].toArray()[1]=="200.00","dimension rows allocated to rate groups"); check(full.values["AWB:TOTAL_PPD"]=="934.00","dimensions feed rating and totals in dependency order");
    settings=r.values["NATIVE:DIMENSION_CALCULATION"].toObject(); settings["divisor"]="0"; r.values["NATIVE:DIMENSION_CALCULATION"]=settings; rejects([&] { calculateEntry(r); },"zero divisor rejected");
}
void suggestionTests() {
    Record booking{1,1,"BOOKING.TPS",{{"BOO:REFERENCE_NUMBER","T"},{"BOO:SHIPPER_ARRAY",QJsonArray{"New shipper","Address",""}},{"BOO:ORIGIN_LOCATION","Houston"},{"BOO:KGS","100"}}};
    auto r=air(); r.values["AWB:REFERENCE_NUMBER"]="T"; r.values["AWB:SHIPPER_ARRAY"]=QJsonArray{"Manual shipper","",""}; r.values["AWB:AIRPORT_OF_ORIGIN"]="";
    const auto previous=r.values; const auto hints=shipmentSuggestions(booking,r);
    check(hints["AWB:AIRPORT_OF_ORIGIN"]=="Houston" && hints["AWB:SHIPPER_ARRAY"].toArray()[0]=="New shipper","shared details proposed");
    check(!hints.contains("AWB:GROSS_WEIGHT") && r.values==previous,"suggestions do not overwrite document or cargo");
    r.values["NATIVE:BOOKING_ID"]="2"; rejects([&] { shipmentSuggestions(booking,r); },"unrelated shipment cannot fill document");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try { mathTests(); dimensionTests(); suggestionTests(); QTextStream(stdout)<<count<<" entry calculation checks passed\n"; }
    catch(const std::exception &e) { QTextStream(stderr)<<e.what()<<'\n'; return 1; }
}
