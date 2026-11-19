#include "preferences.h"
#include <QFileInfo>
#include <cmath>
namespace shadowlogis {
QString rememberedWorkspace() { return Preferences().value("workspace/lastPath").toString(); }
void rememberWorkspace(const QString &path) { Preferences().setValue("workspace/lastPath",QFileInfo(path).absoluteFilePath()); }
namespace {
double bounded(const Preferences &p,const QString &key,double fallback,double lo,double hi) {
    bool ok=false; const double v=p.value(key,fallback).toDouble(&ok);
    return ok && std::isfinite(v) && v>=lo && v<=hi ? v : fallback;
}
template<typename Options> void load(Options &o,const QString &group) {
    Preferences p; o.horizontalOffsetMm=bounded(p,group+"/x",0,-25,25); o.verticalOffsetMm=bounded(p,group+"/y",0,-25,25);
    o.horizontalScalePercent=bounded(p,group+"/scaleX",100,95,105); o.verticalScalePercent=bounded(p,group+"/scaleY",100,95,105);
}
}
void loadPrintPreferences(AirWaybillPrintOptions &awb,OceanBillPrintOptions &ocean) { load(awb,"printing/awb"); load(ocean,"printing/ocean"); }
void savePrintPreferences(bool awb,double x,double y,double sx,double sy) {
    Preferences p; const QString group=awb ? "printing/awb" : "printing/ocean";
    p.setValue(group+"/x",x); p.setValue(group+"/y",y); p.setValue(group+"/scaleX",sx); p.setValue(group+"/scaleY",sy);
}
}
