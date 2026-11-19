#include "privatefile.h"
#include "store.h"
#include "tps.h"
#include "domain.h"
#include "entrylogic.h"
#include "workspace.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <optional>
#include <stdexcept>

namespace shadowlogis {
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
void execute(QSqlQuery &q) { if (!q.exec()) fail(q.lastError().text()); }
void sql(QSqlDatabase &db, const QString &text) {
    QSqlQuery q(db); if (!q.exec(text)) fail(q.lastError().text());
}
QString json(const QJsonObject &object) { return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)); }
QJsonObject object(const QString &text) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) fail("Invalid JSON in database");
    return doc.object();
}
QByteArray readFile(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    if (file.size() > 256 * 1024 * 1024) fail("Source file exceeds 256 MiB import limit");
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) fail(file.errorString());
    return bytes;
}
Record fromQuery(const QSqlQuery &q) {
    return {q.value(0).toLongLong(), q.value(1).toInt(), q.value(2).toString(), object(q.value(3).toString())};
}
// Stable format identifier: retain compatibility with existing migrated data.
constexpr int ApplicationId = 0x4f41534e;
}

QString valueText(const QJsonValue &value) {
    if (value.isArray()) {
        QStringList lines;
        for (const auto &item : value.toArray()) lines << valueText(item);
        return lines.join('\n');
    }
    if (value.isObject()) return "[binary content preserved]";
    if (value.isString()) return value.toString();
    return value.toVariant().toString();
}

