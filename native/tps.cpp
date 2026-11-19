// TPS format handling adapted from tps-parse, Copyright 2012-2013 E.Hooijmeijer.
// Apache-2.0; see third_party/NOTICE.txt and third_party/Apache-2.0.txt.
// C++ adaptation adds bounds checks, explicit unsupported states and read-only I/O.
#include "tps.h"
#include <QDate>
#include <QFile>
#include <QMap>
#include <QJsonArray>
#include <QSet>
#include <QtEndian>
#include <bit>
#include <array>
#include <cmath>
#include <stdexcept>

namespace shadowlogis {
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }
class Reader {
public:
    explicit Reader(QByteArray bytes) : data(std::move(bytes)) {}
    QByteArray data;
    qsizetype pos = 0;
    qsizetype remaining() const { return data.size() - pos; }
    QByteArray take(qsizetype n) {
        if (n < 0 || n > remaining()) fail("Truncated or inconsistent TPS structure");
        const auto out = data.mid(pos, n); pos += n; return out;
    }
    quint8 u8() { return quint8(take(1)[0]); }
    quint16 u16() { const auto b = take(2); return qFromLittleEndian<quint16>(b.constData()); }
    quint16 be16() { const auto b = take(2); return qFromBigEndian<quint16>(b.constData()); }
    quint32 u32() { const auto b = take(4); return qFromLittleEndian<quint32>(b.constData()); }
    quint32 be32() { const auto b = take(4); return qFromBigEndian<quint32>(b.constData()); }
    QString zstring() {
        const auto end = data.indexOf('\0', pos);
        if (end < pos || end - pos > 4096) fail("Invalid TPS metadata string");
        const auto value = QString::fromLatin1(take(end - pos)); take(1); return value;
    }
};
quint32 le32(const QByteArray &b, qsizetype at) {
    if (at < 0 || at + 4 > b.size()) fail("Invalid TPS address");
    return qFromLittleEndian<quint32>(b.constData() + at);
}
int packedLength(Reader &r) {
    const int first = r.u8();
    return first < 128 ? first : (first & 127) + (int(r.u8()) << 7);
}
QByteArray decryptTps(QByteArray bytes, QByteArray password) {
    password.append('\0');
    QByteArray smeared(64, '\0');
    for (int i = 0; i < 64; ++i)
        smeared[(i * 17) & 63] = char(i + quint8(password[(i + 1) % password.size()]));
    std::array<quint32, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = le32(smeared, i * 4);
    for (int pass = 0; pass < 2; ++pass) for (int i = 0; i < 16; ++i) {
        const auto a = key[i], j = a & 15, b = key[j];
        key[j] = a + (a & b); key[i] = a + (a | b);
    }
    const auto decrypt = [&](quint64 start, quint64 end) {
        if (start > end || end > quint64(bytes.size()) || start % 64 || end % 64)
            fail("Encrypted TPS allocation is outside the file");
        for (auto at = start; at < end; at += 64) {
            std::array<quint32, 16> data{};
            for (int i = 0; i < 16; ++i) data[i] = le32(bytes, qsizetype(at + i * 4));
            for (int i = 15; i >= 0; --i) {
                const auto k = key[i], j = k & 15, a = data[i] - k, b = data[j] - k;
                data[i] = (a & k) | (b & ~k); data[j] = (b & k) | (a & ~k);
            }
            for (int i = 0; i < 16; ++i) qToLittleEndian(data[i], bytes.data() + at + i * 4);
        }
    };
    decrypt(0, 512);
    if (le32(bytes, 0) != 0 || bytes.mid(14, 4) != "tOpS")
        fail("Provided owner password did not unlock this file, or its format is unsupported");
    QList<QPair<quint64, quint64>> blocks;
    for (int i = 0; i < 60; ++i) {
        const quint64 start = quint64(le32(bytes, 32 + i * 4)) * 256 + 512;
        const quint64 end = quint64(le32(bytes, 272 + i * 4)) * 256 + 512;
        if (start == end) continue;
        for (const auto &prior : blocks)
            if (start < prior.second && prior.first < end) fail("Overlapping encrypted TPS blocks");
        blocks.append({start, end}); decrypt(start, end);
    }
    return bytes;
}
Table definition(quint32 id, const QByteArray &bytes) {
    Reader r(bytes); Table table; table.id = id;
    r.u16(); // Driver version, retained only in source bytes.
    table.recordLength = r.u16();
    const int fields = r.u16();
    table.memoCount = r.u16(); table.indexCount = r.u16();
    if (fields > 4096 || table.recordLength == 0) fail("Unsupported table dimensions");
    for (int i = 0; i < fields; ++i) {
        Field f;
        f.type = r.u8(); f.offset = r.u16(); f.name = r.zstring();
        f.elements = r.u16(); f.length = r.u16(); r.u16(); r.u16();
        if (f.type == 0x0a) { f.decimals = r.u8(); r.u8(); }
        else if (f.type >= 0x12 && f.type <= 0x14) {
            r.u16(); if (r.zstring().isEmpty()) r.u8();
        } else if (!(f.type >= 1 && f.type <= 9) && f.type != 0x16) {
            fail(QString("Unsupported field type 0x%1").arg(f.type, 0, 16));
        }
        if (f.offset + f.length > table.recordLength || f.length <= 0 ||
            (f.elements > 1 && f.length % f.elements != 0))
            fail("Field extends beyond record or has an invalid array shape");
        table.fields.append(f);
    }
    for (int i = 0; i < table.memoCount; ++i) {
        MemoDefinition memo;
        memo.externalFile = r.zstring();
        if (memo.externalFile.isEmpty() && r.u8() != 1) fail("Invalid memo definition");
        memo.name = r.zstring(); memo.length = r.u16(); memo.flags = r.u16();
        table.memoDefinitions.append(memo);
    }
    return table;
}
}

