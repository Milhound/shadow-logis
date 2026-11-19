#include "workspaceui.h"
#include "workspace.h"
#include "store.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <memory>
#include <stdexcept>

namespace shadowlogis {
ImportWorkspaceDialog::ImportWorkspaceDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("importWorkspace"); setWindowTitle("Import legacy TPS installation"); resize(760,800);
    auto *layout=new QVBoxLayout(this);
    auto *intro=new QLabel("Create a separate Shadow Logis workspace from your legacy TPS installation. Close the legacy program first. Select its installation folder or a copied backup; the data folders inside it will be detected. Original files are only read and remain unchanged.");
    intro->setWordWrap(true); layout->addWidget(intro);
    auto *form=new QFormLayout; layout->addLayout(form);
    auto *folder=new QLineEdit; folder->setObjectName("importInstallation"); folder->setPlaceholderText("Installation folder or copied backup");
    auto *browse=new QPushButton("Browse…"); browse->setObjectName("browseInstallation"); auto *row=new QHBoxLayout; row->addWidget(folder,1); row->addWidget(browse); form->addRow("Installation",row);
    auto *data=new QComboBox; data->setObjectName("importDataFolder"); form->addRow("Data folder",data);
    auto *scan=new QPushButton("Find data folders"); scan->setObjectName("scanInstallation"); form->addRow(scan);
    auto *scroll=new QScrollArea; scroll->setWidgetResizable(true); auto *body=new QWidget; auto *choices=new QVBoxLayout(body); scroll->setWidget(body); layout->addWidget(scroll,1);
    QList<QCheckBox *> checks;
    for(const auto &group : importCategories()) {
        auto *check=new QCheckBox(group.label); check->setObjectName("import_"+group.id); check->setProperty("category",group.id); check->setToolTip(group.description); check->setChecked(group.defaultSelected); check->setEnabled(false); choices->addWidget(check); checks<<check;
        if(group.id=="companies") { auto *hint=new QLabel(group.description); hint->setWordWrap(true); choices->addWidget(hint); }
    }
    choices->addStretch();
    auto *note=new QLabel("Document cargo is included with its document type. Documents can be imported without bookings and found in Archive. Invoices and accounting tables are excluded. Selected source files are preserved privately in the workspace."); note->setWordWrap(true); layout->addWidget(note);
    auto *summary=new QLabel; summary->setObjectName("importSummary"); summary->setWordWrap(true); layout->addWidget(summary);
    auto *outputForm=new QFormLayout; layout->addLayout(outputForm);
    auto *output=new QLineEdit; output->setObjectName("importDestination"); output->setPlaceholderText("Choose a new workspace file");
    auto *save=new QPushButton("Choose…"); auto *saveRow=new QHBoxLayout; saveRow->addWidget(output,1); saveRow->addWidget(save); outputForm->addRow("New workspace",saveRow);
    auto *password=new QLineEdit; password->setObjectName("importPassword"); password->setEchoMode(QLineEdit::Password); password->setPlaceholderText("Optional owner password; never saved"); outputForm->addRow("Owner password",password);
    auto *error=new QLabel; error->setObjectName("importError"); error->setWordWrap(true); layout->addWidget(error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel); auto *import=buttons->addButton("Import into new workspace",QDialogButtonBox::AcceptRole); import->setObjectName("startImport"); import->setProperty("primary",true); import->setEnabled(false); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    auto candidates=std::make_shared<QList<InstallationData>>();
    auto scannedRoot=std::make_shared<QString>();
    const auto selected=[checks] { QStringList result; for(auto *c : checks) if(c->isEnabled() && c->isChecked()) result<<c->property("category").toString(); return result; };
    const auto update=[=] {
        error->clear(); QStringList files;
        const bool valid=data->currentIndex()>=0 && data->currentIndex()<candidates->size() && folder->text()==*scannedRoot;
        if(valid) try { files=selectedImportFiles(candidates->at(data->currentIndex()),selected()); }
        catch(const std::exception &e) { error->setText(QString::fromUtf8(e.what())); }
        summary->setText(valid ? QString("%1 %2 selected from %3. No other data folders will be merged.").arg(files.size()).arg(files.size()==1 ? "file" : "files").arg(data->currentText()) : "Choose an installation, then find and select its data folder.");
        import->setEnabled(valid && !files.isEmpty() && !output->text().trimmed().isEmpty() && error->text().isEmpty());
    };
    connect(data,&QComboBox::currentIndexChanged,this,[=] {
        const bool valid=data->currentIndex()>=0 && data->currentIndex()<candidates->size();
        for(int i=0;i<checks.size();++i) {
            bool available=false;
            if(valid) try { available=!selectedImportFiles(candidates->at(data->currentIndex()),{importCategories()[i].id}).isEmpty(); } catch(...) {}
            checks[i]->setEnabled(available);
        }
        update();
    });
    connect(scan,&QPushButton::clicked,this,[=] {
        try {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            auto found=findInstallationData(folder->text().trimmed());
            QApplication::restoreOverrideCursor();
            data->clear(); *candidates=std::move(found); *scannedRoot=folder->text();
            for(const auto &candidate : *candidates) data->addItem(candidate.directory);
            if(candidates->size()>1) data->setCurrentIndex(-1); // Never silently prefer a backup over live data.
            update(); if(candidates->isEmpty()) error->setText("No supported TPS data folders found. Select the installed application folder or a backup containing TPS files.");
        } catch(const std::exception &e) { QApplication::restoreOverrideCursor(); data->clear(); candidates->clear(); update(); error->setText(QString::fromUtf8(e.what())); }
    });
    connect(browse,&QPushButton::clicked,this,[=,this] { const auto path=QFileDialog::getExistingDirectory(this,"Select legacy TPS installation",folder->text()); if(!path.isEmpty()) { folder->setText(path); scan->click(); } });
    connect(save,&QPushButton::clicked,this,[=,this] { const auto path=QFileDialog::getSaveFileName(this,"Create imported workspace",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/shadow-logis.sqlite","Shadow Logis workspace (*.sqlite)"); if(!path.isEmpty()) output->setText(path); });
    connect(folder,&QLineEdit::textChanged,this,update); connect(output,&QLineEdit::textChanged,this,update);
    for(auto *check : checks) connect(check,&QCheckBox::toggled,this,update);
    connect(import,&QPushButton::clicked,this,[=,this] {
        auto secret=password->text().toUtf8(); password->clear(); import->setEnabled(false);
        try {
            if(folder->text()!=*scannedRoot || data->currentIndex()<0) throw std::runtime_error("Find and select the data folder first");
            const auto choice=candidates->at(data->currentIndex());
            const auto files=selectedImportFiles(choice,selected());
            QApplication::setOverrideCursor(Qt::WaitCursor);
            report=migrateSelected(choice.directory,output->text().trimmed(),secret,{files,true});
            secret.fill('\0'); QApplication::restoreOverrideCursor(); destination=output->text().trimmed(); accept();
        } catch(const std::exception &e) { secret.fill('\0'); QApplication::restoreOverrideCursor(); update(); error->setText(QString::fromUtf8(e.what())); }
    });
    update();
}
}
