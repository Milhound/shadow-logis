#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextLayout>
#include <stdexcept>
namespace shadowlogis {
namespace {
QString words(qint64 n) {
    const QStringList small{"zero","one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
    const QStringList tens{"","","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};
    if(n<20) return small[n];
    if(n<100) return tens[n/10]+(n%10 ? "-"+words(n%10) : "");
    if(n<1000) return words(n/100)+" hundred"+(n%100 ? " "+words(n%100) : "");
    for(const auto &[scale,label] : QList<QPair<qint64,QString>>{{1000000000,"billion"},{1000000,"million"},{1000,"thousand"}})
        if(n>=scale) return words(n/scale)+" "+label+(n%scale ? " "+words(n%scale) : "");
    return {};
}
class SightForm {
    QPainter p;
    QString family="serif";
    const Record &record;
public:
    SightForm(QPdfWriter &w,const Record &r) : p(&w),record(r) {
        if(!p.isActive()) throw std::runtime_error("Cannot create sight draft.");
        for(const auto &name : {"Times New Roman","Times","Liberation Serif","DejaVu Serif"}) if(QFontDatabase::families().contains(name)) { family=name; break; }
    }
    QString v(const QString &key) const { return fieldText("SIG:"+key,record.values["SIG:"+key]).trimmed().replace("\r\n","\n").replace('\r','\n'); }
    void text(QRectF rect,QString value,const QString &label,double size=9,bool bold=false,double minimum=7) {
        if(value.isEmpty()) return;
        for(double point=size;point>=minimum-.01;point-=.25) {
            QFont font(family); font.setPointSizeF(point); font.setBold(bold);
            QTextLayout layout(value.replace('\n',QChar::LineSeparator),font,p.device()); QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); layout.setTextOption(option);
            layout.beginLayout(); double height=0;
            while(true) { auto line=layout.createLine(); if(!line.isValid()) break; line.setLineWidth(rect.width()); line.setPosition({0,height}); height+=line.height(); }
            layout.endLayout(); if(height<=rect.height()+.01) { p.setPen(Qt::black); layout.draw(&p,rect.topLeft()); return; }
        }
        throw std::runtime_error((label+" does not fit the sight draft. Shorten this field; no text was clipped and no PDF was saved.").toStdString());
    }
    void rule(double x,double y,double xx,double yy) { p.setPen(QPen(Qt::black,.5)); p.drawLine(QPointF(x,y),QPointF(xx,yy)); }
    void box(QRectF rect) { p.setPen(QPen(Qt::black,.5)); p.setBrush(Qt::NoBrush); p.drawRect(rect); }
    void check(double x,double y,const QString &key,const QString &caption,double width=290) {
        const auto flag=v(key); if(!flag.isEmpty() && flag!="0" && flag!="1") throw std::runtime_error((key+": choose a valid instruction. No PDF was saved.").toStdString());
        box({x,y+2,8,8}); if(flag=="1") { rule(x+1,y+3,x+7,y+9); rule(x+7,y+3,x+1,y+9); }
        text({x+13,y,width-13,30},caption,"Instruction",8,false,7);
    }
    void page(int copy) {
        const auto money=sightDraftAmount(v("USAMOUNT"));
        auto amountWords=v("IN_DOLLARS"); if(amountWords.isEmpty()) amountWords=sightDraftAmountWords(v("USAMOUNT"));
        box({22,30,568,215});
        text({31,42,300,18},"U.S. $  "+money,"Amount",12,true);
        text({480,42,100,18},v("DATE1"),"Draft date",10,true);
        text({31,76,200,18},v("HOW_PAID"),"Payment terms",11); rule(31,94,235,94);
        text({239,76,342,20},copy==1 ? "of this First of Exchange (Second Unpaid)" : "of this Second of Exchange (First Unpaid)","Exchange wording",11);
        text({31,108,549,24},"Pay to the Order of  "+v("TO_ORDER"),"Pay to the order of",11);
        text({31,136,549,26},amountWords+(amountWords.isEmpty() ? "" : " — United States Dollars"),"Amount in words",10);
        rule(31,164,580,164);
        const auto account=QStringList{v("ACCOUNT_OF_1"),v("ACCOUNT_OF_2")}.filter(QRegularExpression("\\S")).join(" ");
        text({31,173,549,24},"for Value received and charge the same to account of  "+account,"Account of",10);
        text({31,190,260,50},"To  "+v("B_NAME"),"Drawee bank",9,false,5.5);
        text({300,195,38,47},QString::number(copy),"Exchange copy",38,true,38);
        text({347,204,233,18},v("SIGHT_SIG"),"Drawer name",9,true); rule(347,226,580,226);
        text({396,229,184,12},"Authorized Signature","Signature label",8);
        text({31,249,110,15},"No.  "+v("SD_NUMBER"),"Draft number",9,true);
        text({31,269,293,65},v("GENT1"),"Remitting bank / references",9);
        text({352,269,228,14},"TRANSMITTAL DATE   "+v("DATE2"),"Transmittal date",9,true);
        check(363,294,"COLLECTION","FOR COLLECTION",217);
        check(363,320,"COLLECT_LC","FOR PAYMENT / NEGOTIATION UNDER L/C",217);
        const auto extra=QStringList{v("LC_NO"),v("FOR_FIELD")}.filter(QRegularExpression("\\S")).join(" / ");
        text({31,336,549,13},extra,"Collection references",8);
        text({31,353,549,14},"We enclose DRAFT NUMBER  "+v("SD_NUMBER")+"  and documents listed below:","Enclosures",9);
        const QStringList keys{"B_L","B_LCOPY","COM_INV","INS_CER","CER_ORI","CONS_INV","L_C","PCK_LIST","WGHT_CER","OTHER_DOCS"};
        const QStringList labels{"B/L","B/L\nCOPY","COMM.\nINV.","INS.\nCERT.","CERT.\nORIG.","CONS.\nINV.","L/C","PKNG.\nLIST","WGHT.\nCERT.","OTHER DOCUMENTS"};
        double x=31;
        for(int i=0;i<keys.size();++i) {
            const double width=i==9 ? 549-9*42 : 42;
            box({x,373,width,54}); text({x+3,376,width-6,23},labels[i],"Enclosure label",8);
            rule(x,401,x+width,401);
            text({x+3,404,width-6,20},v(keys[i])+(i==9 && !v("OTHER_DOCS2").isEmpty() ? "\n"+v("OTHER_DOCS2") : ""),"Enclosure count / description",8,false,6);
            x+=width;
        }
        box({22,435,568,211});
        text({31,439,549,14},"PLEASE HANDLE IN ACCORDANCE WITH INSTRUCTIONS MARKED 'X'","Instructions heading",9,true);
        check(31,459,"INST_1","Deliver documents in one mailing.");
        check(31,479,"INST_2","Deliver documents in two mailings.");
        check(31,499,"INST_3","Deliver documents against payment only upon payment of draft or acceptance of time draft.");
        check(31,528,"INST_4","All charges for account of drawee.");
        check(31,548,"INST_5","Do not waive charges.");
        text({31,568,48,12},"Protest for","Instruction",8);
        check(85,566,"INST_6","NON-PAYMENT",116); check(206,566,"INST_6A","NON-ACCEPTANCE",130);
        check(31,586,"INST_7","Do not protest."); check(176,586,"INST_8","Present on arrival of goods.",160);
        text({31,608,43,12},"Advise","Instruction",8);
        check(76,606,"INST_9A","NON-PAYMENT",112); check(190,606,"INST_9B","NON-ACCEPTANCE",142);
        check(76,624,"INST_9C","BY AIRMAIL",112); check(190,624,"INST_9D","BY CABLE",142);
        text({349,459,231,14},"IN CASE OF NEED REFER TO:","Contact heading",9,true);
        text({349,477,231,73},v("F_NAME"),"Forwarder / contact",9,false,7);
        text({349,552,231,13},"WHO IS EMPOWERED BY US:","Authority heading",8,true);
        check(349,570,"TO_ACT","To act fully on our behalf, i.e. authorize reductions, extensions, free delivery, waiving of protest, etc.",231);
        check(349,609,"TO_ASSIST","To assist in obtaining acceptance or payment without changing the terms of the draft.",231);
        text({31,653,43,12},"Advise","Instruction",8);
        check(76,651,"INST_10A","PAYMENT",112); check(190,651,"INST_10B","ACCEPTANCE",142);
        check(349,651,"INST_10C","BY AIRMAIL",112); check(467,651,"INST_10D","BY FAX",113);
        box({22,675,568,95});
        text({31,679,549,13},"Other Instructions:","Other instructions label",9,true);
        const auto notes=QStringList{v("OTHER_INSTRUCTIONS"),v("DESC1"),v("DESC2"),v("VALUE1"),v("VALUE2")}.filter(QRegularExpression("\\S")).join('\n');
        text({31,695,549,35},notes,"Other instructions",9,false,6);
        text({31,734,307,12},"REFER ALL QUESTIONS CONCERNING THIS COLLECTION TO:","Questions label",7,true);
        check(31,750,"REFER","SHIPPER",112); check(147,750,"REFER2","FREIGHT FORWARDER",190);
        text({349,734,231,15},v("SIGHT_SIG2"),"Transmittal signatory name",9,true); rule(349,752,580,752);
        text({407,755,173,12},"Authorized Signature","Signature label",8);
    }
};
}
QString sightDraftAmount(const QString &input) {
    auto value=input.trimmed(); if(value.isEmpty()) return {};
    if(!QRegularExpression("^(?:[0-9]{1,12}|[0-9]{1,3}(?:,[0-9]{3}){1,3})(?:\\.[0-9]{1,2})?$").match(value).hasMatch()) throw std::runtime_error("Sight draft amount must be a nonnegative USD amount with at most two decimal places.");
    value.remove(','); auto whole=value.section('.',0,0); auto fraction=value.section('.',1,1).leftJustified(2,'0');
    while(whole.size()>1 && whole.startsWith('0')) whole.remove(0,1);
    for(int i=whole.size()-3;i>0;i-=3) whole.insert(i,',');
    return whole+'.'+fraction;
}
QString sightDraftAmountWords(const QString &input) {
    auto amount=sightDraftAmount(input); if(amount.isEmpty()) return {};
    amount.remove(','); return (words(amount.section('.',0,0).toLongLong())+" and "+amount.section('.',1,1)+"/100").toUpper();
}
void exportSightDraft(const Record &record,const QString &path) {
    if(record.source!="SIGHT.TPS") throw std::runtime_error("A sight draft record is required.");
    QSaveFile file(path); if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setResolution(72); writer.setPageSize(QPageSize(QPageSize::Letter)); writer.setPageMargins(QMarginsF(0,0,0,0));
        writer.setTitle("Sight draft - first and second exchange"); writer.setCreator("Shadow Logis");
        SightForm form(writer,record); form.page(1); if(!writer.newPage()) throw std::runtime_error("Cannot add second exchange copy."); form.page(2);
    }
    if(!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
