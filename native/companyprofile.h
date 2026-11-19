#pragma once
#include "store.h"
#include <QDialog>
namespace shadowlogis {
// Normalize a local logo to a bounded PNG for portable database storage.
QByteArray companyLogo(const QString &path);
class CompanyProfileDialog final : public QDialog {
public:
    explicit CompanyProfileDialog(Store &store,QWidget *parent=nullptr);
};
}
