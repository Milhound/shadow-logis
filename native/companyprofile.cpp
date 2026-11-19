#include "companyprofile.h"
#include "domain.h"
#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <memory>
#include <stdexcept>
namespace shadowlogis {
QByteArray companyLogo(const QString &path) {
    if(QFileInfo(path).size()>10*1024*1024) throw std::runtime_error("Choose a logo smaller than 10 MB.");
    QImageReader reader(path); reader.setAutoTransform(true);
    const auto size=reader.size();
    if(!size.isValid() || qint64(size.width())*size.height()>40000000) throw std::runtime_error("Choose a PNG or JPEG image under 40 megapixels.");
    if(size.width()>1200 || size.height()>1200) reader.setScaledSize(size.scaled(1200,1200,Qt::KeepAspectRatio));
    auto image=reader.read(); if(image.isNull()) throw std::runtime_error("The selected logo could not be read.");
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG") || bytes.size()>2*1024*1024) throw std::runtime_error("Choose a smaller logo; the stored image must be under 2 MB.");
    return bytes;
}
CompanyProfileDialog::CompanyProfileDialog(Store &store,QWidget *parent) : QDialog(parent) {
    setObjectName("companyProfile"); setWindowTitle("Company profile"); resize(660,730);
    const auto prior=store.companyProfile(); auto profile=std::make_shared<QJsonObject>(prior);
    auto *layout=new QVBoxLayout(this);
    auto *note=new QLabel("Choose your logistics company and contact, then review the details. New documents receive a copy of this profile. Saved documents keep their existing details and logo."); note->setWordWrap(true); layout->addWidget(note);
    auto *form=new QFormLayout; layout->addLayout(form);
    auto *customer=new QComboBox; customer->setObjectName("profileCustomer"); customer->addItem("Enter company details",QString());
    for(const auto &r : store.records({"CUSTOMER.TPS"})) customer->addItem(recordTitle(r),QString::number(r.id));
    auto *contact=new QComboBox; contact->setObjectName("profileContact"); contact->addItem("Enter contact details",QString());
    for(const auto &r : store.records({"CONTACTS.TPS"})) contact->addItem(recordTitle(r),QString::number(r.id));
    customer->setCurrentIndex(qMax(0,customer->findData(prior["customer_id"].toString())));
    contact->setCurrentIndex(qMax(0,contact->findData(prior["contact_id"].toString())));
    for(auto *combo : {customer,contact}) { combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(30); }
    form->addRow("My company",customer); form->addRow("My contact",contact);
    QMap<QString,QLineEdit *> fields;
    for(const auto &[key,label] : QList<QPair<QString,QString>>{{"company_name","Company name"},{"contact_name","Contact name"},{"phone","Phone"},{"email","Email"},{"website","Website"},{"iata","Agent IATA code"}}) {
        auto *edit=new QLineEdit(prior[key].toString()); edit->setObjectName("profile_"+key); fields[key]=edit; form->addRow(label,edit);
    }
    auto *address=new QPlainTextEdit(prior["address"].toString()); address->setObjectName("profile_address"); address->setFixedHeight(95); form->insertRow(3,"Company address",address);
    auto *logo=new QLabel; logo->setObjectName("profileLogo"); logo->setFixedHeight(90); logo->setAlignment(Qt::AlignCenter); layout->addWidget(logo);
    const auto showLogo=[logo,profile] { const auto bytes=QByteArray::fromBase64((*profile)["logo_png"].toString().toLatin1()); QPixmap pix; if(pix.loadFromData(bytes,"PNG")) logo->setPixmap(pix.scaled(320,85,Qt::KeepAspectRatio,Qt::SmoothTransformation)); else { logo->clear(); logo->setText("No company logo"); } };
    showLogo();
    auto *actions=new QHBoxLayout; auto *upload=new QPushButton("Upload logo…"); upload->setObjectName("uploadCompanyLogo"); auto *remove=new QPushButton("Remove logo"); remove->setObjectName("removeCompanyLogo"); actions->addWidget(upload); actions->addWidget(remove); actions->addStretch(); layout->addLayout(actions);
    auto *failure=new QLabel; failure->setObjectName("profileError"); failure->setWordWrap(true); layout->addWidget(failure);
    connect(upload,&QPushButton::clicked,this,[this,profile,showLogo,failure] {
        const auto path=QFileDialog::getOpenFileName(this,"Choose company logo",{},"Logo images (*.png *.jpg *.jpeg)"); if(path.isEmpty()) return;
        try { (*profile)["logo_png"]=QString::fromLatin1(companyLogo(path).toBase64()); showLogo(); failure->clear(); } catch(const std::exception &e) { failure->setText(e.what()); }
    });
    connect(remove,&QPushButton::clicked,this,[profile,showLogo] { profile->remove("logo_png"); showLogo(); });
    connect(customer,&QComboBox::currentIndexChanged,this,[&store,customer,contact,fields,address,profile,failure] {
        try {
            (*profile)["customer_id"]=customer->currentData().toString(); profile->remove("customer_number");
            if(customer->currentData().toString().isEmpty()) return;
            const auto r=store.record(customer->currentData().toString().toLongLong());
            contact->setCurrentIndex(0);
            (*profile)["customer_number"]=r.values["CUS:NUMBER"];
            fields["company_name"]->setText(recordTitle(r));
            QStringList lines; for(const auto &key : {"ADDRESS","ADDRESS2","SUITE"}) if(!valueText(r.values["CUS:"+QString(key)]).trimmed().isEmpty()) lines<<valueText(r.values["CUS:"+QString(key)]).trimmed();
            for(const auto &line : {(valueText(r.values["CUS:CITY_STATE"])+" "+valueText(r.values["CUS:ZIP"])).trimmed(),valueText(r.values["CUS:COUNTRY"]).trimmed()}) if(!line.isEmpty()) lines<<line;
            address->setPlainText(lines.join('\n'));
            for(const auto &[from,to] : QList<QPair<QString,QString>>{{"CONTACT","contact_name"},{"PH1_NUMBER","phone"},{"EMAIL","email"},{"WEB_ADDRESS","website"},{"IATA","iata"}}) fields[to]->setText(valueText(r.values["CUS:"+from]));
        } catch(const std::exception &e) { failure->setText(e.what()); }
    });
    connect(contact,&QComboBox::currentIndexChanged,this,[&store,contact,fields,profile,failure] {
        try {
            (*profile)["contact_id"]=contact->currentData().toString(); if(contact->currentData().toString().isEmpty()) return;
            const auto r=store.record(contact->currentData().toString().toLongLong()); fields["contact_name"]->setText(recordTitle(r));
            fields["phone"]->setText(valueText(r.values["CTA:PHONE"])); fields["email"]->setText(valueText(r.values["CTA:EMAIL"]));
        } catch(const std::exception &e) { failure->setText(e.what()); }
    });
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this,&store,prior,profile,fields,address,customer,contact,failure] {
        try {
            for(auto it=fields.begin();it!=fields.end();++it) (*profile)[it.key()]=it.value()->text().trimmed();
            (*profile)["address"]=address->toPlainText().trimmed(); (*profile)["customer_id"]=customer->currentData().toString(); (*profile)["contact_id"]=contact->currentData().toString();
            store.saveCompanyProfile(*profile,prior); accept();
        } catch(const std::exception &e) { failure->setText(e.what()); }
    });
}
}
