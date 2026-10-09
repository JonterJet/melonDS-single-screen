// SPDX-License-Identifier: GPL-3.0-or-later
#include "Window.h" // OpenGL declarations must precede Qt headers.
#include "StudioEditor.h"
#include "EmuInstance.h"
#include "StudioViews.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTimer>
#include <QToolBar>
#include <QTabWidget>
#include <QTransform>
#include <QToolButton>
#include <QStyle>
#include <QKeyEvent>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cstring>

QDockWidget* StudioEditor::dock(const QString& title, QWidget* content, int area)
{
    auto d = new QDockWidget(title, window);
    d->setObjectName("MelonStudio." + title);
    d->setWidget(content);
    window->addDockWidget(static_cast<Qt::DockWidgetArea>(area), d);
    docks.append(d);
    return d;
}

StudioEditor::StudioEditor(MainWindow* window) : QObject(window), window(window), profiles(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/MelonStudio/profiles")
{
    auto menu = window->menuBar()->addMenu("Studio");
    toolbar = new QToolBar("MelonStudio", window);
    toolbar->setObjectName("MelonStudio.Toolbar");
    toolbar->setMovable(false);
    window->addToolBar(toolbar);
    playAction = new QAction(window->style()->standardIcon(QStyle::SP_MediaPlay),"Play",this);
    auto playButton=new QToolButton(toolbar); playButton->setObjectName("StudioPlayButton");
    playButton->setDefaultAction(playAction); playButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    playButton->setFixedSize(120,32);
    playButton->setStyleSheet("QToolButton { background:#246548; color:white; font-weight:bold; border:1px solid #43866a; border-radius:4px; }");
    toolbar->setMinimumHeight(42); playButton->show();
    playAction->setCheckable(true);
    playAction->setToolTip("Enter fullscreen gameplay. Escape returns to the editor without restarting the game.");
    menu->addAction(playAction);
    connect(playAction, &QAction::toggled, this, &StudioEditor::setPlayMode);
    auto saveAction = toolbar->addAction("Save configuration");
    auto loadAction = toolbar->addAction("Load configuration");
    menu->addAction(saveAction);
    menu->addAction(loadAction);
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    connect(loadAction, &QAction::triggered, this, &StudioEditor::load);
    profile = new QLabel(toolbar);
    profile->setTextFormat(Qt::PlainText);
    auto spacer=new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); toolbar->addWidget(spacer);
    profile->setMaximumWidth(300); toolbar->addWidget(profile);
    auto resetAction=menu->addAction("Reset Editor Layout");
    connect(resetAction,&QAction::triggered,this,&StudioEditor::resetWorkspace);
    exitPlay=new QPushButton("Exit Play (Esc)",window); exitPlay->setObjectName("StudioExitPlay"); exitPlay->setAttribute(Qt::WA_NativeWindow); exitPlay->hide();
    exitTimer=new QTimer(this); exitTimer->setSingleShot(true); exitTimer->setInterval(2000);
    connect(exitTimer,&QTimer::timeout,exitPlay,&QWidget::hide);
    connect(exitPlay,&QPushButton::clicked,this,[this] { playAction->setChecked(false); });
    qApp->installEventFilter(this);
    QPalette dark=window->palette();
    dark.setColor(QPalette::Window,QColor(31,33,37)); dark.setColor(QPalette::WindowText,QColor(225,228,234));
    dark.setColor(QPalette::Base,QColor(23,25,29)); dark.setColor(QPalette::AlternateBase,QColor(38,41,46));
    dark.setColor(QPalette::Text,QColor(225,228,234)); dark.setColor(QPalette::Button,QColor(45,48,54));
    dark.setColor(QPalette::ButtonText,QColor(225,228,234)); dark.setColor(QPalette::Highlight,QColor(49,113,163));
    dark.setColor(QPalette::HighlightedText,Qt::white); qApp->setPalette(dark);
    window->setStyleSheet("QDockWidget::title { background:#292c32; padding:5px; } QTabBar::tab { padding:6px 8px; } QTreeView { border:1px solid #40444c; }");

    preview = new StudioScreensWidget;
    originals = dock("Original DS Screens", preview, Qt::LeftDockWidgetArea);

    auto outlineWidget = new QWidget;
    auto outlineLayout = new QVBoxLayout(outlineWidget);
    outliner = new QTreeWidget;
    outliner->setObjectName("StudioOutliner");
    outliner->setHeaderLabels({"HUD elements"});
    outliner->setSelectionMode(QAbstractItemView::SingleSelection);
    outlineLayout->addWidget(outliner);
    auto buttons = new QGridLayout;
    auto add = new QPushButton("Add HUD"); add->setObjectName("StudioAddHUD");
    auto remove = new QPushButton("Remove");
    auto up = new QPushButton("Up");
    auto down = new QPushButton("Down");
    int bi=0; for(auto b : {add,remove,up,down}) { buttons->addWidget(b,bi/3,bi%3); ++bi; }
    outlineLayout->addLayout(buttons);
    dock("Outliner", outlineWidget, Qt::RightDockWidgetArea);
    connect(outliner, &QTreeWidget::itemSelectionChanged, this, &StudioEditor::selectElement);
    connect(add,&QPushButton::clicked,this,[this] { addPolygon(); });
    auto copyHUD=new QPushButton("Duplicate"); copyHUD->setObjectName("StudioDuplicateHUD"); buttons->addWidget(copyHUD,1,1);
    connect(copyHUD,&QPushButton::clicked,this,[this] {
        int row=outliner->indexOfTopLevelItem(outliner->currentItem()); if(row<0) return;
        editElement(); auto& elements=document.elements[document.activeState];
        auto copy=elements[row]; copy.name=(copy.name+" copy").left(128); elements.insert(row+1,copy); document.dirty=true; refresh(row+1);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        int row = outliner->indexOfTopLevelItem(outliner->currentItem());
        if (row < 0) return;
        document.elements[document.activeState].removeAt(row);
        document.dirty = true;
        refresh(row);
    });
    auto move = [this](int offset) {
        int row = outliner->indexOfTopLevelItem(outliner->currentItem());
        auto& items = document.elements[document.activeState];
        if (row < 0 || row + offset < 0 || row + offset >= items.size()) return;
        items.move(row, row + offset);
        document.dirty = true;
        refresh(row + offset);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });

    inspector = new QWidget;
    auto form = new QFormLayout(inspector);
    name = new QLineEdit;
    name->setObjectName("StudioElementName");
    name->setMaxLength(128);
    source = new QComboBox;
    source->addItems({"Top screen", "Bottom screen"});
    enabled = new QCheckBox("Enabled in this scene");
    form->addRow("Name", name);
    form->addRow("Source", source);
    const QStringList labels{"X", "Y", "Width", "Height"};
    for (int i = 0; i < 4; ++i)
    {
        bounds[i] = new QSpinBox;
        bounds[i]->setObjectName("Studio" + labels[i]);
        bounds[i]->setRange(i < 2 ? 0 : 1, i % 2 == 0 ? (i == 0 ? 255 : 256) : (i == 1 ? 191 : 192));
        form->addRow(labels[i] + " (DS pixels)", bounds[i]);
        connect(bounds[i], &QSpinBox::valueChanged, this, &StudioEditor::editElement);
    }
    form->addRow(enabled);
    const QStringList destLabels{"Overlay X", "Overlay Y", "Overlay width", "Overlay height"};
    for (int i = 0; i < 4; ++i)
    {
        destination[i] = new QSpinBox;
        destination[i]->setObjectName(QString("StudioDestination%1").arg(i));
        destination[i]->setRange(i < 2 ? 0 : 1, i % 2 == 0 ? (i == 0 ? 255 : 256) : (i == 1 ? 191 : 192));
        form->addRow(destLabels[i] + " (DS pixels)", destination[i]);
        connect(destination[i], &QSpinBox::valueChanged, this, &StudioEditor::editElement);
    }
    auto cropButton = new QPushButton("Edit polygon mask");
    cropButton->setObjectName("StudioSelectOverlaySource");
    form->addRow(cropButton);
    connect(cropButton, &QPushButton::clicked, this, [this] { addPolygon(true); });
    auto note = new QLabel("Live polygon HUD: drag on the gameplay viewport or HUD Layout to move; drag the lower-right handle to scale. Source coordinates and mask scale together.");
    note->setWordWrap(true);
    form->addRow(note);
    auto inspectorScroll = new QScrollArea; inspectorScroll->setWidgetResizable(true); inspectorScroll->setWidget(inspector);
    dock("Inspector", inspectorScroll, Qt::RightDockWidgetArea);
    connect(name, &QLineEdit::editingFinished, this, &StudioEditor::editElement);
    connect(source, &QComboBox::currentIndexChanged, this, &StudioEditor::editElement);
    connect(enabled, &QCheckBox::toggled, this, &StudioEditor::editElement);

    auto sceneWidget = new QWidget;
    auto sceneLayout = new QVBoxLayout(sceneWidget);
    states = new QComboBox;
    states->setObjectName("StudioSceneStates");
    for (int i = 0; i < 3; ++i) states->addItem(document.sceneName(i), i);
    sceneLayout->addWidget(states);
    auto stateNote = new QLabel("Select a scene for manual override. Enable automatic recognition to resume detection.");
    stateNote->setWordWrap(true);
    states->setToolTip(stateNote->text()); stateNote->hide();
    auto profilesDock=dock("Game Profiles",sceneWidget,Qt::LeftDockWidgetArea);
    window->splitDockWidget(originals,profilesDock,Qt::Vertical);
    connect(states, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshing) return;
        editElement();
        document.activeState = states->itemData(index).toInt();
        if (document.automatic)
        {
            document.automatic = false;
            refreshing = true; automatic->setChecked(false); refreshing = false;
        }
        resetRecognition();
        document.dirty = true;
        refresh();
    });
    // Choosing the already-detected row is also an explicit manual override.
    connect(states, &QComboBox::activated, this, [this](int index) {
        if (refreshing || !document.automatic) return;
        automatic->setChecked(false);
        document.activeState = states->itemData(index).toInt();
        document.dirty = true; refresh();
    });
    initializeProfiles(sceneWidget);
    initializeSceneControls(sceneWidget);
    menu->addSeparator();
    for (auto d : docks) menu->addAction(d->toggleViewAction());
    window->setDockNestingEnabled(true);
    window->setTabPosition(Qt::AllDockWidgetAreas,QTabWidget::North);
    window->resize(1280,800);
    window->resizeDocks({originals,profilesDock},{300,220},Qt::Vertical);
    window->resizeDocks({originals,docks[1]},{250,310},Qt::Horizontal);
    defaultDockState=window->saveState(3);
    restoreWorkspace();

    recognitionClock.start();
    auto timer = new QTimer(this);
    timer->setInterval(100); // Bounded latest-frame mailbox; recognition continues in Play/fullscreen.
    connect(timer, &QTimer::timeout, this, &StudioEditor::tickScreens);
    timer->start();
    setGame(window->getEmuInstance()->getConsoleType() == 1 ? "firmware-dsi" : "firmware-ds", "Firmware / no cartridge");
}

