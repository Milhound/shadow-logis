#include "tps.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QJsonArray results;
    for (const auto &entry : QDir(QString::fromLocal8Bit(argv[1])).entryInfoList(QDir::Files, QDir::Name)) {
        if (entry.suffix().compare("tps", Qt::CaseInsensitive)) continue;
        QJsonObject out{{"file", entry.fileName()}};
        try {
            const auto db = shadowlogis::readTps(entry.absoluteFilePath());
            QJsonArray tables;
            for (const auto &t : db.tables) {
                QJsonArray fields;
                for (const auto &f : t.fields)
                    fields.append(QJsonObject{{"name", f.name}, {"type", f.typeName()},
                        {"offset", f.offset}, {"length", f.length}, {"elements", f.elements}, {"decimals", f.decimals}});
                tables.append(QJsonObject{{"id", int(t.id)}, {"rows", t.rows.size()},
                    {"record_length", t.recordLength}, {"memos", t.memoCount}, {"fields", fields}});
            }
            out["tables"] = tables; out["status"] = "decoded";
        } catch (const std::exception &error) {
            out["status"] = "unsupported"; out["error"] = QString::fromUtf8(error.what());
        }
        results.append(out);
    }
    QTextStream(stdout) << QJsonDocument(results).toJson();
}
