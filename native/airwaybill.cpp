#include "privatefile.h"
#include "documents.h"
#include "domain.h"
#include "entrylogic.h"
#include "awbform.h"
#include <QFontDatabase>
#include <QFile>
#include <QJsonArray>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextLayout>
#include <cmath>
#include <stdexcept>

namespace shadowlogis {
AirWaybillContext airWaybillContext(const Store &store, const Record &record) {
    AirWaybillContext result;
    for (const auto &customer : store.records({"CUSTOMER.TPS"})) {
        const auto number = valueText(customer.values["CUS:NUMBER"]);
        if (number == valueText(record.values["AWB:CARRIER_ACCOUNT"]) && number != "0")
            result.issuer = valueText(customer.values["CUS:NAME"]);
        if (number == valueText(record.values["AWB:FORWARDER_NUMBER"]) && number != "0") {
            for (const auto &key : {"NAME","ADDRESS","ADDRESS2","SUITE"}) {
                const auto text = valueText(customer.values["CUS:"+QString(key)]).trimmed();
                if (!text.isEmpty()) result.agentAddress << text;
            }
            const auto city = (valueText(customer.values["CUS:CITY_STATE"]) + " " + valueText(customer.values["CUS:ZIP"])).trimmed();
            if (!city.isEmpty()) result.agentAddress << city;
            result.agentIata = valueText(customer.values["CUS:IATA"]);
        }
    }
    int matchingBookings = 0;
    const auto reference = recordReference(record).trimmed().toUpper();
    const auto explicitBooking = valueText(record.values["NATIVE:BOOKING_ID"]);
    for (const auto &booking : store.records({"BOOKING.TPS"})) {
        if ((!explicitBooking.isEmpty() && explicitBooking == QString::number(booking.id)) ||
            (explicitBooking.isEmpty() && !reference.isEmpty() && recordReference(booking).trimmed().toUpper() == reference)) {
            ++matchingBookings; result.purchaseOrder = valueText(booking.values["BOO:PO_NUMBER"]);
        }
    }
    if (matchingBookings != 1) result.purchaseOrder.clear();
    const auto profile=record.values["NATIVE:COMPANY_PROFILE"].toObject();
    if(!profile.isEmpty()) { result.agentAddress=companyAddress(profile); result.agentIata=profile["iata"].toString(); }
    result.destination=airDestinationLabel(record,store.records({"AIRPORTS.TPS"}));
    return result;
}

namespace {
QString normalizedLines(QString value) {
    // TPS memos use both CRLF and bare CR. QTextLayout needs one explicit
    // separator for each stored line, independent of the host platform.
    return value.replace("\r\n","\n").replace('\r','\n');
}
class AirWaybillPainter {
public:
    explicit AirWaybillPainter(QPdfWriter &writer,const AirWaybillPrintOptions &options) : painter(&writer), options(options) {
        if (!painter.isActive()) throw std::runtime_error("Could not create the air waybill.");
        const auto fonts = QFontDatabase::families();
        for (const auto &candidate : {"Times New Roman", "Times", "Liberation Serif", "DejaVu Serif"})
            if (fonts.contains(candidate)) { family = candidate; break; }
        for (const auto &candidate : {"Arial", "Liberation Sans", "DejaVu Sans"})
            if (fonts.contains(candidate)) { headerFamily = candidate; break; }
        if (options.preprintedContinuous) {
            for (const auto &candidate : {"Courier New","Courier","Liberation Mono","DejaVu Sans Mono"})
                if (fonts.contains(candidate)) { family=candidate; break; }
            headerFamily=family;
        }
    }
    void draw(const Record &record, const AirWaybillContext &context) {
        { // One form per job; the next colored copy is a separate print action.
            painter.save();
            if (options.preprintedContinuous) {
                painter.translate(options.horizontalOffsetMm*72/25.4,options.verticalOffsetMm*72/25.4);
                painter.scale(options.horizontalScalePercent/100,options.verticalScalePercent/100);
            }
            contents(record,context); painter.restore();
        }
        painter.end();
    }
    void contents(const Record &record,const AirWaybillContext &context) {
        if (!options.preprintedContinuous) drawAirWaybillForm(painter,family,headerFamily);
        const auto value = [&](const QString &key) { return fieldText("AWB:"+key,record.values["AWB:"+key]).trimmed(); };
        const auto items = [&](const QString &key) { return record.values["AWB:"+key].toArray(); };
        const auto at = [&](const QString &key, int row) { return valueText(row < items(key).size() ? items(key)[row] : QJsonValue()).trimmed(); };
        const auto field = [&](const QString &key, QRectF rectangle, double size=9.5, Qt::Alignment alignment=Qt::AlignLeft, double minimum=6.5) {
            text(rectangle,value(key),key,size,minimum,alignment);
        };
        const auto amount = [&](const QString &key, QRectF rectangle) {
            if (value(key).toDouble()!=0) text(rectangle,value(key),key,9.5,6.5,Qt::AlignRight);
        };
        QString leftNumber = value("AWB_NUMBER");
        const auto number = QRegularExpression("^(\\d{3})[- ]?(\\d{4})[- ]?(\\d{4})$").match(leftNumber);
        if (number.hasMatch()) leftNumber = number.captured(1)+" "+value("AIRPORT_FROM")+" "+number.captured(2)+"-"+number.captured(3);
        text({40,17,258,17},leftNumber,"Air waybill number",16,8,Qt::AlignLeft,true);
        text({440,17,123,17},value("AWB_NUMBER"),"Air waybill number",16,8,Qt::AlignRight,true);
        if (!options.preprintedContinuous) text({440,731,123,17},value("AWB_NUMBER"),"Air waybill number",16,8,Qt::AlignRight,true);
        text({367,37,194,33},context.issuer,"Issuing carrier",10,6.5);
        party(items("SHIPPER_ARRAY"),45,56,"Shipper");
        party(items("CONSIGNEE_ARRAY"),104,115,"Consignee");
        field("SHIPPER_ACCOUNT",{202,45,95,11}); field("CONSIGNEE_ACCOUNT",{202,104,95,11});
        text({40,163,256,44},context.agentAddress.join('\n'),"Issuing agent address",9.5,6.5);
        text({40,221,159,11},context.agentIata,"Agent IATA code",9.5,6.5);
        field("FORWARDER_ACCOUNT",{203,221,93,11});
        field("AWB_ACCOUNTING_INFO",{305,163,257,55},9,Qt::AlignLeft,6);
        if (!value("MAWB_NUMBER").isEmpty()) text({305,221,257,11},"Master AWB  "+value("MAWB_NUMBER"),"Master air waybill",9.5,6.5);
        auto origin = value("AIRPORT_OF_ORIGIN");
        const auto airportCode = value("AIRPORT_FROM");
        if (!airportCode.isEmpty() && !QRegularExpression("(?:^|\\W)"+QRegularExpression::escape(airportCode)+"(?:$|\\W)",QRegularExpression::CaseInsensitiveOption).match(origin).hasMatch())
            origin = (airportCode+" "+origin).trimmed();
        text({40,244,257,11},origin,"Departure airport",9.5,6.5);
        text({305,244,79,11},recordReference(record),"Shipment reference",9.5,6.5);
        text({388,244,174,11},context.purchaseOrder,"Purchase order",9.5,6.5,Qt::AlignCenter);
        field("AIRPORT_TO_1",{40,275,22,11}); field("FIRST_CARRIER",{65,275,126,11});
        field("AIRPORT_TO_2",{196,275,24,11}); field("SECOND_CARRIER",{223,275,19,11});
        field("AIRPORT_TO_3",{247,275,22,11}); field("THIRD_CARRIER",{273,275,23,11});
        field("AWB_CURRENCY",{301,275,34,11},9.5,Qt::AlignCenter);
        field("CHARGES_CODE",{338,275,26,11},9.5,Qt::AlignCenter);
        for (const auto &pair : {qMakePair(QString("WEIGHT_VALUE_PPD_COL"),366.0),qMakePair(QString("OTHER_PPD_COL"),402.0)}) {
            const auto payment = value(pair.first).toUpper();
            if (!payment.isEmpty() && payment!="P" && payment!="C") throw std::runtime_error((pair.first+": choose P (prepaid) or C (collect).").toStdString());
            if (!payment.isEmpty()) text({pair.second+(payment=="C" ? 18 : 0),275,17,11},"X",pair.first,10,options.preprintedContinuous ? 6.5 : 8,Qt::AlignCenter,true);
        }
        field("DECLARED_VALUE_CARRIAGE",{440,275,56,11},9.5,Qt::AlignCenter);
        field("DECLARED_VALUE_CUSTOMS",{500,275,62,11},9.5,Qt::AlignCenter);
        const QString destinationLabel="Airport of destination";
        const auto destinationBox=formBox({41,296,98,12},destinationLabel);
        if (context.destination.isEmpty() || !fittedText(destinationBox,context.destination,destinationLabel,9.5,6.5,Qt::AlignLeft|Qt::AlignVCenter)) {
            if (!fittedText(destinationBox,value("AIRPORT_OF_DESTINATION"),destinationLabel,9.5,6.5,Qt::AlignLeft|Qt::AlignVCenter)) textOverflow(destinationLabel);
        }
        field("FLIGHT_DATE1",{144,297,79,11},9.5,Qt::AlignCenter); field("FLIGHT_DATE2",{227,297,70,11},9.5,Qt::AlignCenter);
        field("AMOUNT_OF_INSURANCE",{303,297,80,11},9.5,Qt::AlignCenter);
        field("HANDLING_INFORMATION",{41,319,446,26},9,Qt::AlignLeft,6);
        field("ULTIMATE_DESTINATION",{276,349,141,11},9.5,Qt::AlignCenter);

        const auto totals = airCargoTotals(record.values);
        // Fixed legacy geometry: never repaginate, resize the form, or include slot six.
        for (int row=0; row<AirCargoRows; ++row) {
            if (at("NUMBER_OF_PIECES",row).toDouble()==0 && at("GROSS_WEIGHT",row).toDouble()==0 && at("CHARGEABLE_WEIGHT",row).toDouble()==0) continue;
            const double y=390+row*14;
            text({40,y,33,13},at("NUMBER_OF_PIECES",row),"Cargo pieces",9.5,6.5,Qt::AlignRight);
            text({77,y,37,13},weightText(at("GROSS_WEIGHT",row)),"Cargo gross weight",9.5,6.5,Qt::AlignRight);
            text({117,y,16,13},at("KG_LB",row),"Cargo unit",9.5,6.5,Qt::AlignCenter);
            text({136,y,11,13},at("RATE_CLASS",row),"Rate class",9,6);
            text({150,y,45,13},at("COMMODITY_ITEM_NO",row),"Commodity item number",9,6);
            text({198,y,49,13},weightText(at("CHARGEABLE_WEIGHT",row)),"Chargeable weight",9.5,6.5,Qt::AlignRight);
            const bool agreed=value("PRINT_AS_AGREED")=="1";
            const bool minimum=row==0 && at("RATE_CHARGE",row).toDouble()==0 && value("MINIMUM_CHARGE").toDouble()!=0;
            const auto rate=agreed ? QString("AS AGREED") : minimum ? QString("MINIMUM") : at("RATE_CHARGE",row).toDouble()!=0 ? at("RATE_CHARGE",row) : QString();
            text({251,y,96,13},rate,"Rate / charge",9.5,6.5,Qt::AlignCenter);
            if (!agreed && at("LINE_TOTAL",row).toDouble()!=0) text({351,y,50,13},at("LINE_TOTAL",row),"Line total",9.5,6.5,Qt::AlignRight);
        }
        // Imported totals remain authoritative; inconsistent cargo must be reviewed before printing.
        if ((!value("PIECES_TOTAL").isEmpty() && value("PIECES_TOTAL").toLongLong()!=totals.pieces.toLongLong()) ||
            (!value("GROSS_WEIGHT_TOTAL").isEmpty() && value("GROSS_WEIGHT_TOTAL").toDouble()!=totals.grossWeight.toDouble()))
            throw std::runtime_error("Stored AWB totals do not match the five cargo rows. Review cargo before printing the form.");
        text({40,516,33,12},totals.pieces,"Total pieces",9.5,6.5,Qt::AlignRight);
        text({77,516,37,12},weightText(totals.grossWeight),"Total gross weight",9.5,6.5,Qt::AlignRight);
        if (value("PRINT_AS_AGREED")!="1") amount("CHARGE_TOTAL",{351,516,50,12});

        QList<int> dimensions;
        const auto dimensionRule=record.values.value("NATIVE:DIMENSION_CALCULATION").toObject();
        const bool cbmMode=dimensionRule["enabled"].toBool() && dimensionRule["basis"]=="kg/cbm";
        DimensionResult cbm; if(cbmMode) cbm=calculateDimensions(record);
        const bool showCbm=cbmMode && cbm.cubicMeters.toDouble()>0;
        for (int row=0;row<5 && !cbmMode;++row) if (at("DESCRIPTION_QUANTITY",row).toDouble()!=0 || at("DESCRIPTION_LENGTH",row).toDouble()!=0 ||
            at("DESCRIPTION_WIDTH",row).toDouble()!=0 || at("DESCRIPTION_HEIGHT",row).toDouble()!=0) dimensions << row;
        const auto goods = normalizedLines(value("NATURE_OF_GOODS") + (value("EXTENDED_DESCRIPTION").isEmpty() ? QString() : "\n"+value("EXTENDED_DESCRIPTION"))).trimmed();
        const QString goodsLabel="Nature and quantity of goods / additional description";
        // Leave a small vertical inset for font ascent/bounding-box differences
        // across Times, Liberation and Courier on the supported platforms.
        const auto goodsBox=formBox({405,390,157,134},goodsLabel).adjusted(0,1,0,-1);
        const bool dimensionalWeight=!dimensions.isEmpty() && value("DIMENSIONED_WEIGHT").toDouble()!=0;
        // Dimensions share the goods panel, but only reserve their actual rows.
        // Work in this form's physical points: continuous stock has a taller
        // goods panel than the archived Letter form, with the same font sizes.
        const double rowHeight=10, headingHeight=10, weightHeight=12, gap=3;
        const double dimensionsHeight=showCbm ? weightHeight*2 : dimensions.isEmpty() ? 0 : headingHeight+dimensions.size()*rowHeight+(dimensionalWeight ? weightHeight : 0);
        const double dimensionsTop=goodsBox.bottom()-dimensionsHeight;
        const QRectF descriptionBox(goodsBox.topLeft(),QSizeF(goodsBox.width(),goodsBox.height()-dimensionsHeight-(dimensionsHeight>0 ? gap : 0)));
        if (!fittedText(descriptionBox,goods,goodsLabel,9.5,6.5)) {
            // Old fixed-width fields contain spacer rows. If necessary, omit
            // empty rows on paper only; never truncate words or change storage.
            QStringList compact;
            for (const auto &line : goods.split('\n')) if (!line.trimmed().isEmpty()) compact << line;
            if (!fittedText(descriptionBox,compact.join('\n'),goodsLabel,9.5,6.5)) textOverflow(goodsLabel);
        }
        if (!dimensions.isEmpty()) {
            const auto unit=value("DIMS_STD_METRIC");
            if (unit!="S" && unit!="M") throw std::runtime_error("Review the stored dimension unit (S or M) before printing dimensions.");
            const auto panelText=[&](QRectF box,const QString &content,const QString &label,double size,double minimum,Qt::Alignment alignment=Qt::AlignLeft) {
                if (!fittedText(box,content,label,size,minimum,alignment)) textOverflow(label);
            };
            const auto suffix=unit=="S" ? QString("(in)") : QString("(cm)");
            const QStringList headings{"PCS","L"+suffix,"W"+suffix,"H"+suffix,"CFT"};
            const QList<double> xs{0,25,50,75,101}, widths{23,23,23,23,56};
            const auto cell=[&](int col,double y,double height) {
                return QRectF(goodsBox.x()+xs[col]*goodsBox.width()/157,y,widths[col]*goodsBox.width()/157,height);
            };
            for (int col=0;col<5;++col) panelText(cell(col,dimensionsTop,headingHeight),headings[col],"Dimension headings",7.5,6.5,Qt::AlignRight);
            int line=0;
            for (int row : dimensions) {
                const double y=dimensionsTop+headingHeight+line++*rowHeight;
                const QStringList keys{"DESCRIPTION_QUANTITY","DESCRIPTION_LENGTH","DESCRIPTION_WIDTH","DESCRIPTION_HEIGHT","DESCRIPTION_CFT"};
                for (int col=0;col<5;++col) panelText(cell(col,y,rowHeight),at(keys[col],row),keys[col],8,6,Qt::AlignRight);
            }
            if (dimensionalWeight)
                panelText({goodsBox.x(),goodsBox.bottom()-weightHeight,goodsBox.width(),weightHeight},"Dimensional Weight  "+weightText(value("DIMENSIONED_WEIGHT")),"Dimensional weight",8,6.5,Qt::AlignRight);
        }
        if(showCbm) {
            if(!fittedText({goodsBox.x(),dimensionsTop,goodsBox.width(),weightHeight},"Volume  "+cbm.cubicMeters+" m³","Volume",8,6.5,Qt::AlignRight)) textOverflow("Volume");
            if(!fittedText({goodsBox.x(),dimensionsTop+weightHeight,goodsBox.width(),weightHeight},"Dimensional weight  "+weightText(cbm.dimensionalWeight)+' '+cbm.unit,"Dimensional weight",8,6.5,Qt::AlignRight)) textOverflow("Dimensional weight");
        }
        const QList<QPair<QString,double>> charges{{"WEIGHT_CHARGE",539},{"VALUATION_CHARGE",564},{"TAX",589},
            {"DUE_AGENT",614},{"DUE_CARRIER",639},{"BLANK",663},{"TOTAL",688}};
        for (const auto &[key,y] : charges) { amount(key+"_PPD",{40,y,107,14}); amount(key+"_COL",{151,y,107,14}); }
        amount("CURRENCY_CONVERSION_RATE",{40,713,107,12}); field("CURRENCY_CONVERSION_CHG",{151,713,107,12},9.5,Qt::AlignRight);
        QStringList chargeNames, chargeAmounts;
        for (int row=0;row<12;++row) if (!at("OTHER_CHARGE_DESCRIPTIONS",row).isEmpty() || at("OTHER_CHARGE_AMOUNTS",row).toDouble()!=0) {
            chargeNames << (at("OTHER_CHARGE_DESCRIPTIONS",row).isEmpty() ? at("OTHER_CHARGE_CODE",row) : at("OTHER_CHARGE_DESCRIPTIONS",row));
            chargeAmounts << at("OTHER_CHARGE_AMOUNTS",row);
        }
        // Measure in the selected form's physical coordinates. Mapping a row
        // height measured on the laser form shrinks it on continuous stock;
        // a smaller point size can still have the same rounded line height.
        // Include one physical point of leading for legible multiline charges.
        const auto descriptionPanel=formBox({264,539,223,61},"Other charge description");
        const auto amountPanel=formBox({490,539,72,61},"Other charge amount");
        double chargeSize=9.5;
        QList<double> chargeHeights;
        for (;chargeSize>=6;chargeSize-=.25) {
            chargeHeights.clear(); double used=0;
            QFont font(family); font.setPointSizeF(chargeSize); font.setBold(false);
            const auto heightFor=[&](const QString &value,double width,Qt::Alignment alignment) {
                QTextLayout layout(normalizedLines(value).replace('\n',QChar::LineSeparator),font,painter.device());
                QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); option.setAlignment(alignment);
                layout.setTextOption(option); layout.beginLayout(); double height=0;
                while (true) { auto line=layout.createLine(); if (!line.isValid()) break; line.setLineWidth(width); height+=line.height()+1; }
                layout.endLayout(); return height;
            };
            for (int row=0;row<chargeNames.size();++row) {
                const double height=std::max(heightFor(chargeNames[row],descriptionPanel.width(),Qt::AlignLeft),
                                             heightFor(chargeAmounts[row],amountPanel.width(),Qt::AlignRight));
                chargeHeights << height; used+=height;
            }
            if (used<=std::min(descriptionPanel.height(),amountPanel.height())) break;
        }
        if (chargeSize<6) textOverflow("Other charges");
        double chargeOffset=0;
        for (int row=0;row<chargeNames.size();++row) {
            const QRectF descriptionBox(descriptionPanel.x(),descriptionPanel.y()+chargeOffset,descriptionPanel.width(),chargeHeights[row]);
            const QRectF amountBox(amountPanel.x(),amountPanel.y()+chargeOffset,amountPanel.width(),chargeHeights[row]);
            if (!fittedText(descriptionBox,chargeNames[row],"Other charge description",chargeSize,chargeSize,Qt::AlignLeft,false,1)) textOverflow("Other charge description");
            if (!fittedText(amountBox,chargeAmounts[row],"Other charge amount",chargeSize,chargeSize,Qt::AlignRight,false,1)) textOverflow("Other charge amount");
            chargeOffset+=chargeHeights[row];
        }
        QStringList shipperSignature, carrierSignature;
        for (int i=1;i<=3;++i) { shipperSignature << value("SHIPPER_AGENT_"+QString::number(i)); carrierSignature << value("AGENT_SIGNATURE_"+QString::number(i)); }
        text({264,627,298,29},shipperSignature.join('\n').trimmed(),"Stored shipper/agent signature text",9.5,6.5,Qt::AlignCenter);
        text({264,680,298,24},carrierSignature.join('\n').trimmed(),"Stored issuing-agent signature text",9.5,6.5,Qt::AlignRight);
        field("EXECUTION_DATE",{264,705,66,11},9,Qt::AlignCenter); field("EXECUTION_PLACE",{332,705,105,11},9,Qt::AlignCenter);
        if (!options.preprintedContinuous) text({39,764,526,12},"DRAFT - Air waybill | "+recordReference(record)+" | Sequence "+value("ID_SEQUENCE"),"Review footer",7,6.5);
    }
private:
    QPainter painter;
    AirWaybillPrintOptions options;
    QString family="serif";
    QString headerFamily="sans-serif";
    static double interpolate(double value,const QList<double> &from,const QList<double> &to) {
        int i=0; while (i+2<from.size() && value>from[i+1]) ++i;
        return to[i]+(value-from[i])*(to[i+1]-to[i])/(from[i+1]-from[i]);
    }
    QRectF continuousBox(QRectF r,const QString &label) const {
        // Registration profile measured from the supplied 9.5 x 24 inch stock.
        // Print only one 12-inch form per job. It is a different form geometry
        // from the archived laser AWB; do not scale the laser page wholesale.
        if (label=="Issuing carrier") return {359,151,265,14};
        if (label=="Airport of destination") return {94,377,125,12};
        if (label=="Handling information" || label=="HANDLING_INFORMATION") return {94,400,530,29};
        if (label=="Stored shipper/agent signature text") return {296,735,327,27};
        if (label=="Stored issuing-agent signature text") return {514,785,110,27};
        if (label=="EXECUTION_DATE") return {296,800,92,12};
        if (label=="EXECUTION_PLACE") return {400,800,111,12};
        if (r.y()==17) return {r.x()<400 ? 92.0 : 469.0,85,r.x()<400 ? 256.0 : 155.0,17};
        const auto y=[&](double v) { return interpolate(v,{34,94,151,211,234,256,287,309,362,390,529,729,751},
            {103,175,247,295,319,343,367,391,439,463,631,823,847}); };
        QList<double> from{39,202,300,565},to{92,221,351,626};
        if (r.y()>=390 && r.y()<529) {
            from={39,75,116,134,148,197,250,349,403,565}; to={92,121,171,179,193,250,301,373,459,626};
        } else if (r.y()>=529) { from={39,151,261,565}; to={92,193,294,626}; }
        else if (r.y()>=275 && r.y()<287) {
            from={39,65,196,223,247,273,300,338,366,402,440,500,565};
            to={92,121,250,280,301,331,351,379,395,424,454,540,626};
        } else if (r.y()>=297 && r.y()<309) { from={39,141,300,386,565}; to={92,221,351,454,626}; }
        const auto x=[&](double v) { return interpolate(v,from,to); };
        if (r.y()>=390 && r.y()<529 && r.x()>=405)
            return {468+(r.x()-405)*155/157,y(r.y()),r.width()*155/157,y(r.bottom())-y(r.y())};
        if (label=="Rate class") return {180,y(r.y()),6,y(r.bottom())-y(r.y())};
        if (label=="Commodity item number") return {194,y(r.y()),48,y(r.bottom())-y(r.y())};
        return {x(r.x()),y(r.y()),x(r.right())-x(r.x()),y(r.bottom())-y(r.y())};
    }
    void party(const QJsonArray &lines, double firstY, double restY, const QString &label) {
        QStringList remaining;
        for (int i=1;i<lines.size();++i) remaining << valueText(lines[i]);
        text({40,firstY,158,11},valueText(lines.isEmpty() ? QJsonValue() : lines[0]),label+" name",9.5,6);
        text({40,restY,257,38},remaining.join('\n').trimmed(),label+" address",9.5,5.5);
    }
    void text(QRectF rectangle, const QString &value, const QString &label, double size, double minimum,
              Qt::Alignment alignment=Qt::AlignLeft, bool bold=false) {
        if (!fittedText(formBox(rectangle,label),value,label,size,minimum,alignment,bold)) textOverflow(label);
    }
    QRectF formBox(QRectF rectangle,const QString &label) const {
        return options.preprintedContinuous ? continuousBox(rectangle,label) : rectangle;
    }
    [[noreturn]] void textOverflow(const QString &label) const {
        throw std::runtime_error((label+(options.preprintedContinuous ? " does not fit its preprinted AWB box. " : " does not fit its standard AWB box. ")+"Shorten that field or use an approved attachment; no text was clipped and no PDF was saved.").toStdString());
    }
    bool fittedText(QRectF rectangle, const QString &value, const QString &label, double size, double minimum,
                    Qt::Alignment alignment=Qt::AlignLeft, bool bold=false, double lineGap=0) {
        if (value.trimmed().isEmpty()) return true;
        if (options.preprintedContinuous) {
            const auto physical=painter.worldTransform().mapRect(rectangle);
            if (physical.left()<0 || physical.top()<0 || physical.right()>684 || physical.bottom()>864)
                throw std::runtime_error((label+": print registration moves text outside the 9.5 x 12 inch form. Reduce the offset or scale.").toStdString());
        }
        for (double points=size;points>=minimum-.01;points-=.25) {
            QFont font(bold && size>=15 ? headerFamily : family); font.setPointSizeF(points); font.setBold(bold);
            QTextLayout layout(normalizedLines(value).replace('\n',QChar::LineSeparator),font,painter.device());
            QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); option.setAlignment(alignment);
            layout.setTextOption(option); layout.beginLayout(); double height=0;
            while (true) {
                auto line=layout.createLine(); if (!line.isValid()) break;
                line.setLineWidth(rectangle.width()); line.setPosition({0,height}); height+=line.height()+lineGap;
            }
            layout.endLayout();
            if (height<=rectangle.height()+.01) {
                painter.save(); painter.setPen(Qt::black); painter.setClipRect(rectangle);
                const double offset=alignment.testFlag(Qt::AlignVCenter) ? (rectangle.height()-height)/2 : 0;
                layout.draw(&painter,rectangle.topLeft()+QPointF(0,offset)); painter.restore(); return true;
            }
        }
        return false;
    }
};
}

