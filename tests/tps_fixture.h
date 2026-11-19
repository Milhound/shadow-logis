#pragma once
// Authored synthetic TPS byte generator, shared by reader/import tests.
#include <QByteArray>
#include <QtEndian>
#include <array>
namespace synthetic_tps {
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
}
