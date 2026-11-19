#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include <QFontDatabase>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextLayout>
#include <cmath>
#include <stdexcept>

namespace shadowlogis {
namespace {
// Coordinates trace the blank VICS form supplied by the user (487 x 630).
// Output is US Letter. Static wording, section positions and grids are retained.
class VicsForm {
public:
    explicit VicsForm(QPdfWriter &writer) : p(&writer) {
        if (!p.isActive()) throw std::runtime_error("Cannot create the bill of lading.");
        for (const auto &candidate : {"Arial","Liberation Sans","Helvetica","DejaVu Sans"})
            if (QFontDatabase::families().contains(candidate)) { family=candidate; break; }
        p.scale(612.0/487,792.0/630); p.fillRect(QRectF(0,0,487,630),Qt::white);
    }
    void draw(const Record &record, const QList<Record> &cargo) {
        frame();
        const auto v=[&](const QString &key) { return valueText(record.values["NATIVE:VICS_"+key]).trimmed(); };
        const auto put=[&](QString key,QRectF r) { text(r,v(key),key,6.7); };
        text({41,44,127,11},fieldText("OBL:DATE",record.values["OBL:DATE"]),"Date",6.7);
        text({438,44,21,11},"1","Page count",7);
        text({348,67,110,11},valueText(record.values["OBL:BL_NUMBER"]),"Bill of lading number",7);
        party(record.values["OBL:SHIPPER_ARRAY"],67,"Shipper");
        party(record.values["OBL:CONSIGNEE_ARRAY"],121,"Consignee");
        // The legacy ocean consignee is used only as the supplied ship-to address.
        // No terminal, freight payer, SID/CID, or domestic carrier is inferred.
        party(QJsonArray::fromStringList(v("BILL_TO").split('\n')),175,"Freight bill-to");
        put("SID",{40,100,178,10}); put("CID",{40,154,178,10});
        put("LOCATION",{202,121,56,10});
        mark(v("FROM_FOB"),245,102,"Ship-from FOB"); mark(v("TO_FOB"),245,156,"Ship-to FOB");
        put("CARRIER",{327,113,131,9}); put("TRAILER",{320,122,138,9}); put("SEAL",{320,132,138,9});
        put("SCAC",{294,144,164,10}); put("PRO",{313,154,145,10});
        put("INSTRUCTIONS",{21,224,237,18});
        const auto terms=choice(v("TERMS"),{"P","C","3"},"Freight terms");
        if (terms.isEmpty()) text({267,187,193,9},"FREIGHT TERMS UNCONFIRMED","Freight terms review",5.8);
        else text({terms=="P" ? 299.0 : terms=="C" ? 365.0 : 435.0,214,15,10},"X","Freight terms",7);
        mark(v("MASTER"),286,229,"Master BOL");
        orders(record);
        cargoRows(record,cargo);
        put("VALUE",{24,503,94,10}); put("VALUE_PER",{145,503,113,10}); put("COD",{337,485,117,10});
        const auto cod=choice(v("COD_TERMS"),{"P","C"},"COD fee terms");
        if (!cod.isEmpty()) mark("X",cod=="C" ? 368 : 425,498,"COD fee terms");
        mark(v("COD_CHECK"),400,507,"COD check acceptable");
        const auto loaded=choice(v("LOADED"),{"S","D"},"Trailer loaded by");
        if (!loaded.isEmpty()) mark("X",164,loaded=="S" ? 558 : 568,"Trailer loaded by");
        const auto counted=choice(v("COUNTED"),{"S","P","D"},"Freight counted by");
        if (!counted.isEmpty()) mark("X",217,counted=="S" ? 558 : counted=="P" ? 568 : 579,"Freight counted by");
        // Signatures and pickup date intentionally remain unexecuted. Ocean
        // signature strings must never become a new shipper/carrier attestation.
        text({18,596,450,12},"DRAFT - VICS freight BOL layout | "+recordReference(record)+" | Review before issue","Review footer",6);
        p.end();
    }
private:
    QPainter p;
    QString family="sans-serif";
    static QString choice(QString value,const QStringList &allowed,const QString &label) {
        value=value.toUpper();
        if (!value.isEmpty() && !allowed.contains(value)) throw std::runtime_error((label+": use "+allowed.join(", ")+" or leave blank.").toStdString());
        return value;
    }
    void line(double x,double y,double xx,double yy) { p.setPen(QPen(Qt::black,.4)); p.drawLine(QPointF(x,y),QPointF(xx,yy)); }
    void box(double x,double y,double w,double h) { p.setPen(QPen(Qt::black,.4)); p.setBrush(Qt::NoBrush); p.drawRect(QRectF(x,y,w,h)); }
    void mark(const QString &value,double x,double y,const QString &label) {
        if (!choice(value,{"X"},label).isEmpty()) text({x-.1,y-1,7,8},"X",label,6,5,Qt::AlignCenter);
    }
    double text(QRectF rect,const QString &value,const QString &label,double size=6.2,double minimum=5,
                Qt::Alignment align=Qt::AlignLeft,bool bold=false,QColor ink=Qt::black,bool measure=false) {
        if (value.trimmed().isEmpty()) return 0;
        for (double s=size;s>=minimum-.01;s-=.2) {
            QFont font(family); font.setPointSizeF(s); font.setBold(bold);
            QTextLayout layout(QString(value).replace('\n',QChar::LineSeparator),font,p.device());
            QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); option.setAlignment(align);
            layout.setTextOption(option); layout.beginLayout(); double height=0;
            while (true) { auto l=layout.createLine(); if (!l.isValid()) break; l.setLineWidth(rect.width()); l.setPosition({0,height}); height+=l.height(); }
            layout.endLayout();
            if (height<=rect.height()+.01) {
                if (!measure) { p.save(); p.setPen(ink); p.setClipRect(rect); layout.draw(&p,rect.topLeft()); p.restore(); }
                return height;
            }
        }
        throw std::runtime_error((label+" does not fit the supplied VICS form. Use an approved BOL supplement or shorten the field; no text was clipped and no PDF was saved.").toStdString());
    }
    void label(QRectF r,const QString &s,double size=6.2,bool bold=false,Qt::Alignment a=Qt::AlignLeft) { text(r,s,"Form label: "+s,size,size,a,bold); }
    void band(double y,double h,const QString &s,double x=18,double w=451) {
        p.fillRect(QRectF(x,y,w,h),Qt::black); text({x,y+.1,w,h},s,"Section heading",6,6,Qt::AlignCenter,true,Qt::white);
    }
    void party(const QJsonValue &value,double y,const QString &name) {
        QStringList parts;
        for (const auto &v : value.toArray()) if (!valueText(v).trimmed().isEmpty()) parts<<valueText(v).trimmed();
        if (parts.isEmpty()) return;
        text({54,y,y==121 ? 108.0 : 204.0,10},parts.takeFirst(),name+" name",6.7);
        if (!parts.isEmpty()) text({54,y+11,204,10},parts.takeFirst(),name+" street address",6.5,4.9);
        // Retain every remaining free-form line, including country and contacts.
        text({86,y+22,172,10},parts.join(", "),name+" remaining address",6.5,4.9);
    }
    void frame() {
        box(18,43,451,542);
        label({21,44,28,10},"Date:"); label({170,43,180,15},"BILL OF LADING",12,true,Qt::AlignCenter);
        label({386,44,52,11},"Page 1 of",8,true); line(431,54,460,54); line(18,58,469,58);
        line(262,58,262,244); band(58,8,"SHIP FROM",18,244); band(112,8,"SHIP TO",18,244);
        band(166,8,"THIRD PARTY FREIGHT CHARGES BILL TO:",18,244);
        for (double y : {67.,121.,175.}) {
            label({22,y,32,10},"Name:"); label({22,y+11,32,10},"Address:"); label({22,y+22,64,10},"City/State/Zip:");
        }
        label({22,100,25,10},"SID#:"); label({22,154,25,10},"CID#:");
        label({164,121,40,10},"Location #:",6); line(202,128,259,128);
        for (double y : {100.,154.}) { label({224,y,21,10},"FOB:",7); box(245,y+2,6,6); }
        line(18,214,262,214); label({22,215,172,10},"SPECIAL INSTRUCTIONS:",7);
        label({266,67,83,10},"Bill of Lading Number:",6.5,true); line(349,75,450,75);
        text({283,88,163,14},"B A R  C O D E  S P A C E","Barcode placeholder",8,8,Qt::AlignCenter,true,QColor(175,175,175));
        line(262,112,469,112); label({266,113,60,9},"CARRIER NAME:",6.5,true); line(324,120,448,120);
        label({266,122,54,9},"Trailer number:"); label({266,132,54,9},"Seal number(s):"); line(262,142,469,142);
        label({266,144,29,10},"SCAC:",7,true); label({266,154,48,10},"Pro number:",7,true);
        text({283,174,163,14},"B A R  C O D E  S P A C E","Barcode placeholder",8,8,Qt::AlignCenter,true,QColor(175,175,175));
        line(262,196,469,196); label({266,198,82,10},"Freight Charge Terms:",6.8,true);
        label({347,199,117,8},"(freight charges are prepaid unless",5.7,true);
        label({266,207,90,7},"marked otherwise)",5.7,true);
        label({266,215,48,10},"Prepaid",6.2,true); line(294,222,322,222);
        label({341,215,34,10},"Collect",6.2,true); line(366,222,393,222);
        label({407,215,43,10},"3rd Party",6.2,true); line(434,222,456,222);
        line(262,226,469,226); box(286,229,6,6);
        label({322,227,144,17},"Master Bill of Lading: with attached underlying\nBills of Lading",6);
        label({273,237,46,7},"(check box)",5.5);
        band(244,8,"CUSTOMER ORDER INFORMATION");
        for (double x : {143.,182.,226.,270.}) line(x,252,x,364);
        label({20,253,121,11},"CUSTOMER ORDER NUMBER",6.3,true,Qt::AlignCenter);
        label({144,253,37,11},"# PKGS",6.3,true,Qt::AlignCenter); label({183,253,42,11},"WEIGHT",6.3,true,Qt::AlignCenter);
        label({227,252,42,8},"PALLET/SLIP",6,true,Qt::AlignCenter); label({227,259,42,6},"(CIRCLE ONE)",4.5,false,Qt::AlignCenter);
        label({272,253,195,11},"ADDITIONAL SHIPPER INFO",6.3,true,Qt::AlignCenter);
        for (int i=0;i<=8;++i) line(18,265+i*11,469,265+i*11);
        for (int i=0;i<8;++i) { label({234,266+i*11.,11,10},"Y",5.6,true); label({260,266+i*11.,8,10},"N",5.6,true); }
        label({21,354,120,10},"GRAND TOTAL",7,true);
        band(365,8,"CARRIER INFORMATION");
        const QList<double> xs{18,49,82,110,141,182,208,387,432,469};
        for (double x : xs) line(x,381,x,483);
        for (double x : {82.,141.,182.,208.,387.}) line(x,373,x,381);
        line(18,381,141,381); line(387,381,469,381);
        label({19,374,62,8},"HANDLING UNIT",6.2,true,Qt::AlignCenter); label({83,374,57,8},"PACKAGE",6.2,true,Qt::AlignCenter);
        label({388,374,80,8},"LTL ONLY",6.2,true,Qt::AlignCenter);
        for (int i=0;i<4;++i) label({xs[i]+1,383,xs[i+1]-xs[i]-2,10},i%2 ? "TYPE" : "QTY",6.2,true,Qt::AlignCenter);
        label({143,383,37,10},"WEIGHT",6.2,true,Qt::AlignCenter); label({183,383,24,15},"H.M.\n(X)",6,true,Qt::AlignCenter);
        label({209,374,177,8},"COMMODITY DESCRIPTION",6.3,true,Qt::AlignCenter);
        label({212,382,171,16},"Commodities requiring special or additional care or attention in handling or stowing must be so marked and packaged as to ensure safe transportation with ordinary care. See Section 2(e) of NMFC Item 360",3.7,false,Qt::AlignCenter);
        label({388,383,43,10},"NMFC #",6.3,true,Qt::AlignCenter); label({433,383,35,10},"CLASS",6.3,true,Qt::AlignCenter);
        for (int i=0;i<=8;++i) line(18,398+i*9.5,469,398+i*9.5);
        text({388,436,79,20},"R E C E I V I N G\nS T A M P  S P A C E","Receiving stamp",7,7,Qt::AlignCenter,true,QColor(175,175,175));
        for (int col : {1,3,5,7,8}) p.fillRect(QRectF(xs[col],474,xs[col+1]-xs[col],9),QColor(210,210,210));
        label({210,475,175,9},"GRAND TOTAL",7,true,Qt::AlignCenter); line(18,483,469,483);
        line(262,483,262,514);
        label({22,486,236,19},"Where the rate is dependent on value, shippers are required to state specifically in writing the agreed or declared value of the property as follows:\n\"The agreed or declared value of the property is specifically stated by the shipper to be not exceeding",4.5);
        line(24,512,118,512); label({122,504,19,9},"per",6); line(145,512,251,512);
        label({278,485,63,10},"COD Amount: $",7,true); line(338,493,454,493);
        label({291,497,74,8},"Fee Terms:   Collect:",6.5,true); box(368,498,5,5);
        label({391,497,33,8},"Prepaid:",6.2,true); box(425,498,5,5);
        label({312,506,85,8},"Customer check acceptable:",6.1,true); box(400,507,5,5);
        line(18,514,469,514);
        label({22,515,444,9},"NOTE  Liability Limitation for loss or damage in this shipment may be applicable. See 49 U.S.C. - 14706(c)(1)(A) and (B).",6.3,true);
        line(18,524,469,524); line(262,524,262,547);
        label({22,525,237,21},"RECEIVED, subject to individually determined rates or contracts that have been agreed upon in writing between the carrier and shipper, if applicable, otherwise to the rates, classifications and rules that have been established by the carrier and are available to the shipper, on request, and to all applicable state and federal regulations.",4);
        label({266,525,199,13},"The carrier shall not make delivery of this shipment without payment of freight and all other lawful charges.",5.2);
        line(267,545,396,545); label({398,537,68,9},"Shipper Signature",6.2,true);
        line(18,547,469,547); line(157,547,157,585); line(211,547,211,585); line(313,547,313,585);
        label({22,548,133,8},"SHIPPER SIGNATURE / DATE",6.2,true);
        label({22,557,132,18},"This is to certify that the above named materials are properly classified, packaged, marked and labeled, and are in proper condition for transportation according to the applicable regulations of the DOT.",4);
        label({162,548,49,8},"Trailer Loaded:",6); label({215,548,96,8},"Freight Counted:",6);
        box(164,558,5,5); box(164,568,5,5); label({173,557,36,8},"By Shipper",5.8); label({173,567,36,8},"By Driver",5.8);
        box(217,558,5,5); box(217,568,5,5); box(217,579,5,5);
        label({225,557,85,8},"By Shipper",5.8); label({225,567,85,10},"By Driver/pallets said to contain",5.2);
        label({225,578,85,8},"By Driver/Pieces",5.8);
        label({317,548,150,8},"CARRIER SIGNATURE / PICKUP DATE",6.2,true);
        label({317,557,148,21},"Carrier acknowledges receipt of packages and required placards. Carrier certifies emergency response information was made available and/or carrier has the DOT emergency response guidebook or equivalent documentation in the vehicle. Property described above is received in good order, except as noted.",4);
    }
    static qint64 hundredths(QString value,const QString &label,bool whole=false) {
        if (value.isEmpty()) return 0;
        if (!QRegularExpression("^[0-9]{1,10}(?:\\.[0-9]{1,2})?$").match(value).hasMatch())
            throw std::runtime_error((label+": enter a nonnegative number with at most two decimal places.").toStdString());
        const auto result=value.section('.',0,0).toLongLong()*100+value.section('.',1,1).leftJustified(2,'0').toLongLong();
        if (whole && result%100) throw std::runtime_error((label+": enter a whole number.").toStdString());
        return result;
    }
    static QString decimal(qint64 v) { return QString::number(v/100)+'.'+QString::number(v%100).rightJustified(2,'0'); }
    void orders(const Record &r) {
        qint64 packages=0, weight=0; bool populated=false;
        for (int col=0;col<5;++col) if (r.values.contains(vicsOrderKeys()[col]) && r.values[vicsOrderKeys()[col]].toArray().size()!=8)
            throw std::runtime_error("Invalid VICS customer order table.");
        for (int row=0;row<8;++row) {
            QStringList cells;
            for (const auto &key : vicsOrderKeys()) { const auto a=r.values[key].toArray(); cells<<valueText(row<a.size() ? a[row] : QJsonValue()); }
            for (const auto &cell : cells) populated|=!cell.isEmpty();
            packages+=hundredths(cells[1],"Order packages",true); weight+=hundredths(cells[2],"Order weight");
            const QList<double> xs{20,145,184,228,272}, widths{121,35,40,40,195};
            for (int col : {0,1,2,4}) text({xs[col],266+row*11.,widths[col],9},cells[col]+(col==2 && !cells[col].isEmpty() ? " lb" : ""),"Customer order",6.2);
            const auto pallet=choice(cells[3],{"Y","N"},"Pallet/slip");
            if (!pallet.isEmpty()) { p.setPen(QPen(Qt::black,.6)); p.setBrush(Qt::NoBrush); p.drawEllipse(QRectF(pallet=="Y" ? 232 : 257,265.5+row*11,11,10)); }
        }
        if (populated) { text({145,354,35,10},QString::number(packages/100),"Order package total",6.5); text({184,354,40,10},decimal(weight)+" lb","Order weight total",6.2); }
    }
    void cargoRows(const Record &parent,QList<Record> cargo) {
        if (cargo.isEmpty() && (valueText(parent.values["OBL:PCS"]).toDouble()!=0 || valueText(parent.values["OBL:KGS"]).toDouble()!=0 || valueText(parent.values["OBL:LBS"]).toDouble()!=0)) {
            Record line; line.source="OBLLINE.TPS";
            line.values={{"OLI:PCS",parent.values["OBL:PCS"]},{"OLI:WEIGHT_KGS",parent.values["OBL:KGS"]},{"OLI:WEIGHT_LBS",parent.values["OBL:LBS"]}}; cargo<<line;
        }
        bool pounds=true;
        for (const auto &line : cargo) if (valueText(line.values["OLI:WEIGHT_LBS"]).toDouble()==0 && valueText(line.values["OLI:WEIGHT_KGS"]).toDouble()!=0) pounds=false;
        qint64 packages=0, weight=0, handling=0; int slot=0;
        for (const auto &line : cargo) {
            const auto get=[&](const QString &key) { return valueText(line.values[key]).trimmed(); };
            auto description=get("OLI:DESCRIPTION"); const auto marks=get("OLI:MARKS_NUMBERS");
            if (!marks.isEmpty()) description=marks+(description.isEmpty() ? "" : "\n"+description);
            const auto pcs=hundredths(get("OLI:PCS"),"Cargo packages",true);
            const auto w=hundredths(get(pounds ? "OLI:WEIGHT_LBS" : "OLI:WEIGHT_KGS"),"Cargo weight");
            if (!pounds && !w && get("OLI:WEIGHT_LBS").toDouble()!=0) throw std::runtime_error("Cargo has weights in incompatible units. Review kg/lb before printing; no conversion was guessed.");
            const auto hu=hundredths(get("NATIVE:VICS_HANDLING_QTY"),"Handling units",true);
            const auto hm=choice(get("NATIVE:VICS_HM"),{"X"},"Hazardous material");
            QStringList cells{hu ? QString::number(hu/100) : "",get("NATIVE:VICS_HANDLING_TYPE"),pcs ? QString::number(pcs/100) : "",
                get("NATIVE:VICS_PACKAGE_TYPE"),w ? decimal(w)+(pounds ? " lb" : " kg") : "",hm,description,get("NATIVE:VICS_NMFC"),get("NATIVE:VICS_CLASS")};
            bool any=false; for (const auto &cell : cells) any|=!cell.isEmpty(); if (!any) continue;
            const QList<double> xs{20,51,84,112,143,184,210,389,434}, widths{27,29,24,27,37,22,175,41,33};
            double height=0;
            for (int col=0;col<9;++col) height=std::max(height,text({0,0,widths[col],76},cells[col],"Cargo row",6.2,6.2,Qt::AlignLeft,false,Qt::black,true));
            const int rows=std::max(1,int(std::ceil((height+1)/9.5)));
            if (slot+rows>8) throw std::runtime_error("Cargo does not fit the supplied VICS form. An approved BOL supplement is required; no cargo was dropped and no PDF was saved.");
            const double top=398+slot*9.5;
            // Join ruled lines only within this one wrapped cargo entry.
            p.fillRect(QRectF(18.3,top+.3,450.4,rows*9.5-.6),Qt::white);
            for (double x : {49.,82.,110.,141.,182.,208.,387.,432.}) lineAt(x,top,x,top+rows*9.5);
            for (int col=0;col<9;++col) text({xs[col],top+.5,widths[col],rows*9.5-1},cells[col],"Cargo row",6.2,6.2);
            packages+=pcs; weight+=w; handling+=hu; slot+=rows;
        }
        if (slot) { text({20,475,27,8},QString::number(handling/100),"Handling total",6); text({84,475,24,8},QString::number(packages/100),"Package total",6); text({143,475,37,8},decimal(weight)+(pounds ? " lb" : " kg"),"Weight total",6); }
    }
    void lineAt(double x,double y,double xx,double yy) { line(x,y,xx,yy); }
};
}
void exportVicsBillOfLading(const Record &record,const QString &path,const QList<Record> &cargo) {
    if (record.source!="OBL.TPS") throw std::runtime_error("A bill-of-lading record is required.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setTitle("VICS freight bill of lading (draft)"); writer.setCreator("Shadow Logis");
        writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::Letter)); writer.setPageMargins(QMarginsF(0,0,0,0));
        VicsForm form(writer); form.draw(record,cargo);
    }
    if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
