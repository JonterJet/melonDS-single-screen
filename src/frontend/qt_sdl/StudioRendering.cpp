// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioRendering.h"
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
            painter.drawImage(QRectF(e.destination), screens[e.screen], QRectF(e.sourceRect()));
}
