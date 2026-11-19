#pragma once
#include <QFileDevice>
#include <QString>

namespace shadowlogis {
// Protect the open file BEFORE writing private bytes. Handles QSaveFile's
// temporary file too; never changes the old destination while saving a copy.
void makeFilePrivate(QFileDevice &file);
bool hasPrivateFilePermissions(const QString &path);
}
