#pragma once
#include "store.h"
#include <QStringList>

namespace shadowlogis {
// Bounded, exact decimal arithmetic. Money is rounded half up per line.
QString quantityConversion(const QString &text, const QString &conversion, int decimals);
QString ratedAmount(const QString &weight, const QString &rate);
QString sumAmounts(const QStringList &amounts);
// Only explicitly enabled calculations run. Imported records without settings
// retain their stored values. The same functions serve editing, preview and Save.
Record calculateEntry(Record record);
Record calculateAirCharges(Record record);
struct DimensionResult {
    Record record;
    QString cubicMeters, cubicFeet, dimensionalWeight, unit;
};
DimensionResult calculateDimensions(Record record);
// Candidate shared fields only; callers must review differences before applying.
QJsonObject shipmentSuggestions(const Record &booking, const Record &document);
}
