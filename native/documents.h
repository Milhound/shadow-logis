#pragma once
#include "store.h"
namespace shadowlogis {
struct AirWaybillContext {
    QString issuer;
    QStringList agentAddress;
    QString agentIata;
    QString purchaseOrder;
    QString destination;
};
struct AirWaybillPrintOptions {
    bool preprintedContinuous = false;
    double horizontalOffsetMm = 0;
    double verticalOffsetMm = 0;
    double horizontalScalePercent = 100;
    double verticalScalePercent = 100;
};
struct OceanBillPrintOptions {
    bool textOnly = true;
    double horizontalOffsetMm = 0;
    double verticalOffsetMm = 0;
    double horizontalScalePercent = 100;
    double verticalScalePercent = 100;
};
AirWaybillContext airWaybillContext(const Store &store, const Record &record);
QString sightDraftAmount(const QString &input);
QString sightDraftAmountWords(const QString &input);
QString certificateForwarder(const Store &store,const Record &record);
void exportCertificateOfOrigin(const Record &record,const QString &path,const QList<Record> &cargo={},const QString &forwarder={});
void exportSightDraft(const Record &record,const QString &path);
void exportDangerousGoods(const Record &record, const QString &path);
void exportSummary(const Record &record, const QString &path, const QList<Record> &cargo = {});
void exportAirWaybill(const Record &record, const QString &path, const AirWaybillContext &context = {},
                     const AirWaybillPrintOptions &options = {});
void exportOceanBillOfLading(const Record &record, const QString &path, const QList<Record> &cargo = {},
                            const OceanBillPrintOptions &options = {});
void exportDockReceipt(const Record &record, const QString &path, const QList<Record> &cargo = {});
void exportVicsBillOfLading(const Record &record, const QString &path, const QList<Record> &cargo = {});
}
