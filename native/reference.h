#pragma once
#include "store.h"
#include <QDialog>
#include <QStringList>

namespace shadowlogis {
struct ReferenceRow { QString code, name, detail; };
struct ReferenceTable { QString title, note; QList<ReferenceRow> rows; bool showDetails = true; };
ReferenceTable countryReference();
ReferenceTable currencyReference();
ReferenceTable airportReference(const Store &store);
ReferenceTable incotermReference();
class ReferenceDialog final : public QDialog {
public:
    explicit ReferenceDialog(const ReferenceTable &reference, bool select = false, QWidget *parent = nullptr);
    ReferenceRow selected() const;
};
}
