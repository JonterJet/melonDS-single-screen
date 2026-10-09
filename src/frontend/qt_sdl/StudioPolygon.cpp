// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioPolygon.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineF>
#include <cmath>
StudioPolygon::StudioPolygon(QWidget* parent):QWidget(parent)
{
    setObjectName("StudioPolygonCanvas"); setMinimumSize(640,480); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
}
QRect StudioPolygon::canvasRect() const
{
    QSize size(256,192); size.scale(this->size()-QSize(24,24),Qt::KeepAspectRatio);
    return {QPoint((width()-size.width())/2,(height()-size.height())/2),size};
}
QPointF StudioPolygon::nativePoint(QPointF position) const
{
    auto rect=canvasRect();
    return {qBound(0.0,(position.x()-rect.x())*256/rect.width(),256.0),qBound(0.0,(position.y()-rect.y())*192/rect.height(),192.0)};
}
bool StudioPolygon::valid() const
{
    if(points.size()<3 || points.size()>4096) return false;
    double area=0;
    for(int i=0;i<points.size();++i) { auto a=points[i],b=points[(i+1)%points.size()]; area+=a.x()*b.y()-b.x()*a.y(); }
    return std::abs(area)>1 && points.boundingRect().width()>=1 && points.boundingRect().height()>=1;
}
void StudioPolygon::undo() { if(!points.isEmpty()) points.removeLast(); closed=false; update(); }
void StudioPolygon::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.fillRect(rect(),QColor(18,20,24)); auto r=canvasRect();
    p.drawImage(r,image); p.translate(r.topLeft()); p.scale(r.width()/256.0,r.height()/192.0);
    p.setPen(QPen(Qt::yellow,0.7)); p.setBrush(QColor(255,220,0,45));
    if(closed) p.drawPolygon(points,Qt::OddEvenFill); else { p.setBrush(Qt::NoBrush); p.drawPolyline(points); }
    p.setBrush(Qt::yellow);
    for(auto point:points) p.drawEllipse(point,1.6,1.6);
}
void StudioPolygon::mousePressEvent(QMouseEvent* event)
{
    if(event->button()!=Qt::LeftButton || !canvasRect().contains(event->position().toPoint())) return;
    auto point=nativePoint(event->position());
    double radius=7.0*256/canvasRect().width();
    for(int i=0;i<points.size();++i) if(QLineF(point,points[i]).length()<radius) { dragging=i; return; }
    if(!closed && points.size()<4096) { points.append(point); update(); }
}
void StudioPolygon::mouseMoveEvent(QMouseEvent* event)
{
    if(dragging>=0) { points[dragging]=nativePoint(event->position()); update(); }
}
void StudioPolygon::mouseReleaseEvent(QMouseEvent*) { dragging=-1; }
void StudioPolygon::mouseDoubleClickEvent(QMouseEvent*) { if(valid() && confirm) confirm(); }
void StudioPolygon::keyPressEvent(QKeyEvent* event)
{
    if(event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) { if(valid() && confirm) confirm(); event->accept(); }
    else if(event->key()==Qt::Key_Backspace || (event->key()==Qt::Key_Z && event->modifiers() & Qt::ControlModifier)) { undo(); event->accept(); }
    else QWidget::keyPressEvent(event);
}
