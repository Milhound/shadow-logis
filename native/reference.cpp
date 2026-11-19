#include "reference.h"
#include "airlines.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace shadowlogis {
ReferenceTable countryReference() {
    ReferenceTable table{"Countries / territories","Country names and two-letter codes from Qt/CLDR locale data.",{}};
    for(int i=1;i<=QLocale::LastTerritory;++i) {
        const auto territory=static_cast<QLocale::Territory>(i);
        const auto code=QLocale::territoryToCode(territory);
        if(code.size()==2) table.rows.append({code,QLocale::territoryToString(territory),{}});
    }
    std::sort(table.rows.begin(),table.rows.end(),[](const auto &a,const auto &b){return a.name<b.name;});
    return table;
}
ReferenceTable currencyReference() {
    ReferenceTable table{"Currency codes","Currency codes and associated territories from Qt/CLDR locale data. This lookup does not provide exchange rates.",{}};
    QMap<QString,QSet<QString>> countries;
    for(const auto &locale : QLocale::matchingLocales(QLocale::AnyLanguage,QLocale::AnyScript,QLocale::AnyTerritory)) {
        const auto code=locale.currencySymbol(QLocale::CurrencyIsoCode);
        if(QRegularExpression("^[A-Z]{3}$").match(code).hasMatch() && code!="XXX") countries[code].insert(QLocale::territoryToString(locale.territory()));
    }
    for(auto it=countries.begin();it!=countries.end();++it) {
        auto places=it.value().values(); places.sort();
        table.rows.append({it.key(),places.join(", "),"Currency code"});
    }
    return table;
}
ReferenceTable airportReference(const Store &store) {
    ReferenceTable table{"Airport codes","Imported airport directory. Search by code, airport, city or country in its description. Coverage and names reflect the migrated directory; manual entry is always available.",{},false};
    QSet<QString> seen;
    if(store.sources().contains("AIRPORTS.TPS")) for(const auto &r : store.records({"AIRPORTS.TPS"})) {
        const auto code=valueText(r.values.value("AIR:AP_NUMBER")).trimmed().toUpper();
        const auto name=valueText(r.values.value("AIR:AP_NAME")).trimmed();
        const auto identity=code+'\n'+name;
        if(QRegularExpression("^[A-Z]{3}$").match(code).hasMatch() && !name.isEmpty() && !seen.contains(identity)) {
            seen.insert(identity); table.rows.append({code,name,"Imported directory"});
        }
    }
    return table;
}
ReferenceTable incotermReference() {
    ReferenceTable table{"Incoterms 2020","Code/name reference only. Include the agreed named place or port and edition in the contract. These terms are separate from prepaid/collect freight payment. Incoterms is a trademark of ICC. <a href=\"https://2go.iccwbo.org/incoterms-2020-app\">ICC reference</a>",{
        {"EXW","Ex Works","Any transport mode"},{"FCA","Free Carrier","Any transport mode"},
        {"CPT","Carriage Paid To","Any transport mode"},{"CIP","Carriage and Insurance Paid To","Any transport mode"},
        {"DAP","Delivered at Place","Any transport mode"},{"DPU","Delivered at Place Unloaded","Any transport mode"},
        {"DDP","Delivered Duty Paid","Any transport mode"},{"FAS","Free Alongside Ship","Sea / inland waterway"},
        {"FOB","Free On Board","Sea / inland waterway"},{"CFR","Cost and Freight","Sea / inland waterway"},
        {"CIF","Cost, Insurance and Freight","Sea / inland waterway"}}};
    return table;
}
ReferenceDialog::ReferenceDialog(const ReferenceTable &reference,bool select,QWidget *parent) : QDialog(parent) {
    setObjectName("referenceLookup"); setWindowTitle(reference.title); resize(860,580);
    auto *layout=new QVBoxLayout(this);
    auto *search=new QLineEdit; search->setObjectName("referenceSearch"); search->setClearButtonEnabled(true); search->setPlaceholderText("Search code, name or description"); layout->addWidget(search);
    auto *table=new QTableWidget(0,3); table->setObjectName("referenceTable"); table->setHorizontalHeaderLabels({"Code","Name / location","Details"});
    table->setColumnHidden(2,!reference.showDetails);
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide(); table->setWordWrap(false); table->setAlternatingRowColors(true); table->setColumnWidth(0,85); table->setColumnWidth(1,490); table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table,1);
    auto *count=new QLabel; count->setObjectName("referenceCount"); layout->addWidget(count);
    auto *note=new QLabel(reference.note); note->setWordWrap(true); note->setOpenExternalLinks(true); layout->addWidget(note);
    auto *buttons=new QDialogButtonBox(select ? QDialogButtonBox::Cancel : QDialogButtonBox::Close); layout->addWidget(buttons);
    auto *copy=buttons->addButton("Copy code",QDialogButtonBox::ActionRole); copy->setObjectName("copyReferenceCode");
    auto *use=select ? buttons->addButton("Use selected",QDialogButtonBox::AcceptRole) : nullptr;
    if(use) { use->setObjectName("useReference"); use->setDefault(true); }
    const auto update=[=] { copy->setEnabled(table->currentRow()>=0); if(use) use->setEnabled(table->currentRow()>=0); };
    const auto refresh=[=] {
        const QSignalBlocker blocker(table);
        table->setSortingEnabled(false); table->setRowCount(0);
        const auto query=airlineSearchText(search->text());
        for(const auto &r : reference.rows) {
            const auto text=airlineSearchText(r.code+' '+r.name+' '+r.detail); bool matches=true;
            for(const auto &token : query.split(' ',Qt::SkipEmptyParts)) matches &= text.contains(token);
            if(!matches) continue;
            const int row=table->rowCount(); table->insertRow(row);
            const QStringList values{r.code,r.name,r.detail}; for(int col=0;col<3;++col) { auto *item=new QTableWidgetItem(values[col]); item->setToolTip(values[col]); table->setItem(row,col,item); }
        }
        table->setSortingEnabled(true); table->sortItems(0,Qt::AscendingOrder);
        count->setText(table->rowCount() ? QString("%1 matches").arg(table->rowCount()) : "No matches. Try a different search."); update();
    };
    connect(search,&QLineEdit::textChanged,this,refresh); connect(table,&QTableWidget::itemSelectionChanged,this,update);
    connect(copy,&QPushButton::clicked,this,[this] { QApplication::clipboard()->setText(selected().code); });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this] { if(!selected().code.isEmpty()) accept(); });
    if(select) connect(table,&QTableWidget::cellDoubleClicked,this,[this] { if(!selected().code.isEmpty()) accept(); });
    refresh(); search->setFocus();
}
ReferenceRow ReferenceDialog::selected() const {
    const auto *table=findChild<QTableWidget *>("referenceTable"); const int row=table->currentRow();
    return row<0 ? ReferenceRow{} : ReferenceRow{table->item(row,0)->text(),table->item(row,1)->text(),table->item(row,2)->text()};
}
}
