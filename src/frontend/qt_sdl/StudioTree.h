// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STUDIO_TREE_H
#define STUDIO_TREE_H
#include <QTreeWidget>
#include <QDropEvent>
#include <QTimer>
#include <QSignalBlocker>
#include <functional>

// Reordering keeps object ownership intact: scenes stay in their profile and
// widgets stay in their scene. Other transfers use explicit Copy/Paste.
class StudioTree : public QTreeWidget
{
public:
    explicit StudioTree(QWidget* parent=nullptr) : QTreeWidget(parent)
    {
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    }
    std::function<void()> reordered;
    bool moveRelative(QTreeWidgetItem* source,QTreeWidgetItem* target,bool before)
    {
        if(!source || !target || source==target || source->parent()!=target->parent()) return false;
        auto parent=source->parent();
        QSignalBlocker blocker(this);
        int from=parent ? parent->indexOfChild(source) : indexOfTopLevelItem(source);
        int to=parent ? parent->indexOfChild(target) : indexOfTopLevelItem(target);
        if(!before) ++to; if(from<to) --to;
        if(from==to) return false;
        if(parent) { parent->takeChild(from); parent->insertChild(to,source); }
        else { takeTopLevelItem(from); insertTopLevelItem(to,source); }
        setCurrentItem(source);
        if(reordered) QTimer::singleShot(0,this,[this] { if(reordered) reordered(); });
        return true;
    }
protected:
    void dropEvent(QDropEvent* event) override
    {
        if(event->source()!=this) { event->ignore(); return; }
        auto indicator=dropIndicatorPosition();
        if(indicator!=QAbstractItemView::AboveItem && indicator!=QAbstractItemView::BelowItem) { event->ignore(); return; }
        if(moveRelative(currentItem(),itemAt(event->position().toPoint()),indicator==QAbstractItemView::AboveItem)) { event->setDropAction(Qt::MoveAction); event->accept(); }
        else event->ignore();
    }
};
#endif
