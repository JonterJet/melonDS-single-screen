// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_POLYGON_H
#define STUDIO_POLYGON_H
#include <QWidget>
#include <QImage>
#include <QPolygonF>
#include <functional>
class StudioPolygon : public QWidget
{
public:
    explicit StudioPolygon(QWidget* parent=nullptr);
    QImage image;
    QPolygonF points;
    bool closed=false;
    int dragging=-1;
    QRect canvasRect() const;
    QPointF nativePoint(QPointF position) const;
    void undo();
    bool valid() const;
    std::function<void()> confirm;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
};
#endif
