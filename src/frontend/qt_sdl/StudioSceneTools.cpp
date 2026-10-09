// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h"
#include "StudioEditor.h"
#include "StudioViews.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMutexLocker>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QAction>

namespace
{
void layouts(QComboBox* combo)
{
    combo->addItem("Existing emulator layout", int(StudioLayout::Existing));
    combo->addItem("Top screen only", int(StudioLayout::Top));
    combo->addItem("Bottom screen only", int(StudioLayout::Bottom));
    combo->addItem("Both screens", int(StudioLayout::Both));
}
}
void StudioEditor::initializeSceneControls(QWidget* sceneWidget)
{
    auto sceneOptions = new QVBoxLayout;
    auto previewScene=new QPushButton("Preview selected scene (manual override)"); previewScene->setObjectName("StudioManualOverride"); sceneOptions->addWidget(previewScene);
    connect(previewScene,&QPushButton::clicked,this,[this] { automatic->setChecked(false); applyPresentation(); });
    marioMode = new QCheckBox("Enable scene layouts and HUD");
    marioMode->setObjectName("StudioSceneToolsEnabled");
    automatic = new QCheckBox("Automatic recognition");
    automatic->setObjectName("StudioAutomaticRecognition");
    sceneLayout = new QComboBox; layouts(sceneLayout);
    sceneLayout->setObjectName("StudioSceneLayout");
    sceneOptions->addWidget(marioMode); sceneOptions->addWidget(automatic);
    sceneOptions->addWidget(new QLabel("Scene layout:")); sceneOptions->addWidget(sceneLayout);
    static_cast<QVBoxLayout*>(sceneWidget->layout())->addLayout(sceneOptions);
    connect(marioMode, &QCheckBox::toggled, this, [this](bool value) {
        if (refreshing) return;
        editElement();
        document.sceneToolsEnabled = value; document.automatic = false;

        document.dirty = true; resetRecognition(); refresh();
    });
    connect(automatic, &QCheckBox::toggled, this, [this](bool value) {
        if (refreshing) return;
        editElement();
        document.automatic = value; document.dirty = true;
        resetRecognition(); applyPresentation();
    });
    connect(sceneLayout, &QComboBox::currentIndexChanged, this, [this] { settingsChanged(); });

    rules = new QWidget;
    auto form = new QFormLayout(rules);
    refList = new QListWidget; refList->setObjectName("StudioReferences");
    refList->setMaximumHeight(110); form->addRow(refList);
    refName = new QLineEdit; refName->setMaxLength(128); refName->setObjectName("StudioReferenceName");
    refThreshold = new QDoubleSpinBox; refThreshold->setObjectName("StudioReferenceThreshold");
    refThreshold->setRange(0,100); refThreshold->setDecimals(1); refThreshold->setSuffix(" %"); refThreshold->setValue(96);
    refEnabled = new QCheckBox("Reference enabled");
    refEnabled->setObjectName("StudioReferenceEnabled");
    form->addRow("Reference name", refName); form->addRow("Match threshold",refThreshold); form->addRow(refEnabled);
    auto capture = new QPushButton("Pause & select reference region");
    capture->setObjectName("StudioTeachReference");
    auto remove = new QPushButton("Remove selected reference");
    form->addRow(capture); form->addRow(remove);
    selectionHint = new QLabel("Select a scene in Game Profiles, then pause and drag a distinctive region on either original screen.");
    selectionHint->setWordWrap(true); form->addRow(selectionHint);
    auto resume = new QPushButton("Resume game"); form->addRow(resume);
    auto map = new QPushButton("Add live bottom-screen map to selected scene");
    map->setObjectName("StudioAddLiveMap"); form->addRow(map);
    fallbackLayout = new QComboBox; layouts(fallbackLayout); fallbackLayout->setObjectName("StudioFallbackLayout");
    confirmation = new QSpinBox; confirmation->setRange(0,5000); confirmation->setSuffix(" ms");
    confirmation->setObjectName("StudioConfirmationMs");
    margin = new QDoubleSpinBox; margin->setRange(0,100); margin->setDecimals(1); margin->setSuffix(" %");
    margin->setObjectName("StudioAmbiguityMargin");
    form->addRow("Unknown-state fallback", fallbackLayout);
    form->addRow("Confirmation period", confirmation); form->addRow("Ambiguity margin", margin);
    auto note = new QLabel("Multiple references are alternatives (any can match). Similarity is normalized RGB agreement, not an AI probability. Automatic mode starts on the fallback until a state is confirmed.");
    note->setWordWrap(true); form->addRow(note);
    auto rulesScroll = new QScrollArea; rulesScroll->setWidgetResizable(true); rulesScroll->setWidget(rules);
    auto rulesDock = dock("Recognition Rules", rulesScroll, Qt::RightDockWidgetArea);
    connect(refList, &QListWidget::currentRowChanged, this, [this] { if (!refreshing) refreshReferences(); });
    connect(refName, &QLineEdit::editingFinished, this, &StudioEditor::editReference);
    connect(refThreshold, &QDoubleSpinBox::valueChanged, this, &StudioEditor::editReference);
    connect(refEnabled, &QCheckBox::toggled, this, &StudioEditor::editReference);
    connect(capture, &QPushButton::clicked, this, [this] { beginSelection(false); });
    connect(remove, &QPushButton::clicked, this, [this] {
        int state = document.activeState, row = refList->currentRow();
        if (row < 0) return;
        document.references[state].removeAt(row); document.dirty = true;
        resetRecognition(); refreshReferences(); applyPresentation();
    });
    connect(resume, &QPushButton::clicked, this, [this] { window->studioPause(false); });
    connect(map, &QPushButton::clicked, this, [this] {
        editElement();
        auto& elements = document.elements[document.activeState];
        if (elements.size() >= 512) { showError("A scene can contain at most 512 overlays."); return; }
        StudioElement e; e.name = "Live racing map"; e.x = e.y = 0; e.width = 256; e.height = 192;
        e.destination = QRect(168,120,80,60); elements.append(e);
        document.automatic = false; document.dirty = true; resetRecognition(); refresh(elements.size()-1);
    });
    for (auto combo : {fallbackLayout}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { settingsChanged(); });
    connect(confirmation, &QSpinBox::valueChanged, this, [this] { settingsChanged(); });
    connect(margin, &QDoubleSpinBox::valueChanged, this, [this] { settingsChanged(); });

    debug = new QLabel; debug->setObjectName("StudioRecognitionDebug"); debug->setTextFormat(Qt::PlainText);
    debug->setWordWrap(true); debug->setAlignment(Qt::AlignTop);
    auto debugScroll=new QScrollArea; debugScroll->setWidgetResizable(true); debugScroll->setWidget(debug);
    auto debugDock = dock("Recognition Debug", debugScroll, Qt::RightDockWidgetArea);
    hudCanvas = new StudioHudCanvas;
    auto hudWidget = new QWidget; auto hudLayout = new QVBoxLayout(hudWidget);
    auto hint = new QLabel("Select an Outliner element. Drag it to move; drag its lower-right corner to resize. The canvas is in original DS pixels; the game viewport renders the overlay live.");
    hint->setWordWrap(true); hudLayout->addWidget(hint); hudLayout->addWidget(hudCanvas);
    auto canvasDock = dock("HUD Layout", hudWidget, Qt::RightDockWidgetArea);
    QDockWidget* inspectorDock = nullptr;
    for (auto d : docks) if (d->windowTitle() == "Inspector") inspectorDock = d;
    if (inspectorDock)
    {
        window->tabifyDockWidget(inspectorDock, rulesDock);
        window->tabifyDockWidget(inspectorDock, canvasDock);
        window->tabifyDockWidget(inspectorDock, debugDock);
        inspectorDock->raise();
    }
    hudCanvas->editingStarted = [this] { automatic->setChecked(false); };
    hudCanvas->moved = [this](QRect dest) {
        int row = outliner->indexOfTopLevelItem(outliner->currentItem());
        if (row < 0) return;
        document.elements[document.activeState][row].destination = dest;
        document.dirty = true; selectElement(); applyPresentation();
        profile->setText("  " + document.gameLabel + " *");
    };
    static_cast<StudioScreensWidget*>(preview)->selected = [this](int screen, QRect region) { selectedRegion(screen, region); };
}
void StudioEditor::refreshSceneControls()
{
    marioMode->setChecked(document.sceneToolsEnabled);
    marioMode->setEnabled(!document.gameId.startsWith("firmware"));
    automatic->setChecked(document.automatic); automatic->setEnabled(document.sceneToolsEnabled);
    sceneLayout->setCurrentIndex(int(document.layouts[document.activeState])); sceneLayout->setEnabled(document.sceneToolsEnabled);
    fallbackLayout->setCurrentIndex(int(document.fallback)); confirmation->setValue(document.confirmationMs);
    margin->setValue(document.ambiguityMargin*100);
    rules->setEnabled(document.sceneToolsEnabled && !profileSelected);
    sceneLayout->setEnabled(document.sceneToolsEnabled && !profileSelected);
    auto crop = inspector->findChild<QPushButton*>("StudioSelectOverlaySource");
    if (crop) crop->setEnabled(document.sceneToolsEnabled);
    refreshReferences();
}
void StudioEditor::refreshReferences()
{
    bool wasRefreshing = refreshing; refreshing = true;
    int state = document.activeState, row = refList->currentRow();
    refList->clear();
    for (const auto& r : document.references[state])
        refList->addItem(QString("%1 - %2 (%3,%4 %5x%6)").arg(r.name, r.screen ? "Bottom" : "Top")
            .arg(r.region.x()).arg(r.region.y()).arg(r.region.width()).arg(r.region.height()));
    if (refList->count()) { row = qBound(0,row,refList->count()-1); refList->setCurrentRow(row); }
    else row = -1;
    for (auto w : {static_cast<QWidget*>(refName), static_cast<QWidget*>(refThreshold), static_cast<QWidget*>(refEnabled)}) w->setEnabled(row >= 0);
    if (row >= 0)
    {
        const auto& r = document.references[state][row]; refName->setText(r.name);
        refThreshold->setValue(r.threshold*100); refEnabled->setChecked(r.enabled);
    }
    else refName->clear();
    refreshing = wasRefreshing;
}
void StudioEditor::editReference()
{
    if (refreshing) return;
    int state = document.activeState, row = refList->currentRow();
    if (row < 0 || row >= document.references[state].size()) return;
    auto& r = document.references[state][row];
    auto text = refName->text().trimmed(); if (text.isEmpty()) text = r.name;
    if (r.name == text && r.threshold == refThreshold->value()/100 && r.enabled == refEnabled->isChecked()) return;
    r.name = text; r.threshold = refThreshold->value()/100; r.enabled = refEnabled->isChecked();
    document.dirty = true; resetRecognition(); refreshReferences(); applyPresentation();
    profile->setText("  " + document.gameLabel + " *");
}
void StudioEditor::settingsChanged()
{
    if (refreshing) return;
    document.layouts[document.activeState] = StudioLayout(sceneLayout->currentData().toInt());
    document.fallback = StudioLayout(fallbackLayout->currentData().toInt());
    document.confirmationMs = confirmation->value(); document.ambiguityMargin = margin->value()/100;
    document.dirty = true; resetRecognition(); applyPresentation();
    profile->setText("  " + document.gameLabel + " *");
}
void StudioEditor::resetRecognition()
{
    recognizer.reset(); lastMatch = StudioMatch{}; runtimeState = -2; lastSampleMs = -1;
}
void StudioEditor::applyPresentation()
{
    StudioPresentation p;
    if (document.sceneToolsEnabled && document.gameId==currentRom)
    {
        runtimeState = document.automatic ? lastMatch.state : document.activeState;
        auto layout = runtimeState >= 0 ? document.layouts[runtimeState] : document.fallback;
        switch (layout)
        {
            case StudioLayout::Top: p.sizing = screenSizing_TopOnly; break;
            case StudioLayout::Bottom: p.sizing = screenSizing_BotOnly; break;
            case StudioLayout::Both: p.sizing = screenSizing_Even; break;
            default: break;
        }
        if (runtimeState >= 0) {
            p.overlays=document.elements[runtimeState];
            if(!playMode && !window->isFullScreen() && !profileSelected && runtimeState==document.activeState) p.selected=outliner->indexOfTopLevelItem(outliner->currentItem());
        }
        hudCanvas->primary = p.sizing == screenSizing_BotOnly ? 1 : 0;
    }
    else runtimeState = -2;
    window->setStudioPresentation(p); updateDebug();
    editingStatus->setText(QString("Editing: %1\n%2: %3").arg(document.sceneName(document.activeState),document.automatic ? "Automatic runtime" : "Manual override",runtimeState>=0 ? document.sceneName(runtimeState) : "Fallback"));
}
void StudioEditor::updateDebug()
{
    if (!document.sceneToolsEnabled) { debug->setText("Scene tools disabled. Existing emulator layout is unchanged."); return; }
    QString text = document.automatic ? "Automatic recognition\n" : "Manual override (recognition paused)\n";
    text += "Active: " + (runtimeState >= 0 ? document.sceneName(runtimeState) : "Unknown - fallback layout") + "\n";
    text += "Candidate: " + (lastMatch.ambiguous ? "Ambiguous" : lastMatch.candidate >= 0 ? document.sceneName(lastMatch.candidate) : "None") + "\n";
    text += QString("Best similarity: %1% (not probability)\nConfirmation: %2 / %3 ms\n")
        .arg(lastMatch.confidence*100,0,'f',1).arg(lastMatch.pendingMs).arg(document.confirmationMs);
    for (int i = 0; i < document.sceneCount(); ++i)
        text += QString("%1: %2%  [%3 references]\n").arg(document.sceneName(i)).arg(lastMatch.scores.value(i)*100,0,'f',1).arg(document.references[i].size());
    if (lastSampleMs < 0 || recognitionClock.elapsed()-lastSampleMs > 500) text += "No fresh recognition frames.\n";
    debug->setText(text);
}
void StudioEditor::tickScreens()
{
    QImage latest[2]; quint64 serial;
    { QMutexLocker lock(&imageMutex); latest[0] = images[0]; latest[1] = images[1]; serial = imageSerial; }
    bool fresh = serial != seenSerial; seenSerial = serial;
    qint64 now = recognitionClock.elapsed();
    auto screens = static_cast<StudioScreensWidget*>(preview);
    if (!screens->selecting) { screens->images[0] = latest[0]; screens->images[1] = latest[1]; preview->update(); }
    hudCanvas->images[0] = latest[0]; hudCanvas->images[1] = latest[1]; hudCanvas->update();
    if (teachingPending && serial > teachAfterSerial && !latest[0].isNull() && !latest[1].isNull())
    {
        teachingPending = false; screens->selecting = true; preview->update();
        selectionHint->setText("Game paused. Drag a region on either original screen (at least 4x4 DS pixels). Escape cancels; Resume game continues play.");
    }
    if (document.sceneToolsEnabled && document.automatic && document.gameId==currentRom)
    {
        int previousState = lastMatch.state;
        if (fresh && latest[0].size() == QSize(256,192) && latest[1].size() == QSize(256,192))
        { lastSampleMs = now; lastMatch = recognizer.update(latest,document,now); }
        else if (lastSampleMs < 0 || now-lastSampleMs > 500)
        { recognizer.reset(); lastMatch = StudioMatch{}; }
        if (runtimeState == -2 || previousState != lastMatch.state) applyPresentation();
        else updateDebug();
    }
    auto thread=window->getEmuInstance()->getEmuThread();
    bool active=thread->emuIsActive();
    pauseAction->setEnabled(active); resetGameAction->setEnabled(active);
    pauseAction->setChecked(active && !thread->emuIsRunning()); pauseAction->setText(pauseAction->isChecked() ? "Resume" : "Pause");
    captureRequested.store(originals->isVisible() || hudCanvas->isVisible() || teachingPending
        || (document.sceneToolsEnabled && document.automatic));
}
void StudioEditor::beginSelection(bool overlay)
{
    if (!document.sceneToolsEnabled) { showError("Enable scene layouts and HUD's profile first."); return; }
    if (document.gameId!=currentRom || !window->getEmuInstance()->getEmuThread()->emuIsActive())
    { showError("Load and start the associated game before capturing a game region."); return; }
    if (overlay && outliner->indexOfTopLevelItem(outliner->currentItem()) < 0)
    { showError("Select a HUD element in the Outliner first."); return; }

    window->studioPause(true); automatic->setChecked(false);
    selectingOverlay = overlay; teachingPending = true;
    static_cast<StudioScreensWidget*>(preview)->selecting = false;
    { QMutexLocker lock(&imageMutex); teachAfterSerial = imageSerial; }
    captureRequested.store(true); originals->show(); originals->raise();
    selectionHint->setText("Waiting for a paused game frame. Then drag a region on an original screen.");
}
void StudioEditor::selectedRegion(int screen, const QRect& region)
{
    const bool overlay = selectingOverlay;
    auto screens = static_cast<StudioScreensWidget*>(preview);
    if (screen < 0 || screen > 1 || screens->images[screen].isNull() || !screens->images[screen].rect().contains(region)) return;
    if (overlay)
    {
        int row = outliner->indexOfTopLevelItem(outliner->currentItem()); if (row < 0) return;
        auto& e = document.elements[document.activeState][row];
        e.screen = screen; e.x = region.x(); e.y = region.y(); e.width = region.width(); e.height = region.height();
        document.dirty = true; refresh(row);
    }
    else
    {

        int state = document.activeState;
        auto name = QString("%1 reference %2").arg(document.sceneName(state)).arg(document.references[state].size()+1);
        document.references[state].append({name,screen,region,screens->images[screen].copy(region),0.96,true});
        document.dirty = true; resetRecognition(); refreshReferences(); applyPresentation();
    }
    teachingPending = false;
    selectionHint->setText(overlay ? "HUD source updated. Use HUD Layout to position and resize it." : "Reference saved in this game's profile. Adjust its threshold; capture alternatives as needed.");
    profile->setText("  " + document.gameLabel + " *");
}
