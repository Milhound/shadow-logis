#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include <QFontDatabase>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace shadowlogis {
namespace {
// US Letter export form layout. See forms/README.txt for the public form
// references. These are review drafts; carrier execution fields stay blank.
class MarineForm {
    QPdfWriter &writer;
    QPainter p;
    QString family="sans-serif";
    bool dock;
    OceanBillPrintOptions options;
    bool textOnly;
    double headerShift=0;
    QMap<QString,QString> continued;
    QList<double> edges{26,121,158,178,426,506,586};
    struct Fragment { QList<QStringList> cells; double height; };
    using Page=QList<Fragment>;
    double cargoTop=423, cargoBottom=616;
    static constexpr double leading=10.5;
public:
    MarineForm(QPdfWriter &w,bool receipt,const OceanBillPrintOptions &profile) : writer(w),p(&w),dock(receipt),options(profile),textOnly(!receipt && profile.textOnly) {
        if(textOnly) { edges={8,125,155,155,471,535,604}; cargoTop=362; cargoBottom=620; }
        if(!p.isActive()) throw std::runtime_error("Cannot create the marine document.");
        for(const auto &candidate : {"Arial","Liberation Sans","Helvetica","DejaVu Sans"})
            if(QFontDatabase::families().contains(candidate)) { family=candidate; break; }
    }
    void draw(const Record &r,const QList<Record> &cargo) {
        const auto profile=r.values["NATIVE:COMPANY_PROFILE"].toObject();
        if(dock && !profile.isEmpty() && (!companyAddress(profile,true).isEmpty() || !profile["logo_png"].toString().isEmpty())) { headerShift=58; cargoTop+=headerShift; }
        QList<Page> pages{Page{}}; double used=0;
        const auto prefix=cargoSource(r.source).second;
        QList<Record> printableCargo=cargo;
        // Some preserved ocean headers have aggregate cargo but no cargo rows.
        // Show those stored amounts, without fabricating a goods description.
        if(printableCargo.isEmpty() && !dock) {
            Record totals; totals.source="OBLLINE.TPS"; bool any=false;
            for(const auto &[from,to] : QList<QPair<QString,QString>>{{"PCS","PCS"},{"KGS","WEIGHT_KGS"},{"LBS","WEIGHT_LBS"},{"CBM","MEASURE_CBM"},{"CFT","MEASURE_CFT"}}) {
                const auto value=r.values["OBL:"+from];
                if(valueText(value).toDouble()!=0) { totals.values[prefix+to]=value; any=true; }
            }
            if(any) printableCargo<<totals;
        }
        // Long routing/charge text continues in clearly labeled document-note
        // rows. It is not truncated, counted as cargo or repeated as a charge.
        const auto continuation=[&](const QString &label,const QString &value,double width,double height,double size) {
            const auto cleaned=clean(value);
            if(fits(cleaned,width,height,size)) return;
            continued[label]=cleaned;
            Record note; note.source=cargoSource(r.source).first;
            note.values["NATIVE:CONTINUATION_LABEL"]=label;
            note.values[prefix+"DESCRIPTION"]="DOCUMENT INFORMATION CONTINUATION (not additional cargo)\n"+label+"\n"+cleaned;
            printableCargo<<note;
        };
        const QString pp=dock ? "DOD:" : "OBL:";
        const auto v=[&](const QString &key) { return clean(valueText(r.values[pp+key])); };
        if(dock) {
            continuation("ONWARD INLAND ROUTING",v("ON_NAME"),270,20,7);
            continuation("DOMESTIC ROUTING / DELIVERY INSTRUCTIONS",dockInstructions(r),550,32,7);
        } else {
            continuation("TYPE OF MOVE / ONWARD ROUTING",QStringList{v("TYPE_MOVE"),v("ONWARD_ARRAY")}.filter(QRegularExpression("\\S")).join('\n'),270,20,7);
            continuation("FREIGHT RATES / CHARGES / INSTRUCTIONS",QStringList{v("HBL_RATING_NOTES"),v("BL_INSTRUCTIONS")}.filter(QRegularExpression("\\S")).join('\n'),270,42,7);
        }
        for(const auto &line : printableCargo) {
            if(line.source!=cargoSource(r.source).first) throw std::runtime_error("Cargo belongs to a different document type.");
            for(const auto &suffix : {"REFERENCE_NUMBER","ID_SEQUENCE"}) {
                const auto pp=dock ? "DOD:" : "OBL:";
                if(line.values.contains(prefix+suffix) && line.values[prefix+suffix]!=r.values[QString(pp)+suffix])
                    throw std::runtime_error("Cargo belongs to a different document sequence.");
            }
            const auto v=[&](const QString &key) { return clean(valueText(line.values[prefix+key])); };
            QStringList weight,volume;
            for(const auto &[key,unit] : QList<QPair<QString,QString>>{{"WEIGHT_KGS"," kg"},{"WEIGHT_LBS"," lb"}})
                if(!v(key).isEmpty() && v(key).toDouble()!=0) weight<<weightText(v(key))+unit;
            for(const auto &[key,unit] : QList<QPair<QString,QString>>{{"MEASURE_CBM"," m³"},{"MEASURE_CFT"," ft³"}})
                if(!v(key).isEmpty() && v(key).toDouble()!=0) volume<<weightText(v(key))+unit;
            const QStringList values{v("MARKS_NUMBERS"),weightText(v("PCS")),"",v("DESCRIPTION"),weight.join('\n'),volume.join('\n')};
            bool any=false; for(const auto &value : values) any|=!value.isEmpty() && value!="0";
            if(!any) continue;
            QList<QStringList> cells; int rows=1;
            for(int col=0;col<6;++col) { cells<<(col==1 ? QStringList{values[col]} : wrap(values[col],edges[col+1]-edges[col]-8,8)); rows=std::max(rows,int(cells.last().size())); }
            for(int offset=0;offset<rows;) {
                int capacity=int((cargoBottom-cargoTop-used-8)/leading);
                if(capacity<1) { pages<<Page{}; used=0; capacity=int((cargoBottom-cargoTop-8)/leading); }
                const bool noteContinuation=offset>0 && line.values.contains("NATIVE:CONTINUATION_LABEL");
                if(noteContinuation && capacity<2) { pages<<Page{}; used=0; capacity=int((cargoBottom-cargoTop-8)/leading); }
                const int take=std::min(capacity-int(noteContinuation),rows-offset); Fragment fragment;
                for(const auto &cell : cells) fragment.cells<<cell.mid(offset,take);
                if(noteContinuation) for(int col=0;col<6;++col) fragment.cells[col].prepend(col==3 ? "DOCUMENT INFORMATION (continued)" : "");
                fragment.height=(take+int(noteContinuation))*leading+8; pages.last()<<fragment; used+=fragment.height; offset+=take;
                if(pages.size()>200) throw std::runtime_error("The marine document exceeds 200 pages; no PDF was saved.");
            }
        }
        for(int page=0;page<pages.size();++page) {
            if(page && !writer.newPage()) throw std::runtime_error("Cannot add a PDF page.");
            if(textOnly) {
                p.resetTransform(); p.translate(options.horizontalOffsetMm*72/25.4,options.verticalOffsetMm*72/25.4);
                p.scale(options.horizontalScalePercent/100,options.verticalScalePercent/100);
                stockHeader(r,page+1,pages.size());
            } else {
                p.fillRect(QRectF(0,0,612,792),Qt::white);
                if(headerShift) companyHeader(profile);
                p.save(); p.translate(0,headerShift); header(r,page+1,pages.size()); p.restore();
            }
            double y=cargoTop;
            for(const auto &fragment : pages[page]) {
                for(int col=0;col<6;++col) {
                    double yy=y+4;
                    for(const auto &line : fragment.cells[col]) { text({edges[col]+4,yy,edges[col+1]-edges[col]-8,leading},line,col==1 ? "Package count" : "Cargo",8,col==1 ? 5.5 : 8); yy+=leading; }
                }
                y+=fragment.height; rule(26,y,586,y);
            }
            if(textOnly) { stockFooter(r); continue; }
            footer(r); text({26,758,560,12},"DRAFT - "+QString(dock ? "Dock receipt" : "Ocean bill of lading")+" | "+recordReference(r)+" | Page "+QString::number(page+1)+" of "+QString::number(pages.size()),"Footer",7,7);
        }
        p.end();
    }
private:
    static QString clean(QString value) {
        value.replace("\r\n","\n"); value.replace('\r','\n');
        QStringList lines; for(const auto &line : value.split('\n')) if(!line.trimmed().isEmpty()) lines<<line.trimmed();
        return lines.join('\n');
    }
    QFont font(double size,bool bold=false) const { QFont f(family); f.setPointSizeF(size); f.setBold(bold); return f; }
    QStringList wrap(const QString &value,double width,double size) {
        QStringList result;
        if(value.isEmpty()) return result;
        for(const auto &paragraph : value.split('\n')) {
            QTextLayout layout(paragraph,font(size),p.device()); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
            layout.beginLayout();
            while(true) { auto l=layout.createLine(); if(!l.isValid()) break; l.setLineWidth(width); result<<paragraph.mid(l.textStart(),l.textLength()).trimmed(); }
            layout.endLayout();
            if(paragraph.isEmpty()) result<<"";
        }
        return result;
    }
    bool fits(const QString &value,double width,double height,double size) {
        QTextLayout layout(QString(value).replace('\n',QChar::LineSeparator),font(size),p.device());
        QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
        layout.beginLayout(); double total=0;
        while(true) { auto line=layout.createLine(); if(!line.isValid()) break; line.setLineWidth(width); total+=line.height(); }
        layout.endLayout(); return total<=height+.01;
    }
    void text(QRectF rect,const QString &value,const QString &label,double size=9,double min=7,bool bold=false) {
        if(value.isEmpty()) return;
        if(textOnly) {
            const auto physical=p.worldTransform().mapRect(rect);
            if(!QRectF(0,0,612,792).contains(physical)) throw std::runtime_error((label+": registration moves text outside the Letter sheet. Reduce the offset or scale.").toStdString());
        }
        for(double s=size;s>=min-.01;s-=.25) {
            QTextLayout layout(QString(value).replace('\n',QChar::LineSeparator),font(s,bold),p.device());
            QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
            layout.beginLayout(); double height=0;
            while(true) { auto line=layout.createLine(); if(!line.isValid()) break; line.setLineWidth(rect.width()); line.setPosition({0,height}); height+=line.height(); }
            layout.endLayout();
            if(height<=rect.height()+.01) { p.setPen(Qt::black); layout.draw(&p,rect.topLeft()); return; }
        }
        throw std::runtime_error((label+" does not fit the marine form. Shorten this field or move extra detail to cargo lines; no text was clipped and no PDF was saved.").toStdString());
    }
    void rule(double x,double y,double xx,double yy) { if(textOnly) return; p.setPen(QPen(Qt::black,.5)); p.drawLine(QPointF(x,y),QPointF(xx,yy)); }
    void box(QRectF r) { p.setPen(QPen(Qt::black,.5)); p.setBrush(Qt::NoBrush); p.drawRect(r); }
    void field(QRectF r,const QString &label,const QString &value,double size=9) {
        box(r); text(r.adjusted(4,2,-4,-r.height()+12),label,"Form label",6.6,6.6,true);
        text(r.adjusted(5,14,-5,-3),continued.contains(label) ? "See document information continuation in the table." : clean(value),label,size,7);
    }
    void header(const Record &r,int page,int pages) {
        const QString prefix=dock ? "DOD:" : "OBL:";
        const auto v=[&](const QString &key) { return clean(valueText(r.values[prefix+key])); };
        const auto ext=[&](const QString &key) { return clean(valueText(r.values["NATIVE:"+key])); };
        text({26,19,400,21},dock ? "DOCK RECEIPT" : "OCEAN BILL OF LADING","Title",16,16,true);
        if(dock) text({330,25,120,12},fieldText("DOD:DO_DATE",r.values["DOD:DO_DATE"]),"Document date",8,8);
        text({456,25,130,12},"Page "+QString::number(page)+" of "+QString::number(pages),"Page",8,8);
        field({26,45,280,95},"SHIPPER / EXPORTER",v("SHIPPER_ARRAY"));
        field({306,45,140,30},"BOOKING NUMBER",v("BOOKING_NUMBER"));
        field({446,45,140,30},dock ? "DOCK RECEIPT NUMBER" : "DOCUMENT NUMBER",v(dock ? "DO_NUMBER" : "DOCUMENT_NUMBER"));
        QString refs=v("REFERENCE_ARRAY");
        if(refs.isEmpty()) refs=recordReference(r);
        field({306,75,280,65},"EXPORT REFERENCES",refs,8);
        field({26,140,280,77},"CONSIGNEE",v("CONSIGNEE_ARRAY"));
        field({306,140,280,77},"FORWARDING AGENT / REFERENCES",ext("FORWARDER_ADDRESS"));
        field({26,217,280,77},"NOTIFY PARTY",v("NOTIFY_ARRAY"));
        // DOD's historical TO_NAME is the delivery/document recipient, not
        // evidence that the named company is the cargo consignee.
        field({306,217,280,77},dock ? "DELIVER TO / DELIVERY LOCATION" : "ALSO NOTIFY / DOMESTIC ROUTING",
              dock ? QStringList{v("TO_NAME"),v("PD_NAME")}.filter(QRegularExpression("\\S")).join('\n') : v("ROUTING_ARRAY"),8);
        field({26,294,140,35},"PRE-CARRIAGE BY",v("PRECARRIAGE_BY"),8);
        field({166,294,140,35},"PLACE OF RECEIPT",v("PLACE_OF_RECEIPT"),8);
        field({306,294,280,35},"POINT AND COUNTRY OF ORIGIN",v("POINT_OF_ORIGIN"),8);
        QString carrier=ext("CARRIER_NAME");
        if(!v("VESSEL_NAME").isEmpty()) carrier+=(carrier.isEmpty() ? "" : " / ")+v("VESSEL_NAME");
        if(!v("VOYAGE_NUMBER").isEmpty()) carrier+=" / "+v("VOYAGE_NUMBER");
        field({26,329,280,38},"EXPORTING CARRIER / VESSEL / VOYAGE",carrier,8);
        field({306,329,140,38},"PORT OF LOADING",v("LOAD_PORT"),8);
        field({446,329,140,38},"LOADING PIER / TERMINAL",v("PIER_TERMINAL"),8);
        field({26,367,140,37},"PORT OF DISCHARGE",v("DISCHARGE_PORT"),8);
        field({166,367,140,37},"PLACE OF DELIVERY",v("PLACE_OF_DELIVERY"),8);
        field({306,367,280,37},dock ? "ONWARD INLAND ROUTING" : "TYPE OF MOVE / ONWARD ROUTING",dock ? v("ON_NAME") : QStringList{v("TYPE_MOVE"),v("ONWARD_ARRAY")}.filter(QRegularExpression("\\S")).join('\n'),8);
        const QStringList labels{"MARKS AND NUMBERS","NO. OF\nPKGS.","HM","DESCRIPTION OF PACKAGES AND GOODS","GROSS WEIGHT","MEASUREMENTS"};
        for(int i=0;i<6;++i) {
            box({edges[i],404,edges[i+1]-edges[i],cargoBottom-headerShift-404});
            text({edges[i]+3,407,edges[i+1]-edges[i]-6,15},labels[i],"Cargo heading",i==2 ? 5.5 : 6.2,i==2 ? 5.5 : 6.2,true);
        }
        rule(26,cargoTop-headerShift,586,cargoTop-headerShift);
    }
    // Registration based on page 2 of the supplied logistics scan. That page
    // is scanner-sized; Letter is the provisional physical stock size. No
    // fixed form labels, rules, contract text or signature lines are printed.
    void companyHeader(const QJsonObject &profile) {
        const auto bytes=QByteArray::fromBase64(profile["logo_png"].toString().toLatin1());
        QImage logo; double x=26;
        if(!bytes.isEmpty()) {
            if(!logo.loadFromData(bytes,"PNG")) throw std::runtime_error("The saved company logo could not be read.");
            const QSizeF area(130,52); const auto scaled=QSizeF(logo.size()).scaled(area,Qt::KeepAspectRatio);
            p.drawImage(QRectF(QPointF(26,12)+(QPointF(area.width()-scaled.width(),area.height()-scaled.height())/2),scaled),logo); x=170;
        }
        text({x,10,586-x,17},profile["company_name"].toString(),"Company name",12,8,true);
        // Contact details share a line to keep the header compact.
        const auto address=profile["address"].toString().trimmed();
        QStringList contact; for(const auto &key : {"contact_name","phone","email"}) if(!profile[key].toString().trimmed().isEmpty()) contact<<profile[key].toString().trimmed();
        text({x,29,586-x,38},QStringList{address,contact.join("  |  ")}.filter(QRegularExpression("\\S")).join('\n'),"Company address / contact",8,6);
    }
    void stockHeader(const Record &r,int page,int pages) {
        const auto v=[&](const QString &key) { return clean(valueText(r.values["OBL:"+key])); };
        text({12,54,280,64},v("SHIPPER_ARRAY"),"Shipper");
        text({12,132,280,64},v("CONSIGNEE_ARRAY"),"Consignee");
        text({12,205,280,64},v("NOTIFY_ARRAY"),"Notify party");
        text({310,54,135,22},v("BOOKING_NUMBER"),"Booking number");
        text({451,54,149,22},v("DOCUMENT_NUMBER"),"Document number");
        auto refs=v("REFERENCE_ARRAY"); if(refs.isEmpty()) refs=recordReference(r);
        text({310,80,290,48},refs,"Export references",8);
        text({310,136,290,60},clean(valueText(r.values["NATIVE:FORWARDER_ADDRESS"])),"Forwarding agent",8);
        text({310,205,290,60},v("ROUTING_ARRAY"),"Domestic routing",8);
        text({12,277,150,18},v("PRECARRIAGE_BY"),"Pre-carriage",8);
        text({177,277,120,18},v("PLACE_OF_RECEIPT"),"Place of receipt",8);
        text({310,277,290,18},v("POINT_OF_ORIGIN"),"Point of origin",8);
        auto carrier=QStringList{clean(valueText(r.values["NATIVE:CARRIER_NAME"])),v("VESSEL_NAME"),v("VOYAGE_NUMBER")}.filter(QRegularExpression("\\S")).join(" / ");
        text({12,300,155,22},carrier,"Exporting carrier / vessel / voyage",8);
        text({177,300,120,22},v("LOAD_PORT"),"Port of loading",8);
        text({310,300,290,22},v("PIER_TERMINAL"),"Loading terminal",8);
        text({12,327,150,20},v("DISCHARGE_PORT"),"Port of discharge",8);
        text({177,327,120,20},v("PLACE_OF_DELIVERY"),"Place of delivery",8);
        const auto routing=QStringList{v("TYPE_MOVE"),v("ONWARD_ARRAY")}.filter(QRegularExpression("\\S")).join('\n');
        text({310,327,270,20},continued.contains("TYPE OF MOVE / ONWARD ROUTING") ? "See document information continuation." : routing,"Onward routing",8);
        if(pages>1) text({510,30,90,12},QString("Page %1 of %2").arg(page).arg(pages),"Page",7,7);
    }
    void stockFooter(const Record &r) {
        const auto v=[&](const QString &key) { return clean(valueText(r.values["OBL:"+key])); };
        const auto notes=QStringList{v("HBL_RATING_NOTES"),v("BL_INSTRUCTIONS")}.filter(QRegularExpression("\\S")).join('\n');
        text({12,625,270,42},continued.contains("FREIGHT RATES / CHARGES / INSTRUCTIONS") ? "See document information continuation." : notes,"Freight instructions",8);
        text({12,681,140,18},v("PAYABLE_AT"),"Payable at",8);
        text({177,681,120,18},v("NUMBER_OF_ORIGINALS")=="0" ? "" : v("NUMBER_OF_ORIGINALS"),"Originals",8);
        text({12,720,280,18},v("MBL_NUMBER"),"Master bill of lading",8);
        text({310,681,150,18},v("ISSUED_AT"),"Issued at",8);
        text({470,681,130,18},fieldText("OBL:DATE",r.values["OBL:DATE"]),"Date",8);
        text({310,720,290,18},v("BL_NUMBER"),"Bill of lading number",9);
    }
    static QString dockInstructions(const Record &r) {
        const QString prefix="DOD:";
        const auto v=[&](const QString &key) { return clean(valueText(r.values[prefix+key])); };
        QString instructions=v("INSTRUCTIONS_COMMENTS");
        QStringList details;
        for(const auto &[suffix,label] : QList<QPair<QString,QString>>{{"CUTOFF","Cutoff"},{"SAILING","Sailing"}}) {
            const auto date=fieldText(prefix+suffix,r.values[prefix+suffix]); if(!date.isEmpty()) details<<label+": "+date;
        }
        if(!v("PO_NUMBER").isEmpty()) details<<"P.O.: "+v("PO_NUMBER");
        if(!v("PROJECT_NUMBER").isEmpty()) details<<"Project: "+v("PROJECT_NUMBER");
        if(!v("TEL_NO").isEmpty()) details<<"Tel: "+v("TEL_NO");
        if(!details.isEmpty()) instructions+=(instructions.isEmpty() ? "" : "\n")+details.join("  |  ");
        return instructions;
    }
    void footer(const Record &r) {
        const QString prefix=dock ? "DOD:" : "OBL:";
        const auto v=[&](const QString &key) { return clean(valueText(r.values[prefix+key])); };
        if(dock) {
            field({26,616,560,49},"DOMESTIC ROUTING / DELIVERY INSTRUCTIONS",dockInstructions(r),8);
            field({26,665,280,29},"DELIVERED BY / TRUCK / PRO NUMBER",QStringList{v("DEL_CAR"),v("PRO_NUM")}.filter(QRegularExpression("\\S")).join(" / "),8);
            box({26,694,280,55});
            text({31,697,270,12},"ARRIVED:  Date __________  Time __________","Arrival labels",7,7);
            text({31,715,270,12},"UNLOADED: Date __________  Time __________","Unload labels",7,7);
            text({31,733,270,12},"CHECKED BY: _____________________________","Receiving labels",7,7);
            box({306,665,280,84});
            text({311,668,270,25},"Received subject to the carrier's dock receipt and bill of lading terms. Exceptions: __________________________","Receipt terms",7,7);
            text({311,700,270,12},"FOR THE CARRIER / RECEIVING CLERK","Receipt label",7,7,true);
            text({311,719,270,12},"By ____________________ Date ____________","Signature",8,8);
            text({311,736,270,11},"B/L: "+v("BL_NUMBER"),"Bill of lading number",7,7);
        } else {
            field({26,616,280,59},"FREIGHT RATES / CHARGES / INSTRUCTIONS",QStringList{v("HBL_RATING_NOTES"),v("BL_INSTRUCTIONS")}.filter(QRegularExpression("\\S")).join('\n'),8);
            field({26,675,140,35},"FREIGHT PAYABLE AT",v("PAYABLE_AT"),8);
            field({166,675,140,35},"NUMBER OF ORIGINALS",v("NUMBER_OF_ORIGINALS")=="0" ? "" : v("NUMBER_OF_ORIGINALS"),8);
            field({26,710,280,39},"MASTER BILL OF LADING",v("MBL_NUMBER"),9);
            box({306,616,280,133});
            // Public-domain US TRANSCOM Figure 513-1 receipt/issuance areas.
            // No stored legacy signature is carried into a newly issued form.
            text({311,620,270,47},"Received by Carrier for shipment by ocean vessel between port of loading and port of discharge, the goods specified above in apparent good order and condition unless otherwise stated. Delivery at the port of discharge or place of delivery, as applicable.","Carriage text",7,7);
            text({311,669,270,23},"IN WITNESS WHEREOF the original Bills of Lading have been signed; one being accomplished, the others shall be void.","Issuance text",7,7);
            text({311,695,270,13},"Dated at: "+v("ISSUED_AT")+"   "+fieldText("OBL:DATE",r.values["OBL:DATE"]),"Place and date",8,7);
            text({311,716,270,12},"By _____________________  Agent for carrier","Signature",8,8);
            text({311,735,270,12},"B/L No. "+v("BL_NUMBER"),"Bill of lading number",9,8,true);
        }
    }
};
void exportMarine(const Record &record,const QString &path,const QList<Record> &cargo,bool dock,const OceanBillPrintOptions &options={}) {
    if(record.source!=(dock ? "DODR.TPS" : "OBL.TPS")) throw std::runtime_error("A matching marine document record is required.");
    if(!std::isfinite(options.horizontalOffsetMm) || !std::isfinite(options.verticalOffsetMm) ||
       !std::isfinite(options.horizontalScalePercent) || !std::isfinite(options.verticalScalePercent) ||
       std::abs(options.horizontalOffsetMm)>25 || std::abs(options.verticalOffsetMm)>25 ||
       options.horizontalScalePercent<95 || options.horizontalScalePercent>105 || options.verticalScalePercent<95 || options.verticalScalePercent>105)
        throw std::runtime_error("Registration allows offsets within 25 mm and scale from 95 to 105 percent.");
    QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::Letter)); writer.setPageMargins(QMarginsF(0,0,0,0));
        writer.setTitle(dock ? "Dock receipt (draft)" : (options.textOnly ? "Ocean bill of lading - text only" : "Ocean bill of lading (draft)")); writer.setCreator("Shadow Logis");
        MarineForm form(writer,dock,options); form.draw(record,cargo);
    }
    if(!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
void exportOceanBillOfLading(const Record &r,const QString &path,const QList<Record> &cargo,const OceanBillPrintOptions &options) { exportMarine(r,path,cargo,false,options); }
void exportDockReceipt(const Record &r,const QString &path,const QList<Record> &cargo) { exportMarine(r,path,cargo,true); }
}
