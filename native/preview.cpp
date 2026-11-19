#include "privatefile.h"
#include "preferences.h"
#include <QPrinterInfo>
#include <QDir>
#include "preview.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace shadowlogis {
DocumentPreview::DocumentPreview(QByteArray pdf, const QString &title, QString suggestedName, QWidget *parent)
    : QDialog(parent), filename(std::move(suggestedName)) {
    setObjectName("documentPreview"); setWindowTitle("Document preview · " + title);
    resize(1000, 900); setMinimumSize(580, 480);
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QLabel(title); heading->setTextFormat(Qt::PlainText); heading->setWordWrap(true);
    auto font = heading->font(); font.setBold(true); heading->setFont(font); layout->addWidget(heading);
    auto *toolbar = new QHBoxLayout;
    previous = new QPushButton("Previous"); previous->setObjectName("previewPrevious"); previous->setAutoDefault(false);
    next = new QPushButton("Next"); next->setObjectName("previewNext"); next->setAutoDefault(false);
    page = new QSpinBox; page->setObjectName("previewPage"); page->setPrefix("Page "); page->setMinimum(1);
    page->setKeyboardTracking(false); page->setAccessibleName("Page number");
    pageCount = new QLabel;
    zoom = new QComboBox; zoom->setObjectName("previewZoom"); zoom->setAccessibleName("Zoom");
    zoom->addItems({"Fit page", "Fit width", "50%", "75%", "100%", "125%", "150%", "200%"});
    toolbar->addWidget(previous); toolbar->addWidget(page); toolbar->addWidget(pageCount); toolbar->addWidget(next);
    toolbar->addStretch(); toolbar->addWidget(new QLabel("Zoom")); toolbar->addWidget(zoom); layout->addLayout(toolbar);
    view = new QPdfView; view->setObjectName("previewView");
    view->setPageMode(QPdfView::PageMode::SinglePage); view->setZoomMode(QPdfView::ZoomMode::FitInView);
    view->setDocument(&document); layout->addWidget(view, 1);
    message = new QLabel; message->setObjectName("previewMessage"); message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true); layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    save = buttons->addButton("Save PDF…", QDialogButtonBox::ActionRole); save->setObjectName("previewSave");
    print = buttons->addButton("Print…", QDialogButtonBox::ActionRole); print->setObjectName("previewPrint");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] { chooseSavePath(); });
    connect(print, &QPushButton::clicked, this, [this] { choosePrinter(); });
    connect(previous, &QPushButton::clicked, this, [this] { page->setValue(page->value() - 1); });
    connect(next, &QPushButton::clicked, this, [this] { page->setValue(page->value() + 1); });
    connect(page, &QSpinBox::valueChanged, this, [this](int number) {
        view->pageNavigator()->jump(number - 1, QPointF()); updateState();
    });
    connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this](int number) {
        const QSignalBlocker block(page); page->setValue(number + 1); updateState();
    });
    connect(zoom, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 2) view->setZoomMode(index == 0 ? QPdfView::ZoomMode::FitInView : QPdfView::ZoomMode::FitToWidth);
        else { view->setZoomMode(QPdfView::ZoomMode::Custom); view->setZoomFactor(zoom->currentText().chopped(1).toDouble() / 100); }
    });
    connect(&document, &QPdfDocument::statusChanged, this, [this] { updateState(); });
    connect(&document, &QPdfDocument::pageCountChanged, this, [this] { updateState(); });
    // QBuffer owns its data and outlives the PDF document, including page rendering.
    buffer.setData(std::move(pdf)); buffer.open(QIODevice::ReadOnly);
    document.load(&buffer); updateState();
}

DocumentPreview::~DocumentPreview() {
    // Destroy the view and its renderer before releasing the document or bytes.
    disconnect(&document, nullptr, this, nullptr);
    delete view;
    document.close();
}

void DocumentPreview::updateState() {
    const bool ready = document.status() == QPdfDocument::Status::Ready && document.pageCount() > 0;
    page->setMaximum(qMax(1, document.pageCount())); pageCount->setText("of " + QString::number(document.pageCount()));
    page->setEnabled(ready); zoom->setEnabled(ready); save->setEnabled(ready);
    print->setEnabled(ready);
    previous->setEnabled(ready && page->value() > 1); next->setEnabled(ready && page->value() < document.pageCount());
    if (ready) message->setText("Review the document here. Save PDF saves this exact copy. Print uses actual size; select matching paper in the printer settings.");
    else if (document.status() == QPdfDocument::Status::Error) {
        const bool locked = document.error() == QPdfDocument::Error::IncorrectPassword ||
                            document.error() == QPdfDocument::Error::UnsupportedSecurityScheme;
        message->setText(locked ? "This PDF is protected and cannot be previewed. Its original bytes remain preserved in the database."
                                : "This document could not be read as a PDF. Its original bytes remain preserved in the database.");
    } else if (document.status() == QPdfDocument::Status::Ready) message->setText("This PDF contains no pages.");
    else message->setText("Loading document…");
}

