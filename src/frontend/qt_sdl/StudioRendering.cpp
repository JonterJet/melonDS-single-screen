// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioRendering.h"
#include <QPainterPath>
std::array<float, 30> studioOverlayVertices(const StudioElement& e)
{
    float l = e.destination.x(), t = e.destination.y();
    float r = l + e.destination.width(), b = t + e.destination.height();
    float u = float(e.x)/256, v = float(e.y)/192;
    float ur = float(e.x + e.width)/256, vb = float(e.y + e.height)/192, layer = float(e.screen);
    return {l,t,u,v,layer, l,b,u,vb,layer, r,b,ur,vb,layer,
            l,t,u,v,layer, r,b,ur,vb,layer, r,t,ur,v,layer};
}
void paintStudioOverlays(QPainter& painter, const QImage (&screens)[2], const QVector<StudioElement>& elements)
{
    for (const auto& e : elements)
        if (e.enabled && e.screen >= 0 && e.screen < 2 && !screens[e.screen].isNull())
        {
            painter.save();
            if(!e.polygon.isEmpty()) {
                QPainterPath path; path.addPolygon(e.polygon); path.closeSubpath();
                QTransform transform; transform.translate(e.destination.x(),e.destination.y());
                transform.scale(double(e.destination.width())/e.width,double(e.destination.height())/e.height);
                transform.translate(-e.x,-e.y);
                painter.setClipPath(transform.map(path),Qt::IntersectClip);
            }
            painter.drawImage(QRectF(e.destination), screens[e.screen], QRectF(e.sourceRect()));
            painter.restore();
        }
}

QImage studioPolygonMask(const StudioElement& e)
{
    QImage rgba(256,192,QImage::Format_ARGB32_Premultiplied); rgba.fill(Qt::transparent);
    QPainter painter(&rgba); painter.setPen(Qt::NoPen); painter.setBrush(Qt::white);
    if(e.polygon.isEmpty()) painter.drawRect(e.sourceRect()); else painter.drawPolygon(e.polygon,Qt::OddEvenFill);
    painter.end();
    QImage alpha(256,192,QImage::Format_Alpha8);
    for(int y=0;y<192;++y) for(int x=0;x<256;++x) alpha.scanLine(y)[x]=qAlpha(rgba.pixel(x,y));
    return alpha;
}
void paintStudioSelection(QPainter& painter,const StudioElement& e)
{
    painter.save(); painter.setPen(QPen(Qt::yellow,1)); painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(e.destination));
    painter.fillRect(QRect(e.destination.right()-5,e.destination.bottom()-5,6,6),Qt::yellow); painter.restore();
}
