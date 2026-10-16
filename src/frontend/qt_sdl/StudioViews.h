// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_VIEWS_H
#define STUDIO_VIEWS_H
#include "StudioRendering.h"
#include <QWidget>
#include <functional>
class StudioScreensWidget : public QWidget
{
public:
    explicit StudioScreensWidget(QWidget* parent = nullptr);
    QImage images[2];
    bool selecting = false;
    std::function<void(int, QRect)> selected;
    QRect screenRect(int screen) const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    int dragScreen = -1;
    QPoint origin;
    QRect region;
    QPoint nativePoint(const QPoint& point, int screen) const;
};
class StudioHudCanvas : public QWidget
{
public:
    explicit StudioHudCanvas(QWidget* parent = nullptr);
    QImage images[2];
    QVector<StudioElement> elements;
    int selected = -1, primary = 0;
    bool editingEnabled = false;
    std::function<void()> editingStarted;
    std::function<void(QRect)> moved;
    QRect canvasRect() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    bool dragging = false, resizing = false;
    QPoint origin;
    QRect initial;
    QPoint nativePoint(const QPoint& point) const;
};
#endif