void DocumentPreview::saveCopy(const QString &path) const {
    if (document.status() != QPdfDocument::Status::Ready || document.pageCount() < 1)
        throw std::runtime_error("The PDF is not ready to save.");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) throw std::runtime_error(output.errorString().toStdString());
    makeFilePrivate(output);
    const auto &bytes = buffer.data();
    if (output.write(bytes) != bytes.size() || !output.commit())
        throw std::runtime_error("Could not save the PDF: " + output.errorString().toStdString());
}

void DocumentPreview::chooseSavePath() {
    // Do not append an extension after the file dialog's overwrite check.
    const auto folder=Preferences().value("documents/saveFolder").toString();
    const auto initial=QFileInfo(filename).isAbsolute() || folder.isEmpty() ? filename : QDir(folder).filePath(filename);
    QFileDialog dialog(this, "Save PDF", initial, "PDF (*.pdf)");
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setDefaultSuffix("pdf");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const auto path = dialog.selectedFiles().first();
    try { saveCopy(path); Preferences().setValue("documents/saveFolder",QFileInfo(path).absolutePath()); filename = path; message->setText("Saved PDF to " + QFileInfo(path).absoluteFilePath()); }
    catch (const std::exception &e) { QMessageBox::critical(this, "Could not save PDF", QString::fromUtf8(e.what())); }
}

void DocumentPreview::choosePrinter() {
    if (document.status()!=QPdfDocument::Status::Ready || document.pageCount()<1) return;
    QPrinter printer(QPrinter::HighResolution);
    const auto preferred=Preferences().value("printing/lastPrinter").toString();
    if(QPrinterInfo::availablePrinterNames().contains(preferred)) printer.setPrinterName(preferred);
    printer.setDocName(windowTitle()); printer.setFullPage(true);
    const auto size=document.pagePointSize(0);
    printer.setPageSize(QPageSize(size,QPageSize::Point,"Document paper",QPageSize::ExactMatch));
    printer.setPageOrientation(QPageLayout::Portrait);
    QPrintDialog dialog(&printer,this); dialog.setWindowTitle("Print at actual size");
    dialog.setOption(QAbstractPrintDialog::PrintPageRange); dialog.setMinMax(1,document.pageCount());
    if (dialog.exec()!=QDialog::Accepted) return;
    try { printTo(printer); Preferences().setValue("printing/lastPrinter",printer.printerName()); message->setText("Document sent to the printer at actual size."); }
    catch (const std::exception &e) { QMessageBox::critical(this,"Could not print",QString::fromUtf8(e.what())); }
}

void DocumentPreview::printTo(QPrinter &printer) {
    if (document.status()!=QPdfDocument::Status::Ready || document.pageCount()<1) throw std::runtime_error("The PDF is not ready to print.");
    printer.setFullPage(true);
    const int first=printer.printRange()==QPrinter::PageRange ? std::max(1,printer.fromPage())-1 : 0;
    const int last=printer.printRange()==QPrinter::PageRange ? std::min(document.pageCount(),printer.toPage()) : document.pageCount();
    if (first>=last) throw std::runtime_error("Choose a valid page range.");
    const auto paper=printer.pageLayout().fullRect(QPageLayout::Point).size();
    for (int i=first;i<last;++i) {
        const auto size=document.pagePointSize(i);
        if (qAbs(size.width()-paper.width())>1 || qAbs(size.height()-paper.height())>1)
            throw std::runtime_error(QString("Printer paper must match the PDF (%1 x %2 inches). Set the custom form size in the driver; automatic scaling is disabled.").arg(size.width()/72,0,'f',2).arg(size.height()/72,0,'f',2).toStdString());
    }
    QList<int> pages;
    for (int i=first;i<last;++i) pages << i;
    if (printer.pageOrder()==QPrinter::LastPageFirst) std::reverse(pages.begin(),pages.end());
    const int copies=printer.supportsMultipleCopies() ? 1 : std::max(1,printer.copyCount());
    QList<int> job;
    if (printer.collateCopies()) { for (int copy=0;copy<copies;++copy) job.append(pages); }
    else for (int i : pages) for (int copy=0;copy<copies;++copy) job << i;
    if (copies>1) printer.setCopyCount(1); // Copies are expanded here for drivers without native support.
    QPainter painter;
    if (!painter.begin(&printer)) throw std::runtime_error("The printer could not start the job.");
    try {
        for (int page=0;page<job.size();++page) {
            const int i=job[page];
            if (page>0 && !printer.newPage()) throw std::runtime_error("The printer could not advance the paper.");
            const auto size=document.pagePointSize(i);
            // Native Qt PDF rendering preserves the preview's content. Raster
            // resolution is capped for long continuous sheets; blank pixels
            // remain blank, including text-only preprinted-form jobs.
            const int dpi=std::min(300,printer.resolution());
            const auto image=document.render(i,QSize(qRound(size.width()*dpi/72),qRound(size.height()*dpi/72)));
            if (image.isNull()) throw std::runtime_error("A PDF page could not be rendered for printing.");
            painter.drawImage(QRectF(0,0,size.width()*printer.resolution()/72,size.height()*printer.resolution()/72),image);
        }
        if (!painter.end()) throw std::runtime_error("The printer did not finish the job.");
    } catch (...) { painter.end(); printer.abort(); throw; }
}
}
