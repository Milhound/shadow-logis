#pragma once
#include <QString>
class QPainter;
namespace shadowlogis {
// Conventional AWB face, authored as native vector rules and labels.
void drawAirWaybillForm(QPainter &painter,const QString &bodyFamily,const QString &headingFamily);
}
