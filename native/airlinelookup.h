#pragma once
#include "airlines.h"
#include <QDialog>

namespace shadowlogis {
class AirlineLookupDialog final : public QDialog {
public:
    explicit AirlineLookupDialog(bool chooseCode = false, const QString &initialSearch = {}, QWidget *parent = nullptr);
    QString selectedCode() const;
};
}
