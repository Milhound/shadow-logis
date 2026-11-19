#pragma once
#include <QJsonObject>
#include <QSqlDatabase>
#include <QList>
#include <QString>

namespace shadowlogis {
struct Record {
    qint64 id = 0;
    int version = 0;
    QString source;
    QJsonObject values;
};
enum class DocumentMatch { NativeLink, ShipmentReference, AmbiguousReference };
struct LinkedDocument { Record record; DocumentMatch match; };
class Store {
public:
    explicit Store(const QString &path);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    QList<Record> records(const QStringList &sources, bool includeDeleted = false) const;
    Record record(qint64 id) const;
    qint64 save(const Record &record);
    // Recoverable deletion: preserve shipment, links, raw data and prior versions.
    void setShipmentDeleted(const Record &shipment, bool deleted);
    QList<Record> deletedShipments() const;
    void setDocumentDeleted(const Record &document,bool deleted);
    QList<Record> deletedDocuments() const;
    QJsonObject companyProfile() const;
    void saveCompanyProfile(const QJsonObject &profile,const QJsonObject &expected);
    QJsonObject migrationReport() const;
    QList<QPair<qint64, QString>> documents() const;
    QByteArray document(qint64 id) const;
    QStringList sources() const;
    QJsonObject schema(const QString &source) const;
    Record blankRecord(const QString &source) const;
    QList<Record> cargoLines(const Record &parent) const;
    Record blankCargo(const Record &parent) const;
    QList<LinkedDocument> linkedDocuments(qint64 bookingId) const;
    QList<Record> history(qint64 recordId) const;
    // Consistent, private snapshot, including history and archived sources. Never overwrites.
    void backup(const QString &destination) const;
private:
    void setRecordDeleted(const Record &record,bool deleted);
    QString connection;
    QSqlDatabase db;
};
// Writes a new database via a temporary file. Existing files are never replaced.
QJsonObject migrate(const QString &sourceDirectory, const QString &destination, const QByteArray &ownerPassword = {});
struct ImportSelection { QStringList files; bool selectedOnly = false; };
QJsonObject migrateSelected(const QString &sourceDirectory,const QString &destination,const QByteArray &ownerPassword,const ImportSelection &selection);
void createWorkspace(const QString &destination);
QString valueText(const QJsonValue &value);
}
