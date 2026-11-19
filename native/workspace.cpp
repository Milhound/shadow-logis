#include "workspace.h"
#include "domain.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace shadowlogis {
QStringList workspaceSources() {
    return {"CUSTOMER.TPS","CONTACTS.TPS","BOOKING.TPS","AWB.TPS","OBL.TPS","OBLLINE.TPS",
        "DODR.TPS","DODRLINE.TPS","CERTORGN.TPS","CERTLINE.TPS","SIGHT.TPS","PLIST.TPS","PLLINE.TPS","AIRPORTS.TPS"};
}
QJsonObject workspaceSchema(const QString &source) {
    if(!workspaceSources().contains(source)) throw std::runtime_error("Unknown workspace record type");
    QStringList keys;
    for(const auto &[key,label] : editableFields(source)) if(!key.startsWith("NATIVE:VICS_")) keys<<key;
    const QMap<QString,QString> prefixes{{"CUSTOMER.TPS","CUS"},{"CONTACTS.TPS","CTA"},{"BOOKING.TPS","BOO"},
        {"AWB.TPS","AWB"},{"OBL.TPS","OBL"},{"OBLLINE.TPS","OLI"},{"DODR.TPS","DOD"},{"DODRLINE.TPS","DLI"},
        {"CERTORGN.TPS","CRO"},{"CERTLINE.TPS","CRL"},{"SIGHT.TPS","SIG"},{"PLIST.TPS","PLI"},{"PLLINE.TPS","PLL"},{"AIRPORTS.TPS","AIR"}};
    const auto prefix=prefixes.value(source)+':';
    if(source=="CUSTOMER.TPS" || source=="CONTACTS.TPS") keys<<prefix+"NUMBER";
    else if(source=="BOOKING.TPS") keys<<"BOO:QUOTE_NUMBER";
    else if(source=="AIRPORTS.TPS") keys<<"AIR:AP_NUMBER"<<"AIR:AP_NAME";
    else keys<<prefix+"REFERENCE_NUMBER"<<prefix+"ID_SEQUENCE";
    const bool cargo=QStringList{"OBLLINE.TPS","DODRLINE.TPS","CERTLINE.TPS","PLLINE.TPS"}.contains(source);
    const auto lineKey=cargo ? cargoLineNumberKey(source) : QString();
    if(cargo) keys<<lineKey;
    for(const auto &role : {"SHIPPER","CONSIGNEE","FORWARDER"})
        if(QStringList{"BOOKING.TPS","AWB.TPS","OBL.TPS","CERTORGN.TPS","PLIST.TPS","DODR.TPS"}.contains(source)) keys<<prefix+role+"_NUMBER";
    QMap<QString,int> arrays;
    if(source=="AWB.TPS") {
        for(const auto *key : {"NUMBER_OF_PIECES","GROSS_WEIGHT","KG_LB","CHARGEABLE_WEIGHT","RATE_CLASS","RATE_CHARGE","LINE_TOTAL"}) arrays[prefix+key]=6;
        for(const auto *key : {"DESCRIPTION_QUANTITY","DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT","DESCRIPTION_CFT","COMMODITY_ITEM_NO"}) arrays[prefix+key]=5;
        for(const auto *key : {"OTHER_CHARGE_CODE","OTHER_CHARGE_DESCRIPTIONS","OTHER_CHARGE_PPD_COL","OTHER_CHARGE_AGT_CAR","OTHER_CHARGE_AMOUNTS"}) arrays[prefix+key]=12;
        arrays["AWB:AWB_ACCOUNTING_INFO"]=10; arrays["AWB:NATURE_OF_GOODS"]=10; arrays["AWB:HANDLING_INFORMATION"]=3;
        for(const auto *key : {"AWB_CURRENCY","CARRIER_ACCOUNT","DIMS_STD_METRIC","DIMENSIONED_WEIGHT","MINIMUM_CHARGE","PRINT_AS_AGREED",
                "WEIGHT_VALUE_PPD_COL","OTHER_PPD_COL","DECLARED_VALUE_CARRIAGE","DECLARED_VALUE_CUSTOMS","AMOUNT_INSURANCE","CHARGE_TOTAL"}) keys<<prefix+key;
        for(const auto *key : {"WEIGHT_CHARGE","VALUATION_CHARGE","TAX","DUE_AGENT","DUE_CARRIER","TOTAL"})
            for(const auto *term : {"PPD","COL"}) keys<<prefix+key+'_'+term;
    }
    if(source=="SIGHT.TPS") { arrays["SIG:B_NAME"]=7; arrays["SIG:F_NAME"]=7; arrays["SIG:GENT1"]=5; }
    keys<<arrays.keys(); keys.removeDuplicates(); keys.sort();
    const QSet<QString> numbers{"NUMBER","ID_SEQUENCE","QUOTE_NUMBER","SHIPPER_NUMBER","CONSIGNEE_NUMBER","FORWARDER_NUMBER",
        "PCS","QTY","KGS","LBS","CBM","TOTAL_KGS","TOTAL_LBS","TOTAL_CBM","WEIGHT_KGS","WEIGHT_LBS","MEASURE_CFT","MEASURE_CBM",
        "NUMBER_OF_PIECES","GROSS_WEIGHT","CHARGEABLE_WEIGHT","PIECES_TOTAL","GROSS_WEIGHT_TOTAL","RATE_CHARGE","LINE_TOTAL",
        "DESCRIPTION_QUANTITY","DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT","DESCRIPTION_CFT","DIMENSIONED_WEIGHT",
        "MINIMUM_CHARGE","OTHER_CHARGE_AMOUNTS","CHARGE_TOTAL","USAMOUNT"};
    QJsonArray fields,memos;
    for(const auto &key : keys) {
        const auto suffix=key.section(':',1);
        const bool numeric=(source=="SIGHT.TPS" && (suffix.startsWith("INST_") || QStringList{"COLLECTION","COLLECT_LC","TO_ACT","TO_ASSIST","REFER","REFER2"}.contains(suffix))) || isDateField(key) || numbers.contains(suffix) || (cargo && key==lineKey) ||
            (source=="AWB.TPS" && (suffix.endsWith("_PPD") || suffix.endsWith("_COL")) && suffix!="WEIGHT_VALUE_PPD_COL" && suffix!="OTHER_PPD_COL" && !arrays.contains(key));
        int count=arrays.value(key,1);
        if(key.endsWith("_ARRAY")) count=source=="BOOKING.TPS" ? 5 : 7;
        const bool memo=count==1 && (suffix.contains("COMMENTS") || suffix.contains("INSTRUCTIONS") || suffix=="NOTES" || suffix=="EXTENDED_DESCRIPTION" || key=="NATIVE:FORWARDER_ADDRESS");
        if(memo) memos.append(QJsonObject{{"name",key},{"flags",0},{"length",0}});
        else fields.append(QJsonObject{{"name",key},{"type",numeric ? 8 : 11},{"elements",count},{"length",0},{"decimals",2}});
    }
    return marineSchema(source,{{"fields",fields},{"memos",memos},{"native",true}});
}
QList<ImportCategory> importCategories() {
    return {{"companies","Shippers, consignees and companies","These share one company address book; all company records are imported together.",{"CUSTOMER.TPS"}},
        {"contacts","Contact people","Names, telephone numbers and email addresses.",{"CONTACTS.TPS"}},
        {"bookings","Bookings and shipments","Shipment headers and preserved booking detail records.",{"BOOKING.TPS","BOOKLINE.TPS"}},
        {"air","Air waybills","Saved AWBs, including their cargo, rates and dimensions.",{"AWB.TPS"}},
        {"ocean","Ocean bills of lading","Includes the related cargo lines.",{"OBL.TPS","OBLLINE.TPS"}},
        {"dock","Dock receipts","Includes the related cargo lines.",{"DODR.TPS","DODRLINE.TPS"}},
        {"origin","Certificates of origin","Includes the related cargo lines.",{"CERTORGN.TPS","CERTLINE.TPS"}},
        {"drafts","Sight drafts","Saved draft and collection instructions.",{"SIGHT.TPS"}},
        {"references","Reference directories","The installation's airport and airline directories.",{"AIRPORTS.TPS","AIRLINES.TPS"}},
        {"packing","Archived packing lists","Preserved in Archive; the packing-list tab remains hidden.",{"PLIST.TPS","PLLINE.TPS"},false},
        {"pdfs","Archived PDF documents","Original PDFs inside the selected data folder and its subfolders.",{},false}};
}
QList<InstallationData> findInstallationData(const QString &installation) {
    const QFileInfo root(installation);
    if(!root.isDir() || root.isSymLink()) throw std::runtime_error("Choose an existing installation or data folder, not a symbolic link");
    QSet<QString> known; for(const auto &group : importCategories()) for(const auto &file : group.files) known.insert(file);
    QMap<QString,QStringList> candidates; int visited=0;
    QDirIterator it(root.absoluteFilePath(),QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        const QFileInfo file(it.next());
        if(++visited>100000) throw std::runtime_error("Too many files to scan. Choose the installation folder rather than a whole drive.");
        if(known.contains(file.fileName().toUpper())) candidates[file.absolutePath()]<<file.fileName();
    }
    QList<InstallationData> result;
    for(auto i=candidates.begin();i!=candidates.end();++i) {
        // A cargo/reference-only directory is not a separate shipping installation.
        bool primary=false; for(const auto &f : i.value()) primary|=QStringList{"CUSTOMER.TPS","BOOKING.TPS","CONTACTS.TPS","AWB.TPS","OBL.TPS","DODR.TPS","SIGHT.TPS","CERTORGN.TPS"}.contains(f.toUpper());
        if(primary) result.append({i.key(),i.value()});
    }
    return result;
}
QStringList selectedImportFiles(const InstallationData &data,const QStringList &categories) {
    QSet<QString> wanted; bool pdfs=false;
    for(const auto &id : categories) {
        bool valid=false;
        for(const auto &group : importCategories()) if(group.id==id) { valid=true; pdfs|=id=="pdfs"; for(const auto &f : group.files) wanted.insert(f); }
        if(!valid) throw std::runtime_error("Unknown import category");
    }
    QStringList result; QSet<QString> seen;
    const QDir root(data.directory);
    for(const auto &file : root.entryInfoList(QDir::Files|QDir::NoSymLinks)) if(wanted.contains(file.fileName().toUpper())) {
        const auto canonical=file.fileName().toUpper();
        if(seen.contains(canonical)) throw std::runtime_error("Duplicate database filenames differ only by case. Choose one data snapshot.");
        seen.insert(canonical); result<<file.fileName();
    }
    if(pdfs) {
        QDirIterator it(root.absolutePath(),QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
        while(it.hasNext()) { const auto path=it.next(); if(QFileInfo(path).suffix().compare("pdf",Qt::CaseInsensitive)==0) result<<root.relativeFilePath(path); }
    }
    result.sort(); return result;
}
}
