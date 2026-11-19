#include "appearance.h"
#include <QIcon>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFontDatabase>
#include <QPalette>
#include <QTimer>
#include <algorithm>
#include <cmath>

// Explicitly retain resources when linking the static UI library.
static void initFonts() { Q_INIT_RESOURCE(ui_fonts); }

namespace shadowlogis {
namespace {
QColor mix(const QColor &a,const QColor &b,double amount) {
    return QColor::fromRgbF(a.redF()*(1-amount)+b.redF()*amount,
                           a.greenF()*(1-amount)+b.greenF()*amount,
                           a.blueF()*(1-amount)+b.blueF()*amount);
}
double luminance(const QColor &c) {
    const auto linear=[](double v) { return v<=0.04045 ? v/12.92 : std::pow((v+0.055)/1.055,2.4); };
    return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
}
double contrast(const QColor &a,const QColor &b) {
    const auto x=luminance(a), y=luminance(b);
    return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
QColor readable(QColor ink,const QColor &background) {
    if(contrast(ink,background)>=4.5) return ink;
    const QColor target=luminance(background)>.179 ? Qt::black : Qt::white;
    for(int step=1;step<=20;++step) {
        const auto candidate=mix(ink,target,step/20.0);
        if(contrast(candidate,background)>=4.5) return candidate;
    }
    return target;
}
QString stylesheet(const QPalette &p) {
    const auto surface=p.color(QPalette::Base), window=p.color(QPalette::Window);
    const auto text=p.color(QPalette::Text), windowText=p.color(QPalette::WindowText);
    const auto accent=p.color(QPalette::Highlight);
    const auto button=mix(surface,window,.6);
    const auto subtle=mix(surface,text,.035), line=mix(surface,text,.19);
    const auto hover=mix(surface,accent,.09), tab=mix(window,accent,.13);
    const auto selection=mix(surface,accent,.18);
    auto css=QString::fromLatin1(R"CSS(
QPushButton {
    background: @button; color: @text; border: 1px solid @line;
    border-radius: 6px; padding: 5px 12px; min-height: 1.25em;
}
QPushButton:hover:enabled { background: @hover; border-color: @accent; }
QPushButton:pressed:enabled { background: @selection; }
QPushButton:default, QPushButton[primary="true"] {
    background: @accent; color: @onAccent; border-color: @accent; font-weight: 600;
}
QPushButton:default:hover:enabled, QPushButton[primary="true"]:hover:enabled { background: @accentHover; }
QPushButton:default:pressed:enabled, QPushButton[primary="true"]:pressed:enabled { background: @accentPressed; }
QPushButton:focus { border: 2px solid @focus; padding: 4px 11px; }
QPushButton:default:focus, QPushButton[primary="true"]:focus { border-color: @onAccent; }
QPushButton:disabled { background: @button; color: @disabled; border-color: @line; }
QLineEdit, QAbstractSpinBox, QComboBox {
    background: @surface; color: @text; border: 1px solid @line;
    border-radius: 5px; padding: 4px 7px; min-height: 1.2em;
    selection-background-color: @accent; selection-color: @onAccent;
}
QComboBox { padding-right: 22px; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @focus; }
QLineEdit:read-only { background: @subtle; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { background: @button; color: @disabled; }
QPlainTextEdit, QTextEdit {
    background: @surface; color: @text; border: 1px solid @line; border-radius: 5px;
    padding: 4px; selection-background-color: @accent; selection-color: @onAccent;
}
QPlainTextEdit:focus, QTextEdit:focus { border-color: @focus; }
QAbstractItemView {
    background: @surface; color: @text; alternate-background-color: @subtle;
    border: 1px solid @line; border-radius: 6px; gridline-color: @grid;
    selection-background-color: @selection; selection-color: @selectionText;
}
QAbstractItemView::item { padding: 3px 5px; }
QAbstractItemView::item:hover { background: @hover; }
QAbstractItemView::item:selected { background: @selection; color: @selectionText; }
QHeaderView::section {
    background: @subtle; color: @text; font-weight: 600; padding: 7px 6px;
    border: none; border-bottom: 1px solid @line; border-right: 1px solid @grid;
}
QTableCornerButton::section { background: @subtle; border: none; }
QGroupBox {
    background: @surface; border: 1px solid @line; border-radius: 8px;
    margin-top: .8em; padding-top: 5px; font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    left: 12px; padding: 0px 5px; color: @windowText;
}
QTabWidget::pane { border: 1px solid @line; border-radius: 6px; }
QTabBar::tab {
    color: @windowText; background: transparent; border: none;
    border-bottom: 2px solid @line; padding: 8px 10px; margin-right: 2px;
}
QTabBar::tab:selected { color: @tabText; background: @tab; border-bottom-color: @focus; }
QTabBar::tab:hover:!selected { background: @hover; }
QTabBar::tab:disabled { color: @disabled; }
QCheckBox, QRadioButton { spacing: 7px; }
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: @tab; }
QMenuBar { padding: 3px; }
QMenuBar::item { padding: 5px 9px; border-radius: 4px; }
QMenuBar::item:selected { background: @tab; }
QMenu { padding: 5px; border: 1px solid @line; }
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected { background: @selection; color: @selectionText; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @line; margin: 4px 8px; }
QStatusBar { color: @windowText; }
QStatusBar::item { border: none; }
QLabel#applicationVersion { color: @muted; padding: 0px 6px; }
QWidget#shipmentPanel { background: @surface; border: 1px solid @line; border-radius: 9px; }
QToolButton#closeShipmentDetails { color: @muted; background: transparent; border: 1px solid transparent; border-radius: 4px; font-size: 18px; }
QToolButton#closeShipmentDetails:hover { color: @text; background: @hover; }
QToolButton#closeShipmentDetails:pressed { background: @selection; }
QToolButton#closeShipmentDetails:focus { border-color: @focus; }
QTextBrowser#shipmentOverview { border: none; padding: 4px; }
QLabel#saveError { color: @error; padding: 4px 0px; }
)CSS");
    const QList<QPair<QString,QColor>> colors{
        {"@surface",surface},{"@windowText",windowText},{"@text",text},
        {"@muted",readable(mix(window,windowText,.55),window)},
        {"@button",button},{"@subtle",subtle},{"@line",line},{"@grid",mix(surface,text,.09)},
        {"@onAccent",readable(p.color(QPalette::HighlightedText),accent)},
        {"@accentHover",mix(accent,text,.08)},{"@accentPressed",mix(accent,text,.14)},
        {"@accent",accent},{"@focus",readable(accent,surface)},
        {"@hover",hover},{"@selectionText",readable(text,selection)},{"@selection",selection},
        {"@tabText",readable(text,tab)},{"@tab",tab},
        {"@disabled",p.color(QPalette::Disabled,QPalette::Text)},
        {"@error",readable(QColor("#c03838"),window)}};
    for(const auto &[key,color] : colors) css.replace(key,color.name());
    return css;
}

class Appearance final : public QObject {
public:
    explicit Appearance(QApplication &application) : QObject(&application),app(application) {
        setObjectName("shadowLogisAppearance");
        initFonts();
        // Raster resource avoids adding an SVG plugin dependency to startup.
        app.setWindowIcon(QIcon(":/shadow-logis/branding/app-icon.png"));
        const int regular=QFontDatabase::addApplicationFont(":/shadow-logis/fonts/InterVariable.ttf");
        const int italic=QFontDatabase::addApplicationFont(":/shadow-logis/fonts/InterVariable-Italic.ttf");
        const auto families=QFontDatabase::applicationFontFamilies(regular);
        if(!families.isEmpty()) {
            auto font=app.font(); font.setFamily(families.first()); font.setStyleName({});
            font.setWeight(QFont::Normal); font.setItalic(false);
            // Keep larger desktop/accessibility sizes and let Qt handle DPI.
            if(font.pointSizeF()>0 && font.pointSizeF()<10) font.setPointSizeF(10);
            app.setFont(font);
        } else qWarning("Bundled Inter could not be loaded; using the system font.");
        if(italic<0) qWarning("Bundled Inter italic could not be loaded.");
        app.installEventFilter(this); refresh();
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::ApplicationPaletteChange && !updating && !pending) {
            pending=true;
            QTimer::singleShot(0,this,[this] { pending=false; refresh(); });
        }
        return false;
    }
private:
    QApplication &app;
    bool updating=false,pending=false;
    void refresh() {
        const auto css=stylesheet(app.palette());
        if(css==app.styleSheet()) return;
        updating=true; app.setStyleSheet(css); updating=false;
    }
};
}

void installAppearance(QApplication &app) {
    if(!app.findChild<QObject *>("shadowLogisAppearance",Qt::FindDirectChildrenOnly)) new Appearance(app);
}
}
