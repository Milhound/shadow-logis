// Self-contained visual/behavior checks, using synthetic display rows only.
#define SHADOW_LOGIS_GUI_TESTING
#include "../native/main.cpp"
#include <QFontInfo>
#include <QRawFont>
#include <QScrollBar>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QPalette samplePalette(bool dark) {
    QPalette p;
    const QColor window(dark ? "#20272d" : "#f1f3f5"), base(dark ? "#252e35" : "#ffffff");
    const QColor text(dark ? "#edf2f5" : "#263440");
    p.setColor(QPalette::Window,window); p.setColor(QPalette::Base,base);
    p.setColor(QPalette::WindowText,text); p.setColor(QPalette::Text,text);
    p.setColor(QPalette::Button,window); p.setColor(QPalette::ButtonText,text);
    p.setColor(QPalette::Highlight,QColor(dark ? "#8bd5c5" : "#226f81"));
    p.setColor(QPalette::HighlightedText,dark ? Qt::black : Qt::white);
    p.setColor(QPalette::Link,QColor(dark ? "#8bd5c5" : "#226f81"));
    p.setColor(QPalette::Disabled,QPalette::Text,QColor(dark ? "#94a4ad" : "#71808c"));
    return p;
}
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir settings; QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
        QApplication::setPalette(samplePalette(false)); installAppearance(app);
        require(QApplication::font().family()=="Inter Variable","Bundled Inter is not the application font");
        require(QRawFont::fromFont(QApplication::font()).familyName()=="Inter Variable","Inter did not resolve to actual font data");
        auto italic=QApplication::font(); italic.setItalic(true);
        require(QRawFont::fromFont(italic).familyName()=="Inter Variable" && QRawFont::fromFont(italic).style()==QFont::StyleItalic,"Bundled italic is unavailable");
        QFile license(":/shadow-logis/fonts/OFL.txt"); require(license.open(QIODevice::ReadOnly) && license.readAll().contains("SIL OPEN FONT LICENSE"),"Font license is not embedded");
        const auto css=app.styleSheet(); installAppearance(app); require(css==app.styleSheet(),"Repeated appearance setup changed the theme");
        
        const auto db=settings.filePath("theme.sqlite"); createWorkspace(db); Window window(db); window.show();
        auto *table=window.findChild<QTableView *>("shipmentsTable");
        auto *proxy=qobject_cast<QSortFilterProxyModel *>(table->model()); auto *model=qobject_cast<QStandardItemModel *>(proxy->sourceModel());
        model->setHorizontalHeaderLabels({"Reference","Customer / shipment","Origin","Destination"});
        const QList<QStringList> shipments{{"A-1042-26","Northstar Export · Machine parts","Houston, US","Singapore, SG"},
            {"O-1041-26","Harbor Supply · Industrial equipment","Houston, US","Rotterdam, NL"},
            {"A-1040-26","Summit Trade · Sample shipment","Chicago, US","Frankfurt, DE"}};
        for(const auto &values : shipments) { QList<QStandardItem *> cells; for(const auto &v : values) cells<<new QStandardItem(v); model->appendRow(cells); }
        table->selectRow(0); table->resizeColumnsToContents();
        window.findChild<QTextBrowser *>("shipmentOverview")->setHtml("<h3>A-1042-26</h3><p>Northstar Export · Machine parts</p><p><b>Route</b><br>Houston → Singapore</p><p><b>Cargo</b><br>4 pieces · 320 kg · 2.4 m³</p>");
        window.findChild<QPushButton *>("editShipment")->setEnabled(true);
        window.findChild<QPushButton *>("deleteShipment")->setEnabled(true);
        window.findChild<QPushButton *>("newShipment")->setEnabled(true);
        auto *search=window.findChild<QLineEdit *>("shipmentSearch"); search->setFocus();
        const auto output=argc==2 ? QString::fromLocal8Bit(argv[1]) : QString();
        if(!output.isEmpty()) QDir().mkpath(output);
        for(bool dark : {false,true}) {
            QApplication::setPalette(samplePalette(dark)); app.processEvents(); app.processEvents();
            const auto expected=samplePalette(dark);
            require(app.palette().color(QPalette::Window)==expected.color(QPalette::Window),"Theme overwrote the desktop palette");
            require(app.styleSheet().contains(expected.color(QPalette::Highlight).name()),"Theme did not follow the accent change");
            require(search->palette().color(QPalette::Base)==expected.color(QPalette::Base),"Entry surface did not follow light/dark change");
            require(QFontInfo(search->font()).family()=="Inter Variable","Entry widget did not inherit Inter");
            if(!output.isEmpty()) require(window.grab().save(output+(dark ? "/workspace-dark.png" : "/workspace-light.png")),"Could not save theme screenshot");
        }
        // Existing windows update again when the desktop switches back.
        QApplication::setPalette(samplePalette(false)); app.processEvents(); app.processEvents();
        require(app.styleSheet()==css,"Returning to the original desktop palette did not restore the theme");
        require(search->hasFocus(),"Palette refresh lost keyboard focus");
        QTextStream(stdout)<<"PASS: embedded regular/italic fonts and license, real font resolution, live light/dark/accent changes, palette preservation and keyboard focus.\n";
        return 0;
    } catch(const std::exception &e) { QTextStream(stderr)<<e.what()<<'\n'; return 1; }
}
