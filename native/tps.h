#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QMap>

namespace shadowlogis {
struct Field {
    QString name;
    int type = 0, offset = 0, length = 0, elements = 0, decimals = 0;
    QString typeName() const;
};
struct MemoDefinition { QString name, externalFile; int length = 0, flags = 0; };
struct Memo { quint32 row = 0; int field = 0; QByteArray bytes; };
struct Row { quint32 id = 0; QByteArray bytes; };
struct Table {
    quint32 id = 0;
    int recordLength = 0, memoCount = 0, indexCount = 0;
    QList<Field> fields;
    QList<Row> rows;
    QList<MemoDefinition> memoDefinitions;
    QList<Memo> memos;
};
struct Database {
    QList<Table> tables;
    int pages = 0;
    QStringList limitations;
};
// Strict, bounded, read-only decoder. Unsupported or damaged data raises an error.
Database readTps(const QString &path, const QByteArray &ownerPassword = {});
Database parseTps(const QByteArray &bytes, const QByteArray &ownerPassword = {});
QByteArray expandRle(const QByteArray &bytes, int expectedSize);
QString displayValue(const Field &field, const QByteArray &row);
QString decodeText(const QByteArray &bytes);
QJsonObject recordValues(const Table &table, const Row &row);
}
