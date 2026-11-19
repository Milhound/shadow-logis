#include "privatefile.h"
#include "preview.h"
#include <QApplication>
#include <QSettings>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QLabel>
#include <QPainter>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPdfWriter>
#include <QPushButton>
#include <QPrinter>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <functional>
#include <stdexcept>

using namespace shadowlogis;
namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
bool waitFor(const std::function<bool()> &condition) {
    QElapsedTimer deadline; deadline.start();
    while (!condition() && deadline.elapsed() < 5000) {
        QEventLoop loop; QTimer::singleShot(30, &loop, &QEventLoop::quit); loop.exec();
    }
    return condition();
}
QByteArray fixture() {
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
    QPdfWriter writer(&output); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    for (int i=0; i<3; ++i) {
        if (i) require(writer.newPage(), "Fixture page creation");
        painter.fillRect(QRect(50, 80, 300, 200), i==0 ? QColor(220,20,20) : QColor(20,20,220));
        painter.setPen(Qt::black); painter.drawText(QPoint(50,50), QString("Document preview page %1").arg(i+1));
    }
    painter.end(); return bytes;
}
int coloredPixels(const QImage &image, bool red) {
    int count=0;
    for (int y=0; y<image.height(); ++y) for (int x=0; x<image.width(); ++x) {
        const auto color=image.pixelColor(x,y);
        if (red ? color.red()>150 && color.green()<80 && color.blue()<80
                : color.blue()>150 && color.green()<80 && color.red()<80) ++count;
    }
    return count;
}
}

int main(int argc, char **argv) {
    // Automate Qt widgets on every OS; production retains native save dialogs.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    QTemporaryDir settings; QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try {
        QTemporaryDir temporary; require(temporary.isValid(), "Temporary directory");
        const auto bytes=fixture();
        DocumentPreview preview(bytes, "Synthetic document", temporary.filePath("saved.pdf"));
        preview.show();
        auto *view=preview.findChild<QPdfView *>("previewView");
        auto *page=preview.findChild<QSpinBox *>("previewPage");
        auto *zoom=preview.findChild<QComboBox *>("previewZoom");
        auto *previous=preview.findChild<QPushButton *>("previewPrevious");
        auto *next=preview.findChild<QPushButton *>("previewNext");
        auto *save=preview.findChild<QPushButton *>("previewSave");
        auto *print=preview.findChild<QPushButton *>("previewPrint");
        require(view && page && zoom && previous && next && save && print, "Preview controls missing");
        require(waitFor([&] { return view->document()->status()==QPdfDocument::Status::Ready; }), "PDF did not load");
        require(view->document()->pageCount()==3 && page->maximum()==3, "Preview page count");
        require(!previous->isEnabled() && next->isEnabled() && save->isEnabled(), "First-page action states");
        require(waitFor([&] { return coloredPixels(view->viewport()->grab().toImage(), true)>1000; }), "Visible preview is blank");
        require(QDir(temporary.path()).entryList(QDir::Files).isEmpty(), "Preview saved a file without being asked");
        next->click();
        require(page->value()==2 && view->pageNavigator()->currentPage()==1, "Next page navigation");
        require(waitFor([&] { return coloredPixels(view->viewport()->grab().toImage(), false)>1000; }), "Second page did not render");
        page->setValue(3); require(!next->isEnabled() && previous->isEnabled(), "Last-page boundaries");
        previous->click(); require(page->value()==2, "Previous page navigation");
        zoom->setCurrentText("150%");
        require(view->zoomMode()==QPdfView::ZoomMode::Custom && qAbs(view->zoomFactor()-1.5)<0.001, "Custom zoom");
        zoom->setCurrentText("Fit width"); require(view->zoomMode()==QPdfView::ZoomMode::FitToWidth, "Fit width");
        zoom->setCurrentText("Fit page"); require(view->zoomMode()==QPdfView::ZoomMode::FitInView, "Fit page");
        bool sawPicker=false;
        QTimer::singleShot(0, &preview, [&] {
            if (auto *picker=qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) { sawPicker=true; picker->reject(); }
        });
        save->click();
        require(sawPicker && QDir(temporary.path()).entryList(QDir::Files).isEmpty(), "Cancel Save wrote a file");
        // Exercise the Save button and its actual dialog; the saved file must be the viewed snapshot.
        QTimer::singleShot(0, &preview, [&] {
            if (auto *picker=qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                picker->selectFile(temporary.filePath("saved.pdf")); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
            }
        });
        save->click();
        QFile saved(temporary.filePath("saved.pdf"));
        require(saved.open(QIODevice::ReadOnly) && saved.readAll()==bytes, "Save changed the previewed PDF bytes");
        require(hasPrivateFilePermissions(saved.fileName()), "Saved PDF is not private");
        bool failed=false;
        try { preview.saveCopy(temporary.filePath("missing/invalid.pdf")); } catch (const std::exception &) { failed=true; }
        require(failed && view->document()->status()==QPdfDocument::Status::Ready, "Save failure destroyed the preview");
        QPrinter printer(QPrinter::HighResolution); printer.setOutputFormat(QPrinter::PdfFormat); printer.setResolution(144);
        printer.setPageSize(QPageSize(QPageSize::A4)); printer.setOutputFileName(temporary.filePath("printed.pdf"));
        preview.printTo(printer);
        QPdfDocument printed; require(printed.load(temporary.filePath("printed.pdf"))==QPdfDocument::Error::None && printed.pageCount()==3,"Native print path lost pages");
        require(coloredPixels(printed.render(0,{600,850}),true)>1000 && coloredPixels(printed.render(2,{600,850}),false)>1000,"Print path lost visible content");
        printer.setPrintRange(QPrinter::PageRange); printer.setFromTo(2,2); printer.setCopyCount(2);
        printer.setOutputFileName(temporary.filePath("range-copies.pdf")); preview.printTo(printer);
        QPdfDocument copies; require(copies.load(temporary.filePath("range-copies.pdf"))==QPdfDocument::Error::None && copies.pageCount()==2,"Print range or copy count was ignored");
        require(coloredPixels(copies.render(0,{600,850}),false)>1000 && coloredPixels(copies.render(1,{600,850}),false)>1000,"Print copies used the wrong page");
        printer.setPageSize(QPageSize(QPageSize::Letter)); printer.setOutputFileName(temporary.filePath("wrong-paper.pdf"));
        failed=false; try { preview.printTo(printer); } catch (const std::exception &) { failed=true; }
        require(failed && !QFile::exists(temporary.filePath("wrong-paper.pdf")),"Print path silently scaled to the wrong paper");
        DocumentPreview invalid("not a PDF", "Invalid document", temporary.filePath("invalid.pdf"));
        require(!invalid.findChild<QPushButton *>("previewSave")->isEnabled(), "Invalid PDF can be saved");
        require(!invalid.findChild<QPushButton *>("previewPrint")->isEnabled(), "Invalid PDF can be printed");
        require(invalid.findChild<QLabel *>("previewMessage")->text().contains("could not be read"), "Invalid PDF has no explanation");
        QTextStream(stdout)<<"PASS: native preview renders pages, navigation and zoom work, cancel creates no file, Save preserves exact bytes and private permissions, errors remain reviewable.\n";
        return 0;
    } catch (const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
