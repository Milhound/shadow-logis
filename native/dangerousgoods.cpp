#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include <QFontDatabase>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextLayout>
#include <stdexcept>

namespace shadowlogis {
namespace {
// Standard column-format declaration, following the supplied examples and
// IATA's published blank. A4 points; no logo or endorsement is reproduced.
class Declaration {
    QPainter p;
    QString family="sans-serif";
    const QList<double> xs{54,98,256,317,357,456,493,540};
    double text(QRectF box, const QString &value, double size=8, bool bold=false, bool measure=false) {
        if (value.trimmed().isEmpty()) return 0;
        QFont font(family); font.setPointSizeF(size); font.setBold(bold);
        QTextLayout layout(QString(value).replace('\n',QChar::LineSeparator),font,p.device());
        QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
        layout.beginLayout(); double height=0;
        while (true) { auto line=layout.createLine(); if (!line.isValid()) break; line.setLineWidth(box.width()); line.setPosition({0,height}); height+=line.height(); }
        layout.endLayout();
        if (!measure) {
            if (height>box.height()+.1) throw std::runtime_error((QString("Declaration field beginning \"")+value.left(65)+"\" exceeds its form box. Shorten the field or split the goods row; no text was clipped and no PDF was saved.").toStdString());
            p.setPen(Qt::black); layout.draw(&p,box.topLeft());
        }
        return height;
    }
    void line(double x,double y,double xx,double yy) { p.setPen(QPen(Qt::black,.45)); p.drawLine(QPointF(x,y),QPointF(xx,yy)); }
    void label(QRectF box,const QString &value,double size=8,bool bold=false) {
        while(size>5 && text(box,value,size,bold,true)>box.height()) size-=.25;
        text(box,value,size,bold);
    }
    void frame() {
        p.fillRect(QRectF(0,0,595.28,841.89),Qt::white);
        p.setPen(Qt::NoPen); p.setBrush(QColor(210,28,44));
        for (double x : {27.0,555.0}) for (double y=26;y<792;y+=30)
            p.drawPolygon(QPolygonF{{x,y},{x+12,y+8},{x+12,y+28},{x,y+20}});
        p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::black,.45)); p.drawRect(QRectF(54,44,486,736));
        label({54,28,486,14},"SHIPPER'S DECLARATION FOR DANGEROUS GOODS",10,true);
        for (double y : {113,183,318,342,388,604,666}) line(54,y,540,y);
        line(297,44,297,318); line(54,210,297,210); line(54,228,291,228);
        line(169,228,169,290); line(54,290,291,290); line(304,287,534,287);
        label({60,47,230,13},"Shipper",9,true); label({60,116,230,13},"Consignee",9,true);
        label({304,50,90,13},"Air Waybill No.",9);
        label({304,75,150,13},"Page        of         Pages",8.5);
        label({304,94,124,18},"Shipper's Reference No. (optional)",7.5);
        label({60,187,230,21},"Two completed and signed copies of this Declaration must be handed to the operator.",6.8,true);
        label({60,214,225,13},"TRANSPORT DETAILS",9,true);
        label({60,232,103,33},"This shipment is within the limitations prescribed for:\n(delete non-applicable)",6.5);
        line(60,267,163,267); line(60,288,163,288); for(double x : {60,114,163}) line(x,267,x,288);
        label({63,270,48,18},"PASSENGER AND\nCARGO AIRCRAFT",5,true);
        label({117,270,43,18},"CARGO\nAIRCRAFT ONLY",5,true);
        label({174,232,117,22},"Airport of Departure (optional):",7.5);
        label({60,294,230,12},"Airport of Destination (optional):",8);
        label({304,188,230,13},"WARNING",9,true);
        label({304,205,229,63},"Failure to comply in all respects with the applicable Dangerous Goods Regulations may be in breach of the applicable law, subject to legal penalties.",8.5,true);
        label({304,291,230,13},"Shipment type: (delete non-applicable)",8);
        line(322,303,528,303); line(322,317,528,317); for(double x : {322,427,528}) line(x,303,x,317);
        label({325,305,100,12},"NON-RADIOACTIVE",8,true); label({435,305,91,12},"RADIOACTIVE",8,true);
        label({60,322,470,16},"NATURE AND QUANTITY OF DANGEROUS GOODS",9,true);
        label({137,346,210,12},"Dangerous Goods Identification",8.5,true);
        line(54,360,357,360); line(357,342,357,604); line(456,342,456,604); line(493,342,493,604);
        for(double x : {98,256,317}) line(x,360,x,604);
        const QStringList labels{"UN or\nID No.","Proper Shipping Name","Class or Division\n(subsidiary hazard)","Packing Group","Quantity and Type of Packing","Packing Inst.","Authorization"};
        for(int i=0;i<7;++i) label({xs[i]+3,i<4 ? 365.0 : 350.0,xs[i+1]-xs[i]-6,i<4 ? 22.0 : 34.0},labels[i],i==2 ? 5.5 : 6.7);
        label({58,607,478,12},"Additional Handling Information",8);
        line(359,666,359,780);
        label({59,672,294,101},"I hereby declare that the contents of this consignment are fully and accurately described above by the proper shipping name, and are classified, packaged marked and labelled/placarded, and are in all respects in proper condition for transport according to applicable international and national governmental regulations. I declare that all of the applicable air transport requirements have been met.",8);
        label({364,672,170,12},"Name of Signatory",8); label({364,708,170,12},"Date",8);
        label({364,746,170,26},"Signature\n(See warning above)",8);
    }
    void field(QRectF box,const QString &v,double size=9.5) {
        while(size>8 && text(box,v,size,false,true)>box.height()) size-=.25;
        text(box,v,size);
    }
public:
    explicit Declaration(QPdfWriter &w) : p(&w) {
        if (!p.isActive()) throw std::runtime_error("Cannot initialize declaration PDF.");
        for(const auto &name : {"Arial","Liberation Sans","Helvetica","DejaVu Sans"})
            if(QFontDatabase::families().contains(name)) { family=name; break; }
    }
    void draw(QPdfWriter &w,const Record &r) {
        const auto rows=r.values["DGD:LINES"].toArray(); const auto columns=dangerousGoodsColumns();
        QList<QList<int>> pages{{}}; QList<double> heights; double used=0;
        for(int i=0;i<rows.size();++i) {
            double height=18;
            for(int c=0;c<7;++c) height=qMax(height,6+text({0,0,xs[c+1]-xs[c]-6,210},rows[i].toObject()[columns[c].first].toString(),8.5,false,true));
            if(height>208) throw std::runtime_error("A dangerous goods row exceeds one page. Split the row explicitly; no PDF was saved.");
            if(used+height>208) { pages.append(QList<int>{}); used=0; }
            pages.last().append(i); heights.append(height); used+=height;
        }
        const auto v=[&](const QString &key) { return valueText(r.values["DGD:"+key]).trimmed(); };
        for(int page=0;page<pages.size();++page) {
            if(page && !w.newPage()) throw std::runtime_error("Cannot create declaration page.");
            frame();
            field({60,62,230,49},v("SHIPPER_ARRAY"),9); field({60,131,230,49},v("CONSIGNEE_ARRAY"),9);
            field({397,49,137,22},v("AWB_NUMBER"),10);
            text({330,75,18,12},QString::number(page+1),8.5); text({365,75,20,12},QString::number(pages.size()),8.5);
            field({432,92,102,20},v("REFERENCE_NUMBER"),9);
            field({174,254,116,33},v("DEPARTURE"),9); field({60,306,230,11},v("DESTINATION"),8.5);
            const auto strike=[&](QRectF rect) { line(rect.left(),rect.top(),rect.right(),rect.bottom()); line(rect.left(),rect.bottom(),rect.right(),rect.top()); };
            if(v("AIRCRAFT")=="Cargo aircraft only") strike({61,268,52,19});
            if(v("AIRCRAFT")=="Passenger and cargo aircraft") strike({115,268,47,19});
            if(v("SHIPMENT_TYPE")=="Non-radioactive") strike({428,304,99,12});
            if(v("SHIPMENT_TYPE")=="Radioactive") strike({323,304,103,12});
            double y=391;
            for(int row : pages[page]) {
                for(int c=0;c<7;++c) text({xs[c]+3,y,xs[c+1]-xs[c]-6,heights[row]-4},rows[row].toObject()[columns[c].first].toString(),8.5);
                y+=heights[row];
            }
            QString handling=v("HANDLING");
            if(!v("EMERGENCY").isEmpty()) handling+=(handling.isEmpty() ? "" : "\n")+QString("Emergency contact / 24-hour number: ")+v("EMERGENCY");
            field({59,621,475,41},handling,9);
            field({364,686,170,20},v("SIGNATORY"),9); field({364,723,170,20},v("DATE"),9);
            // No name is used as a signature. Signing remains a separate action.
            text({54,815,486,12},"DRAFT - Review declaration details before signing | "+v("REFERENCE_NUMBER"),7);
        }
        p.end();
    }
};
}
void exportDangerousGoods(const Record &record,const QString &path) {
    if(record.source!=DangerousGoodsSource) throw std::runtime_error("Select an air dangerous goods declaration.");
    validateDangerousGoods(record.values);
    QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::A4)); writer.setPageMargins(QMarginsF(0,0,0,0));
        writer.setCreator("Shadow Logis"); writer.setTitle("Shipper's declaration for dangerous goods - "+recordReference(record));
        Declaration form(writer); form.draw(writer,record);
    }
    if(!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
