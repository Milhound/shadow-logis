#pragma once
#include "store.h"
#include <QPair>
namespace shadowlogis {
QList<QPair<QString, QString>> editableFields(const QString &source);
QList<QPair<QString, QString>> vicsFields(const QString &source);
QStringList vicsOrderKeys();
QString recordTitle(const Record &record);
QString recordReference(const Record &record);
bool isAddressArray(const QString &key);
bool isDateField(const QString &key);
QString fieldText(const QString &key, const QJsonValue &value);
QString dateStorage(const QString &text);
// Display only: no rounding or changes to stored decimal values.
QString weightText(const QString &text);
// Complete a legacy truncated destination only from an unambiguous matching
// airport directory entry and the final routing code. Custom text is retained.
QString airDestinationLabel(const Record &record, const QList<Record> &airports);
QPair<QString,QString> cargoSource(const QString &source);
QString cargoLineNumberKey(const QString &source);
QJsonObject marineSchema(const QString &source, QJsonObject schema);
Record marineDocumentFromDocument(const Record &source, Record target);
QStringList airCargoKeys();
// TPS stores six slots, but only the first five are shipment lines. The last
// slot contains legacy working values and must be preserved, not totaled.
inline constexpr int AirCargoRows = 5;
inline constexpr int AirCargoStorageSlots = 6;
struct AirCargoTotals { QString pieces, grossWeight, unit; };
// Exact decimal arithmetic; changing cargo requires one explicit weight unit.
AirCargoTotals airCargoTotals(const QJsonObject &values);
inline constexpr auto DangerousGoodsSource = "DGDECLARATION";
QList<QPair<QString,QString>> dangerousGoodsColumns();
QJsonObject dangerousGoodsSchema();
Record blankDangerousGoods();
QString dangerousGoodsText(const QJsonObject &values);
void validateDangerousGoods(const QJsonObject &values);
Record dangerousGoodsFromAirWaybill(const Record &awb, Record declaration);
QStringList companyAddress(const QJsonObject &profile,bool contactDetails=false);
Record applyCompanyProfile(Record record,const QJsonObject &profile);
QList<QPair<QString,QString>> sightInstructionFields();
Record documentFromBooking(const Record &booking, Record blank);
}
