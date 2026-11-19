#pragma once
#include <QSettings>
#include "documents.h"
namespace shadowlogis {
class Preferences : public QSettings {
public:
    Preferences() : QSettings(QSettings::IniFormat,QSettings::UserScope,"Shadow Logis","Shadow Logis") {}
};
QString rememberedWorkspace();
void rememberWorkspace(const QString &path);
void loadPrintPreferences(AirWaybillPrintOptions &awb,OceanBillPrintOptions &ocean);
void savePrintPreferences(bool awb,double x,double y,double sx,double sy);
}
