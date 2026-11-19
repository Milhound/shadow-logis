#include "privatefile.h"
#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <stdexcept>
using namespace shadowlogis;
namespace { void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp; check(temp.isValid(), "Temporary folder");
        const auto path = temp.filePath(QString::fromUtf8("Private shipment é.txt"));
        QFile file(path); check(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Create output");
        makeFilePrivate(file); check(file.write("first") == 5, "Write private bytes"); file.close();
        check(hasPrivateFilePermissions(path), "Created file is not private");
        {
            QSaveFile canceled(path); check(canceled.open(QIODevice::WriteOnly), "Open canceled copy");
            makeFilePrivate(canceled); check(canceled.write("canceled") == 8, "Write canceled copy");
            canceled.cancelWriting(); check(!canceled.commit(), "Canceled copy committed");
        }
        check(file.open(QIODevice::ReadOnly) && file.readAll() == "first", "Cancel changed destination"); file.close();
        QSaveFile saved(path); check(saved.open(QIODevice::WriteOnly), "Open replacement");
        makeFilePrivate(saved); check(saved.write("second") == 6 && saved.commit(), "Replace private output");
        check(hasPrivateFilePermissions(path), "Atomic replacement lost private permissions");
        check(file.open(QIODevice::ReadOnly) && file.readAll() == "second", "Replacement contents"); file.close();
#ifdef Q_OS_WIN
        QProcess permissions;
        permissions.start("icacls.exe", {path, "/grant", "*S-1-1-0:R"});
        check(permissions.waitForFinished(10000) && permissions.exitCode() == 0, "Create a deliberately shared test ACL");
#else
        check(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther), "Create deliberately shared test permissions");
#endif
        check(!hasPrivateFilePermissions(path), "Shared file incorrectly accepted as private");
        check(file.open(QIODevice::ReadWrite), "Reopen shared fixture"); makeFilePrivate(file); file.close();
        check(hasPrivateFilePermissions(path), "Restricting shared output failed");
        bool rejected = false;
        try { makeFilePrivate(file); } catch (const std::exception &) { rejected = true; }
        check(rejected, "Closed file accepted");
        QTextStream(stdout) << "PASS: private file creation, atomic replacement, cancellation, Unicode path and closed-file rejection.\n";
        return 0;
    } catch (const std::exception &error) { QTextStream(stderr) << error.what() << '\n'; return 1; }
}
