#pragma once
#include <QDialog>
#include <QJsonObject>
namespace shadowlogis {
class ImportWorkspaceDialog final : public QDialog {
public:
    explicit ImportWorkspaceDialog(QWidget *parent=nullptr);
    QString destination;
    QJsonObject report;
};
}
