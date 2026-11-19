#pragma once
#include "store.h"
#include <QDialog>
namespace shadowlogis {
class DimensionsDialog final : public QDialog {
public:
    DimensionsDialog(const Record &record,QWidget *parent=nullptr);
    Record value;
};
}
