// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioViews.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

StudioScreensWidget::StudioScreensWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName("StudioOriginalScreens");
    setMinimumSize(160, 270);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setToolTip("View original screens. Pause & select a region to teach a state or crop a live overlay. Escape cancels selection.");
}
QRect StudioScreensWidget::screenRect(int i) const
{
    QRect cell(8, i*height()/2 + 32, width() - 16, height()/2 - 40);
    QSize size(256, 192); size.scale(cell.size(), Qt::KeepAspectRatio);
    return {QPoint(cell.center().x() - size.width()/2, cell.center().y() - size.height()/2), size};
}
void StudioScreensWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.fillRect(rect(), QColor(20,22,26));
    for (int i = 0; i < 2; ++i)
    {
        p.setPen(Qt::white);
        p.drawText(QRect(8, i*height()/2+8, width()-16, 24), Qt::AlignTop|Qt::AlignHCenter, i ? "Bottom screen" : "Top screen");
        auto target = screenRect(i); p.fillRect(target, Qt::black);
        if (!images[i].isNull()) p.drawImage(target, images[i]);
        else p.drawText(target, Qt::AlignCenter, "No game frame");
        if (selecting && i == dragScreen && !region.isEmpty())
        {
            p.setPen(QPen(Qt::yellow, 2));
            p.drawRect(QRectF(target.x()+region.x()*target.width()/256.0, target.y()+region.y()*target.height()/192.0,
                region.width()*target.width()/256.0, region.height()*target.height()/192.0));
        }
    }
    setCursor(selecting ? Qt::CrossCursor : Qt::ArrowCursor);
}
QPoint StudioScreensWidget::nativePoint(const QPoint& point, int screen) const
{
    auto r = screenRect(screen);
    return {qBound(0, (point.x()-r.x())*256/qMax(1,r.width()), 255),
        qBound(0, (point.y()-r.y())*192/qMax(1,r.height()), 191)};
}
void StudioScreensWidget::mousePressEvent(QMouseEvent* e)
{
    if (!selecting || e->button() != Qt::LeftButton) return;
    for (int i = 0; i < 2; ++i) if (!images[i].isNull() && screenRect(i).contains(e->pos()))
    { dragScreen = i; origin = nativePoint(e->pos(), i); region = QRect(origin, origin); update(); break; }
}
void StudioScreensWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (selecting && dragScreen >= 0)
    { region = QRect(origin, nativePoint(e->pos(), dragScreen)).normalized(); update(); }
}
void StudioScreensWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (!selecting || dragScreen < 0 || e->button() != Qt::LeftButton) return;
    region = QRect(origin, nativePoint(e->pos(), dragScreen)).normalized();
    if (region.width() < 4 || region.height() < 4) { dragScreen = -1; region = QRect(); update(); return; }
    int screen = dragScreen; selecting = false; dragScreen = -1; update();
    if (selected) selected(screen, region);
}
void StudioScreensWidget::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) { selecting = false; dragScreen = -1; region = QRect(); update(); e->accept(); }
    else QWidget::keyPressEvent(e);
}
StudioHudCanvas::StudioHudCanvas(QWidget* parent) : QWidget(parent)
{
    setObjectName("StudioHudCanvas"); setMinimumSize(220,180);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setToolTip("Drag the selected overlay to move it. Drag its lower-right corner to resize. Editing selects manual scene mode.");
}
QRect StudioHudCanvas::canvasRect() const
{
    QSize size(256,192); size.scale(this->size()-QSize(16,16), Qt::KeepAspectRatio);
    return {QPoint((width()-size.width())/2, (height()-size.height())/2), size};
}
QPoint StudioHudCanvas::nativePoint(const QPoint& p) const
{
    auto r = canvasRect(); return {qBound(0,(p.x()-r.x())*256/qMax(1,r.width()),255), qBound(0,(p.y()-r.y())*192/qMax(1,r.height()),191)};
}
void StudioHudCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.fillRect(rect(), QColor(20,22,26)); auto r = canvasRect();
    p.translate(r.topLeft()); p.scale(r.width()/256.0, r.height()/192.0);
    p.fillRect(QRect(0,0,256,192), Qt::black);
    if (!images[primary].isNull()) p.drawImage(QRect(0,0,256,192), images[primary]);
    if (editingEnabled)
    {
        paintStudioOverlays(p, images, elements);
        if (selected >= 0 && selected < elements.size())
        {
            auto dest = elements[selected].destination;
            p.setPen(QPen(Qt::yellow, 1)); p.drawRect(dest);
            p.fillRect(QRect(dest.x()+dest.width()-6, dest.y()+dest.height()-6,6,6),Qt::yellow);
        }
    }
    else { p.setPen(Qt::white); p.drawText(QRect(0,0,256,192),Qt::AlignCenter,"Enable Mario Kart DS tools\nto edit live overlays"); }
}
void StudioHudCanvas::mousePressEvent(QMouseEvent* e)
{
    if (!editingEnabled || e->button() != Qt::LeftButton || selected < 0 || selected >= elements.size()) return;
    origin = nativePoint(e->pos()); initial = elements[selected].destination;
    if (!canvasRect().contains(e->pos()) || !initial.contains(origin)) return;
    dragging = true;
    resizing = origin.x() >= initial.x()+initial.width()-10 && origin.y() >= initial.y()+initial.height()-10;
    if (editingStarted) editingStarted();
}
void StudioHudCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging) return;
    auto delta = nativePoint(e->pos()) - origin; QRect dest;
    if (resizing) dest = {initial.topLeft(), QSize(qBound(qMin(4,256-initial.x()),initial.width()+delta.x(),256-initial.x()), qBound(qMin(4,192-initial.y()),initial.height()+delta.y(),192-initial.y()))};
    else dest = {QPoint(qBound(0,initial.x()+delta.x(),256-initial.width()),qBound(0,initial.y()+delta.y(),192-initial.height())),initial.size()};
    if (moved) moved(dest);
}
void StudioHudCanvas::mouseReleaseEvent(QMouseEvent*) { dragging = false; }
