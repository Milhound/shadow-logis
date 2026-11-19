#include "documents.h"
#include "domain.h"
#include "privatefile.h"
#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextLayout>
#include <algorithm>
#include <stdexcept>

namespace shadowlogis {
QString certificateForwarder(const Store &store,const Record &record) {
    if(record.values.contains("NATIVE:FORWARDER_ADDRESS")) return valueText(record.values["NATIVE:FORWARDER_ADDRESS"]);
    const auto profile=record.values["NATIVE:COMPANY_PROFILE"].toObject();
    if(!profile.isEmpty()) return companyAddress(profile).join('\n');
    const auto number=valueText(record.values["CRO:FORWARDER_NUMBER"]);
    if(number.isEmpty() || number=="0") return {};
    QStringList matches;
    for(const auto &customer : store.records({"CUSTOMER.TPS"})) if(valueText(customer.values["CUS:NUMBER"])==number) {
        QStringList address;
        for(const auto &key : {"NAME","ADDRESS","ADDRESS2","SUITE"}) {
            const auto line=valueText(customer.values["CUS:"+QString(key)]).trimmed(); if(!line.isEmpty()) address<<line;
        }
        const auto city=(valueText(customer.values["CUS:CITY_STATE"])+" "+valueText(customer.values["CUS:ZIP"])).trimmed();
        if(!city.isEmpty()) address<<city;
        matches<<address.join('\n');
    }
    return matches.size()==1 ? matches.first() : QString();
}
namespace {
QString clean(QString text) { text.replace("\r\n","\n"); text.replace('\r','\n'); return text.trimmed(); }
class CertificateForm {
    QPdfWriter &writer;
    QPainter painter;
    QString family="serif";
    const QList<double> edges{18,94,154,400,500,594};
    static constexpr double cargoTop=334,cargoBottom=550,leading=10;
    struct Fragment { QList<QStringList> cells; double height=0; };
    using Page=QList<Fragment>;
public:
    explicit CertificateForm(QPdfWriter &pdf) : writer(pdf),painter(&pdf) {
        if(!painter.isActive()) throw std::runtime_error("Cannot create the certificate of origin.");
        for(const auto &candidate : {"Times New Roman","Liberation Serif","Times","DejaVu Serif"})
            if(QFontDatabase::families().contains(candidate)) { family=candidate; break; }
    }
    void draw(const Record &record,const QList<Record> &cargo,const QString &forwarder) {
        QList<Page> pages{Page{}}; double used=0;
        for(const auto &row : cargo) {
            if(row.source!="CERTLINE.TPS") throw std::runtime_error("Certificate cargo belongs to a different document type.");
            for(const auto &key : {"REFERENCE_NUMBER","ID_SEQUENCE"}) {
                const auto field="CRL:"+QString(key);
                if(row.values.contains(field) && row.values[field]!=record.values["CRO:"+QString(key)])
                    throw std::runtime_error("Certificate cargo belongs to a different document sequence.");
            }
            const auto v=[&](const QString &key) { return clean(valueText(row.values["CRL:"+key])); };
            const auto amount=[&](const QString &key,const QString &unit) {
                const auto value=v(key); bool number=false; const auto quantity=value.toDouble(&number);
                return value.isEmpty() || (number && quantity==0) ? QString() : weightText(value)+unit;
            };
            const auto join=[](QStringList lines) { lines.removeAll(QString()); return lines.join('\n'); };
            const QStringList values{v("MARKS_NUMBERS"),amount("PCS",{}),v("DESCRIPTION"),
                join({amount("WEIGHT_LBS"," lb"),amount("WEIGHT_KGS"," kg")}),
                join({amount("MEASURE_CFT"," ft³"),amount("MEASURE_CBM"," m³")})};
            if(std::all_of(values.begin(),values.end(),[](const QString &s) { return s.isEmpty(); })) continue;
            QList<QStringList> cells; int rows=1;
            for(int col=0;col<values.size();++col) { cells<<wrap(values[col],edges[col+1]-edges[col]-8,8); rows=std::max(rows,int(cells.last().size())); }
            // Keep normal entries together. Oversized entries continue without
            // repeating package counts, weights or measurements on later pages.
            const double fullHeight=rows*leading+4;
            if(used && fullHeight<=cargoBottom-cargoTop && used+fullHeight>cargoBottom-cargoTop) { pages<<Page{}; used=0; }
            for(int offset=0;offset<rows;) {
                int capacity=int((cargoBottom-cargoTop-used-4)/leading);
                if(capacity<1) { pages<<Page{}; used=0; capacity=int((cargoBottom-cargoTop-4)/leading); }
                if(pages.size()>200) throw std::runtime_error("Certificate exceeds 200 pages; no PDF was saved.");
                const int take=std::min(capacity,rows-offset); Fragment fragment;
                for(const auto &cell : cells) fragment.cells<<cell.mid(offset,take);
                fragment.height=take*leading+4; pages.last()<<fragment; used+=fragment.height; offset+=take;
            }
        }
        for(int page=0;page<pages.size();++page) {
            if(page && !writer.newPage()) throw std::runtime_error("Cannot add a certificate page.");
            painter.fillRect(QRectF(0,0,612,792),Qt::white); painter.setPen(Qt::black);
            header(record,forwarder);
            double y=cargoTop;
            for(const auto &fragment : pages[page]) {
                for(int col=0;col<fragment.cells.size();++col) {
                    double lineY=y+2;
                    for(const auto &line : fragment.cells[col]) { text({edges[col]+4,lineY,edges[col+1]-edges[col]-8,leading},line,"Cargo",8,8); lineY+=leading; }
                }
                y+=fragment.height;
            }
            if(page+1==pages.size()) declaration(record);
            else text({32,570,548,34},"Goods continued on the next page. The declaration and signature areas are on the final page of this certificate.","Continuation",9,9);
            text({18,742,460,14},"Reference: "+recordReference(record),"Reference",8,7);
            text({510,742,84,14},QString("Page %1 of %2").arg(page+1).arg(pages.size()),"Page number",8,8);
        }
        painter.end();
    }
private:
    QFont font(double size,bool bold=false) const { QFont result(family); result.setPointSizeF(size); result.setBold(bold); return result; }
    QStringList wrap(const QString &value,double width,double size) const {
        QStringList lines; if(value.isEmpty()) return lines;
        for(const auto &paragraph : value.split('\n')) {
            if(paragraph.isEmpty()) { lines<<""; continue; }
            QTextLayout layout(paragraph,font(size),painter.device()); QTextOption options; options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(options);
            layout.beginLayout();
            while(true) { auto line=layout.createLine(); if(!line.isValid()) break; line.setLineWidth(width); lines<<paragraph.mid(line.textStart(),line.textLength()); }
            layout.endLayout();
        }
        return lines;
    }
    void text(const QRectF &rect,const QString &value,const QString &label,double size=9,double minimum=7,bool bold=false) {
        if(value.isEmpty()) return;
        for(double current=size;current>=minimum-.01;current-=.25) {
            auto content=value; content.replace('\n',QChar::LineSeparator);
            QTextLayout layout(content,font(current,bold),painter.device()); QTextOption options; options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(options);
            layout.beginLayout(); double height=0; bool widthFits=true;
            while(true) {
                auto line=layout.createLine(); if(!line.isValid()) break;
                line.setLineWidth(rect.width()); line.setPosition({0,height}); height+=line.height(); widthFits&=line.naturalTextWidth()<=rect.width()+.1;
            }
            layout.endLayout();
            if(height<=rect.height()+.1 && widthFits) { painter.setPen(Qt::black); layout.draw(&painter,rect.topLeft()); return; }
        }
        throw std::runtime_error((label+" does not fit the certificate of origin. Shorten the field or move extra detail to cargo notes; no text was clipped and no PDF was saved.").toStdString());
    }
    void rule(double x,double y,double right,double bottom) { painter.setPen(QPen(Qt::black,.5)); painter.drawLine(QPointF(x,y),QPointF(right,bottom)); }
    void field(const QRectF &rect,const QString &label,const QString &value,double size=9) {
        painter.setPen(QPen(Qt::black,.5)); painter.drawRect(rect);
        text(rect.adjusted(3,1,-3,-rect.height()+10),label,label+" label",6.8,6.8,true);
        text(rect.adjusted(5,10.5,-5,-2),value,label,size,6.5);
    }
    void header(const Record &r,const QString &forwarder) {
        const auto v=[&](const QString &key) { return clean(valueText(r.values["CRO:"+key])); };
        painter.setPen(QPen(Qt::black,.8)); painter.drawRect(QRectF(18,18,576,716));
        painter.setFont(font(16,true)); painter.drawText(QRectF(18,18,576,20),Qt::AlignCenter,"CERTIFICATE OF ORIGIN");
        field({18,38,294,65},"Shipper / Exporter",v("SHIPPER_ARRAY"));
        field({18,103,294,65},"Consignee",v("CONSIGNEE_ARRAY"));
        field({18,168,294,65},"Notify Party",v("NOTIFY_ARRAY"));
        auto number=v("DOCUMENT_NUMBER"); if(number.isEmpty()) number=v("BL_NUMBER"); if(number.isEmpty()) number=recordReference(r);
        painter.setPen(QPen(Qt::black,.5)); painter.drawRect(QRectF(312,38,147,19)); painter.drawRect(QRectF(459,38,135,19));
        text({315,39,141,8},"Document No.","Document label",6.5,6.5,true);
        text({315,47,141,9},number,"Document number",8,7);
        text({462,39,129,8},"Booking No.","Booking label",6.5,6.5,true);
        text({462,47,129,9},v("BOOKING_NUMBER"),"Booking number",8,7);
        QString refs=v("REFERENCE_ARRAY");
        if(refs.isEmpty()) refs="Reference: "+recordReference(r);
        if(!v("BL_NUMBER").isEmpty() && v("BL_NUMBER")!=number) refs+="\nB/L: "+v("BL_NUMBER");
        if(!v("CONSOLIDATION_NUMBER").isEmpty()) refs+="\nConsolidation: "+v("CONSOLIDATION_NUMBER");
        field({312,57,282,46},"Exporting References",refs,8);
        field({312,103,282,50},"Forwarding Agent (Name, Address and References)",clean(forwarder),8);
        field({312,153,282,24},"Point (State) of Origin or FTZ Number",v("POINT_OF_ORIGIN"),8);
        QString routing=v("ROUTING_ARRAY");
        if(!v("ONWARD_ARRAY").isEmpty()) routing+=(routing.isEmpty() ? "" : "\n")+v("ONWARD_ARRAY");
        if(!v("ULTIMATE_DESTINATION").isEmpty() && !v("PLACE_OF_DELIVERY").isEmpty() && v("ULTIMATE_DESTINATION")!=v("PLACE_OF_DELIVERY"))
            routing+=(routing.isEmpty() ? "" : "\n")+QString("Ultimate destination: ")+v("ULTIMATE_DESTINATION");
        field({312,177,282,81},"Domestic Routing / Export Instructions",routing,8);
        field({18,233,162,25},"Pre-Carriage By",v("PRECARRIAGE_BY"),8);
        field({180,233,132,25},"Place of Receipt by Pre-Carrier",v("PLACE_OF_RECEIPT"),8);
        field({18,258,162,25},"Exporting Carrier (Vessel / Airline)",v("VESSEL_NAME")+(v("VOYAGE_NUMBER").isEmpty() ? "" : " / "+v("VOYAGE_NUMBER")),8);
        field({180,258,132,25},"Port of Loading",v("LOAD_PORT"),8);
        field({312,258,282,25},"Loading Pier / Terminal",v("PIER_TERMINAL"),8);
        field({18,283,162,25},"Air / Sea Port of Discharge",v("DISCHARGE_PORT"),8);
        const auto delivery=v("PLACE_OF_DELIVERY");
        field({180,283,132,25},"For Transshipment To",delivery.isEmpty() ? v("ULTIMATE_DESTINATION") : delivery,8);
        field({312,283,144,25},"Type of Move",v("TYPE_MOVE"),8);
        field({456,283,138,25},"Containerized (Vessel Only)",v("CONTAINERIZED"),8);
        rule(18,320,594,320); rule(18,334,594,334);
        painter.setFont(font(7,true)); painter.drawText(QRectF(18,308,576,12),Qt::AlignCenter,"Particulars Furnished by Shipper");
        const QStringList titles{"MARKS & NOS.","NO. OF PKGS","DESCRIPTION OF PACKAGES AND GOODS","GROSS WEIGHT","MEASUREMENT"};
        for(int col=0;col<titles.size();++col) text({edges[col]+2,322,edges[col+1]-edges[col]-4,10},titles[col],"Cargo heading",7.3,7,true);
    }
    void declaration(const Record &r) {
        const auto v=[&](const QString &key) { const auto value=clean(valueText(r.values["CRO:"+key])); return value.isEmpty() ? QString("____________________") : value; };
        text({32,558,548,38},"The undersigned "+v("UNDERSIGNED")+" ("+(valueText(r.values["CRO:OWNER_AGENT"]).trimmed().isEmpty() ? "Owner or Agent" : v("OWNER_AGENT"))+
            "), hereby declares for the above named shipper that the goods described above were shipped on the date below, consigned as indicated, and are products of "+v("COUNTRY")+".","Origin declaration",9,8);
        text({32,599,270,22},"Dated at: "+v("DATED_AT"),"Declaration place",9,8);
        text({312,599,268,22},"On: "+fieldText("CRO:DATE",r.values["CRO:DATE"]),"Declaration date",9,8);
        text({32,624,270,14},"Sworn to before me on: "+fieldText("CRO:SWORN_DATE",r.values["CRO:SWORN_DATE"]),"Sworn date",8.5,8);
        const auto expires=fieldText("CRO:COMMISSION_EXPIRES_DATE",r.values["CRO:COMMISSION_EXPIRES_DATE"]);
        if(!expires.isEmpty()) text({32,638,270,14},"My commission expires on: "+expires,"Commission expiry",8.5,8);
        rule(32,657,270,657); rule(374,645,580,645);
        text({374,648,206,10},"SIGNATURE OF OWNER OR AGENT","Signature label",7,7,true);
        text({32,665,548,44},"The "+v("COC_AREA")+", a recognized Chamber of Commerce under the laws of the State of "+v("STATE")+
            ", certifies in reliance on the exporter's representation and not on the basis of independent verification that, to the best of its knowledge and belief, the products named in this document originated in "+v("COUNTRY")+".","Chamber declaration",9,8);
        text({365,718,105,12},"Authorized Signature:","Chamber signature label",8,8,true); rule(470,728,580,728);
    }
};
}
void exportCertificateOfOrigin(const Record &record,const QString &path,const QList<Record> &cargo,const QString &forwarder) {
    if(record.source!="CERTORGN.TPS") throw std::runtime_error("A certificate of origin record is required.");
    QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString()); makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::Letter)); writer.setPageMargins(QMarginsF(0,0,0,0));
        writer.setTitle("Certificate of origin"); writer.setCreator("Shadow Logis");
        QString agent=forwarder;
        if(record.values.contains("NATIVE:FORWARDER_ADDRESS")) agent=valueText(record.values["NATIVE:FORWARDER_ADDRESS"]);
        else if(agent.isEmpty()) agent=companyAddress(record.values["NATIVE:COMPANY_PROFILE"].toObject()).join('\n');
        CertificateForm form(writer); form.draw(record,cargo,agent);
    }
    if(!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
