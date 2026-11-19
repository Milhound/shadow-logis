#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include <QDate>
#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace shadowlogis {
namespace {
// Ordinary rows stay together. Oversized memo rows continue without losing text.
class SummaryPainter {
public:
    SummaryPainter(QPdfWriter &writer, const QString &reference, const QString &name = "Shipment summary", int bodySize = 10) : writer(writer), painter(&writer), reference(reference), name(name) {
        if (!painter.isActive()) throw std::runtime_error("Cannot initialize PDF painter");
        const auto fonts = QFontDatabase::families();
        QString family = "sans-serif";
        for (const QString &candidate : {QString("DejaVu Sans"), QString("Arial"), QString("Helvetica"), QString("Noto Sans")})
            if (fonts.contains(candidate)) { family = candidate; break; }
        font = QFont(family,bodySize); painter.setFont(font);
        width = writer.width(); bottom = writer.height()-38;
        heading(true);
    }
    void details(const Record &record, const QList<Record> &cargo) {
        header = {"Detail", "Information"}; widths = {0.30,0.70}; row(header,true);
        for (const auto &[key,label] : editableFields(record.source)) {
            const auto text = fieldText(key,record.values[key]).trimmed();
            if (!text.isEmpty()) row({label,text});
        }
        const auto profile=record.values["NATIVE:COMPANY_PROFILE"].toObject();
        if(!profile.isEmpty()) row({"Logistics company",companyAddress(profile,true).join('\n')});
        if (!cargo.isEmpty()) {
            header.clear(); y += 15;
            if (y > bottom-110) nextPage();
            title("Cargo",14); y += 8;
            header = {"Marks / numbers", "Pkgs", "Description", "Weight (kg)", "Volume (m3)"};
            widths = {0.16,0.12,0.42,0.16,0.14}; row(header,true);
            const auto prefix = cargoSource(record.source).second;
            for (const auto &line : cargo) {
                QStringList cells;
                for (const QString &key : {QString("MARKS_NUMBERS"), QString("PCS"), QString("DESCRIPTION"), QString("WEIGHT_KGS"), QString("MEASURE_CBM")})
                    cells << valueText(line.values[prefix+key]);
                row(cells);
            }
        }
        header.clear(); widths = {1.0}; y += 12;
        row({"Shipment information summary. This is not an issued air waybill, bill of lading, or certificate of origin."});
        footer(); painter.end();
    }

private:
    QPdfWriter &writer;
    QPainter painter;
    QString reference, name;
    QFont font;
    QStringList header;
    QList<double> widths;
    double width = 0, bottom = 0, y = 0;
    int page = 1;
    bool boldFirstCell = true;
    void title(const QString &text, int size) {
        QFont headingFont = font; headingFont.setPointSize(size); headingFont.setBold(true);
        painter.setFont(headingFont); painter.setPen(QColor("#172b3a"));
        const double height = QFontMetricsF(headingFont).height()+5;
        painter.drawText(QRectF(0,y,width,height),Qt::AlignLeft | Qt::AlignVCenter,text); y += height;
        painter.setFont(font);
    }
    void heading(bool first) {
        y = 0;
        title(name + (first ? "" : " (continued)"),first ? 22 : 13);
        y += 7;
        painter.drawText(QRectF(0,y,width,42),Qt::AlignLeft | Qt::AlignTop,
            "Reference: " + reference + (first ? "\nPrepared: " + QDate::currentDate().toString(Qt::ISODate) : ""));
        y += first ? 55 : 30;
    }
    void footer() {
        painter.setPen(QColor("#52616d"));
        painter.drawText(QRectF(0,writer.height()-24,width,24),Qt::AlignLeft | Qt::AlignVCenter,"Prepared with Shadow Logis");
        painter.drawText(QRectF(0,writer.height()-24,width,24),Qt::AlignRight | Qt::AlignVCenter,QString::number(page));
        painter.setPen(QColor("#172b3a"));
    }
    void nextPage() {
        footer(); if (!writer.newPage()) throw std::runtime_error("Cannot create PDF page");
        ++page; heading(false);
        if (!header.isEmpty()) row(header,true);
    }
    void row(const QStringList &cells, bool isHeader = false) {
        constexpr double padding = 7;
        const auto layout = [&](const QStringList &strings) {
            std::vector<std::unique_ptr<QTextDocument>> docs;
            for (int i=0;i<strings.size();++i) {
                auto text = std::make_unique<QTextDocument>();
                QFont cellFont = font; cellFont.setBold(isHeader || (boldFirstCell && strings.size()==2 && i==0));
                text->setDefaultFont(cellFont); text->setDocumentMargin(0);
                auto option = text->defaultTextOption(); option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); text->setDefaultTextOption(option);
                text->setTextWidth(width*widths[i]-2*padding); text->setPlainText(strings[i]);
                text->size(); docs.push_back(std::move(text));
            }
            return docs;
        };
        QStringList remaining = cells;
        bool more = true;
        while (more) {
            auto documents = layout(remaining);
            double height = 0; for (const auto &doc : documents) height = std::max(height,doc->size().height()+2*padding);
            // Keep ordinary rows together; oversized notes use the remaining page space.
            if (height <= bottom-110 && y+height > bottom && y > 110) {
                const bool repeated = isHeader && cells == header;
                nextPage(); if (repeated) return;
            }
            if (bottom-y < QFontMetricsF(font).height()+2*padding) nextPage();
            more = height > bottom-y;
            if (more) {
                QStringList fragments;
                bool progressed = false;
                for (int i=0;i<remaining.size();++i) {
                    int count = 0;
                    for (auto block = documents[size_t(i)]->begin(); block.isValid(); block = block.next()) {
                        const auto *lines = block.layout();
                        for (int j=0;j<lines->lineCount();++j) {
                            const auto line = lines->lineAt(j);
                            if (lines->position().y()+line.y()+line.height()+2*padding > bottom-y) break;
                            count = block.position()+line.textStart()+line.textLength();
                            if (j+1 == lines->lineCount() && block.next().isValid()) ++count;
                        }
                    }
                    count = std::min(count,int(remaining[i].size()));
                    auto fragment = remaining[i].left(count);
                    if (fragment.endsWith('\n')) fragment.chop(1);
                    fragments << fragment; remaining[i].remove(0,count); progressed |= count > 0;
                }
                if (!progressed) throw std::runtime_error("Cannot fit PDF text on a page");
                documents = layout(fragments);
                height = 0; for (const auto &doc : documents) height = std::max(height,doc->size().height()+2*padding);
            } else { for (auto &rest : remaining) rest.clear(); }
            if (isHeader) painter.fillRect(QRectF(0,y,width,height),QColor("#edf2f6"));
            double x = 0;
            for (int i=0;i<cells.size();++i) {
                painter.save(); painter.translate(x+padding,y+padding);
                // PDF ink must not inherit a dark desktop's white text palette.
                QAbstractTextDocumentLayout::PaintContext context;
                context.palette.setColor(QPalette::Text,QColor("#172b3a"));
                context.palette.setColor(QPalette::Base,Qt::white);
                documents[size_t(i)]->documentLayout()->draw(&painter,context);
                painter.restore(); x += width*widths[i];
            }
            painter.setPen(QColor("#d7e1e8")); painter.drawLine(QPointF(0,y+height),QPointF(width,y+height));
            painter.setPen(QColor("#172b3a")); y += height;
            more = false; for (const auto &rest : remaining) more |= !rest.isEmpty();
            if (more) nextPage();
        }
    }
};
}
void exportSummary(const Record &record, const QString &path, const QList<Record> &cargo) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setTitle("Shipment summary"); writer.setCreator("Shadow Logis");
        writer.setPageSize(QPageSize(QPageSize::A4)); writer.setResolution(96);
        writer.setPageMargins(QMarginsF(17,17,17,17),QPageLayout::Millimeter);
        SummaryPainter document(writer,recordReference(record)); document.details(record,cargo);
    }
    if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