QByteArray expandRle(const QByteArray &bytes, int expectedSize) {
    if (expectedSize < 0 || expectedSize > 65535) fail("Invalid expanded page size");
    Reader r(bytes); QByteArray out;
    while (r.remaining()) {
        // TopSpeed may append one zero padding byte after a complete RLE stream.
        if (out.size() == expectedSize && r.remaining() == 1 && r.data[r.pos] == '\0') {
            r.take(1); break;
        }
        const int literal = packedLength(r);
        if (!literal || literal > expectedSize - out.size()) fail("Invalid RLE literal length");
        out += r.take(literal);
        if (r.remaining()) {
            const int repeats = packedLength(r);
            if (repeats > expectedSize - out.size()) fail("RLE page exceeds declared size");
            out += QByteArray(repeats, out.back());
        }
    }
    if (out.size() != expectedSize) fail("Expanded TPS page size mismatch");
    return out;
}

Database parseTps(const QByteArray &input, const QByteArray &ownerPassword) {
    if (input.size() > 256 * 1024 * 1024) fail("File exceeds the 256 MiB import limit");
    QByteArray bytes = input;
    if (bytes.size() < 512) fail("File is too small for a TPS header");
    if (le32(bytes, 0) != 0 || bytes.mid(14, 4) != "tOpS") {
        if (ownerPassword.isEmpty()) fail("Unrecognized or encrypted TPS file; an owner password may be required");
        bytes = decryptTps(bytes, ownerPassword);
    }
    Reader header(bytes.left(512)); header.u32();
    if (header.u16() != 512) fail("Unsupported TPS header size");
    QMap<quint32, QMap<int, QByteArray>> definitions;
    QMap<quint32, QMap<quint32, QByteArray>> rows;
    QMap<quint32, QMap<quint32, QMap<int, QMap<int, QByteArray>>>> memoParts;
    QSet<qsizetype> visited;
    Database database;
    for (int block = 0; block < 60; ++block) {
        const quint64 start = quint64(le32(bytes, 32 + block * 4)) * 256 + 512;
        const quint64 end = quint64(le32(bytes, 272 + block * 4)) * 256 + 512;
        if (start == end) continue;
        if (start > end || end > quint64(bytes.size())) fail("TPS allocation block is outside the file");
        for (quint64 at = start; at + 13 <= end;) {
            if (le32(bytes, qsizetype(at)) != at) { at += 256; continue; }
            Reader page(bytes.mid(qsizetype(at), 13)); page.u32();
            const int size = page.u16(), expanded = page.u16();
            page.u16(); // Sum of record sizes before prefix compression.
            const int count = page.u16(), flags = page.u8();
            if (size < 13 || expanded < 13 || at + size > end) fail("Invalid TPS page extent");
            for (quint64 inside = at + 256; inside < at + size; inside += 256)
                if (le32(bytes, qsizetype(inside)) == inside)
                    fail("Overlapping TPS pages require recovery; no rows are silently skipped");
            if (visited.contains(qsizetype(at))) fail("Overlapping TPS allocation blocks");
            visited.insert(qsizetype(at)); ++database.pages;
            if (flags == 0) {
                auto payload = bytes.mid(qsizetype(at + 13), size - 13);
                if (size != expanded) payload = expandRle(payload, expanded - 13);
                Reader records(payload); QByteArray previous;
                int length = 0, headerLength = 0;
                for (int index = 0; index < count; ++index) {
                    const int control = records.u8();
                    if (index == 0 && (control & 0xc0) != 0xc0) fail("First record has no complete lengths");
                    if (control & 0x80) length = records.u16();
                    if (control & 0x40) headerLength = records.u16();
                    const int copied = control & 63;
                    if (copied > previous.size() || copied > length || headerLength > length)
                        fail("Invalid record prefix or header length");
                    const auto record = previous.left(copied) + records.take(length - copied);
                    previous = record;
                    if (headerLength < 5) continue; // Empty/index sentinel, not a row.
                    if (quint8(record[0]) == 0xfe) continue; // Table-name entry.
                    Reader key(record.left(headerLength));
                    const auto table = key.be32(); const int type = key.u8();
                    const auto body = record.mid(headerLength);
                    if (type == 0xfa) {
                        const int part = key.u16();
                        auto &parts = definitions[table];
                        if (parts.contains(part) && parts[part] != body) fail("Conflicting table definitions");
                        parts[part] = body;
                    } else if (type == 0xf3) {
                        const auto id = key.be32(); auto &data = rows[table];
                        if (data.contains(id) && data[id] != body) fail("Conflicting records; index validation is needed");
                        data[id] = body;
                        if (data.size() > 1000000) fail("Record limit exceeded");
                    } else if (type == 0xfc) {
                        const auto row = key.be32(); const int field = key.u8(), part = key.be16();
                        auto &parts = memoParts[table][row][field];
                        if (parts.contains(part) && parts[part] != body) fail("Conflicting memo fragments");
                        parts[part] = body;
                    }
                }
                if (records.remaining() != 0) fail("Unconsumed bytes after TPS page records");
            }
            at = (at + quint64(size) + 255) & ~quint64(255);
        }
    }
    for (auto entry = definitions.cbegin(); entry != definitions.cend(); ++entry) {
        QByteArray joined; int expected = 0;
        for (auto part = entry.value().cbegin(); part != entry.value().cend(); ++part) {
            if (part.key() != expected++) fail("Missing table definition fragment");
            joined += part.value();
        }
        auto table = definition(entry.key(), joined);
        const auto data = rows.value(entry.key());
        for (auto row = data.cbegin(); row != data.cend(); ++row) {
            if (row.value().size() != table.recordLength) fail("Data row does not match its table definition");
            table.rows.append({row.key(), row.value()});
        }
        const auto rowMemos = memoParts.value(entry.key());
        for (auto row = rowMemos.cbegin(); row != rowMemos.cend(); ++row) {
            for (auto field = row.value().cbegin(); field != row.value().cend(); ++field) {
                if (field.key() >= table.memoDefinitions.size()) fail("Memo has no field definition");
                int sequence = 0; QByteArray memo;
                for (auto part = field.value().cbegin(); part != field.value().cend(); ++part) {
                    if (part.key() != sequence++) fail("Missing memo fragment");
                    memo += part.value();
                }
                table.memos.append({row.key(), field.key(), memo});
            }
        }
        database.tables.append(std::move(table));
    }
    for (auto it = rows.cbegin(); it != rows.cend(); ++it)
        if (!definitions.contains(it.key())) fail("Data records have no matching table definition");
    for (auto it = memoParts.cbegin(); it != memoParts.cend(); ++it)
        if (!definitions.contains(it.key())) fail("Memos have no matching table definition");
    if (database.tables.isEmpty()) fail("No supported table definitions were found");
    database.limitations << "Read-only preview. Indexes and business rules have not been validated."
                         << "Text uses Windows-1252; original bytes are retained for encoding corrections."
                         << "Key definitions are retained in the original file; key semantics are not implemented.";
    return database;
}

