#include "runtimecheck.h"
#include "airlines.h"
#include "privatefile.h"
#include <QApplication>
#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPrinter>
#include <QRawFont>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <stdexcept>

namespace shadowlogis {
namespace {
void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
}
void checkRuntime(QApplication &app) {
    require(QRawFont::fromFont(app.font()).familyName() == "Inter Variable",
            "Runtime check: embedded Inter font did not load.");
    auto italic = app.font(); italic.setItalic(true);
    const auto actualItalic = QRawFont::fromFont(italic);
    require(actualItalic.familyName() == "Inter Variable" && actualItalic.style() == QFont::StyleItalic,
            "Runtime check: embedded italic font did not load.");
    require(!app.windowIcon().pixmap(32, 32).isNull(), "Runtime check: application icon is missing.");
    require(!airlineDirectory().isEmpty(), "Runtime check: airline reference resource is missing.");

    const QString connection = "synthetic-runtime-check";
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(":memory:");
        require(database.open(), "Runtime check: SQLite driver could not open an in-memory database.");
        QSqlQuery query(database);
        require(query.exec("CREATE TABLE sample (weight TEXT)") &&
                query.exec("INSERT INTO sample VALUES ('8.50')") &&
                query.exec("SELECT weight FROM sample") && query.next() && query.value(0).toString() == "8.50",
                "Runtime check: SQLite read/write failed.");
    }
    QSqlDatabase::removeDatabase(connection);

    // Exercise logo import codecs as well as PDF output and PDFium rendering.
    for (const auto *format : {"PNG", "JPEG"}) {
        QImage source(24, 24, QImage::Format_RGB32); source.fill(Qt::white);
        QByteArray encoded; QBuffer buffer(&encoded);
        require(buffer.open(QIODevice::WriteOnly) && source.save(&buffer, format),
                "Runtime check: image encoder is missing.");
        require(!QImage::fromData(encoded, format).isNull(), "Runtime check: image decoder is missing.");
    }
    QTemporaryDir temporary;
    require(temporary.isValid(), "Runtime check: temporary storage is unavailable.");
    {
        QFile output(temporary.filePath("synthetic-private.txt"));
        require(output.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Runtime check: private file creation failed.");
        makeFilePrivate(output);
        require(output.write("synthetic") == 9, "Runtime check: private file write failed."); output.close();
        require(hasPrivateFilePermissions(output.fileName()), "Runtime check: file privacy was not preserved.");
    }
    const auto path = temporary.filePath("synthetic.pdf");
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat); printer.setOutputFileName(path);
        QPainter painter;
        require(painter.begin(&printer), "Runtime check: PDF print device failed.");
        painter.setFont(app.font()); painter.drawText(100, 200, "Shadow Logis runtime check");
        require(painter.end(), "Runtime check: PDF print output failed.");
    }
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Runtime check: printed PDF is missing.");
    QBuffer pdf; pdf.setData(file.readAll()); pdf.open(QIODevice::ReadOnly);
    QPdfDocument document;
    document.load(&pdf);
    QElapsedTimer deadline; deadline.start();
    while (document.status() == QPdfDocument::Status::Loading && deadline.elapsed() < 10000)
        app.processEvents(QEventLoop::AllEvents, 50);
    require(document.status() == QPdfDocument::Status::Ready && document.pageCount() == 1,
            "Runtime check: PDF preview could not read printed output.");
    require(!document.render(0, QSize(256, 330)).isNull(), "Runtime check: PDF preview rendering failed.");
}
}