void exportAirWaybill(const Record &record, const QString &path, const AirWaybillContext &context,const AirWaybillPrintOptions &options) {
    if (record.source!="AWB.TPS") throw std::runtime_error("An air waybill is required.");
    if (!std::isfinite(options.horizontalOffsetMm) || !std::isfinite(options.verticalOffsetMm) ||
        !std::isfinite(options.horizontalScalePercent) || !std::isfinite(options.verticalScalePercent) ||
        std::abs(options.horizontalOffsetMm)>25 || std::abs(options.verticalOffsetMm)>25 ||
        options.horizontalScalePercent<95 || options.horizontalScalePercent>105 || options.verticalScalePercent<95 || options.verticalScalePercent>105)
        throw std::runtime_error("Registration allows offsets within 25 mm and scale from 95 to 105 percent.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw std::runtime_error(file.errorString().toStdString());
    makeFilePrivate(file);
    {
        QPdfWriter writer(&file); writer.setTitle("Air waybill - standard form (draft)"); writer.setCreator("Shadow Logis");
        writer.setResolution(72);
        writer.setPageSize(options.preprintedContinuous ? QPageSize(QSizeF(9.5,12),QPageSize::Inch,"AWB 9.5 x 12 single form",QPageSize::ExactMatch) : QPageSize(QPageSize::Letter));
        writer.setPageMargins(QMarginsF(0,0,0,0));
        if (options.preprintedContinuous) writer.setTitle("Air waybill - text only - 9.5 x 12 inch preprinted form");
        AirWaybillPainter form(writer,options); form.draw(record,context);
    }
    if (!file.commit()) throw std::runtime_error(file.errorString().toStdString());
}
}
