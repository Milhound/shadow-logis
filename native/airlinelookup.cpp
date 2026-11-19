#include "airlinelookup.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace shadowlogis {
AirlineLookupDialog::AirlineLookupDialog(bool chooseCode, const QString &initialSearch, QWidget *parent) : QDialog(parent) {
    setObjectName("airlineLookup"); setWindowTitle("Airline codes"); resize(1020,640); setMinimumSize(760,460);
    auto *layout = new QVBoxLayout(this);
    auto *filters = new QHBoxLayout;
    auto *search = new QLineEdit(initialSearch); search->setObjectName("airlineSearch"); search->setClearButtonEnabled(true);
    search->setPlaceholderText("Country, airline, IATA, ICAO or numeric code"); filters->addWidget(search,2);
    auto *country = new QComboBox; country->setObjectName("airlineCountry"); country->setEditable(true); country->setInsertPolicy(QComboBox::NoInsert);
    country->addItem("All countries / territories", "");
    QSet<QString> countries; for (const auto &a : airlineDirectory()) if (a.member && !a.country.isEmpty()) countries.insert(a.country);
    auto names = countries.values(); names.sort(Qt::CaseInsensitive);
    for (const auto &name : names) country->addItem(name,name);
    country->completer()->setCompletionMode(QCompleter::PopupCompletion); country->completer()->setFilterMode(Qt::MatchContains);
    country->setToolTip("Airline home country or territory; this is not a list of destinations served."); filters->addWidget(country,1);
    layout->addLayout(filters);
    auto *historical = new QCheckBox("Include historical / unverified entries"); historical->setObjectName("airlineHistorical"); layout->addWidget(historical);
    auto *count = new QLabel; count->setObjectName("airlineCount"); layout->addWidget(count);
    auto *table = new QTableWidget(0,6); table->setObjectName("airlineTable");
    table->setHorizontalHeaderLabels({"Airline","Home country / territory","IATA","ICAO","Numeric","Directory"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->verticalHeader()->hide(); table->setAlternatingRowColors(true);
    table->setWordWrap(false); table->setSortingEnabled(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0,280); table->setColumnWidth(1,200); table->setColumnWidth(2,65); table->setColumnWidth(3,65); table->setColumnWidth(4,75);
    table->horizontalHeaderItem(4)->setToolTip("Three-digit code published in the IATA member directory. Confirm the cargo prefix with the airline before using it in an AWB number.");
    layout->addWidget(table,1);
    auto *empty = new QLabel("No matching airlines. Try another country or enable historical / unverified entries."); empty->setObjectName("airlineEmpty"); empty->setWordWrap(true); layout->addWidget(empty);
    auto *detail = new QLabel; detail->setObjectName("airlineDetail"); detail->setTextFormat(Qt::PlainText); detail->setWordWrap(true); detail->setMinimumHeight(42); layout->addWidget(detail);
    auto *sources = new QLabel("Snapshot retrieved "+airlineSnapshotDate()+" · <a href=\"https://www.iata.org/en/about/members/airline-list/\">IATA members</a> + <a href=\"https://openflights.org/data\">OpenFlights (ODbL)</a>. Member coverage is not the complete IATA register. Older entries may have retired or reassigned codes. <a href=\"https://www.iata.org/codes/index\">Verify with IATA</a>.");
    sources->setWordWrap(true); sources->setOpenExternalLinks(true); layout->addWidget(sources);
    auto *buttons = new QDialogButtonBox(chooseCode ? QDialogButtonBox::Cancel : QDialogButtonBox::Close); layout->addWidget(buttons);
    QList<QPushButton *> copies;
    for (const auto &label : {QString("Copy IATA"),QString("Copy ICAO"),QString("Copy numeric")}) {
        auto *button = buttons->addButton(label,QDialogButtonBox::ActionRole); auto objectName=label; objectName.remove(' '); button->setObjectName(objectName); copies.append(button);
    }
    QPushButton *use = nullptr;
    if (chooseCode) { use = buttons->addButton("Use IATA code",QDialogButtonBox::AcceptRole); use->setObjectName("useAirlineCode"); use->setDefault(true); }
    const auto updateSelection = [=] {
        const int row = table->currentRow();
        for (int i=0;i<copies.size();++i) copies[i]->setEnabled(row>=0 && !table->item(row,i+2)->text().isEmpty());
        if (use) use->setEnabled(row>=0 && !table->item(row,0)->data(Qt::UserRole).toString().isEmpty());
        detail->setText(row<0 ? "Select an airline to copy its codes." : table->item(row,0)->toolTip());
    };
    const auto refresh = [=] {
        const QSignalBlocker blocker(table);
        const auto countryText = country->currentText()==country->itemText(0) ? QString() : country->currentText();
        table->setSortingEnabled(false); table->setRowCount(0);
        for (const auto &a : airlineDirectory()) {
            if (!airlineMatches(a,search->text(),countryText,historical->isChecked())) continue;
            const int row = table->rowCount(); table->insertRow(row);
            const QStringList fields{a.name,a.country,a.iata+(a.controlledDuplicate ? "*" : ""),a.icao,a.numeric,a.member ? "IATA member" : "Older / unverified"};
            QString info = a.name+" — "+a.country+". "+(a.member ? "Listed in the IATA member snapshot." : "Historical OpenFlights snapshot; current assignment and operating status are unverified.");
            if (a.controlledDuplicate) info += " * Controlled duplicate: this IATA code is shared; confirm the selected airline and service.";
            if (a.iata.isEmpty()) info += " No IATA designator is listed; ICAO is not a substitute for this AWB field.";
            for(int col=0;col<fields.size();++col) { auto *item=new QTableWidgetItem(fields[col]); item->setToolTip(info); table->setItem(row,col,item); }
            table->item(row,0)->setData(Qt::UserRole,a.iata);
            table->item(row,2)->setData(Qt::UserRole,a.iata);
        }
        table->setSortingEnabled(true); table->sortItems(0,Qt::AscendingOrder);
        count->setText(QString("%1 airlines · Country identifies where the airline is based").arg(table->rowCount()));
        empty->setVisible(!table->rowCount()); updateSelection();
    };
    connect(search,&QLineEdit::textChanged,this,refresh); connect(country,&QComboBox::currentTextChanged,this,refresh);
    connect(historical,&QCheckBox::toggled,this,[=](bool includeOld) {
        const auto previous=country->currentText();
        { const QSignalBlocker blocker(country); country->clear(); country->addItem("All countries / territories", "");
          QSet<QString> places; for(const auto &a : airlineDirectory()) if((includeOld || a.member) && !a.country.isEmpty()) places.insert(a.country);
          auto sorted=places.values(); sorted.sort(Qt::CaseInsensitive); for(const auto &place : sorted) country->addItem(place,place);
          const int index=country->findText(previous); country->setCurrentIndex(index<0 ? 0 : index);
        }
        refresh();
    });
    connect(table,&QTableWidget::itemSelectionChanged,this,updateSelection);
    for (int i=0;i<copies.size();++i) connect(copies[i],&QPushButton::clicked,this,[table,i] {
        if (table->currentRow()<0) return;
        const auto *item=table->item(table->currentRow(),i+2);
        QApplication::clipboard()->setText(i==0 ? item->data(Qt::UserRole).toString() : item->text());
    });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this] { if(!selectedCode().isEmpty()) accept(); });
    if (chooseCode) connect(table,&QTableWidget::cellDoubleClicked,this,[this] { if(!selectedCode().isEmpty()) accept(); });
    refresh(); search->setFocus(); search->selectAll();
}
QString AirlineLookupDialog::selectedCode() const {
    const auto *table=findChild<QTableWidget *>("airlineTable");
    return table && table->currentRow()>=0 ? table->item(table->currentRow(),0)->data(Qt::UserRole).toString() : QString();
}
}
