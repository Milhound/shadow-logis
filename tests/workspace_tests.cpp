#define SHADOW_LOGIS_GUI_TESTING
#include "../native/main.cpp"
#include "workspace.h"
#include "privatefile.h"
#include "fixtures.h"
#include "tps_fixture.h"
#include <limits>
namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
void rejects(const std::function<void()> &fn,const char *message) { bool failed=false; try { fn(); } catch(const std::exception &) { failed=true; } require(failed,message); }
void write(const QString &path,const QByteArray &bytes) { QFile f(path); require(f.open(QIODevice::WriteOnly),"Fixture open failed"); require(f.write(bytes)==bytes.size(),"Fixture write failed"); }
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); installAppearance(app);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    try {
        QTimer::singleShot(30000,&app,[] { std::exit(2); });
        QTemporaryDir tmp; require(tmp.isValid(),"Temporary directory failed");
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,tmp.path());
        const auto blank=tmp.filePath("empty.sqlite"); createWorkspace(blank);
        require(hasPrivateFilePermissions(blank),"Workspace is not private");
        rejects([&] { createWorkspace(blank); },"New workspace overwrote an existing file");
        {
            Store store(blank); require(store.records({"BOOKING.TPS","CUSTOMER.TPS","CONTACTS.TPS"}).isEmpty(),"Empty workspace contains data");
            require(store.sources().contains("AWB.TPS") && store.sources().contains("SIGHT.TPS"),"Empty workspace cannot create documents");
            require(store.migrationReport()["workspace_kind"]=="new","Empty workspace report is misleading");
        }
        Window welcome({}); welcome.show(); app.processEvents();
        require(welcome.findChild<QWidget *>("workspaceWelcome")->isVisible(),"Fresh startup has no onboarding");
        if(argc==2) { QDir().mkpath(QString::fromLocal8Bit(argv[1])); welcome.grab().save(QString::fromLocal8Bit(argv[1])+"/welcome.png"); }
        const auto chosen=tmp.filePath("chosen.sqlite");
        QTimer::singleShot(0,&welcome,[&] {
            auto *picker=welcome.findChild<QFileDialog *>("newWorkspaceFile"); require(picker,"New workspace picker missing");
            picker->selectFile(chosen); QMetaObject::invokeMethod(picker,"accept");
        });
        welcome.findChild<QPushButton *>("welcome_newWorkspace")->click(); app.processEvents();
        require(QFileInfo::exists(chosen) && !welcome.findChild<QWidget *>("workspaceWelcome")->isVisible(),"New workspace did not open");
        require(welcome.findChild<QPushButton *>("newShipment")->isEnabled(),"Cannot add a shipment to a new workspace");
        require(rememberedWorkspace()==chosen,"Last workspace was not remembered");
        welcome.resize(1100,760); welcome.close(); require(!Preferences().value("window/geometry").toByteArray().isEmpty(),"Window geometry not stored");
        savePrintPreferences(true,1.25,-.75,99.5,100.25); savePrintPreferences(false,-2,3,101,98);
        AirWaybillPrintOptions awb{true}; OceanBillPrintOptions ocean; loadPrintPreferences(awb,ocean);
        require(awb.horizontalOffsetMm==1.25 && awb.verticalOffsetMm==-.75 && ocean.horizontalOffsetMm==-2 && ocean.verticalScalePercent==98,"Print preferences did not reload independently");
        Preferences().setValue("printing/awb/x",std::numeric_limits<double>::quiet_NaN()); loadPrintPreferences(awb,ocean);
        require(awb.horizontalOffsetMm==0,"Corrupt print settings were accepted");
        QTimer::singleShot(0,&welcome,[&] { auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); require(dialog,"Print alignment dialog missing"); dialog->findChild<QDoubleSpinBox *>("printOffsetX")->setValue(20); dialog->reject(); });
        require(!choosePrintRegistration(&welcome,true,awb),"Print cancel accepted");
        loadPrintPreferences(awb,ocean); require(awb.horizontalOffsetMm==0,"Cancel changed saved print alignment");
        const auto root=tmp.filePath("installation"); QDir().mkpath(root+"/Data"); QDir().mkpath(root+"/Backup");
        const auto bytes=synthetic_tps::fixture(); write(root+"/Data/customer.tps",bytes); write(root+"/Backup/BOOKING.TPS",synthetic_tps::fixture(false,false,true));
        write(root+"/Data/INVOICE.TPS",bytes); write(root+"/Data/OBL.TPS",bytes); write(root+"/Data/OBLLINE.TPS",bytes);
        write(root+"/Data/original.pdf","%PDF-synthetic");
        const auto found=findInstallationData(root); require(found.size()==2,"Installation folders were not detected independently");
        const InstallationData data{root+"/Data",{}};
        require(selectedImportFiles(data,{"companies"})==QStringList{"customer.tps"},"Companies imported unrelated data");
        require(selectedImportFiles(data,{"ocean"})==QStringList({"OBL.TPS","OBLLINE.TPS"}),"Document cargo dependency missing");
        ImportWorkspaceDialog importer; importer.show(); app.processEvents();
        importer.findChild<QLineEdit *>("importInstallation")->setText(root);
        importer.findChild<QPushButton *>("scanInstallation")->click();
        auto *folders=importer.findChild<QComboBox *>("importDataFolder"); require(folders->currentIndex()==-1,"Multiple snapshots were silently combined/chosen");
        folders->setCurrentIndex(folders->findText(root+"/Data"));
        for(const auto &group : importCategories()) importer.findChild<QCheckBox *>("import_"+group.id)->setChecked(group.id=="companies");
        const auto imported=tmp.filePath("selected.sqlite"); importer.findChild<QLineEdit *>("importDestination")->setText(imported);
        if(argc==2) { app.processEvents(); importer.grab().save(QString::fromLocal8Bit(argv[1])+"/import.png"); }
        importer.findChild<QPushButton *>("startImport")->click();
        require(importer.result()==QDialog::Accepted && importer.report["records"]==2,"Selective import failed");
        require(importer.report["decoded_tps"]==1 && importer.report["historical_pdfs"]==0,"Unchecked data was imported");
        { Store store(imported); require(store.records({"CUSTOMER.TPS"}).size()==2 && store.records({"BOOKING.TPS","OBL.TPS"}).isEmpty(),"Selected import contains wrong records"); require(store.blankRecord("AWB.TPS").values["AWB:NUMBER_OF_PIECES"].toArray().size()==6,"Unselected types cannot be created later"); }
        QFile original(root+"/Data/customer.tps"); require(original.open(QIODevice::ReadOnly) && original.readAll()==bytes,"Import changed source bytes");
        rejects([&] { migrateSelected(data.directory,imported,{},{{"customer.tps"},true}); },"Import overwrote workspace");
        rejects([&] { migrateSelected(data.directory,tmp.filePath("escape.sqlite"),{},{{"../Backup/BOOKING.TPS"},true}); },"Import escaped selected folder");
        const auto protectedDir=tmp.filePath("protected"); QDir().mkpath(protectedDir);
        write(protectedDir+"/CUSTOMER.TPS",synthetic_tps::encrypted(bytes,"synthetic-owner"));
        rejects([&] { migrateSelected(protectedDir,tmp.filePath("bad-password.sqlite"),"wrong",{{"CUSTOMER.TPS"},true}); },"Bad password produced a successful empty import");
        require(!QFileInfo::exists(tmp.filePath("bad-password.sqlite")),"Failed import published destination");
        migrateSelected(protectedDir,tmp.filePath("protected.sqlite"),"synthetic-owner",{{"CUSTOMER.TPS"},true});
        QTextStream(stdout)<<"PASS: empty workspaces, first-run UI, persisted settings, selective imports, snapshot separation, cargo dependencies, privacy, passwords and atomic failure.\n";
    } catch(const std::exception &e) { QTextStream(stderr)<<"FAILED: "<<e.what()<<'\n'; return 1; }
}
