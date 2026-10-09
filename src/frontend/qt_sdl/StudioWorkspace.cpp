// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "StudioEditor.h"
#include "StudioViews.h"
#include "StudioTree.h"
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QTimer>
#include <QToolBar>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QUuid>
#include <QToolButton>
#include <QMenu>

void StudioEditor::initializeProfiles(QWidget* widget)
{
    auto layout=static_cast<QVBoxLayout*>(widget->layout());
    auto tree=new StudioTree; profileTree=tree; profileTree->setObjectName("StudioProfiles");
    profileTree->setHeaderLabel("Game profiles / scenes"); profileTree->setMinimumHeight(160);
    profileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->insertWidget(0,profileTree,1);
    auto row=new QHBoxLayout; row->setSpacing(3);
    auto button=[&](const QString& text,const QString& id,const QString& command) {
        auto b=new QPushButton(text); b->setObjectName(id); b->setToolTip(command);
        row->addWidget(b); connect(b,&QPushButton::clicked,this,[this,command] { profileCommand(command); }); return b;
    };
    button("New Profile","StudioNewProfile","New Profile"); button("+ Scene","StudioAddScene","Add Scene");
    button(QString::fromUtf8("−"),"StudioDeleteItem","Delete")->setFixedWidth(28);
    auto up=button("","StudioProfileUp","Move Up"), down=button("","StudioProfileDown","Move Down");
    up->setIcon(window->style()->standardIcon(QStyle::SP_ArrowUp)); down->setIcon(window->style()->standardIcon(QStyle::SP_ArrowDown));
    up->setFixedWidth(28); down->setFixedWidth(28); layout->insertLayout(1,row);
    // Shared command handlers serve shortcuts, toolbar controls and context menus.
    connect(profileTree,&QTreeWidget::customContextMenuRequested,this,[this](QPoint pos) {
        auto item=profileTree->itemAt(pos); if(item) profileTree->setCurrentItem(item);
        QMenu menu(window);
        for(auto command : {"Rename","Duplicate","Copy","Paste","Delete"}) {
            auto action=menu.addAction(command);
            if(QString(command)=="Paste") action->setEnabled(clipboardType==1 || clipboardType==2);
            connect(action,&QAction::triggered,this,[this,command] { profileCommand(command); });
        }
        if(item && !item->parent()) {
            menu.addSeparator(); auto action=menu.addAction("Associate with open ROM");
            connect(action,&QAction::triggered,this,[this] { profileCommand("Associate"); });
        }
        menu.exec(profileTree->viewport()->mapToGlobal(pos));
    });
    connect(profileTree,&QTreeWidget::itemSelectionChanged,this,[this] {
        if(refreshing) return; auto item=profileTree->currentItem(); if(!item) return;
        profileSelected=!item->parent();
        chooseProfile(item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toInt());
    });
    connect(profileTree,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int) {
        if(refreshing) return;
        auto text=item->text(0).trimmed().left(128); if(text.isEmpty()) { QTimer::singleShot(0,this,[this] { refresh(); }); return; }
        auto id=item->data(0,Qt::UserRole).toString(); int scene=item->data(0,Qt::UserRole+1).toInt();
        if(id!=document.profileId) return;
        if(scene<0) document.gameLabel=text; else document.sceneNames[scene]=text;
        document.dirty=true; save(); QTimer::singleShot(0,this,[this] { refresh(); });
    });
    tree->reordered=[this] { reorderProfiles(); };
    auto shortcut=[this](const QString& command,const QKeySequence& key) {
        auto action=new QAction(command,profileTree); action->setShortcut(key); action->setShortcutContext(Qt::WidgetWithChildrenShortcut); profileTree->addAction(action);
        connect(action,&QAction::triggered,this,[this,command] { profileCommand(command); });
    };
    shortcut("Copy",QKeySequence::Copy); shortcut("Paste",QKeySequence::Paste); shortcut("Duplicate",QKeySequence(Qt::CTRL | Qt::Key_D));
    shortcut("Delete",QKeySequence::Delete);
    editingStatus=new QLabel; editingStatus->setWordWrap(true); editingStatus->setObjectName("StudioEditingStatus"); layout->addWidget(editingStatus);
}
void StudioEditor::beginInlineName(bool scene)
{
    auto item=profileTree->currentItem();
    if(!item) return;
    if(!scene && item->parent()) item=item->parent();
    profileTree->setCurrentItem(item); profileTree->setFocus(); profileTree->editItem(item,0);
}
void StudioEditor::profileCommand(const QString& command)
{
    auto item=profileTree->currentItem(); bool scene=item && item->parent();
    if(command=="Rename") { beginInlineName(scene); return; }
    if(command=="Copy") { editElement(); editReference(); copiedProfile=document; copiedScene=scene ? document.activeState : -1; clipboardType=scene ? 2 : 1; return; }
    if(command=="Associate") {
        if(currentRom.startsWith("firmware")) { showError("Open the ROM you want to associate first."); return; }
        document.gameId=currentRom; document.dirty=true;
        if(save()) { QString error; if(!profiles.prefer(document,error)) showError(error); } clearScreens(); refresh(); return;
    }
    if(command=="New Profile" || ((command=="Duplicate" && !scene) || (command=="Paste" && clipboardType==1))) {
        if(!saveOnClose()) return;
        StudioDocument next;
        if(command!="New Profile") {
            next=command=="Paste" ? copiedProfile : document;
            next.profileId=QUuid::createUuid().toString(QUuid::WithoutBraces);
            next.gameLabel=(next.gameLabel+" copy").left(128);
            for(auto& id : next.sceneIds) id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            for(auto& elements : next.elements) for(auto& e : elements) e.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        } else { next.gameId=currentRom; next.gameLabel="New profile"; }
        next.dirty=true; QString error;
        if(!profiles.save(next,error) || (next.gameId==currentRom && !profiles.prefer(next,error))) { showError(error); return; }
        document=std::move(next); profileSelected=true; clearScreens(); refresh();
        if(command=="New Profile") beginInlineName(false); return;
    }
    if(command=="Add Scene" || (command=="Duplicate" && scene) || (command=="Paste" && clipboardType==2)) {
        editElement(); editReference();
        if(command=="Add Scene") document.activeState=document.addScene("New scene");
        else {
            auto source=command=="Paste" ? copiedProfile : document;
            int index=command=="Paste" ? copiedScene : document.activeState;
            if(index<0 || index>=source.sceneCount()) return;
            int target=document.addScene(source.sceneName(index)+" copy");
            document.elements[target]=source.elements[index]; document.references[target]=source.references[index]; document.layouts[target]=source.layouts[index];
            for(auto& e : document.elements[target]) e.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            document.activeState=target;
        }
        profileSelected=false; resetRecognition(); refresh(); save();
        if(command=="Add Scene") beginInlineName(true); return;
    }
    if(command=="Delete") {
        if(QMessageBox::question(window,scene ? "Delete scene" : "Delete profile",scene ? "Delete the selected scene and its widgets and references?" : "Archive the selected profile and all its scenes?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
        if(scene) document.removeScene(document.activeState);
        else {
            QString error; if(!profiles.remove(document.profileId,error)) { showError(error); return; }
            StudioDocument next; if(!profiles.forRom(currentRom,currentRomLabel,QString{},next,error)) { showError(error); return; } document=std::move(next);
        }
        resetRecognition(); refresh(); save(); return;
    }
    if(command=="Move Up" || command=="Move Down") {
        int delta=command=="Move Up" ? -1 : 1;
        if(scene) { document.moveScene(document.activeState,document.activeState+delta); resetRecognition(); refresh(); save(); }
        else {
            QStringList ids; for(const auto& p : profiles.list()) ids.append(p.id);
            int from=ids.indexOf(document.profileId), to=from+delta;
            if(from<0 || to<0 || to>=ids.size()) return; ids.move(from,to);
            QString error; if(!profiles.setOrder(ids,error)) showError(error); refresh();
        }
    }
}
void StudioEditor::reorderProfiles()
{
    QStringList ids;
    for(int i=0;i<profileTree->topLevelItemCount();++i) {
        auto item=profileTree->topLevelItem(i); ids.append(item->data(0,Qt::UserRole).toString());
        if(ids.last()!=document.profileId) continue;
        for(int j=0;j<item->childCount();++j) {
            QString id=item->child(j)->data(0,Qt::UserRole+2).toString();
            document.moveScene(document.sceneIds.indexOf(id),j);
        }
    }
    QString error; if(!profiles.setOrder(ids,error)) showError(error);
    resetRecognition(); refresh(); save();
}
void StudioEditor::refreshProfiles()
{
    profileTree->clear();
    for(const auto& p : profiles.list()) {
        auto item=new QTreeWidgetItem(profileTree,{p.name}); item->setData(0,Qt::UserRole,p.id); item->setData(0,Qt::UserRole+1,-1);
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        item->setToolTip(0,"ROM SHA-256: "+p.rom);
        StudioDocument d; QString error;
        if(p.id==document.profileId) d=document;
        else if(!profiles.load(p.id,d,error)) continue;
        for(int i=0;i<d.sceneCount();++i) {
            auto scene=new QTreeWidgetItem(item,{d.sceneName(i)}); scene->setData(0,Qt::UserRole,p.id); scene->setData(0,Qt::UserRole+1,i); scene->setData(0,Qt::UserRole+2,d.sceneIds[i]);
            scene->setFlags((scene->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
            if(p.id==document.profileId && i==document.activeState && !profileSelected) profileTree->setCurrentItem(scene);
        }
        if(p.id==document.profileId && profileSelected) profileTree->setCurrentItem(item);
        item->setExpanded(p.id==document.profileId);
    }
}
void StudioEditor::chooseProfile(const QString& id,int scene)
{
    if(id!=document.profileId) {
        if(!saveOnClose()) return;
        StudioDocument next; QString error; if(!profiles.load(id,next,error)) { showError(error); return; }
        document=std::move(next);
        if(document.gameId==currentRom && !profiles.prefer(document,error)) showError(error);
        clearScreens();
    } else { editElement(); editReference(); }
    if(scene>=0 && scene<document.sceneCount()) document.activeState=scene;
    // Keep tree items/indexes alive during mouse selection so a press can become
    // a drag or double-click edit. Rebuild the hierarchy only after mutations.
    if(auto item=profileTree->currentItem()) { if(item->parent()) item->parent()->setExpanded(true); else item->setExpanded(true); }
    document.dirty=true; refresh(-1,false);
}
static QString workspacePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/MelonStudio/workspace.ini";
}
void StudioEditor::saveWorkspace()
{
    if(playMode || window->isFullScreen()) return;
    QSettings settings(workspacePath(),QSettings::IniFormat);
    settings.setValue("geometry",window->saveGeometry()); settings.setValue("docks",window->saveState(3));
}
void StudioEditor::restoreWorkspace()
{
    QSettings settings(workspacePath(),QSettings::IniFormat);
    auto geometry=settings.value("geometry").toByteArray(), docks=settings.value("docks").toByteArray();
    if(!geometry.isEmpty()) window->restoreGeometry(geometry);
    if(!docks.isEmpty()) window->restoreState(docks,3);
}
void StudioEditor::resetWorkspace()
{
    if(playMode) playAction->setChecked(false);
    if(window->isFullScreen()) window->toggleFullscreen();
    window->restoreState(defaultDockState,3); window->resize(1280,800);
    saveWorkspace();
}
void StudioEditor::resetPlayMouse()
{
    lastMousePosition=QCursor::pos(); exitPlay->hide(); exitTimer->stop();
}
bool StudioEditor::eventFilter(QObject* object,QEvent* event)
{
    if(event->type()==QEvent::Resize && object==toolbar) {
        if(transport) transport->move((toolbar->width()-transport->width())/2,(toolbar->height()-transport->height())/2);
    }
    auto target=qobject_cast<QWidget*>(object);
    bool belongs=target && (target==window || window->isAncestorOf(target));
    if(!belongs) return QObject::eventFilter(object,event);
    if(event->type()==QEvent::KeyRelease && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape && eatEscapeRelease) {
        eatEscapeRelease=false; return true;
    }
    if(playMode && (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress)) {
        auto key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape) {
            event->accept();
            if(event->type()==QEvent::KeyPress) { eatEscapeRelease=true; playAction->setChecked(false); }
            return true;
        }
    }
    if(!playMode && !window->isFullScreen() && target==window->panel && document.gameId==currentRom && document.sceneToolsEnabled && !profileSelected && runtimeState==document.activeState) {
        bool mouseEvent=event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseMove || event->type()==QEvent::MouseButtonRelease;
        if(mouseEvent) {
            auto mouse=static_cast<QMouseEvent*>(event); bool invertible;
            auto inverse=window->panel->studioTransform().inverted(&invertible);
            if(invertible) {
                QPoint point=inverse.map(mouse->position()).toPoint();
                auto& elements=document.elements[document.activeState];
                int selected=outliner->indexOfTopLevelItem(outliner->currentItem());
                if(event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton) {
                    for(int row=elements.size()-1;row>=0;--row) {
                        const auto& e=elements[row]; if(!e.enabled || !e.destination.contains(point)) continue;
                        bool handle=row==selected && point.x()>=e.destination.right()-9 && point.y()>=e.destination.bottom()-9;
                        QPointF source(e.x+(point.x()-e.destination.x())*double(e.width)/e.destination.width(),e.y+(point.y()-e.destination.y())*double(e.height)/e.destination.height());
                        if(!handle && !e.polygon.isEmpty() && !e.polygon.containsPoint(source,Qt::OddEvenFill)) continue;
                        automatic->setChecked(false); outliner->setCurrentItem(outliner->topLevelItem(row));
                        viewportDrag=true; window->panel->grabMouse(); viewportResize=handle; viewportOrigin=point; viewportInitial=e.destination;
                        event->accept(); return true;
                    }
                } else if(viewportDrag && selected>=0 && selected<elements.size()) {
                    if(event->type()==QEvent::MouseMove && !(mouse->buttons() & Qt::LeftButton)) { viewportDrag=false; window->panel->releaseMouse(); return false; }
                    if(event->type()==QEvent::MouseMove) {
                        QPoint delta=point-viewportOrigin; QRect dest;
                        if(viewportResize) dest={viewportInitial.topLeft(),QSize(qBound(1,viewportInitial.width()+delta.x(),256-viewportInitial.x()),qBound(1,viewportInitial.height()+delta.y(),192-viewportInitial.y()))};
                        else dest={QPoint(qBound(0,viewportInitial.x()+delta.x(),256-viewportInitial.width()),qBound(0,viewportInitial.y()+delta.y(),192-viewportInitial.height())),viewportInitial.size()};
                        elements[selected].destination=dest; document.dirty=true; selectElement();
                    }
                    if(event->type()==QEvent::MouseButtonRelease) { viewportDrag=false; window->panel->releaseMouse(); }
                    event->accept(); return true;
                }
            }
        }
    }
    if(playMode && (event->type()==QEvent::MouseButtonPress || event->type()==QEvent::MouseButtonRelease || event->type()==QEvent::MouseButtonDblClick)) {
        // In console Play mode the physical mouse only operates the transient Exit
        // control. Never forward stray clicks as Nintendo DS touchscreen presses.
        if(target!=exitPlay) { event->accept(); return true; }
    }
    if(playMode && event->type()==QEvent::MouseMove) {
        auto mouse=static_cast<QMouseEvent*>(event);
        if(mouse->source()==Qt::MouseEventNotSynthesized && mouse->globalPosition().toPoint()!=lastMousePosition) {
            lastMousePosition=mouse->globalPosition().toPoint(); exitPlay->adjustSize();
            window->setCursor(Qt::ArrowCursor); window->panel->setCursor(Qt::ArrowCursor); exitPlay->move(window->width()-exitPlay->width()-16,16); exitPlay->show(); exitPlay->raise(); exitTimer->start();
        }
    }
    if(playMode && event->type()==QEvent::MouseMove && target!=exitPlay) { event->accept(); return true; }
    return QObject::eventFilter(object,event);
}

void StudioEditor::widgetCommand(const QString& command)
{
    int row=outliner->indexOfTopLevelItem(outliner->currentItem());
    auto& elements=document.elements[document.activeState];
    if(command=="Paste") {
        if(clipboardType!=3 || elements.size()>=512) return;
        auto copy=copiedWidget; copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        elements.append(copy); document.dirty=true; refresh(elements.size()-1); save(); return;
    }
    if(row<0 || row>=elements.size()) return;
    if(command=="Rename") { outliner->setFocus(); outliner->editItem(outliner->currentItem(),0); return; }
    if(command=="Copy") { editElement(); copiedWidget=elements[row]; clipboardType=3; return; }
    if(command=="Duplicate") {
        if(elements.size()>=512) return;
        auto copy=elements[row]; copy.id=QUuid::createUuid().toString(QUuid::WithoutBraces); copy.name=(copy.name+" copy").left(128);
        elements.insert(row+1,copy); ++row;
    } else if(command=="Delete") elements.removeAt(row);
    else if(command=="Hide/Show") elements[row].enabled=!elements[row].enabled;
    else if(command=="Move Up" || command=="Move Down") {
        int next=row+(command=="Move Up" ? -1 : 1); if(next<0 || next>=elements.size()) return;
        elements.move(row,next); row=next;
    }
    document.dirty=true;
    // Refresh Inspector before saving: stale controls must never overwrite a newly
    // pasted, hidden, reordered or deleted object.
    refresh(row); save();
}
void StudioEditor::reorderWidgets()
{
    auto& elements=document.elements[document.activeState];
    for(int i=0;i<outliner->topLevelItemCount();++i) {
        auto id=outliner->topLevelItem(i)->data(0,Qt::UserRole).toString();
        for(int j=i;j<elements.size();++j) if(elements[j].id==id) { elements.move(j,i); break; }
    }
    document.dirty=true; int row=outliner->indexOfTopLevelItem(outliner->currentItem()); refresh(row); save();
}