Database readTps(const QString &path, const QByteArray &ownerPassword) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    if (file.size() > 256 * 1024 * 1024) fail("File exceeds the 256 MiB preview limit");
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) fail(file.errorString());
    return parseTps(bytes, ownerPassword);
}

QString Field::typeName() const {
    switch (type) {
    case 1: return "BYTE"; case 2: return "SHORT"; case 3: return "USHORT";
    case 4: return "DATE"; case 5: return "TIME (raw)"; case 6: return "LONG";
    case 7: return "ULONG"; case 8: return "REAL4"; case 9: return "REAL8";
    case 10: return "DECIMAL"; case 18: return "STRING"; case 19: return "CSTRING";
    case 20: return "PSTRING"; case 22: return "GROUP (raw)";
    default: return "Unsupported";
    }
}

QString displayValue(const Field &f, const QByteArray &row) {
    if (f.offset < 0 || f.length < 1 || f.offset + f.length > row.size()) return "[invalid field bounds]";
    if (f.elements > 1) return QString("[array: %1 elements]").arg(f.elements);
    const auto bytes = row.mid(f.offset, f.length);
    const auto raw = [&] { return "0x" + QString::fromLatin1(bytes.toHex()); };
    const char *p = bytes.constData();
    switch (f.type) {
    case 1: if (bytes.size() == 1) return QString::number(quint8(p[0])); break;
    case 2: if (bytes.size() == 2) return QString::number(qFromLittleEndian<qint16>(p)); break;
    case 3: if (bytes.size() == 2) return QString::number(qFromLittleEndian<quint16>(p)); break;
    case 6: if (bytes.size() == 4) return QString::number(qFromLittleEndian<qint32>(p)); break;
    case 7: if (bytes.size() == 4) return QString::number(qFromLittleEndian<quint32>(p)); break;
    case 4:
        if (bytes.size() == 4) {
            const auto v = qFromLittleEndian<quint32>(p);
            if (!v) return {};
            QDate date(int(v >> 16), int((v >> 8) & 255), int(v & 255));
            return date.isValid() ? date.toString(Qt::ISODate) : "[invalid date] " + raw();
        } break;
    case 8:
        if (bytes.size() == 4) return QString::number(std::bit_cast<float>(qFromLittleEndian<quint32>(p)), 'g', 9);
        break;
    case 9:
        if (bytes.size() == 8) return QString::number(std::bit_cast<double>(qFromLittleEndian<quint64>(p)), 'g', 17);
        break;
    case 10: {
        const auto hex = QString::fromLatin1(bytes.toHex());
        auto digits = hex.mid(1);
        for (const auto ch : digits) if (ch < '0' || ch > '9') return "[invalid decimal] " + raw();
        if (f.decimals < 0 || f.decimals > digits.size()) return "[invalid decimal scale] " + raw();
        if (f.decimals == digits.size()) digits.prepend('0');
        if (f.decimals) digits.insert(digits.size() - f.decimals, '.');
        while (digits.size() > 1 && digits[0] == '0' && digits[1] != '.') digits.remove(0, 1);
        return (hex[0] == '0' ? "" : "-") + digits;
    }
    case 18: return decodeText(bytes).trimmed();
    case 19: return decodeText(bytes.left(bytes.indexOf('\0') < 0 ? bytes.size() : bytes.indexOf('\0')));
    case 20:
        if (quint8(bytes[0]) < bytes.size()) return decodeText(bytes.mid(1, quint8(bytes[0])));
        return "[invalid string length]";
    }
    return raw();
}

