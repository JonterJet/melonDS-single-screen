// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "StudioEditor.h"
#include "StudioViews.h"
#include "StudioPolygon.h"
#include "EmuThread.h"
#include "EmuInstance.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QMutexLocker>
#include <cmath>

void StudioEditor::addPolygon(bool edit)
{
    if(document.gameId!=currentRom) { showError("Open this profile's associated ROM before tracing a HUD."); return; }
    int row=outliner->indexOfTopLevelItem(outliner->currentItem());
    if(edit && row<0) return;
    bool ok=false; QStringList sources{"Top screen","Bottom screen"};
    auto choice=QInputDialog::getItem(window,"HUD source","Source screen",sources,edit ? document.elements[document.activeState][row].screen : 1,false,&ok);
    if(!ok) return;
    int screen=sources.indexOf(choice);
    auto thread=window->getEmuInstance()->getEmuThread();
    if(!thread->emuIsActive()) { showError("Load and start the associated game first."); return; }
    bool wasPaused=!thread->emuIsRunning();
    window->studioPause(true); automatic->setChecked(false);
    QDialog dialog(window); dialog.setWindowTitle(edit ? "Edit live HUD polygon" : "Trace live HUD polygon"); dialog.resize(850,700);
    auto layout=new QVBoxLayout(&dialog);
    auto help=new QLabel("Click points to trace the cutout. Drag vertices to adjust. Close shape previews its outline. Undo: Backspace / Ctrl+Z. Confirm: Enter, double-click, or Confirm. Pixels outside the polygon are transparent.");
    help->setWordWrap(true); layout->addWidget(help);
    auto canvas=new StudioPolygon; layout->addWidget(canvas,1);
    if(edit) { const auto& e=document.elements[document.activeState][row]; if(e.screen==screen) canvas->points=e.polygon; }
    auto buttons=new QHBoxLayout;
    auto undo=new QPushButton("Undo point"), close=new QPushButton("Close / adjust shape"), clear=new QPushButton("Clear");
    buttons->addWidget(undo); buttons->addWidget(close); buttons->addWidget(clear); layout->addLayout(buttons);
    auto box=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); box->button(QDialogButtonBox::Ok)->setText("Confirm");
    box->button(QDialogButtonBox::Ok)->setObjectName("StudioConfirmPolygon"); box->button(QDialogButtonBox::Ok)->setEnabled(false); layout->addWidget(box);
    connect(undo,&QPushButton::clicked,canvas,&StudioPolygon::undo);
    connect(close,&QPushButton::clicked,canvas,[canvas] { canvas->closed=canvas->valid(); canvas->update(); canvas->setFocus(); });
    connect(clear,&QPushButton::clicked,canvas,[canvas] { canvas->points.clear(); canvas->closed=false; canvas->update(); });
    canvas->confirm=[&dialog,canvas] { if(canvas->valid() && !canvas->image.isNull()) dialog.accept(); };
    connect(box,&QDialogButtonBox::accepted,&dialog,[canvas] { canvas->confirm(); }); connect(box,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    quint64 after; { QMutexLocker lock(&imageMutex); after=imageSerial; }
    captureRequested.store(true); bool captured=false;
    QTimer poll; poll.setInterval(50);
    connect(&poll,&QTimer::timeout,&dialog,[&,canvas] {
        if(!captured) { QMutexLocker lock(&imageMutex); if(imageSerial>after && !images[screen].isNull()) { canvas->image=images[screen]; captured=true; canvas->update(); } }
        box->button(QDialogButtonBox::Ok)->setEnabled(captured && canvas->valid());
        if(!captured) captureRequested.store(true);
    }); poll.start(); canvas->setFocus();
    int outcome=dialog.exec(); poll.stop();
    if(outcome==QDialog::Accepted && captured && canvas->valid()) {
        StudioElement e=edit ? document.elements[document.activeState][row] : StudioElement{};
        e.name=edit ? e.name : QString("HUD %1").arg(document.elements[document.activeState].size()+1);
        e.screen=screen; e.polygon=canvas->points;
        auto bounds=e.polygon.boundingRect(); int x=qBound(0,int(std::floor(bounds.left())),255), y=qBound(0,int(std::floor(bounds.top())),191);
        e.x=x; e.y=y; e.width=qBound(1,int(std::ceil(bounds.right()))-x,256-x); e.height=qBound(1,int(std::ceil(bounds.bottom()))-y,192-y);
        if(!edit) e.destination=QRect(32,32,qMin(e.width,224),qMin(e.height,160));
        if(edit) document.elements[document.activeState][row]=e;
        else { document.elements[document.activeState].append(e); row=document.elements[document.activeState].size()-1; }
        document.sceneToolsEnabled=true; document.dirty=true; refresh(row);
    }
    if(!wasPaused && thread->emuIsActive()) window->studioPause(false);
}