static QJsonObject writeWorkspace(const QString &sourceDirectory, const QString &destination, const QByteArray &ownerPassword, const ImportSelection &selection, bool empty) {
    const QDir source(sourceDirectory);
    if (!source.exists()) fail("Source directory does not exist");
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) fail("Refusing to replace an existing database");
    if (!QFileInfo(destination).dir().exists()) fail("Output directory must already exist");
    const auto partial = destination + ".partial-" + QUuid::createUuid().toString(QUuid::Id128);
    QFile reservation(partial);
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail(reservation.errorString());
    makeFilePrivate(reservation);
    reservation.close();
    const auto name = QUuid::createUuid().toString();
    auto db = QSqlDatabase::addDatabase("QSQLITE", name); db.setDatabaseName(partial);
    QJsonObject report;
    try {
        if (!db.open()) fail(db.lastError().text());
        sql(db, "PRAGMA synchronous=FULL"); sql(db, "PRAGMA foreign_keys=ON");
        sql(db, QString("PRAGMA application_id=%1").arg(ApplicationId)); sql(db, "PRAGMA user_version=1");
        if (!db.transaction()) fail(db.lastError().text());
        sql(db, "CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)");
        sql(db, "CREATE TABLE source_files(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, sha256 TEXT NOT NULL, original BLOB NOT NULL, status TEXT NOT NULL, issue TEXT NOT NULL)");
        sql(db, "CREATE TABLE datasets(source TEXT NOT NULL, table_id INTEGER NOT NULL, schema_json TEXT NOT NULL, imported_rows INTEGER NOT NULL, PRIMARY KEY(source,table_id))");
        sql(db, "CREATE TABLE records(id INTEGER PRIMARY KEY, version INTEGER NOT NULL DEFAULT 1, source TEXT NOT NULL, table_id INTEGER NOT NULL, legacy_id INTEGER, values_json TEXT NOT NULL, raw_record BLOB, UNIQUE(source,table_id,legacy_id))");
        sql(db, "CREATE INDEX records_source ON records(source)");
        sql(db, "CREATE TABLE record_history(record_id INTEGER NOT NULL REFERENCES records(id), version INTEGER NOT NULL, values_json TEXT NOT NULL, saved_at TEXT NOT NULL, PRIMARY KEY(record_id,version))");
        sql(db, "CREATE TABLE memos(source TEXT NOT NULL, table_id INTEGER NOT NULL, row_id INTEGER NOT NULL, field_index INTEGER NOT NULL, original BLOB NOT NULL, PRIMARY KEY(source,table_id,row_id,field_index))");
        sql(db, "CREATE TABLE documents(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, sha256 TEXT NOT NULL, pdf BLOB NOT NULL)");
        int decoded = 0, archived = 0, recordCount = 0, memoCount = 0, documentCount = 0;
        QJsonArray issues, fileCounts;
        QDirIterator iterator(source.absolutePath(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        QStringList paths;
        while (!empty && !selection.selectedOnly && iterator.hasNext()) {
            const auto path = iterator.next();
            const auto suffix = QFileInfo(path).suffix().toLower();
            if (!empty && !selection.selectedOnly && (suffix == "tps" || suffix == "pdf")) paths << path;
        }
        if(selection.selectedOnly) for(const auto &relative : selection.files) {
            const QFileInfo file(source.filePath(relative));
            const auto canonical=file.canonicalFilePath();
            if(QDir::isAbsolutePath(relative) || relative.split('/').contains("..") ||
               !file.isFile() || file.isSymLink() || !canonical.startsWith(source.canonicalPath()+'/') ||
               !QStringList{"tps","pdf"}.contains(file.suffix().toLower())) fail("Invalid selected import file");
            paths<<file.absoluteFilePath();
        }
        paths.removeDuplicates(); paths.sort(Qt::CaseSensitive);
        for (const auto &path : paths) {
            const auto relative = source.relativeFilePath(path);
            const auto datasetSource=selection.selectedOnly ? QFileInfo(path).fileName().toUpper() : relative;
            const auto bytes = readFile(path);
            const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            if (QFileInfo(path).suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
                QSqlQuery q(db); q.prepare("INSERT INTO documents(path,sha256,pdf) VALUES(?,?,?)");
                q.addBindValue(relative); q.addBindValue(hash); q.addBindValue(bytes); execute(q); ++documentCount;
                continue;
            }
            std::optional<Database> parsed; QString issue;
            try { parsed = parseTps(bytes, ownerPassword); }
            catch (const std::exception &error) { issue = QString::fromUtf8(error.what()); }
            QSqlQuery archive(db); archive.prepare("INSERT INTO source_files(path,sha256,original,status,issue) VALUES(?,?,?,?,?)");
            archive.addBindValue(relative); archive.addBindValue(hash); archive.addBindValue(bytes);
            archive.addBindValue(parsed ? "decoded" : "archived-only"); archive.addBindValue(issue.isNull() ? QString("") : issue); execute(archive);
            if (!parsed) {
                ++archived; issues.append(QJsonObject{{"file", relative}, {"issue", issue}}); continue;
            }
            ++decoded; int rowsInFile = 0;
            for (const auto &table : parsed->tables) {
                QJsonArray fields, memoDefinitions;
                for (const auto &f : table.fields)
                    fields.append(QJsonObject{{"name", f.name}, {"type", f.type}, {"offset", f.offset},
                        {"length", f.length}, {"elements", f.elements}, {"decimals", f.decimals}});
                for (const auto &m : table.memoDefinitions)
                    memoDefinitions.append(QJsonObject{{"name", m.name}, {"external_file", m.externalFile}, {"flags", m.flags}, {"length", m.length}});
                QSqlQuery schema(db); schema.prepare("INSERT INTO datasets VALUES(?,?,?,?)");
                schema.addBindValue(datasetSource); schema.addBindValue(table.id);
                schema.addBindValue(json({{"fields", fields}, {"memos", memoDefinitions}, {"record_length", table.recordLength}}));
                schema.addBindValue(table.rows.size()); execute(schema);
                QSqlQuery row(db); row.prepare("INSERT INTO records(source,table_id,legacy_id,values_json,raw_record) VALUES(?,?,?,?,?)");
                for (const auto &record : table.rows) {
                    row.bindValue(0, datasetSource); row.bindValue(1, table.id); row.bindValue(2, record.id);
                    row.bindValue(3, json(recordValues(table, record))); row.bindValue(4, record.bytes); execute(row);
                    ++recordCount; ++rowsInFile;
                }
                QSqlQuery memo(db); memo.prepare("INSERT INTO memos VALUES(?,?,?,?,?)");
                for (const auto &m : table.memos) {
                    memo.bindValue(0, datasetSource); memo.bindValue(1, table.id); memo.bindValue(2, m.row);
                    memo.bindValue(3, m.field); memo.bindValue(4, m.bytes); execute(memo); ++memoCount;
                }
                QSqlQuery verify(db); verify.prepare("SELECT legacy_id,raw_record FROM records WHERE source=? AND table_id=? ORDER BY legacy_id");
                verify.addBindValue(datasetSource); verify.addBindValue(table.id); execute(verify);
                int index = 0;
                while (verify.next()) {
                    if (index >= table.rows.size() || verify.value(0).toUInt() != table.rows[index].id ||
                        verify.value(1).toByteArray() != table.rows[index].bytes) fail("Record read-back verification failed");
                    ++index;
                }
                if (index != table.rows.size()) fail("Record count verification failed");
            }
            fileCounts.append(QJsonObject{{"file", relative}, {"rows", rowsInFile}});
        }
        if (!empty && !decoded && !(selection.selectedOnly && documentCount && !archived))
            fail("No selected TPS tables decoded. Check the data folder and owner password; no workspace was created.");
        // Every workspace can create all supported document types, even after a selective import.
        for(const auto &recordSource : workspaceSources()) {
            QSqlQuery existing(db); existing.prepare("SELECT 1 FROM datasets WHERE source=? LIMIT 1"); existing.addBindValue(recordSource); execute(existing);
            if(existing.next()) continue;
            QSqlQuery native(db); native.prepare("INSERT INTO datasets VALUES(?,1,?,0)");
            native.addBindValue(recordSource); native.addBindValue(json(workspaceSchema(recordSource))); execute(native);
        }
        // Verify the archived source files and historical PDFs independently of row decoding.
        for (const QString &query : {QString("SELECT sha256,original FROM source_files"), QString("SELECT sha256,pdf FROM documents")}) {
            QSqlQuery check(db); if (!check.exec(query)) fail(check.lastError().text());
            while (check.next())
                if (QCryptographicHash::hash(check.value(1).toByteArray(), QCryptographicHash::Sha256).toHex() != check.value(0).toByteArray())
                    fail("Archived file hash verification failed");
        }
        report = {{"workspace_kind",empty ? "new" : "imported"},{"selected_only",selection.selectedOnly},{"selected_files",QJsonArray::fromStringList(selection.files)},{"format_version", 1}, {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"decoded_tps", decoded}, {"archived_only_tps", archived}, {"records", recordCount},
            {"memos", memoCount}, {"historical_pdfs", documentCount}, {"issues", issues}, {"files", fileCounts},
            {"encoding", "Windows-1252 (original bytes retained)"},
            {"validation", "Source hashes and raw record read-back verified; original application parity and TPS indexes not yet validated"}};
        QSqlQuery meta(db); meta.prepare("INSERT INTO metadata VALUES('migration_report',?)"); meta.addBindValue(json(report)); execute(meta);
        if (!db.commit()) fail(db.lastError().text());
        QSqlQuery integrity(db); if (!integrity.exec("PRAGMA integrity_check") || !integrity.next() || integrity.value(0).toString() != "ok") fail("SQLite integrity check failed");
        integrity.finish(); db.close();
    } catch (...) {
        db.rollback(); db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
        // The explicitly named partial database is retained for diagnosis, never promoted.
        throw;
    }
    db = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
    if (!QFile::rename(partial, destination)) fail("Could not publish database; the partial file is retained");
    return report;
}

QJsonObject migrate(const QString &sourceDirectory,const QString &destination,const QByteArray &password) {
    return writeWorkspace(sourceDirectory,destination,password,{},false);
}
QJsonObject migrateSelected(const QString &sourceDirectory,const QString &destination,const QByteArray &password,const ImportSelection &selection) {
    if(!selection.selectedOnly || selection.files.isEmpty()) fail("Select at least one available import category");
    return writeWorkspace(sourceDirectory,destination,password,selection,false);
}
void createWorkspace(const QString &destination) {
    writeWorkspace(QFileInfo(destination).absolutePath(),destination,{},{{},true},true);
}

Store::Store(const QString &path) : connection(QUuid::createUuid().toString()) {
    if (!QFileInfo(path).isFile()) fail("Database does not exist; run migration first");
    db = QSqlDatabase::addDatabase("QSQLITE", connection); db.setDatabaseName(path);
    try {
        if (!db.open()) fail(db.lastError().text());
        QSqlQuery id(db); id.exec("PRAGMA application_id");
        if (!id.next() || id.value(0).toInt() != ApplicationId) fail("This is not a Shadow Logis database");
        QSqlQuery version(db); version.exec("PRAGMA user_version");
        if (!version.next() || version.value(0).toInt() != 1) fail("Unsupported database schema version");
        sql(db, "PRAGMA foreign_keys=ON"); sql(db, "PRAGMA busy_timeout=5000");
    } catch (...) { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); throw; }
}
Store::~Store() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); }
QJsonObject Store::companyProfile() const {
    QSqlQuery q(db); q.prepare("SELECT value FROM metadata WHERE key='company_profile'"); execute(q);
    return q.next() ? object(q.value(0).toString()) : QJsonObject{};
}
void Store::saveCompanyProfile(const QJsonObject &profile,const QJsonObject &expected) {
    if(json(profile).size()>3*1024*1024) fail("Company profile is too large.");
    if(!db.transaction()) fail(db.lastError().text());
    try {
        if(companyProfile()!=expected) fail("Company profile changed. Reopen it before saving.");
        for(const auto &[key,source] : QList<QPair<QString,QString>>{{"contact_id","CONTACTS.TPS"},{"customer_id","CUSTOMER.TPS"}})
            if(!profile[key].toString().isEmpty() && record(profile[key].toString().toLongLong()).source!=source) fail("Invalid company profile contact.");
        QSqlQuery q(db); q.prepare("INSERT INTO metadata(key,value) VALUES('company_profile',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        q.addBindValue(json(profile)); execute(q); if(!db.commit()) fail(db.lastError().text());
    } catch(...) { db.rollback(); throw; }
}
QStringList Store::sources() const {
    QSqlQuery q(db); if (!q.exec("SELECT DISTINCT source FROM datasets ORDER BY source")) fail(q.lastError().text());
    QStringList out; while (q.next()) out << q.value(0).toString(); out << DangerousGoodsSource; return out;
}
QList<Record> Store::records(const QStringList &sources, bool includeDeleted) const {
    if (sources.isEmpty()) return {};
    QStringList marks; for (qsizetype i = 0; i < sources.size(); ++i) marks << "?";
    const QString visible = includeDeleted ? QString() : " AND COALESCE(json_extract(values_json,'$.\"NATIVE:DELETED_AT\"'),'')=''";
    QSqlQuery q(db); q.prepare("SELECT id,version,source,values_json FROM records WHERE source IN (" + marks.join(',') + ")" + visible + " ORDER BY id DESC");
    for (const auto &source : sources) q.addBindValue(source);
    execute(q); QList<Record> out; while (q.next()) out << fromQuery(q); return out;
}
Record Store::record(qint64 id) const {
    QSqlQuery q(db); q.prepare("SELECT id,version,source,values_json FROM records WHERE id=?"); q.addBindValue(id); execute(q);
    if (!q.next()) fail("Record no longer exists");
    auto out=fromQuery(q);
    if(out.source=="DODR.TPS" || out.source=="OBL.TPS") {
        const auto blank=blankRecord(out.source);
        for(auto it=blank.values.begin();it!=blank.values.end();++it) if(!out.values.contains(it.key())) out.values[it.key()]=it.value();
    }
    return out;
}
qint64 Store::save(const Record &input) {
    Record r = calculateEntry(input);
    if (r.source.isEmpty() || r.values.isEmpty()) fail("Record needs a source and fields");
    if (editableFields(r.source).isEmpty()) fail("Editing is not enabled for this record type");
    if (r.source==DangerousGoodsSource) validateDangerousGoods(r.values);
    const bool cargo = r.source == "DODRLINE.TPS" || r.source == "OBLLINE.TPS" || r.source == "PLLINE.TPS" || r.source == "CERTLINE.TPS";
    if (r.source == "CUSTOMER.TPS" || r.source == "CONTACTS.TPS") {
        if (recordTitle(r).trimmed().isEmpty()) fail("A name is required");
    } else if (recordReference(r).trimmed().isEmpty()) fail("A reference is required");
    if (!db.transaction()) fail(db.lastError().text());
    try {
        qint64 id = r.id;
        const auto prior = id ? record(id) : Record{};
        if (r.values.value("NATIVE:DELETED_AT") != prior.values.value("NATIVE:DELETED_AT")) fail("Use the delete or restore action");
        if (!prior.values.value("NATIVE:DELETED_AT").toString().isEmpty()) fail("Restore this record before editing it");
        if (r.values.contains("NATIVE:BOOKING_ID")) {
            bool ok = false; const auto bookingId = r.values["NATIVE:BOOKING_ID"].toString().toLongLong(&ok);
            if (!ok || record(bookingId).source != "BOOKING.TPS" ||
                !QStringList{"AWB.TPS","OBL.TPS","DODR.TPS","PLIST.TPS","CERTORGN.TPS","SIGHT.TPS",DangerousGoodsSource}.contains(r.source)) fail("Invalid linked booking");
            if(!id && !record(bookingId).values.value("NATIVE:DELETED_AT").toString().isEmpty()) fail("Restore the shipment before creating documents");
        }
        if (id && r.values.value("NATIVE:BOOKING_ID") != prior.values.value("NATIVE:BOOKING_ID")) fail("The linked booking cannot be changed");
        Record marineSource;
        for(const auto &key : {"NATIVE:MARINE_SOURCE_ID","NATIVE:MARINE_SOURCE_VERSION"})
            if(id && r.values.value(key)!=prior.values.value(key)) fail("The source document link cannot be changed");
        if(!id && r.values.contains("NATIVE:MARINE_SOURCE_ID")) {
            if(!r.values.contains("NATIVE:BOOKING_ID")) fail("Choose a shipment before copying a marine document");
            marineSource=record(r.values["NATIVE:MARINE_SOURCE_ID"].toString().toLongLong());
            if(marineSource.version!=r.values["NATIVE:MARINE_SOURCE_VERSION"].toString().toInt()) fail("The source document changed. Reopen it before copying.");
            marineDocumentFromDocument(marineSource,r); // validates types and shipment reference
            const auto owner=marineSource.values.value("NATIVE:BOOKING_ID").toString();
            if(!owner.isEmpty() && owner!=r.values.value("NATIVE:BOOKING_ID").toString()) fail("The source document belongs to another shipment");
        }
        if(cargo && id) {
            const auto prefix=editableFields(r.source).first().first.section(':',0,0)+':';
            const auto parentSource=r.source=="DODRLINE.TPS" ? "DODR.TPS" : r.source=="OBLLINE.TPS" ? "OBL.TPS" : r.source=="PLLINE.TPS" ? "PLIST.TPS" : "CERTORGN.TPS";
            bool active=false;
            for(const auto &parent : records({parentSource})) {
                const auto pp=editableFields(parent.source).first().first.section(':',0,0)+':';
                if(parent.values[pp+"REFERENCE_NUMBER"]==r.values[prefix+"REFERENCE_NUMBER"] && parent.values[pp+"ID_SEQUENCE"]==r.values[prefix+"ID_SEQUENCE"]) { active=true; break; }
            }
            if(!active) fail("Restore the parent document before editing cargo.");
        }
        if (r.source == "AWB.TPS") {
            bool cargoChanged = !id;
            for (const auto &key : airCargoKeys()) cargoChanged |= r.values[key] != prior.values[key];
            if (cargoChanged) {
                const auto totals = airCargoTotals(r.values);
                r.values["AWB:PIECES_TOTAL"] = totals.pieces; r.values["AWB:GROSS_WEIGHT_TOTAL"] = totals.grossWeight;
            }
        }
        QMap<QString,QJsonObject> fields;
        for (const auto &item : schema(r.source).value("fields").toArray()) {
            const auto f = item.toObject(); fields[f["name"].toString()] = f;
        }
        for (const auto &[key,label] : editableFields(r.source)) {
            if (!r.values.contains(key) || (id && r.values[key] == prior.values[key])) continue;
            const auto f = fields.value(key); const auto value = r.values[key];
            const int count = vicsOrderKeys().contains(key) ? 8 : f.value("elements").toInt(1), type = f.value("type").toInt();
            if ((count > 1 && (!value.isArray() || value.toArray().size() != count)) || (count <= 1 && !value.isString()))
                fail(label + ": invalid field shape");
            QJsonArray values = count > 1 ? value.toArray() : QJsonArray{value};
            for (const auto &entry : values) {
                if (!entry.isString()) fail(label + ": text value required");
                const auto text = entry.toString();
                if (isDateField(key)) {
                    bool ok = false; const auto days = text.toLongLong(&ok);
                    if (!ok || (days != 0 && (days < 4 || days > 2994626))) fail(label + ": invalid date");
                } else if (type >= 1 && type <= 10 && type != 4) {
                    const auto pattern = type <= 7 ? "^[0-9]+$" : "^[0-9]+(?:\\.[0-9]+)?$";
                    if (!QRegularExpression(pattern).match(text).hasMatch()) fail(label + ": a nonnegative number is required");
                }
            }
        }
        if (id) {
            const auto old = prior;
            if (old.source != r.source || old.version != r.version) fail("Record changed since it was opened. Reload it before saving.");
            for (auto field = old.values.begin(); field != old.values.end(); ++field)
                if ((field.key().endsWith(":REFERENCE_NUMBER") || field.key().endsWith(":ID_SEQUENCE") ||
                     field.key() == "CUS:NUMBER" || field.key() == "CTA:NUMBER" || field.key() == "BOO:QUOTE_NUMBER") &&
                    r.values.value(field.key()) != field.value()) fail("An existing record's identity cannot be changed");
            QSqlQuery history(db); history.prepare("INSERT INTO record_history VALUES(?,?,?,?)");
            history.addBindValue(id); history.addBindValue(old.version); history.addBindValue(json(old.values));
            history.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)); execute(history);
            QSqlQuery update(db); update.prepare("UPDATE records SET values_json=?,version=version+1 WHERE id=? AND version=?");
            update.addBindValue(json(r.values)); update.addBindValue(id); update.addBindValue(r.version); execute(update);
            if (update.numRowsAffected() != 1) fail("Concurrent record modification");
        } else {
            if (cargo) {
                const auto prefix = editableFields(r.source).first().first.section(':',0,0) + ':';
                const auto parentSource = r.source == "DODRLINE.TPS" ? "DODR.TPS" : r.source == "OBLLINE.TPS" ? "OBL.TPS" : r.source == "PLLINE.TPS" ? "PLIST.TPS" : "CERTORGN.TPS";
                bool found = false;
                for (const auto &parent : records({parentSource})) {
                    const auto pp = editableFields(parent.source).first().first.section(':',0,0) + ':';
                    if (parent.values[pp+"REFERENCE_NUMBER"] == r.values[prefix+"REFERENCE_NUMBER"] &&
                        parent.values[pp+"ID_SEQUENCE"] == r.values[prefix+"ID_SEQUENCE"]) { found = true; break; }
                }
                if (!found) fail("Cargo line has no matching document");
                const auto lineKey = cargoLineNumberKey(r.source);
                QSqlQuery next(db); next.prepare("SELECT COALESCE(MAX(CAST(json_extract(values_json,?) AS REAL)),0)+1 FROM records WHERE source=? AND json_extract(values_json,?)=? AND json_extract(values_json,?)=?");
                next.addBindValue("$.\""+lineKey+"\""); next.addBindValue(r.source);
                next.addBindValue("$.\""+prefix+"REFERENCE_NUMBER\""); next.addBindValue(r.values[prefix+"REFERENCE_NUMBER"].toString());
                next.addBindValue("$.\""+prefix+"ID_SEQUENCE\""); next.addBindValue(r.values[prefix+"ID_SEQUENCE"].toString()); execute(next);
                if (!next.next()) fail("Cannot allocate cargo line number");
                r.values[lineKey] = next.value(0).toString();
            }
            // Allocate legacy-style identities inside the same transaction as the insert.
            // SQLite serializes writers; a competing snapshot fails instead of reusing an ID.
            for (auto field = r.values.begin(); field != r.values.end(); ++field) {
                if (!cargo && (field.key().endsWith(":ID_SEQUENCE") || field.key() == "CUS:NUMBER" ||
                    field.key() == "CTA:NUMBER" || field.key() == "BOO:QUOTE_NUMBER")) {
                    QSqlQuery next(db); next.prepare("SELECT COALESCE(MAX(CAST(json_extract(values_json,?) AS INTEGER)),0)+1 FROM records WHERE source=?");
                    next.addBindValue("$.\"" + field.key() + "\""); next.addBindValue(r.source); execute(next);
                    if (!next.next()) fail("Cannot allocate record number");
                    field.value() = next.value(0).toString();
                }
                if (!cargo && r.source!=DangerousGoodsSource && !r.values.contains("NATIVE:BOOKING_ID") && field.key().endsWith(":REFERENCE_NUMBER")) {
                    QSqlQuery duplicate(db); duplicate.prepare("SELECT 1 FROM records WHERE source=? AND lower(trim(json_extract(values_json,?)))=lower(trim(?)) LIMIT 1");
                    duplicate.addBindValue(r.source); duplicate.addBindValue("$.\"" + field.key() + "\"");
                    duplicate.addBindValue(field.value().toString()); execute(duplicate);
                    if (duplicate.next()) fail("This reference already exists in this record type");
                }
            }
            QSqlQuery table(db); table.prepare("SELECT table_id FROM datasets WHERE source=? ORDER BY table_id LIMIT 1"); table.addBindValue(r.source); execute(table);
            const bool hasTable=table.next();
            if (!hasTable && r.source!=DangerousGoodsSource) fail("No imported schema exists for this record type");
            QSqlQuery insert(db); insert.prepare("INSERT INTO records(source,table_id,values_json) VALUES(?,?,?)");
            insert.addBindValue(r.source); insert.addBindValue(hasTable ? table.value(0) : QVariant(0)); insert.addBindValue(json(r.values)); execute(insert);
            id = insert.lastInsertId().toLongLong();
            // Seed the document's first cargo line from the booking in the same
            // transaction. A failed line insert must not leave a partial document.
            if(r.values.contains("NATIVE:BOOKING_ID") && !cargoSource(r.source).first.isEmpty()) {
                auto initialLines=cargoLines(r);
                if(!initialLines.isEmpty()) {
                    QSqlQuery cargoTable(db); cargoTable.prepare("SELECT table_id FROM datasets WHERE source=? ORDER BY table_id LIMIT 1"); cargoTable.addBindValue(initialLines.first().source); execute(cargoTable);
                    if(!cargoTable.next()) fail("No cargo schema exists for this document type");
                    int number=0;
                    for(auto &initial : initialLines) {
                        initial.values[cargoLineNumberKey(initial.source)]=QString::number(++number);
                        QSqlQuery addCargo(db); addCargo.prepare("INSERT INTO records(source,table_id,values_json) VALUES(?,?,?)");
                        addCargo.addBindValue(initial.source); addCargo.addBindValue(cargoTable.value(0)); addCargo.addBindValue(json(initial.values)); execute(addCargo);
                    }
                }
            }
        }
        if (!db.commit()) fail(db.lastError().text());
        return id;
    } catch (...) { db.rollback(); throw; }
}
void Store::setShipmentDeleted(const Record &shipment, bool deleted) {
    if(!shipment.id || shipment.source!="BOOKING.TPS") fail("Select a saved shipment");
    setRecordDeleted(shipment,deleted);
}
void Store::setDocumentDeleted(const Record &document,bool deleted) {
    if(!document.id || !QStringList{"AWB.TPS","OBL.TPS","DODR.TPS","CERTORGN.TPS","PLIST.TPS","SIGHT.TPS"}.contains(document.source)) fail("Select a saved shipping document");
    setRecordDeleted(document,deleted);
}
void Store::setRecordDeleted(const Record &recordToChange,bool deleted) {
    if(!db.transaction()) fail(db.lastError().text());
    try {
        const auto prior=record(recordToChange.id);
        if(prior.source!=recordToChange.source || prior.version!=recordToChange.version) fail("This record changed. Reload it before deleting or restoring it.");
        const bool wasDeleted=!prior.values.value("NATIVE:DELETED_AT").toString().isEmpty();
        if(wasDeleted==deleted) fail(deleted ? "Record is already deleted" : "Record is already active");
        auto values=prior.values;
        if(deleted) values["NATIVE:DELETED_AT"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        else values.remove("NATIVE:DELETED_AT");
        QSqlQuery history(db); history.prepare("INSERT INTO record_history VALUES(?,?,?,?)");
        history.addBindValue(prior.id); history.addBindValue(prior.version); history.addBindValue(json(prior.values));
        history.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)); execute(history);
        QSqlQuery update(db); update.prepare("UPDATE records SET values_json=?,version=version+1 WHERE id=? AND version=?");
        update.addBindValue(json(values)); update.addBindValue(prior.id); update.addBindValue(prior.version); execute(update);
        if(update.numRowsAffected()!=1) fail("Record changed before the operation completed");
        if(!db.commit()) fail(db.lastError().text());
    } catch(...) { db.rollback(); throw; }
}
QList<Record> Store::deletedShipments() const {
    auto all=records({"BOOKING.TPS"},true);
    all.removeIf([](const Record &r) { return r.values.value("NATIVE:DELETED_AT").toString().isEmpty(); });
    return all;
}
QList<Record> Store::deletedDocuments() const {
    auto all=records({"AWB.TPS","OBL.TPS","DODR.TPS","CERTORGN.TPS","PLIST.TPS","SIGHT.TPS"},true);
    all.removeIf([](const Record &r) { return r.values.value("NATIVE:DELETED_AT").toString().isEmpty(); }); return all;
}
QJsonObject Store::migrationReport() const {
    QSqlQuery q(db); if (!q.exec("SELECT value FROM metadata WHERE key='migration_report'") || !q.next()) fail("Migration report missing");
    return object(q.value(0).toString());
}
QList<QPair<qint64, QString>> Store::documents() const {
    QSqlQuery q(db); if (!q.exec("SELECT id,path FROM documents ORDER BY path")) fail(q.lastError().text());
    QList<QPair<qint64, QString>> out; while (q.next()) out.append({q.value(0).toLongLong(), q.value(1).toString()}); return out;
}
QByteArray Store::document(qint64 id) const {
    QSqlQuery q(db); q.prepare("SELECT pdf FROM documents WHERE id=?"); q.addBindValue(id); execute(q);
    if (!q.next()) fail("Historical PDF not found");
    return q.value(0).toByteArray();
}
QJsonObject Store::schema(const QString &source) const {
    if (source==DangerousGoodsSource) return dangerousGoodsSchema();
    QSqlQuery q(db); q.prepare("SELECT schema_json FROM datasets WHERE source=? ORDER BY table_id LIMIT 1");
    q.addBindValue(source); execute(q);
    if (!q.next()) fail("No imported schema for this record type");
    return marineSchema(source,object(q.value(0).toString()));
}
Record Store::blankRecord(const QString &source) const {
    if (source==DangerousGoodsSource) return blankDangerousGoods();
    Record out; out.source = source;
    const auto definition = schema(source);
    for (const auto &item : definition.value("fields").toArray()) {
        const auto field = item.toObject();
        const int type = field.value("type").toInt(), count = field.value("elements").toInt();
        const QString value = type <= 10 && type != 4 ? "0" : "";
        if (count > 1) {
            QJsonArray array; for (int i = 0; i < count; ++i) array.append(value);
            out.values[field.value("name").toString()] = array;
        } else out.values[field.value("name").toString()] = value;
    }
    for (const auto &item : definition.value("memos").toArray()) {
        const auto memo = item.toObject();
        if (!(memo.value("flags").toInt() & 4)) out.values[memo.value("name").toString()] = "";
    }
    return out;
}
QList<Record> Store::cargoLines(const Record &parent) const {
    const auto [source, prefix] = cargoSource(parent.source); if (source.isEmpty()) return {};
    if(!parent.id) {
        if(!parent.values.contains("NATIVE:BOOKING_ID")) return {};
        Record marineSource;
        if(parent.values.contains("NATIVE:MARINE_SOURCE_ID")) {
            marineSource=record(parent.values["NATIVE:MARINE_SOURCE_ID"].toString().toLongLong());
            if(marineSource.version!=parent.values["NATIVE:MARINE_SOURCE_VERSION"].toString().toInt()) fail("The source document changed. Reopen it before copying.");
            marineDocumentFromDocument(marineSource,parent);
            const auto owner=marineSource.values.value("NATIVE:BOOKING_ID").toString();
            if(!owner.isEmpty() && owner!=parent.values.value("NATIVE:BOOKING_ID").toString()) fail("The source document belongs to another shipment");
        }
        const auto booking=record(parent.values["NATIVE:BOOKING_ID"].toString().toLongLong());
        auto line=blankCargo(parent);
        QStringList goods;
        for(const auto &key : {"BOO:COMMODITY_1","BOO:COMMODITY_2"}) if(!valueText(booking.values[key]).trimmed().isEmpty()) goods<<valueText(booking.values[key]);
        line.values[prefix+"DESCRIPTION"]=goods.join('\n');
        bool content=!goods.isEmpty();
        for(const auto &[from,to] : QList<QPair<QString,QString>>{{"QTY","PCS"},{"KGS","WEIGHT_KGS"},{"LBS","WEIGHT_LBS"},{"CBM","MEASURE_CBM"}}) {
            if(booking.values.contains("BOO:"+from)) { line.values[prefix+to]=booking.values["BOO:"+from]; content|=valueText(booking.values["BOO:"+from]).toDouble()!=0; }
        }
        QList<Record> initialLines;
        if(marineSource.id) {
            const auto sp=cargoSource(marineSource.source).second;
            for(const auto &original : cargoLines(marineSource)) {
                auto copied=blankCargo(parent);
                for(const auto &suffix : {"MARKS_NUMBERS","PCS","DESCRIPTION","WEIGHT_KGS","WEIGHT_LBS","MEASURE_CBM","MEASURE_CFT"})
                    if(original.values.contains(sp+suffix)) copied.values[prefix+suffix]=original.values[sp+suffix];
                initialLines<<copied;
            }
            // A few legacy ocean documents contain header totals but
            // no cargo rows. Carry those known amounts into the receipt.
            if(initialLines.isEmpty() && marineSource.source=="OBL.TPS") {
                auto totals=blankCargo(parent); bool amounts=false;
                for(const auto &[from,to] : QList<QPair<QString,QString>>{{"PCS","PCS"},{"KGS","WEIGHT_KGS"},{"LBS","WEIGHT_LBS"},{"CBM","MEASURE_CBM"},{"CFT","MEASURE_CFT"}}) {
                    const auto value=marineSource.values["OBL:"+from];
                    if(valueText(value).toDouble()!=0) { totals.values[prefix+to]=value; amounts=true; }
                }
                if(amounts) initialLines<<totals;
            }
        } else if(content) initialLines<<line;
        return initialLines;
    }
    const auto pp = editableFields(parent.source).first().first.section(':',0,0) + ':';
    const auto reference = parent.values[pp+"REFERENCE_NUMBER"].toString();
    const auto sequence = parent.values[pp+"ID_SEQUENCE"].toString();
    if (reference.isEmpty() || sequence.isEmpty()) return {};
    QSqlQuery q(db); q.prepare("SELECT id,version,source,values_json FROM records WHERE source=? AND json_extract(values_json,?)=? AND json_extract(values_json,?)=? ORDER BY CAST(json_extract(values_json,?) AS REAL),id");
    q.addBindValue(source); q.addBindValue("$.\""+prefix+"REFERENCE_NUMBER\""); q.addBindValue(reference);
    q.addBindValue("$.\""+prefix+"ID_SEQUENCE\""); q.addBindValue(sequence);
    q.addBindValue("$.\""+cargoLineNumberKey(source)+"\""); execute(q);
    QList<Record> out; while (q.next()) out << fromQuery(q); return out;
}
Record Store::blankCargo(const Record &parent) const {
    const auto [source,prefix] = cargoSource(parent.source);
    if (source.isEmpty()) fail("Separate cargo rows are not supported for this document type");
    auto line = blankRecord(source); const auto pp = editableFields(parent.source).first().first.section(':',0,0) + ':';
    line.values[prefix+"REFERENCE_NUMBER"] = parent.values[pp+"REFERENCE_NUMBER"];
    line.values[prefix+"ID_SEQUENCE"] = parent.values[pp+"ID_SEQUENCE"]; return line;
}
QList<LinkedDocument> Store::linkedDocuments(qint64 bookingId) const {
    const auto booking = record(bookingId);
    if (booking.source != "BOOKING.TPS") fail("A booking is required");
    const auto reference = recordReference(booking).trimmed().toUpper();
    QSqlQuery count(db); count.prepare("SELECT count(*) FROM records WHERE source='BOOKING.TPS' AND upper(trim(json_extract(values_json,'$.\"BOO:REFERENCE_NUMBER\"')))=?");
    count.addBindValue(reference); execute(count); count.next();
    const bool ambiguous = count.value(0).toInt() > 1;
    QSqlQuery q(db); q.prepare(
        "SELECT id,version,source,values_json FROM records WHERE source IN ('AWB.TPS','OBL.TPS','DODR.TPS','PLIST.TPS','CERTORGN.TPS','SIGHT.TPS','DGDECLARATION') AND ("
        "json_extract(values_json,'$.\"NATIVE:BOOKING_ID\"')=? OR ("
        "COALESCE(json_extract(values_json,'$.\"NATIVE:BOOKING_ID\"'),'')='' AND ?<>'' AND upper(trim(CASE source "
        "WHEN 'AWB.TPS' THEN json_extract(values_json,'$.\"AWB:REFERENCE_NUMBER\"') "
        "WHEN 'DODR.TPS' THEN json_extract(values_json,'$.\"DOD:REFERENCE_NUMBER\"') "
        "WHEN 'OBL.TPS' THEN json_extract(values_json,'$.\"OBL:REFERENCE_NUMBER\"') "
        "WHEN 'PLIST.TPS' THEN json_extract(values_json,'$.\"PLI:REFERENCE_NUMBER\"') "
        "WHEN 'SIGHT.TPS' THEN json_extract(values_json,'$.\"SIG:REFERENCE_NUMBER\"') "
        "WHEN 'DGDECLARATION' THEN json_extract(values_json,'$.\"DGD:REFERENCE_NUMBER\"') "
        "WHEN 'CERTORGN.TPS' THEN json_extract(values_json,'$.\"CRO:REFERENCE_NUMBER\"') END))=?)) AND COALESCE(json_extract(values_json,'$.\"NATIVE:DELETED_AT\"'),'')='' ORDER BY source,id DESC");
    q.addBindValue(QString::number(bookingId)); q.addBindValue(reference); q.addBindValue(reference); execute(q);
    QList<LinkedDocument> out;
    while (q.next()) {
        auto document = fromQuery(q);
        const auto match = document.values.value("NATIVE:BOOKING_ID").toString() == QString::number(bookingId)
            ? DocumentMatch::NativeLink : ambiguous ? DocumentMatch::AmbiguousReference : DocumentMatch::ShipmentReference;
        out.append({document,match});
    }
    return out;
}
QList<Record> Store::history(qint64 recordId) const {
    const auto source = record(recordId).source;
    QSqlQuery q(db); q.prepare("SELECT record_id,version,?,values_json FROM record_history WHERE record_id=? ORDER BY version DESC");
    q.addBindValue(source); q.addBindValue(recordId); execute(q);
    QList<Record> out; while (q.next()) out << fromQuery(q); return out;
}
void Store::backup(const QString &destination) const {
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) fail("Backup destination already exists. Choose a new filename.");
    if (!QFileInfo(destination).dir().exists()) fail("Backup directory does not exist");
    const auto partial = destination + ".partial-" + QUuid::createUuid().toString(QUuid::Id128);
    QFile reservation(partial);
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) fail(reservation.errorString());
    makeFilePrivate(reservation);
    reservation.close();
    try {
        QSqlQuery q(db); q.prepare("VACUUM INTO ?"); q.addBindValue(QFileInfo(partial).absoluteFilePath()); execute(q); q.finish();
        {
            Store snapshot(partial);
            QSqlQuery integrity(snapshot.db);
            if (!integrity.exec("PRAGMA integrity_check") || !integrity.next() || integrity.value(0).toString() != "ok") fail("Backup integrity check failed");
        }
        if (!QFile::rename(partial,destination)) fail("Cannot publish backup; destination may already exist");
    } catch (...) { QFile::remove(partial); throw; }
}
}
