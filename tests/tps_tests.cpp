#include "privatefile.h"
#include "tps.h"
#include "store.h"
#include "domain.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <array>
#include <functional>
#include <stdexcept>

using namespace shadowlogis;
namespace {
int checks = 0;
void check(bool ok, const char *label) { ++checks; if (!ok) throw std::runtime_error(label); }
void rejects(const std::function<void()> &action, const char *label) {
    bool rejected = false; try { action(); } catch (const std::exception &) { rejected = true; }
    check(rejected, label);
}
void le16(QByteArray &b, quint16 v) { char out[2]; qToLittleEndian(v, out); b.append(out,2); }
void le32(QByteArray &b, quint32 v) { char out[4]; qToLittleEndian(v, out); b.append(out,4); }
void be32(QByteArray &b, quint32 v) { char out[4]; qToBigEndian(v, out); b.append(out,4); }
void z(QByteArray &b, const QByteArray &v) { b += v; b += '\0'; }
QByteArray field(int type, int offset, const QByteArray &name, int elements, int length) {
    QByteArray b; b += char(type); le16(b, offset); z(b, name); le16(b, elements); le16(b, length); le16(b, 0); le16(b, 0);
    if (type == 18) { le16(b, length/elements); b.append(2, '\0'); }
    if (type == 10) { b += char(2); b += char(5); }
    return b;
}
QByteArray header(int type, int id = 0) {
    QByteArray b; be32(b, 1); b += char(type);
    if (type == 0xfa) le16(b, 0);
    else be32(b, id);
    if (type == 0xfc) { b += char(0); b.append(2, '\0'); }
    return b;
}
QByteArray record(const QByteArray &h, const QByteArray &body, int copied = 0) {
    QByteArray b; b += char(0xc0 | copied); le16(b, h.size()+body.size()); le16(b, h.size()); b += (h+body).mid(copied); return b;
}
QByteArray fixture(bool memoGap = false, bool conflict = false, bool booking = false) {
    QByteArray schema; le16(schema, 1); le16(schema, 31); le16(schema, 4); le16(schema, 1); le16(schema, 0);
    schema += field(6,0,booking ? "BOO:QUOTE_NUMBER" : "CUS:NUMBER",1,4) + field(18,4,booking ? "BOO:REFERENCE_NUMBER" : "CUS:NAME",1,16) + field(18,20,booking ? "BOO:SHIPPER_ARRAY" : "CUS:ADDRESS_ARRAY",2,8) + field(10,28,booking ? "BOO:CBM" : "CUS:AMOUNT",1,3);
    schema += '\0'; schema += char(1); z(schema, "CUS:NOTE"); le16(schema, 200); le16(schema, 0);
    QByteArray first; le32(first, 1); first += QByteArray("Alpha").leftJustified(16,' '); first += "AB  CD  "; first += QByteArray::fromHex("012345");
    QByteArray second = first; qToLittleEndian(quint32(2),second.data()); second.replace(4,16,QByteArray("Beta").leftJustified(16,' '));
    QByteArray payload = record(header(0xfa),schema) + record(header(0xf3,1),first) + record(header(0xf3,conflict ? 1 : 2),second,5);
    auto mh = header(0xfc,1); if (memoGap) mh[mh.size()-1] = 1;
    payload += record(mh, QByteArray("Special instructions\0",21));
    QByteArray file(512,'\0'); qToLittleEndian(quint16(512),file.data()+4); file.replace(14,4,"tOpS");
    QByteArray page; le32(page,512); le16(page,payload.size()+13); le16(page,payload.size()+13); le16(page,payload.size()); le16(page,4); page += '\0'; page += payload;
    page.resize((page.size()+255)&~255); file += page;
    qToLittleEndian(quint32(page.size()/256),file.data()+272); return file;
}
// Independent encryptor for synthetic data, inverse of the published TPS algorithm.
// No operational passwords or customer data belong in fixtures.
QByteArray encrypted(QByteArray bytes, QByteArray password) {
    password += '\0'; std::array<char,64> smear{};
    for (int i=0;i<64;++i) smear[(i*17)&63] = char(i + quint8(password[(i+1)%password.size()]));
    std::array<quint32,16> key{};
    for (int i=0;i<16;++i) key[i] = qFromLittleEndian<quint32>(smear.data()+i*4);
    for (int pass=0;pass<2;++pass) for (int i=0;i<16;++i) {
        auto a=key[i], j=a&15, b=key[j]; key[j]=a+(a&b); key[i]=a+(a|b);
    }
    for (int at=0;at<bytes.size();at+=64) {
        std::array<quint32,16> d{};
        for (int i=0;i<16;++i) d[i]=qFromLittleEndian<quint32>(bytes.constData()+at+i*4);
        for (int i=0;i<16;++i) {
            auto k=key[i], j=k&15, a=d[i], b=d[j]; d[i]=k+((k&a)|(~k&b)); d[j]=k+((k&b)|(~k&a));
        }
        for (int i=0;i<16;++i) qToLittleEndian(d[i],bytes.data()+at+i*4);
    }
    return bytes;
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path); check(f.open(QIODevice::WriteOnly),"fixture open"); check(f.write(bytes)==bytes.size(),"fixture write");
}
void parserTests() {
    const auto file = fixture(); const auto db = parseTps(file);
    check(db.tables.size()==1,"table count"); const auto &t = db.tables.first();
    check(t.rows.size()==2,"row count"); check(t.rows[1].id==2,"prefix-compressed second row");
    const auto first = recordValues(t,t.rows[0]);
    check(first["CUS:NAME"]=="Alpha","string decoding"); check(first["CUS:NUMBER"]=="1","integer decoding");
    check(first["CUS:AMOUNT"]=="123.45","exact decimal decoding");
    check(first["CUS:ADDRESS_ARRAY"].toArray()==QJsonArray{"AB","CD"},"array decoding");
    check(first["CUS:NOTE"]=="Special instructions","memo decoding");
    check(expandRle(QByteArray::fromHex("014103"),4)=="AAAA","RLE repeat");
    check(expandRle(QByteArray::fromHex("0241420000"),2)=="AB","RLE trailing pad");
    rejects([]{ expandRle(QByteArray::fromHex("0141ff7f"),4); },"RLE overflow rejected");
    rejects([]{ expandRle(QByteArray::fromHex("03ff"),3); },"truncated literal rejected");
    rejects([]{ parseTps(fixture(true)); },"missing memo fragment rejected");
    rejects([]{ parseTps(fixture(false,true)); },"conflicting rows rejected");
    for (int n=0;n<512;++n) rejects([&]{parseTps(file.left(n));},"truncated header rejected");
    rejects([&]{parseTps(file.left(file.size()-1));},"truncated block rejected");
    const auto cipher = encrypted(file,"synthetic-owner");
    check(parseTps(cipher,"synthetic-owner").tables[0].rows[0].bytes==t.rows[0].bytes,"owner decryption");
    rejects([&]{parseTps(cipher,"incorrect");},"wrong password rejected");
    rejects([&]{parseTps(cipher);},"missing password rejected");
    check(decodeText(QByteArray::fromHex("809793"))==QString::fromUtf8("€—“"),"Windows ANSI decoding");
    check(fieldText("BOO:ETA","4")=="1801-01-01","Clarion date epoch");
    check(dateStorage("1801-01-01")=="4","Clarion date encode");
    check(fieldText("BOO:ETA",dateStorage("2024-02-29"))=="2024-02-29","leap date roundtrip");
    check(dateStorage("")=="0" && fieldText("BOO:ETA","0").isEmpty(),"empty date preserved");
    rejects([]{dateStorage("2025-02-29");},"invalid date rejected");
}
void storeTests() {
    QTemporaryDir tmp; check(tmp.isValid(),"temporary directory");
    const auto source = tmp.path()+"/input"; check(QDir().mkdir(source),"create source directory");
    const auto original = fixture(); write(source+"/CUSTOMER.TPS",original); write(source+"/protected.tps",QByteArray(512,'x'));
    const QByteArray pdf("%PDF-1.4\nsynthetic-preservation-fixture\n%%EOF\n"); write(source+"/original.pdf",pdf);
    const auto path = tmp.path()+"/new.sqlite"; const auto report = migrate(source,path);
    check(report["decoded_tps"]==1 && report["archived_only_tps"]==1,"partial migration explicitly reported");
    check(report["records"]==2 && report["memos"]==1 && report["historical_pdfs"]==1,"migration counts");
    rejects([&]{migrate(source,path);},"existing destination protected");
    QFile sourceFile(source+"/CUSTOMER.TPS"); check(sourceFile.open(QIODevice::ReadOnly) && sourceFile.readAll()==original,"source unchanged");
    check(hasPrivateFilePermissions(path),"private database permissions");
    {
        Store store(path); auto rows = store.records({"CUSTOMER.TPS"}); check(rows.size()==2,"load imported records");
        auto row = rows.last(); const auto stale = row; row.values["CUS:NAME"]="Updated";
        store.save(row); const auto changed=store.record(row.id);
        check(changed.version==2 && changed.values["CUS:NAME"]=="Updated","edit persisted with version");
        check(changed.values["CUS:NOTE"]=="Special instructions","unexposed memo survives edit");
        rejects([&]{store.save(stale);},"stale update rejected");
        check(store.record(row.id).values==changed.values,"stale update rolled back");
        auto identity=changed; identity.values["CUS:NUMBER"]="100"; rejects([&]{store.save(identity);},"identity changes rejected");
        auto blank=store.blankRecord("CUSTOMER.TPS"); rejects([&]{store.save(blank);},"empty name rejected");
        blank.values["CUS:NAME"]="New customer"; const auto id = store.save(blank);
        check(store.record(id).values["CUS:NUMBER"]=="3","unique customer number assigned");
        check(store.document(store.documents()[0].first)==pdf,"original PDF preserved");
        check(store.records({"none"}).isEmpty(),"unknown source empty");
        const auto backup = tmp.path()+"/backup.sqlite"; store.backup(backup);
        Store snapshot(backup);
        check(snapshot.record(row.id).values==changed.values,"backup preserves edited fields");
        check(snapshot.history(row.id).size()==1 && snapshot.history(row.id)[0].values==stale.values,"backup includes prior revisions");
        check(snapshot.document(snapshot.documents()[0].first)==pdf,"backup retains archived PDF bytes");
        check(hasPrivateFilePermissions(backup),"private backup permissions");
        check(QDir(tmp.path()).entryList({"backup.sqlite.partial-*"}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
              "successful backup removes private staging directory");
        rejects([&]{store.backup(backup);},"backup refuses overwrite");
        rejects([&]{store.backup(path);},"backup refuses source overwrite");
        rejects([&]{store.backup(tmp.path()+"/missing/new.sqlite");},"backup directory must exist");
    }
    { Store reopened(path); check(reopened.records({"CUSTOMER.TPS"}).size()==3,"save survives restart"); }
    const QString connection="verify-test";
    {
        auto sql=QSqlDatabase::addDatabase("QSQLITE",connection); sql.setDatabaseName(path); check(sql.open(),"open verification database");
        QSqlQuery q(sql); check(q.exec("SELECT raw_record FROM records WHERE legacy_id=1") && q.next(),"raw row available");
        check(q.value(0).toByteArray()==parseTps(original).tables[0].rows[0].bytes,"raw row unchanged by edit");
        check(q.exec("SELECT count(*) FROM record_history") && q.next() && q.value(0).toInt()==1,"one committed revision");
        check(q.exec("PRAGMA integrity_check") && q.next() && q.value(0).toString()=="ok","SQLite integrity");
        check(q.exec("SELECT original FROM source_files WHERE path='CUSTOMER.TPS'") && q.next() && q.value(0).toByteArray()==original,"full TPS archive exact");
        sql.close();
    }
    QSqlDatabase::removeDatabase(connection);
}
void bookingTests() {
    QTemporaryDir tmp; const auto source=tmp.path()+"/input"; check(QDir().mkdir(source),"booking source folder");
    write(source+"/BOOKING.TPS",fixture(false,false,true)); const auto path=tmp.path()+"/bookings.sqlite"; migrate(source,path);
    Store store(path); auto newBooking=store.blankRecord("BOOKING.TPS"); newBooking.values["BOO:REFERENCE_NUMBER"]="New booking";
    newBooking.values["BOO:CBM"]="2.50"; const auto id=store.save(newBooking);
    check(store.record(id).values["BOO:QUOTE_NUMBER"]=="3","booking identity allocated");
    rejects([&]{store.save(newBooking);},"duplicate booking blocked");
    newBooking.values["BOO:REFERENCE_NUMBER"]=" NEW BOOKING "; rejects([&]{store.save(newBooking);},"case/whitespace duplicate blocked");
    auto edited=store.record(id); edited.values["BOO:CBM"]="bad"; rejects([&]{store.save(edited);},"malformed volume rejected");
    edited=store.record(id); edited.values["BOO:SHIPPER_ARRAY"]="flattened"; rejects([&]{store.save(edited);},"array type corruption rejected");
    edited=store.record(id); edited.values["BOO:SHIPPER_ARRAY"]=QJsonArray{"a","b","c"}; rejects([&]{store.save(edited);},"array overflow rejected");
    check(store.record(id).version==1,"failed edits did not change revision");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    try { parserTests(); storeTests(); bookingTests(); QTextStream(stdout)<<checks<<" checks passed\n"; return 0; }
    catch (const std::exception &e) { QTextStream(stderr)<<"FAILED after "<<checks<<" checks: "<<e.what()<<'\n'; return 1; }
}
