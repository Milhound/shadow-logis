#include "dimensionsdialog.h"
#include "entrylogic.h"
#include "domain.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace shadowlogis {
DimensionsDialog::DimensionsDialog(const Record &record,QWidget *parent) : QDialog(parent),value(record) {
    setObjectName("dimensionsDialog"); setWindowTitle("Dimensions and chargeable weight"); resize(960,720);
    auto *layout=new QVBoxLayout(this);
    auto *note=new QLabel("Choose a formula, then enter package dimensions or CBM. Chargeable weight uses the greater of gross or dimensional weight, rounded up using your rule. Rates with weight breaks can use manual chargeable weights."); note->setWordWrap(true); layout->addWidget(note);
    const auto settings=record.values["NATIVE:DIMENSION_CALCULATION"].toObject();
    auto *form=new QFormLayout; layout->addLayout(form);
    auto *unit=new QComboBox; unit->setObjectName("dimensionUnits"); unit->addItem("Centimeters","M"); unit->addItem("Inches","S"); unit->setCurrentIndex(record.values["AWB:DIMS_STD_METRIC"]=="S" ? 1 : 0); form->addRow("Dimensions entered in",unit);
    auto *dimensionRounding=new QComboBox; dimensionRounding->setObjectName("dimensionRounding"); dimensionRounding->addItem("Use dimensions as entered","as_entered"); dimensionRounding->addItem("Nearest whole cm / inch","nearest"); dimensionRounding->addItem("Round up to whole cm / inch","up"); dimensionRounding->setCurrentIndex(std::max(0,dimensionRounding->findData(settings["dimension_rounding"].toString()))); form->addRow("Dimension rounding",dimensionRounding);
    auto *divisor=new QLineEdit(settings["divisor"].toString("6000")); divisor->setObjectName("volumetricDivisor");
    auto *basis=new QComboBox; basis->setObjectName("volumetricBasis"); basis->addItem("Metric: cm³ ÷ divisor → kg","cm3/kg"); basis->addItem("Imperial: in³ ÷ divisor → lb","in3/lb"); basis->addItem("From volume: CBM × factor → kg","kg/cbm"); basis->setCurrentIndex(std::max(0,basis->findData(settings["basis"].toString("cm3/kg"))));
    form->insertRow(0,"Formula",basis);
    auto *factorLabel=new QLabel; form->addRow(factorLabel,divisor);
    auto *rounding=new QComboBox; rounding->setObjectName("chargeableRounding"); rounding->addItem("Round up to whole kg / lb","1"); rounding->addItem("Round up to 0.5 kg / lb","0.5"); rounding->addItem("Round up to 0.01 kg / lb","0.01"); rounding->setCurrentIndex(std::max(0,rounding->findData(settings["rounding"].toString("1")))); form->addRow("Chargeable-weight rounding",rounding);
    auto *table=new QTableWidget(5,5); table->setObjectName("dimensionRows"); table->setHorizontalHeaderLabels({"Packages","Length","Width","Height","Cargo row"}); table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); table->setFixedHeight(200); layout->addWidget(table);
    const QStringList keys{"AWB:DESCRIPTION_QUANTITY","AWB:DESCRIPTION_LENGTH","AWB:DESCRIPTION_WIDTH","AWB:DESCRIPTION_HEIGHT"};
    int first=0,active=0;
    for(int i=0;i<5;++i) if(valueText(record.values["AWB:NUMBER_OF_PIECES"].toArray()[i]).toDouble()>0) { if(!active) first=i; ++active; }
    for(int row=0;row<5;++row) {
        for(int col=0;col<4;++col) table->setItem(row,col,new QTableWidgetItem(valueText(record.values[keys[col]].toArray()[row])));
        auto *cargo=new QComboBox; for(int i=0;i<5;++i) cargo->addItem(QString("Row %1 · %2 pieces").arg(i+1).arg(valueText(record.values["AWB:NUMBER_OF_PIECES"].toArray()[i])),i);
        const auto mapping=settings["cargo_rows"].toArray(); cargo->setCurrentIndex(mapping.size()==5 ? mapping[row].toInt() : first); table->setCellWidget(row,4,cargo);
    }
    auto *cbmTable=new QTableWidget(5,2); cbmTable->setObjectName("cbmRows"); cbmTable->setHorizontalHeaderLabels({"Cargo row","Volume (m³ / CBM)"}); cbmTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); cbmTable->setFixedHeight(200); layout->addWidget(cbmTable);
    const auto savedCbm=settings["cbm_rows"].toArray();
    for(int row=0;row<5;++row) {
        auto *label=new QTableWidgetItem(QString("Row %1 · %2 pieces").arg(row+1).arg(valueText(record.values["AWB:NUMBER_OF_PIECES"].toArray()[row]))); label->setFlags(label->flags() & ~Qt::ItemIsEditable); cbmTable->setItem(row,0,label);
        const auto initial=savedCbm.size()==5 ? savedCbm[row].toString() : active==1 && row==first ? valueText(record.values["NATIVE:CARGO_VOLUME_CBM"]) : "0";
        cbmTable->setItem(row,1,new QTableWidgetItem(initial.isEmpty() ? "0" : initial));
    }
    auto *automatic=new QHBoxLayout; automatic->addWidget(new QLabel("Automatic chargeable weight:")); QList<QCheckBox *> checks;
    for(int i=0;i<5;++i) {
        auto *check=new QCheckBox("Row "+QString::number(i+1)); check->setObjectName("autoChargeable"+QString::number(i));
        const auto modes=settings["auto_chargeable"].toArray(); check->setChecked(modes.size()==5 ? modes[i].toBool() : active==1 && i==first); automatic->addWidget(check); checks<<check;
    }
    layout->addLayout(automatic);
    auto *summary=new QLabel; summary->setObjectName("dimensionSummary"); summary->setWordWrap(true); summary->setMinimumHeight(130); summary->setAlignment(Qt::AlignTop|Qt::AlignLeft); layout->addWidget(summary); layout->addStretch();
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel); layout->addWidget(buttons);
    const auto update=[=,this] {
        try {
            const bool fromCbm=basis->currentData()=="kg/cbm";
            // Hide before showing the alternate input table, avoiding a transient
            // double-height minimum that would enlarge the user's window.
            if(fromCbm) { table->hide(); cbmTable->show(); } else { cbmTable->hide(); table->show(); }
            unit->setEnabled(!fromCbm); dimensionRounding->setEnabled(!fromCbm);
            factorLabel->setText(fromCbm ? "Factor (kg per CBM)" : basis->currentData()=="in3/lb" ? "Divisor (in³ per lb)" : "Divisor (cm³ per kg)");
            auto candidate=record; QJsonArray assignments,modes,cbm;
            for(int col=0;col<4;++col) {
                QJsonArray values; for(int row=0;row<5;++row) values.append(table->item(row,col)->text().trimmed().isEmpty() ? "0" : table->item(row,col)->text().trimmed()); candidate.values[keys[col]]=values;
            }
            for(int row=0;row<5;++row) { assignments.append(static_cast<QComboBox *>(table->cellWidget(row,4))->currentData().toInt()); modes.append(checks[row]->isChecked()); cbm.append(cbmTable->item(row,1)->text().trimmed().isEmpty() ? "0" : cbmTable->item(row,1)->text().trimmed()); }
            candidate.values["AWB:DIMS_STD_METRIC"]=unit->currentData().toString();
            candidate.values["NATIVE:DIMENSION_CALCULATION"]=QJsonObject{{"enabled",true},{"basis",basis->currentData().toString()},{"divisor",divisor->text().trimmed()},{"rounding",rounding->currentData().toString()},{"dimension_rounding",dimensionRounding->currentData().toString()},{"cargo_rows",assignments},{"auto_chargeable",modes},{"cbm_rows",cbm}};
            const auto calculated=calculateDimensions(candidate); value=calculated.record;
            QString text="Volume: "+calculated.cubicMeters+" m³ / "+calculated.cubicFeet+" ft³\nDimensional weight: "+weightText(calculated.dimensionalWeight)+' '+calculated.unit;
            text+=" ("+weightText(quantityConversion(calculated.dimensionalWeight,calculated.unit=="kg" ? "kg_to_lb" : "lb_to_kg",2))+(calculated.unit=="kg" ? " lb)" : " kg)");
            for(int row=0;row<5;++row) if(modes[row].toBool()) text+="\nRow "+QString::number(row+1)+" chargeable: "+weightText(valueText(value.values["AWB:CHARGEABLE_WEIGHT"].toArray()[row]))+' '+calculated.unit;
            summary->setText(text); buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
        } catch(const std::exception &e) { summary->setText(QString::fromUtf8(e.what())); buttons->button(QDialogButtonBox::Apply)->setEnabled(false); }
    };
    connect(table,&QTableWidget::itemChanged,this,[=] { update(); });
    connect(cbmTable,&QTableWidget::itemChanged,this,[=] { update(); });
    auto previousUnit=std::make_shared<QString>(unit->currentData().toString());
    connect(unit,&QComboBox::currentIndexChanged,this,[=] {
        const auto selected=unit->currentData().toString(); if(selected==*previousUnit) return;
        try {
            QStringList converted;
            for(int row=0;row<5;++row) for(int col=1;col<4;++col) converted<<quantityConversion(table->item(row,col)->text(),selected=="M" ? "in_to_cm" : "cm_to_in",3);
            QSignalBlocker guard(table); int index=0;
            for(int row=0;row<5;++row) for(int col=1;col<4;++col) table->item(row,col)->setText(converted[index++]);
            *previousUnit=selected; update();
        } catch(const std::exception &e) { QSignalBlocker guard(unit); unit->setCurrentIndex(unit->findData(*previousUnit)); summary->setText(QString::fromUtf8(e.what())); }
    });
    connect(basis,&QComboBox::currentIndexChanged,this,[=] {
        { QSignalBlocker guard(divisor); divisor->setText(basis->currentData()=="kg/cbm" ? "167" : basis->currentData()=="in3/lb" ? "166" : "6000"); }
        if(basis->currentData()!="kg/cbm") unit->setCurrentIndex(basis->currentData()=="in3/lb" ? 1 : 0);
        update();
    });
    for(auto *combo : {rounding,dimensionRounding}) connect(combo,&QComboBox::currentIndexChanged,this,update);
    for(int row=0;row<5;++row) connect(static_cast<QComboBox *>(table->cellWidget(row,4)),&QComboBox::currentIndexChanged,this,update);
    for(auto *check : checks) connect(check,&QCheckBox::toggled,this,update);
    connect(divisor,&QLineEdit::textChanged,this,update);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[=,this] { update(); if(buttons->button(QDialogButtonBox::Apply)->isEnabled()) accept(); });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    update();
}
}