QString decodeText(const QByteArray &bytes) {
    // Clarion's Windows ANSI text. Undefined CP1252 codes retain their values.
    static const ushort high[] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,
        0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    QString out; out.reserve(bytes.size());
    for (const auto ch : bytes) { const auto c = quint8(ch); out += QChar(c >= 128 && c <= 159 ? high[c-128] : c); }
    return out;
}

QJsonObject recordValues(const Table &table, const Row &row) {
    QJsonObject values;
    for (const auto &field : table.fields) {
        if (field.elements > 1) {
            QJsonArray array; Field scalar = field; scalar.elements = 1;
            scalar.length = field.length / field.elements;
            for (int i = 0; i < field.elements; ++i) {
                scalar.offset = field.offset + i * scalar.length;
                array.append(displayValue(scalar, row.bytes));
            }
            values[field.name] = array;
        } else {
            values[field.name] = displayValue(field, row.bytes);
        }
    }
    for (const auto &memo : table.memos) {
        if (memo.row != row.id) continue;
        const auto def = table.memoDefinitions.at(memo.field);
        if (def.flags & 4) {
            values[def.name] = QJsonObject{{"kind", "blob"}, {"base64", QString::fromLatin1(memo.bytes.toBase64())}};
        } else {
            auto text = memo.bytes;
            while (text.endsWith('\0')) text.chop(1);
            values[def.name] = decodeText(text);
        }
    }
    return values;
}
}
