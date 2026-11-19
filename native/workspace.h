#pragma once
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace shadowlogis {
// Authored application schemas: empty workspaces never need a customer's TPS files.
QStringList workspaceSources();
QJsonObject workspaceSchema(const QString &source);
struct ImportCategory { QString id, label, description; QStringList files; bool defaultSelected = true; };
QList<ImportCategory> importCategories();
struct InstallationData { QString directory; QStringList files; };
QList<InstallationData> findInstallationData(const QString &installation);
// Includes dependent cargo files. Names are normalized for application queries;
// the original relative paths and bytes remain in provenance, not in the source tree.
QStringList selectedImportFiles(const InstallationData &data,const QStringList &categories);
}
