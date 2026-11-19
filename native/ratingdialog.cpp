#include "ratingdialog.h"
#include "reference.h"
#include <QStyle>
#include "entrylogic.h"
#include "domain.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace shadowlogis {
RatingDialog::RatingDialog(const Record &record,QWidget *parent) : QDialog(parent),value(record) {
    setObjectName("awbRatesDialog"); setWindowTitle("Rates and charges"); resize(990,850);
    auto *layout=new QVBoxLayout(this);
    auto *hint=new QLabel("Amounts update while you type. Edit a calculated amount to keep a manual override; select Calculate again to resume. Rates use the cargo row's weight unit. Changes are saved with the air waybill."); hint->setWordWrap(true); layout->addWidget(hint);
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); auto *body=new QWidget; auto *content=new QVBoxLayout(body); scroll->setWidget(body); layout->addWidget(scroll,1);
    const auto settings=record.values["NATIVE:AWB_CALCULATIONS"].toObject();
    auto *rates=new QTableWidget(AirCargoRows,5); rates->setObjectName("ratingRows"); rates->setHorizontalHeaderLabels({"Chargeable weight","Rate per unit","Line amount","Calculate","Rate class"}); rates->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); rates->setFixedHeight(190); content->addWidget(rates);
    for(int row=0;row<AirCargoRows;++row) {
        const auto weight=valueText(record.values["AWB:CHARGEABLE_WEIGHT"].toArray()[row]);
        auto *item=new QTableWidgetItem(weightText(weight)+" "+(record.values["AWB:KG_LB"].toArray()[row]=="K" ? "kg" : record.values["AWB:KG_LB"].toArray()[row]=="L" ? "lb" : "")); item->setFlags(item->flags() & ~Qt::ItemIsEditable); rates->setItem(row,0,item);
        for(int col=1;col<=2;++col) rates->setItem(row,col,new QTableWidgetItem(valueText(record.values[col==1 ? "AWB:RATE_CHARGE" : "AWB:LINE_TOTAL"].toArray()[row])));
        auto *check=new QCheckBox; check->setObjectName("calculateLine"+QString::number(row));
        check->setChecked(settings["lines"].toArray().size()==AirCargoRows ? settings["lines"].toArray()[row].toBool() : !record.id); rates->setCellWidget(row,3,check);
        auto *rateClass=new QComboBox; rateClass->addItem("Choose…","");
        for(const auto &[code,label] : QList<QPair<QString,QString>>{{"N","N · Normal"},{"Q","Q · Quantity"},{"K","K · Per kg"},{"M","M · Minimum"},{"B","B · Basic"},{"C","C · Commodity"},{"R","R · Reduction"},{"S","S · Surcharge"},{"U","U · ULD"},{"P","P · Priority"}}) rateClass->addItem(label,code);
        const auto stored=valueText(record.values["AWB:RATE_CLASS"].toArray()[row]); if(rateClass->findData(stored)<0) rateClass->addItem(stored,stored); rateClass->setCurrentIndex(rateClass->findData(stored)); rates->setCellWidget(row,4,rateClass);
    }
    auto *chargeBox=new QGroupBox("Payment and totals"); auto *grid=new QGridLayout(chargeBox); content->addWidget(chargeBox);
    auto *currency=new QLineEdit(valueText(record.values["AWB:AWB_CURRENCY"])); currency->setObjectName("ratingCurrency"); currency->setMaxLength(3); currency->setPlaceholderText("e.g. USD"); grid->addWidget(new QLabel("Currency"),0,0); grid->addWidget(currency,0,1);
    auto *currencyLookup=currency->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),QLineEdit::TrailingPosition);
    currencyLookup->setObjectName("lookupCurrency"); currencyLookup->setToolTip("Find currency by code or country");
    connect(currencyLookup,&QAction::triggered,this,[this,currency] {
        ReferenceDialog dialog(currencyReference(),true,this);
        if(dialog.exec()==QDialog::Accepted) currency->setText(dialog.selected().code);
    });
    auto *weightSide=new QComboBox; weightSide->setObjectName("weightPayment"); weightSide->addItem("Choose…",""); weightSide->addItem("Prepaid","P"); weightSide->addItem("Collect","C");
    const auto existingSide=valueText(record.values["AWB:WEIGHT_VALUE_PPD_COL"]);
    if(weightSide->findData(existingSide)<0) weightSide->addItem(existingSide,existingSide);
    weightSide->setCurrentIndex(weightSide->findData(existingSide)); grid->addWidget(new QLabel("Weight charges"),0,2); grid->addWidget(weightSide,0,3);
    auto *autoWeight=new QCheckBox("Calculate weight charges"); autoWeight->setObjectName("calculateWeightCharges"); autoWeight->setChecked(settings["weight"].toBool()); grid->addWidget(autoWeight,1,0,1,2);
    auto *autoOther=new QCheckBox("Calculate other charges"); autoOther->setObjectName("calculateOtherCharges"); autoOther->setChecked(settings.contains("other") ? settings["other"].toBool() : !record.id); grid->addWidget(autoOther,1,2,1,2);
    auto *autoTotal=new QCheckBox("Calculate totals"); autoTotal->setObjectName("calculateChargeTotals"); autoTotal->setChecked(settings.contains("totals") ? settings["totals"].toBool() : !record.id); grid->addWidget(autoTotal,2,0,1,2);
    auto *minimum=new QLineEdit(valueText(record.values["AWB:MINIMUM_CHARGE"])); minimum->setObjectName("ratingMinimum"); grid->addWidget(new QLabel("Minimum charge (manual rating)"),2,2); grid->addWidget(minimum,2,3);
    grid->addWidget(new QLabel("Prepaid"),3,1); grid->addWidget(new QLabel("Collect"),3,3);
    QMap<QString,QLineEdit *> amounts;
    int row=4;
    for(const auto &[suffix,label] : QList<QPair<QString,QString>>{{"WEIGHT_CHARGE","Weight charge"},{"VALUATION_CHARGE","Valuation"},{"TAX","Tax"},{"DUE_AGENT","Due agent"},{"DUE_CARRIER","Due carrier"},{"BLANK","Additional amount"},{"TOTAL","Total"}}) {
        for(int col=0;col<2;++col) {
            const auto key="AWB:"+suffix+(col==0 ? "_PPD" : "_COL"); auto *edit=new QLineEdit(valueText(record.values[key])); edit->setObjectName(key); amounts[key]=edit;
            grid->addWidget(new QLabel(label),row,col*2); grid->addWidget(edit,row,col*2+1);
        }
        ++row;
    }
    auto *subtotal=new QLineEdit(valueText(record.values["AWB:CHARGE_TOTAL"])); subtotal->setObjectName("AWB:CHARGE_TOTAL"); amounts["AWB:CHARGE_TOTAL"]=subtotal;
    grid->addWidget(new QLabel("Cargo charge subtotal"),row,0); grid->addWidget(subtotal,row,1);
    auto *otherFlag=new QComboBox; otherFlag->setObjectName("otherPayment"); otherFlag->addItem("Choose…",""); otherFlag->addItem("Prepaid","P"); otherFlag->addItem("Collect","C");
    const auto flag=valueText(record.values["AWB:OTHER_PPD_COL"]); if(otherFlag->findData(flag)<0) otherFlag->addItem(flag,flag); otherFlag->setCurrentIndex(otherFlag->findData(flag));
    grid->addWidget(new QLabel("Other charges payment"),row,2); grid->addWidget(otherFlag,row,3);
    auto *asAgreed=new QCheckBox("Print rates as AS AGREED"); asAgreed->setObjectName("printAsAgreed"); asAgreed->setChecked(record.values["AWB:PRINT_AS_AGREED"]=="1"); grid->addWidget(asAgreed,++row,0,1,4);
    auto *other=new QTableWidget(12,5); other->setObjectName("otherChargeRows"); other->setHorizontalHeaderLabels({"Description","Code","Amount","Payment","Due to"}); other->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); other->setMinimumHeight(240); content->addWidget(new QLabel("Other charges")); content->addWidget(other);
    const QStringList keys{"AWB:OTHER_CHARGE_DESCRIPTIONS","AWB:OTHER_CHARGE_CODE","AWB:OTHER_CHARGE_AMOUNTS","AWB:OTHER_CHARGE_PPD_COL","AWB:OTHER_CHARGE_AGT_CAR"};
    for(int r=0;r<12;++r) for(int c=0;c<5;++c) {
        const auto text=valueText(record.values[keys[c]].toArray()[r]);
        if(c<3) other->setItem(r,c,new QTableWidgetItem(text));
        else {
            auto *combo=new QComboBox; combo->addItem("Choose…",""); combo->addItem(c==3 ? "Prepaid" : "Agent",c==3 ? "P" : "A"); combo->addItem(c==3 ? "Collect" : "Carrier","C");
            if(combo->findData(text)<0) combo->addItem(text,text);
            combo->setCurrentIndex(combo->findData(text)); other->setCellWidget(r,c,combo);
        }
    }
    auto *status=new QLabel; status->setObjectName("ratingStatus"); status->setWordWrap(true); layout->addWidget(status);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel); layout->addWidget(buttons);
    auto updating=std::make_shared<bool>(false);
    const auto update=[=,this] {
        if(*updating) return;
        *updating=true;
        try {
            auto candidate=record; auto modes=QJsonArray{};
            for(int col=1;col<=2;++col) {
                const auto key=col==1 ? "AWB:RATE_CHARGE" : "AWB:LINE_TOTAL"; auto values=record.values[key].toArray();
                for(int r=0;r<AirCargoRows;++r) values[r]=rates->item(r,col)->text().trimmed().isEmpty() ? "0" : rates->item(r,col)->text().trimmed();
                candidate.values[key]=values;
            }
            for(int r=0;r<AirCargoRows;++r) modes.append(static_cast<QCheckBox *>(rates->cellWidget(r,3))->isChecked());
            auto classes=record.values["AWB:RATE_CLASS"].toArray(); for(int r=0;r<AirCargoRows;++r) classes[r]=static_cast<QComboBox *>(rates->cellWidget(r,4))->currentData().toString(); candidate.values["AWB:RATE_CLASS"]=classes;
            for(int c=0;c<5;++c) {
                auto values=record.values[keys[c]].toArray();
                for(int r=0;r<12;++r) values[r]=c<3 ? other->item(r,c)->text().trimmed() : static_cast<QComboBox *>(other->cellWidget(r,c))->currentData().toString();
                if(c==2) for(int r=0;r<12;++r) if(values[r].toString().isEmpty()) values[r]="0";
                candidate.values[keys[c]]=values;
            }
            for(auto it=amounts.begin();it!=amounts.end();++it) candidate.values[it.key()]=it.value()->text().trimmed().isEmpty() ? "0" : it.value()->text().trimmed();
            candidate.values["AWB:AWB_CURRENCY"]=currency->text().trimmed(); candidate.values["AWB:WEIGHT_VALUE_PPD_COL"]=weightSide->currentData().toString(); candidate.values["AWB:MINIMUM_CHARGE"]=minimum->text().trimmed().isEmpty() ? "0" : minimum->text().trimmed();
            candidate.values["AWB:OTHER_PPD_COL"]=otherFlag->currentData().toString();
            if(asAgreed->isChecked()!=(record.values["AWB:PRINT_AS_AGREED"]=="1")) candidate.values["AWB:PRINT_AS_AGREED"]=asAgreed->isChecked() ? "1" : "0";
            candidate.values["NATIVE:AWB_CALCULATIONS"]=QJsonObject{{"lines",modes},{"weight",autoWeight->isChecked()},{"other",autoOther->isChecked()},{"totals",autoTotal->isChecked()}};
            candidate=calculateAirCharges(candidate);
            // Validate changed manual inputs too, without rejecting untouched legacy values.
            for(auto it=candidate.values.begin();it!=candidate.values.end();++it) if(it.value()!=record.values[it.key()]) {
                if(it.key()=="AWB:RATE_CHARGE") for(int r=0;r<AirCargoRows;++r) ratedAmount("1",it.value().toArray()[r].toString());
                if(it.key()=="AWB:LINE_TOTAL" || it.key()=="AWB:OTHER_CHARGE_AMOUNTS") {
                    const int count=it.key()=="AWB:LINE_TOTAL" ? AirCargoRows : 12;
                    for(int r=0;r<count;++r) sumAmounts({it.value().toArray()[r].toString()});
                }
                if(amounts.contains(it.key()) || it.key()=="AWB:MINIMUM_CHARGE") sumAmounts({it.value().toString()});
            }
            value=candidate;
            otherFlag->setCurrentIndex(otherFlag->findData(valueText(value.values["AWB:OTHER_PPD_COL"])));
            for(int r=0;r<AirCargoRows;++r) if(modes[r].toBool()) rates->item(r,2)->setText(valueText(value.values["AWB:LINE_TOTAL"].toArray()[r]));
            for(auto it=amounts.begin();it!=amounts.end();++it) if(value.values[it.key()]!=candidate.values[it.key()] || it.value()->text()!=valueText(value.values[it.key()])) it.value()->setText(valueText(value.values[it.key()]));
            status->setText(currency->text().trimmed().isEmpty() ? "Choose a currency before issuing the AWB. Calculated amounts are rounded to cents per line." : "Calculated amounts are rounded to cents per line. Manual amounts stay as entered."); buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
        } catch(const std::exception &e) { status->setText(QString::fromUtf8(e.what())); buttons->button(QDialogButtonBox::Apply)->setEnabled(false); }
        *updating=false;
    };
    connect(rates,&QTableWidget::itemChanged,this,[=](QTableWidgetItem *item) { if(*updating) return; if(item->column()==2) { QSignalBlocker guard(rates->cellWidget(item->row(),3)); static_cast<QCheckBox *>(rates->cellWidget(item->row(),3))->setChecked(false); } update(); });
    for(int r=0;r<AirCargoRows;++r) connect(static_cast<QCheckBox *>(rates->cellWidget(r,3)),&QCheckBox::toggled,this,update);
    for(int r=0;r<AirCargoRows;++r) connect(static_cast<QComboBox *>(rates->cellWidget(r,4)),&QComboBox::currentIndexChanged,this,update);
    connect(other,&QTableWidget::itemChanged,this,[=] { update(); });
    for(int r=0;r<12;++r) for(int c=3;c<5;++c) connect(static_cast<QComboBox *>(other->cellWidget(r,c)),&QComboBox::currentIndexChanged,this,update);
    for(auto *check : {autoWeight,autoOther,autoTotal}) connect(check,&QCheckBox::toggled,this,update);
    for(auto it=amounts.begin();it!=amounts.end();++it) connect(it.value(),&QLineEdit::textEdited,this,[=,key=it.key()] {
        auto *mode=key.startsWith("AWB:WEIGHT_") || key=="AWB:CHARGE_TOTAL" ? autoWeight : key.startsWith("AWB:DUE_") ? autoOther : key.startsWith("AWB:TOTAL_") ? autoTotal : nullptr;
        if(mode) { QSignalBlocker guard(mode); mode->setChecked(false); } update();
    });
    for(auto *edit : {currency,minimum}) connect(edit,&QLineEdit::textChanged,this,update);
    connect(weightSide,&QComboBox::currentIndexChanged,this,update);
    connect(asAgreed,&QCheckBox::toggled,this,update);
    connect(otherFlag,&QComboBox::activated,this,[=] { autoOther->setChecked(false); update(); });
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[=,this] { update(); if(buttons->button(QDialogButtonBox::Apply)->isEnabled()) accept(); });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    update();
}
}
