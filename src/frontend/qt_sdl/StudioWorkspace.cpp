// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "StudioEditor.h"
#include "StudioViews.h"
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
#include <QToolButton>
#include <QMenu>

void StudioEditor::initializeProfiles(QWidget* widget)
{
    auto layout=static_cast<QVBoxLayout*>(widget->layout());
    profileTree=new QTreeWidget; profileTree->setObjectName("StudioProfiles");
    profileTree->setHeaderLabel("Game profiles / scenes"); profileTree->setMinimumHeight(160);
    layout->insertWidget(0,profileTree,1);
    auto row=new QGridLayout;
    int profileButtonIndex=0;
    auto button=[&](const QString& text,const QString& id) { auto b=new QPushButton(text); b->setObjectName(id); row->addWidget(b,profileButtonIndex/2,profileButtonIndex%2); ++profileButtonIndex; return b; };
    auto create=button("New profile","StudioNewProfile"), rename=button("Rename","StudioRenameProfile"), copy=button("Duplicate","StudioDuplicateProfile"), remove=button("Delete","StudioDeleteProfile");
    layout->insertLayout(1,row);
    auto scenes=new QGridLayout;
    int sceneButtonIndex=0;
    auto sceneButton=[&](const QString& text,const QString& id) { auto b=new QPushButton(text); b->setObjectName(id); scenes->addWidget(b,sceneButtonIndex/3,sceneButtonIndex%3); ++sceneButtonIndex; return b; };
    auto addScene=sceneButton("+ Scene","StudioAddScene"), nameScene=sceneButton("Rename","StudioRenameScene"), copyScene=sceneButton("Copy","StudioDuplicateScene"), deleteScene=sceneButton("Delete","StudioDeleteScene"), up=sceneButton("Up","StudioSceneUp"), down=sceneButton("Down","StudioSceneDown");
    layout->insertLayout(2,scenes);
    auto askName=[this](const QString& title,const QString& initial) { bool ok=false; auto text=QInputDialog::getText(window,title,"Name",QLineEdit::Normal,initial,&ok).trimmed().left(128); return ok ? text : QString{}; };
    connect(create,&QPushButton::clicked,this,[this,askName] {
        auto name=askName("New game profile",currentRomLabel); if(name.isEmpty() || !saveOnClose()) return;
        StudioDocument next; next.gameId=currentRom; next.gameLabel=name;
        QString error; if(!profiles.save(next,error) || !profiles.prefer(next,error)) { showError(error); return; }
        document=std::move(next); resetRecognition(); refresh();
    });
    connect(rename,&QPushButton::clicked,this,[this,askName] {
        auto name=askName("Rename game profile",document.gameLabel); if(name.isEmpty()) return;
        document.gameLabel=name; document.dirty=true; save(); refresh();
    });
    connect(copy,&QPushButton::clicked,this,[this,askName] {
        auto name=askName("Duplicate game profile",document.gameLabel+" copy"); if(name.isEmpty() || !saveOnClose()) return;
        StudioDocument next; QString error;
        if(!profiles.duplicate(document.profileId,name,next,error) || !profiles.prefer(next,error)) { showError(error); return; }
        document=std::move(next); resetRecognition(); refresh();
    });
    connect(remove,&QPushButton::clicked,this,[this] {
        if(QMessageBox::question(window,"Delete game profile","Archive this profile and all its scenes?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
        QString error; if(!profiles.remove(document.profileId,error)) { showError(error); return; }
        StudioDocument next;
        if(!profiles.forRom(currentRom,currentRomLabel,QString{},next,error)) { showError(error); return; }
        document=std::move(next); resetRecognition(); refresh();
    });
    connect(addScene,&QPushButton::clicked,this,[this,askName] { auto name=askName("New scene","New scene"); if(name.isEmpty()) return; editElement(); document.activeState=document.addScene(name); document.automatic=false; resetRecognition(); refresh(); });
    connect(nameScene,&QPushButton::clicked,this,[this,askName] { auto name=askName("Rename scene",document.sceneName(document.activeState)); if(name.isEmpty()) return; document.sceneNames[document.activeState]=name; document.dirty=true; refresh(); });
    connect(copyScene,&QPushButton::clicked,this,[this] { editElement(); document.duplicateScene(document.activeState); document.automatic=false; resetRecognition(); refresh(); });
    connect(deleteScene,&QPushButton::clicked,this,[this] {
        if(QMessageBox::question(window,"Delete scene","Delete this scene and its HUD and references?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
        document.removeScene(document.activeState); document.automatic=false; resetRecognition(); refresh();
    });
    auto move=[this](int offset) { document.moveScene(document.activeState,document.activeState+offset); document.automatic=false; resetRecognition(); refresh(); };
    connect(up,&QPushButton::clicked,this,[move] { move(-1); }); connect(down,&QPushButton::clicked,this,[move] { move(1); });
    connect(profileTree,&QTreeWidget::itemSelectionChanged,this,[this] {
        if(refreshing) return; auto item=profileTree->currentItem(); if(!item) return;
        chooseProfile(item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toInt());
    });
    auto associate=new QPushButton("Associate with open ROM"); associate->setObjectName("StudioAssociateROM"); layout->addWidget(associate);
    auto profileMenu=new QMenu(widget);
    for(auto b : {rename,copy,remove,associate}) { auto action=profileMenu->addAction(b->text()); connect(action,&QAction::triggered,b,&QPushButton::click); b->hide(); }
    auto profileActions=new QToolButton; profileActions->setText("Profile actions"); profileActions->setPopupMode(QToolButton::InstantPopup); profileActions->setMenu(profileMenu); row->addWidget(profileActions,0,1);
    auto sceneMenu=new QMenu(widget);
    for(auto b : {nameScene,copyScene,deleteScene,up,down}) { auto action=sceneMenu->addAction(b->text()); connect(action,&QAction::triggered,b,&QPushButton::click); b->hide(); }
    auto sceneActions=new QToolButton; sceneActions->setText("Scene actions"); sceneActions->setPopupMode(QToolButton::InstantPopup); sceneActions->setMenu(sceneMenu); scenes->addWidget(sceneActions,0,1,1,2);
    connect(associate,&QPushButton::clicked,this,[this] {
        if(currentRom.startsWith("firmware")) { showError("Open the ROM you want to associate first."); return; }
        // Use the emulator's established identity, including its ROM normalization.
        // Hashing the raw file separately can differ for trimmed or re-encrypted carts.
        document.gameId=currentRom; document.dirty=true;
        if(save()) { QString error; if(!profiles.prefer(document,error)) showError(error); }
        resetRecognition(); refresh();
    });
}
void StudioEditor::refreshProfiles()
{
    profileTree->clear();
    for(const auto& p : profiles.list()) {
        auto item=new QTreeWidgetItem(profileTree,{p.name}); item->setData(0,Qt::UserRole,p.id); item->setData(0,Qt::UserRole+1,-1);
        item->setToolTip(0,"ROM SHA-256: "+p.rom);
        StudioDocument d; QString error;
        if(p.id==document.profileId) d=document;
        else if(!profiles.load(p.id,d,error)) continue;
        for(int i=0;i<d.sceneCount();++i) {
            auto scene=new QTreeWidgetItem(item,{d.sceneName(i)}); scene->setData(0,Qt::UserRole,p.id); scene->setData(0,Qt::UserRole+1,i);
            if(p.id==document.profileId && i==document.activeState) profileTree->setCurrentItem(scene);
        }
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
    } else editElement();
    if(scene>=0 && scene<document.sceneCount()) document.activeState=scene;
    document.automatic=false; document.dirty=true; clearScreens(); refresh();
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
        if(auto button=toolbar->findChild<QWidget*>("StudioPlayButton")) button->move((toolbar->width()-button->width())/2,(toolbar->height()-button->height())/2);
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
    if(!playMode && !window->isFullScreen() && target==window->panel && document.gameId==currentRom && document.sceneToolsEnabled && runtimeState>=0) {
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
    if(playMode && event->type()==QEvent::MouseMove) {
        auto mouse=static_cast<QMouseEvent*>(event);
        if(mouse->source()==Qt::MouseEventNotSynthesized && mouse->globalPosition().toPoint()!=lastMousePosition) {
            lastMousePosition=mouse->globalPosition().toPoint(); exitPlay->adjustSize();
            exitPlay->move(window->width()-exitPlay->width()-16,16); exitPlay->show(); exitPlay->raise(); exitTimer->start();
        }
    }
    return QObject::eventFilter(object,event);
}
