#include "timeline.h"
#include "eventmodel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QJsonArray>
#include <algorithm>
Timeline::Timeline(QWidget *parent):QWidget(parent) {setMinimumHeight(180);setToolTip("Click an activity bin to inspect its time window. Height is recorded event count.");}
void Timeline::setBins(const QJsonObject &data){bins=data;update();}
void Timeline::paintEvent(QPaintEvent *) {
    QPainter p(this);p.fillRect(rect(),palette().base());p.setPen(palette().text().color());
    const auto rows=bins.value("bins").toArray(); const auto from=bins.value("from_ms").toInteger();
    const auto step=bins.value("bin_ms").toInteger(1000);
    p.drawText(12,20,"Recorded events per time bin — click to navigate");
    if(rows.isEmpty()){p.drawText(12,65,"No recorded events in this selection");return;}
    qint64 max=1;int last=1;for(const auto v:rows){max=std::max(max,v.toObject().value("count").toInteger());last=std::max(last,v.toObject().value("bin").toInt()+1);}
    auto area=rect().adjusted(12,35,-12,-28);double width=double(area.width())/last;
    p.setPen(Qt::NoPen);p.setBrush(QColor(45,120,165));
    for(const auto v:rows){const auto r=v.toObject();double h=area.height()*double(r.value("count").toInteger())/max;
        p.drawRect(QRectF(area.left()+r.value("bin").toInt()*width,area.bottom()-h,std::max(1.0,width-1),h));}
    p.setPen(palette().text().color());p.drawText(12,height()-8,EventModel::timestamp(from));
    const auto end=EventModel::timestamp(from+last*step);p.drawText(rect().width()-p.fontMetrics().horizontalAdvance(end)-12,height()-8,end);
}
void Timeline::mousePressEvent(QMouseEvent *event){
    const auto rows=bins.value("bins").toArray();if(rows.isEmpty()||!navigate)return;
    int last=1;for(const auto v:rows)last=std::max(last,v.toObject().value("bin").toInt()+1);
    auto fraction=std::clamp((event->position().x()-12)/(width()-24),0.0,1.0);
    navigate(bins.value("from_ms").toInteger()+qint64(fraction*last)*bins.value("bin_ms").toInteger(1000));
}
