#pragma once
#include "store.h"
#include <QDialog>
namespace shadowlogis {
// Changes remain in memory until the containing document is saved.
class RatingDialog final : public QDialog {
public:
    RatingDialog(const Record &record,QWidget *parent=nullptr);
    Record value;
};
}
