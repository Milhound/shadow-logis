#include "awbform.h"
#include <QPainter>
#include <QTextLayout>
#include <stdexcept>

namespace shadowlogis {
void drawAirWaybillForm(QPainter &p,const QString &bodyFamily,const QString &headingFamily) {
    p.save(); p.setPen(QPen(Qt::black,.45)); p.setBrush(Qt::NoBrush);
    const auto line=[&](double x1,double y1,double x2,double y2) { p.drawLine(QPointF(x1,y1),QPointF(x2,y2)); };
    const auto label=[&](QRectF box,const QString &text,double size=6.5,bool bold=false,bool center=false) {
        QFont font(bold ? headingFamily : bodyFamily); font.setPointSizeF(size); font.setBold(bold);
        QTextLayout layout(text,font,p.device()); QTextOption option; option.setWrapMode(QTextOption::WordWrap);
        option.setAlignment(center ? Qt::AlignHCenter : Qt::AlignLeft); layout.setTextOption(option);
        layout.beginLayout(); double height=0;
        for(;;) { auto row=layout.createLine(); if(!row.isValid()) break; row.setLineWidth(box.width()); row.setPosition({0,height}); height+=row.height(); }
        layout.endLayout();
        if(height>box.height()+.1) throw std::runtime_error("AWB fixed label exceeds its box: "+text.toStdString());
        layout.draw(&p,box.topLeft());
    };
    // Geometry uses points on Letter stock. Matches the standard face sections
    // in the user's blank stationery reference; no legacy PDF data is embedded.
    p.drawRect(QRectF(38,34,528,714)); line(301,34,301,255);
    line(38,94,566,94); line(38,152,566,152); line(38,233,566,233);
    line(38,210,301,210); line(201,34,201,56); line(201,94,201,116);
    line(201,210,201,233); line(38,255,566,255);
    line(301,84,566,84); line(385,233,385,255);
    label({41,36,155,8},"Shipper's Name and Address"); label({204,36,94,8},"Shipper's Account Number");
    label({41,96,155,8},"Consignee's Name and Address"); label({204,96,94,8},"Consignee's Account Number");
    label({305,37,60,8},"Not Negotiable"); label({305,51,60,14},"Air Waybill",9,true);
    label({305,68,55,8},"Issued by");
    label({305,86,258,8},"Copies 1, 2 and 3 of this Air Waybill are originals and have the same validity.",6);
    label({305,97,256,53},
        "It is agreed that the goods described herein are accepted in apparent good order and condition "
        "(except as noted) for carriage SUBJECT TO THE CONDITIONS OF CONTRACT ON THE REVERSE HEREOF. "
        "ALL GOODS MAY BE CARRIED BY ANY OTHER MEANS INCLUDING ROAD OR ANY OTHER CARRIER UNLESS SPECIFIC "
        "CONTRARY INSTRUCTIONS ARE GIVEN HEREON BY THE SHIPPER, AND SHIPPER AGREES THAT THE SHIPMENT MAY "
        "BE CARRIED VIA INTERMEDIATE STOPPING PLACES WHICH THE CARRIER DEEMS APPROPRIATE. THE SHIPPER'S "
        "ATTENTION IS DRAWN TO THE NOTICE CONCERNING CARRIER'S LIMITATION OF LIABILITY. Shipper may increase "
        "such limitation of liability by declaring a higher value for carriage and paying a supplemental charge if required.",5.1);
    label({41,154,255,8},"Issuing Carrier's Agent Name and City"); label({305,154,256,8},"Accounting Information");
    label({41,212,156,8},"Agent's IATA Code"); label({204,212,93,8},"Account No.");
    label({41,235,257,8},"Airport of Departure (Addr. of First Carrier) and Requested Routing");
    label({305,235,77,8},"Reference Number"); label({389,235,171,8},"Optional Shipping Information",6.5,false,true);
    // Routing and declared values.
    line(38,286,566,286); line(38,309,566,309);
    for(double x : {63,194,221,245,271,299,337,365,401,438,498}) line(x,255,x,286);
    for(double x : {383,419}) line(x,265,x,286);
    label({41,257,20,8},"To"); label({66,257,126,8},"By First Carrier");
    label({117,257,74,8},"Routing and Destination",5.3,false,true);
    for(const auto &entry : {qMakePair(197.,QString("to")),{224.,"by"},{248.,"to"},{274.,"by"}}) label({entry.first,257,20,8},entry.second);
    label({301,257,34,8},"Currency",6,false,true); label({339,257,24,16},"CHGS Code",6,false,true);
    label({366,257,34,8},"WT/VAL",6,false,true); label({402,257,35,8},"OTHER",6,false,true);
    for(double x : {366,402}) { label({x,266,17,8},"PPD",6,false,true); label({x+18,266,17,8},"COLL",6,false,true); }
    label({440,257,56,17},"Declared Value for Carriage",6,false,true); label({500,257,63,17},"Declared Value for Customs",6,false,true);
    for(double x : {141,224,300,386}) line(x,286,x,309);
    label({41,288,98,8},"Airport of Destination"); label({144,288,153,8},"Requested Flight / Date",6.5,false,true);
    label({303,288,80,8},"Amount of Insurance",6,false,true);
    label({389,288,174,20},"INSURANCE - If carrier offers insurance, and such insurance is requested in accordance with the conditions thereof, indicate amount to be insured in figures in box marked 'Amount of Insurance'.",5.3);
    label({41,311,446,8},"Handling Information");
    line(38,362,566,362); line(491,339,491,362); line(491,339,566,339);
    label({41,347,230,15},"These commodities, technology or software were exported from the United States in accordance with the Export Administration Regulations. Ultimate destination",5.5);
    label({420,344,69,17},"Diversion contrary to U.S. law prohibited.",6,false,true); label({495,341,67,8},"SCI",6,false,true);
    // Cargo columns. Header rows remain above the data area at y=390.
    for(double x : {75,116,134,197,249,349,403}) line(x,362,x,528);
    line(149,372,149,528);
    line(38,388,566,388); line(38,528,566,528);
    label({40,364,33,23},"No. of Pieces RCP",6.2,false,true); label({78,364,35,22},"Gross Weight",6.2,false,true);
    label({118,364,14,22},"kg lb",6,false,true); label({135,364,60,8},"Rate Class",6,false,true);
    label({151,373,44,14},"Commodity Item No.",5.8,false,true);
    label({199,364,48,22},"Chargeable Weight",6.2,false,true); label({252,364,95,22},"Rate / Charge",6.5,false,true);
    label({351,364,50,22},"Total",6.5,false,true); label({406,364,156,22},"Nature and Quantity of Goods (incl. Dimensions or Volume)",6.5,false,true);
    // Charges and execution. The conventional charge headings have angled ends.
    line(260,528,260,748);
    for(double y : {528,553,578,603,628}) line(149,y+9,149,y+25);
    line(149,653,149,748);
    const auto chargeHeading=[&](double y,const QString &text) {
        line(38,y,260,y); line(94,y,103,y+9); line(103,y+9,203,y+9); line(203,y+9,212,y);
        label({103,y+1,100,8},text,6,false,true);
    };
    chargeHeading(528,"Weight Charge"); label({40,530,52,8},"Prepaid",6.5,false,true); label({213,530,45,8},"Collect",6.5,false,true);
    chargeHeading(553,"Valuation Charge"); chargeHeading(578,"Tax");
    chargeHeading(603,"Total Other Charges Due Agent"); chargeHeading(628,"Total Other Charges Due Carrier");
    line(38,653,260,653); line(38,678,566,678); line(38,703,260,703); line(38,728,566,728);
    label({40,680,107,8},"Total Prepaid",6.5,false,true); label({151,680,107,8},"Total Collect",6.5,false,true);
    label({40,705,107,8},"Currency Conversion Rates",6,false,true); label({151,705,107,8},"CC Charges in Dest. Currency",6,false,true);
    label({40,731,106,15},"For Carrier's Use Only at Destination",6,false,true); label({151,731,107,8},"Charges at Destination",6,false,true);
    label({264,731,107,8},"Total Collect Charges",6,false,true);
    label({264,530,297,8},"Other Charges"); line(260,603,566,603);
    label({264,606,298,20},"Shipper certifies that the particulars on the face hereof are correct and that insofar as any part of the consignment contains dangerous goods, such part is properly described by name and is in proper condition for carriage by air according to the applicable Dangerous Goods Regulations.",5);
    p.setPen(QPen(Qt::black,.4,Qt::DashLine)); line(271,666,559,666); line(271,720,559,720); p.setPen(QPen(Qt::black,.45));
    label({271,668,288,8},"Signature of Shipper or his Agent",6,false,true);
    label({264,721,66,7},"Executed on (date)",5.8,false,true); label({332,721,105,7},"at (place)",5.8,false,true);
    label({439,721,122,7},"Signature of Issuing Carrier or its Agent",5.8,false,true);
    p.restore();
}
}
