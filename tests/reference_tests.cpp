#include "airlinelookup.h"
#include "reference.h"
#include <QApplication>
#include "appearance.h"
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

using namespace shadowlogis;
namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
const Airline &member(const QString &iata) {
    for(const auto &a : airlineDirectory()) if(a.iata==iata && a.member) return a;
    throw std::runtime_error("Missing expected airline");
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    shadowlogis::installAppearance(app);
    try {
        const auto &aa=member("AA"),&cx=member("CX");
        require(airlineDirectory().size()>6000 && aa.country=="United States" && cx.country.startsWith("Hong Kong"),"Airline snapshot incomplete");
        require(member("EI").numeric=="053","Numeric code lost leading zero");
        require(airlineMatches(aa,"USA") && airlineMatches(aa,"american AAL") && airlineMatches(aa,"AA","US"),"Country aliases or combined code/name search failed");
        require(!airlineMatches(aa,"AA","Canada") && !airlineMatches(cx,"United States"),"Country filter matches wrong home country");
        Airline old{"Historical Air","Canada","ZZ","ZZZ","","","",false,false};
        require(!airlineMatches(old,"") && airlineMatches(old,"zz","Canada",true),"Historical filter failed");
        require(airlineSearchText("AÉRÖ Türkiye")=="aero turkiye","Accent-insensitive search failed");
        AirlineLookupDialog dialog(true); dialog.show(); app.processEvents();
        auto *table=dialog.findChild<QTableWidget *>("airlineTable"); auto *country=dialog.findChild<QComboBox *>("airlineCountry"); auto *search=dialog.findChild<QLineEdit *>("airlineSearch");
        auto *use=dialog.findChild<QPushButton *>("useAirlineCode"); auto *oldToggle=dialog.findChild<QCheckBox *>("airlineHistorical");
        require(table->rowCount()>350 && table->rowCount()<500 && !use->isEnabled(),"Default lookup state invalid");
        require(country->findText("United States")>=0,"Missing country choice"); country->setCurrentText("United States");
        for(int i=0;i<table->rowCount();++i) require(table->item(i,1)->text()=="United States","Country selection leaked another country");
        search->setText("American Airlines"); require(table->rowCount()==1 && table->item(0,2)->text()=="AA","Airline search failed");
        table->selectRow(0); require(use->isEnabled() && dialog.selectedCode()=="AA","Selection cannot insert IATA code");
        dialog.findChild<QPushButton *>("CopyIATA")->click(); require(QApplication::clipboard()->text()=="AA","IATA copy failed");
        search->setText("no such airline 12345"); require(table->rowCount()==0 && !use->isEnabled() && dialog.selectedCode().isEmpty(),"Filter retained stale selection");
        search->clear(); country->setCurrentIndex(0); oldToggle->setChecked(true); require(table->rowCount()>6000,"Expanded directory missing");
        search->setText("053"); require(table->rowCount()>=1,"Numeric code search failed");
        oldToggle->setChecked(false); search->setText("Hong Kong"); app.processEvents();
        if(argc==2) { QDir().mkpath(argv[1]); dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/airline-lookup.png"); }
        search->setText("GetJet"); require(table->rowCount()==1 && table->item(0,2)->text()=="GW*","Controlled duplicate marker lost"); table->selectRow(0);
        dialog.findChild<QPushButton *>("CopyIATA")->click(); require(QApplication::clipboard()->text()=="GW" && dialog.selectedCode()=="GW","Duplicate marker inserted into IATA field");
        oldToggle->setChecked(true); search->clear();
        int icaoOnly=-1; for(int row=0;row<table->rowCount();++row) if(table->item(row,2)->text().isEmpty() && !table->item(row,3)->text().isEmpty()) { icaoOnly=row; break; }
        require(icaoOnly>=0,"Missing ICAO-only coverage"); table->selectRow(icaoOnly); require(!use->isEnabled(),"ICAO-only entry accepted as IATA");
        require(countryReference().rows.size()>240 && currencyReference().rows.size()>140,"Country/currency coverage missing");
        const auto terms=incotermReference(); require(terms.rows.size()==11,"Incoterm list incomplete");
        ReferenceDialog currencies(currencyReference(),true); currencies.show();
        auto *s=currencies.findChild<QLineEdit *>("referenceSearch"); auto *t=currencies.findChild<QTableWidget *>("referenceTable");
        s->setText("USD"); require(t->rowCount()==1,"Currency code not unique"); t->selectRow(0); require(currencies.selected().code=="USD","Wrong currency selection");
        currencies.findChild<QPushButton *>("copyReferenceCode")->click(); require(QApplication::clipboard()->text()=="USD","Currency copy failed");
        s->setText("nonexistent currency"); require(currencies.selected().code.isEmpty() && !currencies.findChild<QPushButton *>("useReference")->isEnabled(),"Stale currency selection");
        ReferenceDialog incoterms(terms); incoterms.show(); auto *termSearch=incoterms.findChild<QLineEdit *>("referenceSearch");
        termSearch->setText("waterway"); require(incoterms.findChild<QTableWidget *>("referenceTable")->rowCount()==4,"Incoterm transport groups incorrect");
        if(argc==2) { termSearch->clear(); app.processEvents(); incoterms.grab().save(QString::fromLocal8Bit(argv[1])+"/incoterms-lookup.png"); }
        QTextStream(stdout)<<"PASS: offline airline coverage, country aliases, numeric leading zeros, historical filtering, controlled duplicates, copying, ICAO-only protection, empty selections, countries, currencies and Incoterms.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