QString StudioEditor::configurationPath() const
{
    return profiles.path(document.profileId);
}

void StudioEditor::showError(const QString& error)
{
    QMessageBox::warning(window, "MelonStudio configuration", error);
}

bool StudioEditor::save()
{
    editElement();
    editReference();
    QString error;
    if (!document.save(configurationPath(), error)) { showError(error); return false; }
    profile->setText("  " + document.gameLabel);
    profile->setToolTip(configurationPath());
    return true;
}

bool StudioEditor::saveOnClose()
{
    editElement();
    editReference();
    saveWorkspace();
    return !document.dirty || save();
}

void StudioEditor::load()
{
    editElement();
    if (document.dirty && QMessageBox::question(window, "Reload configuration",
        "Discard unsaved editor changes and reload this game's configuration?",
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    QString error;
    if (!document.load(configurationPath(), error)) { showError(error); return; }
    resetRecognition();
    refresh();
}

void StudioEditor::setGame(const QString& id,const QString& label)
{
    if(currentRom==id && document.gameId==id) return;
    if(!document.gameId.isEmpty() && !saveOnClose()) return;
    currentRom=id; currentRomLabel=label;
    StudioDocument candidate; QString error;
    QString legacy=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/MelonStudio/games/"+id+".json";
    if(!profiles.forRom(id,label,legacy,candidate,error)) { showError(error); return; }
    document=std::move(candidate); clearScreens(); refresh();
}

void StudioEditor::refresh(int selected)
{
    teachingPending = selectingOverlay = false;
    static_cast<StudioScreensWidget*>(preview)->selecting = false;
    refreshing = true;
    states->clear();
    int first = 0;
    int end = document.sceneCount();
    if (document.activeState < first || document.activeState >= end) document.activeState = first;
    for (int i = first; i < end; ++i) states->addItem(document.sceneName(i), i);
    states->setCurrentIndex(states->findData(document.activeState));
    outliner->clear();
    for (const auto& e : document.elements[document.activeState])
    {
        auto item = new QTreeWidgetItem(outliner, {e.name});
        item->setForeground(0, e.enabled ? window->palette().brush(QPalette::Text) : window->palette().brush(QPalette::Disabled, QPalette::Text));
    }
    if (outliner->topLevelItemCount() > 0)
        outliner->setCurrentItem(outliner->topLevelItem(qBound(0, selected, outliner->topLevelItemCount() - 1)));
    profile->setText("  " + document.gameLabel + (document.dirty ? " *" : ""));
    profile->setToolTip(configurationPath());
    refreshProfiles();
    refreshSceneControls();
    refreshing = false;
    selectElement();
    applyPresentation();
}

void StudioEditor::selectElement()
{
    if (refreshing) return;
    int row = outliner->indexOfTopLevelItem(outliner->currentItem());
    inspector->setEnabled(row >= 0);
    refreshing = true;
    if (row >= 0)
    {
        const auto& e = document.elements[document.activeState][row];
        name->setText(e.name);
        source->setCurrentIndex(e.screen);
        bounds[2]->setMaximum(256);
        bounds[3]->setMaximum(192);
        bounds[0]->setValue(e.x); bounds[1]->setValue(e.y);
        bounds[2]->setMaximum(256 - e.x); bounds[3]->setMaximum(192 - e.y);
        bounds[2]->setValue(e.width); bounds[3]->setValue(e.height);
        enabled->setChecked(e.enabled);
        destination[2]->setMaximum(256); destination[3]->setMaximum(192);
        destination[0]->setValue(e.destination.x()); destination[1]->setValue(e.destination.y());
        destination[2]->setMaximum(256-e.destination.x()); destination[3]->setMaximum(192-e.destination.y());
        destination[2]->setValue(e.destination.width()); destination[3]->setValue(e.destination.height());
        for (auto spin : destination) spin->setEnabled(document.sceneToolsEnabled);
    }
    else name->clear();
    hudCanvas->selected = row;
    hudCanvas->elements = document.elements[document.activeState];
    hudCanvas->editingEnabled = document.sceneToolsEnabled;
    hudCanvas->update();
    refreshing = false;
    applyPresentation();
}

void StudioEditor::editElement()
{
    if (refreshing) return;
    int row = outliner->indexOfTopLevelItem(outliner->currentItem());
    if (row < 0) return;
    auto& e = document.elements[document.activeState][row];
    auto text = name->text().trimmed();
    if (text.isEmpty()) text = e.name;
    StudioElement edited{text, source->currentIndex(), bounds[0]->value(), bounds[1]->value(),
        qMin(bounds[2]->value(), 256 - bounds[0]->value()),
        qMin(bounds[3]->value(), 192 - bounds[1]->value()), enabled->isChecked()};
    edited.polygon = e.polygon;
    edited.destination = QRect(destination[0]->value(), destination[1]->value(),
        qMin(destination[2]->value(),256-destination[0]->value()), qMin(destination[3]->value(),192-destination[1]->value()));
    if(!e.polygon.isEmpty() && e.sourceRect()!=edited.sourceRect()) {
        QTransform transform; transform.translate(edited.x,edited.y);
        transform.scale(double(edited.width)/e.width,double(edited.height)/e.height); transform.translate(-e.x,-e.y);
        edited.polygon=transform.map(e.polygon);
    }
    if (e.destination == edited.destination && e.name == edited.name && e.screen == edited.screen && e.x == edited.x && e.y == edited.y
        && e.width == edited.width && e.height == edited.height && e.enabled == edited.enabled) return;
    if (document.automatic)
    {
        document.automatic = false;
        refreshing = true; automatic->setChecked(false); refreshing = false;
        resetRecognition();
    }
    e = edited;
    document.dirty = true;
    outliner->currentItem()->setText(0, e.name);
    outliner->currentItem()->setForeground(0, e.enabled ? window->palette().brush(QPalette::Text) : window->palette().brush(QPalette::Disabled, QPalette::Text));
    profile->setText("  " + document.gameLabel + " *");
    selectElement();
    applyPresentation();
}

void StudioEditor::setPlayMode(bool play)
{
    if(play==playMode) return;
    if(viewportDrag) { viewportDrag=false; window->panel->releaseMouse(); }
    if(play) {
        editElement(); editReference(); saveWorkspace();
        editorState=window->saveState(3); editorGeometry=window->saveGeometry();
        editorMaximized=window->isMaximized(); playStartedFullscreen=window->isFullScreen();
        editorToolbarVisible=toolbar->isVisible(); playMode=true;
        for(auto dock : docks) { dock->hide(); dock->toggleViewAction()->setEnabled(false); }
        toolbar->hide(); playAction->setText("Exit Play");
        if(!playStartedFullscreen) window->toggleFullscreen();
        resetPlayMouse();
    } else {
        exitTimer->stop(); exitPlay->hide();
        if(!playStartedFullscreen && window->isFullScreen()) window->toggleFullscreen();
        window->restoreState(editorState,3);
        if(!playStartedFullscreen) { window->restoreGeometry(editorGeometry); if(editorMaximized) window->showMaximized(); }
        toolbar->setVisible(editorToolbarVisible); playMode=false;
        for(auto dock : docks) dock->toggleViewAction()->setEnabled(!window->isFullScreen());
        playAction->setText("Play");
    }
    applyPresentation();
    window->studioReleaseKeys();
    window->panel->setFocus(Qt::OtherFocusReason);
}

void StudioEditor::setFullscreen(bool full)
{
    if(playMode) return;
    if (full)
    {
        editElement();
        fullscreenVisibility.clear();
        for (auto d : docks)
        {
            fullscreenVisibility.append(d->isVisible());
            d->hide();
            d->toggleViewAction()->setEnabled(false);
        }
        fullscreenToolbarVisible = toolbar->isVisible();
        toolbar->hide();
        playAction->setEnabled(false);
    }
    else
    {
        toolbar->setVisible(fullscreenToolbarVisible);
        for (int i = 0; i < docks.size(); ++i)
        {
            docks[i]->setVisible(fullscreenVisibility.value(i, true));
            docks[i]->toggleViewAction()->setEnabled(!playMode);
        }
        playAction->setEnabled(true);
    }
    applyPresentation();
    window->panel->setFocus(Qt::OtherFocusReason);
}

bool StudioEditor::ownsFocus() const
{
    QWidget* focused = QApplication::focusWidget();
    if (!focused) focused = window->focusWidget();
    for (auto d : docks) if (focused && (d == focused || d->isAncestorOf(focused))) return true;
    return false;
}

void StudioEditor::clearScreens()
{
    if(viewportDrag) { viewportDrag=false; window->panel->releaseMouse(); }
    teachingPending = selectingOverlay = false;
    auto screens = static_cast<StudioScreensWidget*>(preview);
    screens->selecting = false;
    screens->images[0] = screens->images[1] = QImage();
    preview->update();
    QMutexLocker lock(&imageMutex);
    images[0] = QImage(); images[1] = QImage();
    ++imageSerial;
    resetRecognition();
}

void StudioEditor::captureScreens(void* top, void* bottom, bool software)
{
    if (!captureRequested.exchange(false)) return;
    QImage captured[2];
    if (software)
    {
        captured[0] = QImage(static_cast<uchar*>(top), 256, 192, QImage::Format_RGB32).copy();
        captured[1] = QImage(static_cast<uchar*>(bottom), 256, 192, QImage::Format_RGB32).copy();
    }
    else
    {
        // GLRenderer exposes an array texture. Read it on the render thread,
        // restoring bindings so the central viewport and renderer are unaffected.
        GLint oldTexture = 0, oldPackBuffer = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &oldTexture);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldPackBuffer);
        const GLenum parameters[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_IMAGE_HEIGHT,
            GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_IMAGES};
        GLint packValues[6];
        for (int i = 0; i < 6; ++i) { glGetIntegerv(parameters[i], &packValues[i]); glPixelStorei(parameters[i], i == 0 ? 4 : 0); }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, *static_cast<GLuint*>(top));
        GLint width = 0, height = 0, depth = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH, &depth);
        if (width > 0 && height > 0 && width <= 4096 && height <= 3072 && depth == 2)
        {
            QImage both(width, height * 2, QImage::Format_RGB32);
            if (!both.isNull())
            {
                glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_BGRA, GL_UNSIGNED_BYTE, both.bits());
                for (int i = 0; i < 2; ++i)
                    captured[i] = both.copy(0, height * i, width, height).scaled(256, 192, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            }
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, oldTexture);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, oldPackBuffer);
        for (int i = 0; i < 6; ++i) glPixelStorei(parameters[i], packValues[i]);
    }
    QMutexLocker lock(&imageMutex);
    images[0] = captured[0]; images[1] = captured[1];
    ++imageSerial;
}
