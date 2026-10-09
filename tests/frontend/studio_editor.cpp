// Exercises the actual Qt editor and persisted documents without game/BIOS assets.
#include "main.h"
#include "StudioEditor.h"
#include "StudioViews.h"
#include "StudioPolygon.h"
#include "StudioProfiles.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QTabWidget>
extern const char* kScreenVS;
extern const char* kScreenFS;
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMouseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QToolBar>
#include <QStyle>
#include <QMenu>
#include <QToolButton>
#include <QStackedWidget>
#include <QLibrary>
#include "StudioTree.h"
#include <iostream>
#include <stdexcept>
extern EmuInstance* emuInstances[];
static void require(bool value, const char* what)
{
    if (!value) throw std::runtime_error(what);
}
static void* glAddress(const char* name)
{
    return reinterpret_cast<void*>(QOpenGLContext::currentContext()->getProcAddress(name));
}
static void settle(int ms = 30)
{
    QElapsedTimer timer; timer.start();
    do { QApplication::processEvents(); QThread::msleep(5); } while (timer.elapsed() < ms);
}
// Test the production shader and overlay geometry against a live array texture.
static void checkGlOverlay(GLuint texture)
{
    auto shader = [](GLenum type, const char* source) {
        GLuint id = glCreateShader(type); glShaderSource(id,1,&source,nullptr); glCompileShader(id);
        GLint ok; glGetShaderiv(id,GL_COMPILE_STATUS,&ok); require(ok,"Overlay shader compile failed"); return id;
    };
    GLuint vs=shader(GL_VERTEX_SHADER,kScreenVS), fs=shader(GL_FRAGMENT_SHADER,kScreenFS);
    GLuint program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs);
    glBindAttribLocation(program,0,"vPosition"); glBindAttribLocation(program,1,"vTexcoord"); glLinkProgram(program);
    GLint ok; glGetProgramiv(program,GL_LINK_STATUS,&ok); require(ok,"Overlay shader link failed");
    glUseProgram(program); glUniform2f(glGetUniformLocation(program,"uScreenSize"),256,192);
    float matrix[]={1,0,0,1,0,0}; glUniformMatrix2x3fv(glGetUniformLocation(program,"uTransform"),1,GL_TRUE,matrix);
    glUniform1i(glGetUniformLocation(program,"ScreenTex"),0);
    glUniform1i(glGetUniformLocation(program,"StudioMask"),1);
    GLuint output,fbo,vao,vbo;
    glGenTextures(1,&output); glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,192,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Overlay framebuffer failed");
    glViewport(0,0,256,192); glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,5*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY,texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    StudioElement base; base.screen=0; base.x=base.y=0; base.width=256; base.height=192; base.destination=QRect(0,0,256,192);
    StudioElement overlay; overlay.screen=1; overlay.x=32; overlay.y=24; overlay.width=128; overlay.height=96;
    overlay.destination=QRect(168,120,80,60);
    auto draw=[&](const StudioElement& element) { auto v=studioOverlayVertices(element); glBufferData(GL_ARRAY_BUFFER,sizeof(v),v.data(),GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES,0,6); };
    auto pixel=[&](int x,int y) { unsigned char rgba[4]; glReadPixels(x,191-y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba); return QColor(rgba[0],rgba[1],rgba[2]); };
    draw(base); draw(overlay);
    require(pixel(180,140).blue()>240 && pixel(20,20).red()>240,"Live GL crop/scale output failed");
    QVector<quint32> green(256*192,0xff00ff00);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,1,256,192,1,GL_BGRA,GL_UNSIGNED_BYTE,green.constData());
    draw(base); draw(overlay); require(pixel(180,140).green()>240,"GL overlay did not update with live pixels");
    draw(base); require(pixel(180,140).red()>240,"Omitting overlay did not restore base");
    overlay.polygon=QPolygonF{QPointF(32,24),QPointF(160,24),QPointF(96,120)};
    auto mask=studioPolygonMask(overlay); GLuint maskTexture; glGenTextures(1,&maskTexture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,maskTexture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,256,192,0,GL_RED,GL_UNSIGNED_BYTE,mask.constBits());
    glActiveTexture(GL_TEXTURE0); glUniform1i(glGetUniformLocation(program,"StudioMask"),1);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    draw(base); glUniform1i(glGetUniformLocation(program,"uMasked"),1); draw(overlay);
    require(pixel(208,140).green()>240 && pixel(169,175).red()>240,"GL polygon alpha must preserve exterior after scaling");
    QVector<quint32> blue(256*192,0xff0000ff); glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,1,256,192,1,GL_BGRA,GL_UNSIGNED_BYTE,blue.constData());
    glUniform1i(glGetUniformLocation(program,"uMasked"),0); draw(base);
    glUniform1i(glGetUniformLocation(program,"uMasked"),1); draw(overlay);
    require(pixel(208,140).blue()>240 && pixel(169,175).red()>240,"Masked GL pixels must remain live");
    glDisable(GL_BLEND); glDeleteTextures(1,&maskTexture);

    glBindFramebuffer(GL_FRAMEBUFFER,0); glUseProgram(0); glBindVertexArray(0);
    glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&output);
    glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
}
// An actual X11 drag reaches QDrag::exec and its platform drop lifecycle. Sending
// QWidget mouse events alone does not exercise Qt's source-side drag cleanup.
static void dragTreeGesture(QTreeWidget* tree,QTreeWidgetItem* source,QTreeWidgetItem* target)
{
    QLibrary x11("libX11.so.6"), xtst("libXtst.so.6");
    auto open=reinterpret_cast<void*(*)(const char*)>(x11.resolve("XOpenDisplay"));
    auto close=reinterpret_cast<int(*)(void*)>(x11.resolve("XCloseDisplay"));
    auto flush=reinterpret_cast<int(*)(void*)>(x11.resolve("XFlush"));
    auto motion=reinterpret_cast<int(*)(void*,int,int,int,unsigned long)>(xtst.resolve("XTestFakeMotionEvent"));
    auto button=reinterpret_cast<int(*)(void*,unsigned int,int,unsigned long)>(xtst.resolve("XTestFakeButtonEvent"));
    require(open && close && flush && motion && button,"X11/XTest required for real drag gesture test");
    auto display=open(nullptr); require(display,"Open X display for drag");
    tree->setCurrentItem(source); tree->scrollToItem(source); settle();
    if(!tree->viewport()->rect().contains(tree->visualItemRect(target).center())) { tree->scrollToItem(target); settle(); }
    require(tree->viewport()->rect().contains(tree->visualItemRect(source).center()) && tree->viewport()->rect().contains(tree->visualItemRect(target).center()),"Both drag items must be visible");
    QPoint from=tree->viewport()->mapToGlobal(tree->visualItemRect(source).center());
    auto targetRect=tree->visualItemRect(target); QPoint to=tree->viewport()->mapToGlobal(QPoint(targetRect.center().x(),targetRect.top()+1));
    motion(display,-1,from.x(),from.y(),0); flush(display); settle(30);
    button(display,1,1,0); flush(display); settle(30);
    // A queued move/release runs even inside QDrag's nested event loop.
    QTimer::singleShot(80,[=] { motion(display,-1,to.x(),to.y(),0); flush(display); });
    QTimer::singleShot(250,[=] { button(display,1,0,0); flush(display); });
    motion(display,-1,from.x()+20,from.y()+5,0); flush(display); settle(400);
    close(display);
}
static void drag(QWidget* widget, QPoint from, QPoint to)
{
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(from),QPointF(from),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove,QPointF(to),QPointF(to),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(to),QPointF(to),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(widget,&press); QApplication::sendEvent(widget,&move); QApplication::sendEvent(widget,&release);
}
int main(int argc, char** argv)
{
    QTemporaryDir temporary;
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("MelonStudio-tests");
    app.setQuitOnLastWindowClosed(false);
    emuDirectory = temporary.path();
    QString theme = QApplication::style()->objectName();
    systemThemeName = &theme;
    sysTimer.start();
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) == 0, SDL_GetError());
    const bool dsi = argc > 1 && std::string(argv[1]) == "--dsi";
    Config::GetGlobalTable().SetInt("Emu.ConsoleType", dsi ? 1 : 0);
    Config::GetLocalTable(0).SetInt("Window0.ScreenSizing", screenSizing_TopOnly);
    setMPInterface(melonDS::MPInterface_Local);
    require(createEmuInstance(), "createEmuInstance failed");
    auto inst = emuInstances[0];
    auto window = inst->getMainWindow();
    int result = 0;
    try
    {
        settle();
        auto editor = window->findChild<StudioEditor*>();
        require(editor && window->centralWidget() == window->panel, "Live central viewport missing");
        auto docks = window->findChildren<QDockWidget*>();
        require(docks.size() == 7, "Expected editor and Mario Kart tool panels");
        auto outline = window->findChild<QTreeWidget*>("StudioOutliner");
        auto name = window->findChild<QLineEdit*>("StudioElementName");
        auto hierarchy=window->findChild<QTreeWidget*>("StudioProfiles");
        struct SceneSelection {
            QTreeWidget* tree;
            QTreeWidgetItem* root() const { auto i=tree->currentItem(); return i->parent() ? i->parent() : i; }
            int count() const { return root()->childCount(); }
            int currentIndex() const { return tree->currentItem()->data(0,Qt::UserRole+1).toInt(); }
            QString currentText() const { return tree->currentItem()->text(0); }
            QVariant currentData() const { return tree->currentItem()->data(0,Qt::UserRole+1); }
            int findData(int i) const { return i; }
            void setCurrentIndex(int i) { tree->setCurrentItem(root()->child(i)); settle(); }
        } sceneSelection{hierarchy};
        auto states=&sceneSelection;
        require(!window->findChild<QComboBox*>("StudioSceneStates") && !window->findChild<QComboBox*>("StudioReferenceState"),"Redundant scene dropdowns must be removed");
        auto x = window->findChild<QSpinBox*>("StudioX");
        auto width = window->findChild<QSpinBox*>("StudioWidth");
        auto click = [&](const QString& text) {
            for (auto b : window->findChildren<QPushButton*>()) if (b->text() == (text=="Add" ? "Add live bottom-screen map to selected scene" : text) || b->objectName()==(text=="Up" ? "StudioWidgetUp" : text=="Remove" ? "StudioRemoveWidget" : "none")) { b->click(); return; }
            throw std::runtime_error("Editor button missing");
        };
        editor->setGame("test-game-a", "Test Game A");
        click("Add");
        require(outline->topLevelItemCount() == 1, "Add element failed");
        name->setText("Health");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        x->setValue(250);
        require(width->value() == 6, "Inspector must clamp source bounds to DS screen");
        click("Add");
        name->setText("Map");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        click("Up");
        require(outline->topLevelItem(0)->text(0) == "Map", "Outliner ordering failed");
        states->setCurrentIndex(1);
        require(outline->topLevelItemCount() == 0, "Scenes must have separate elements");
        click("Add");
        name->setText("Menu title");
        QMetaObject::invokeMethod(name, "editingFinished", Qt::DirectConnection);
        require(editor->saveOnClose(), "Saving profile failed");
        QString firstPath = editor->configurationPath();
        require(QFile::exists(firstPath), "Profile was not saved");
        editor->setGame("test-game-b", "Test Game B");
        require(outline->topLevelItemCount() == 0 && editor->configurationPath() != firstPath, "Per-game isolation failed");
        click("Add"); click("Remove");
        require(outline->topLevelItemCount() == 0, "Remove failed");
        editor->setGame("test-game-a", "Test Game A");
        require(states->currentIndex() == 1 && outline->topLevelItem(0)->text(0) == "Menu title", "Automatic profile reload failed");
        states->setCurrentIndex(0);
        require(outline->topLevelItemCount() == 2 && outline->topLevelItem(1)->text(0) == "Health", "Scene metadata round-trip failed");
        require(x->maximum() == 255, "Inspector bounds incorrect");
        auto actions = window->findChildren<QAction*>();
        QAction* play = nullptr;
        for (auto a : actions) if (a->text() == "Play") play = a;
        require(play, "Play toggle missing");
        docks[0]->hide();
        play->setChecked(true);
        require(editor->isPlayMode(), "Play mode failed");
        for (auto d : docks) require(!d->isVisible(), "Play mode must hide all editor panels");
        play->setChecked(false);
        require(!docks[0]->isVisible() && docks[1]->isVisible(), "Editor mode must restore dock visibility");
        auto toolbar = window->findChild<QToolBar*>("MelonStudio.Toolbar");
        auto playButton=window->findChild<QWidget*>("StudioTransport");
        require(playButton && std::abs(playButton->geometry().center().x()-toolbar->width()/2)<=1,"Transport controls must be centered");
        window->toggleFullscreen(); settle();
        require(window->isFullScreen() && !toolbar->isVisible(), "Fullscreen must hide the editor toolbar");
        for (auto d : docks) require(!d->isVisible(), "Fullscreen must hide all editor panels");
        require(window->panel->size() == window->contentsRect().size(), "Fullscreen viewport must fill the window");
        window->toggleFullscreen(); settle();
        require(!window->isFullScreen() && toolbar->isVisible() && !docks[0]->isVisible()
            && docks[1]->isVisible(), "Leaving fullscreen must restore editor visibility");
        play->setChecked(true); settle();
        require(window->isFullScreen() && !toolbar->isVisible(),"Play must enter clean fullscreen");
        auto exit=window->findChild<QPushButton*>("StudioExitPlay");
        require(exit && !exit->isVisible() && window->panel->cursor().shape()==Qt::BlankCursor,"Exit and viewport cursor must start hidden");
        QKeyEvent ordinary(QEvent::KeyPress,Qt::Key_A,Qt::NoModifier);
        QApplication::sendEvent(window->panel,&ordinary); settle();
        require(!exit->isVisible(),"Keyboard/controller-style input must not show Exit");
        QMouseEvent synthesized(QEvent::MouseMove,QPointF(20,20),QPointF(20,20),QPointF(QCursor::pos()+QPoint(3,4)),Qt::NoButton,Qt::NoButton,Qt::NoModifier,Qt::MouseEventSynthesizedByApplication);
        QApplication::sendEvent(window->panel,&synthesized); settle();
        require(!exit->isVisible(),"Synthesized mouse events must not show Exit");
        auto mouseGlobal=QCursor::pos()+QPoint(31,27);
        QMouseEvent mouse(QEvent::MouseMove,QPointF(20,20),QPointF(mouseGlobal),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(window->panel,&mouse); settle();
        require(exit->isVisible(),"Mouse movement must reveal Exit Play"); settle(2150);
        require(!exit->isVisible() && window->panel->cursor().shape()==Qt::BlankCursor,"Exit and cursor must hide after idle timeout");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier), escapeUp(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(window->panel,&escape); QApplication::sendEvent(window->panel,&escapeUp); settle();
        require(!editor->isPlayMode() && !window->isFullScreen() && toolbar->isVisible(),"Escape must restore editor");
        play->setChecked(true); settle();
        QMetaObject::invokeMethod(window,"onFullscreenToggled",Qt::DirectConnection); settle();
        require(!editor->isPlayMode() && !window->isFullScreen() && toolbar->isVisible(),"Fullscreen hotkey must safely restore editor from Play");
        docks[0]->show();
        settle(220);
        QVector<quint32> top(256 * 192, 0xffff0000), bottom(256 * 192, 0xff0000ff);
        editor->captureScreens(top.data(), bottom.data(), true);
        settle(220);
        QWidget* preview = nullptr;
        for (auto d : docks) if (d->windowTitle() == "Original DS Screens") preview = d->widget();
        require(preview, "Original screens preview missing");
        auto snapshot = preview->grab().toImage();
        require(snapshot.pixelColor(snapshot.width()/2, snapshot.height()/4).red() > 240,
            "Original top screen preview missing");
        require(snapshot.pixelColor(snapshot.width()/2, snapshot.height()*3/4).blue() > 240,
            "Original bottom screen preview missing");
        // Exercise the same array-texture interface exposed by GLRenderer.
        QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
        QOpenGLContext context; context.setFormat(format);
        require(context.create(), "OpenGL preview test context failed");
        QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
        require(context.makeCurrent(&surface) && gladLoadGLLoader(glAddress), "OpenGL preview context unavailable");
        GLuint texture;
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        QVector<quint32> layers = top; layers += bottom;
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256, 192, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, layers.constData());
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glPixelStorei(GL_PACK_ROW_LENGTH, 272); glPixelStorei(GL_PACK_SKIP_ROWS, 3);
        settle(220);
        editor->captureScreens(&texture, nullptr, false);
        GLint restored = -1; glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &restored);
        require(restored == 0, "Preview must restore the texture binding");
        glGetIntegerv(GL_PACK_ROW_LENGTH, &restored);
        require(restored == 272, "Preview must restore pixel pack state");
        glGetIntegerv(GL_PACK_SKIP_ROWS, &restored);
        require(restored == 3, "Preview must restore pixel pack offsets");
        glPixelStorei(GL_PACK_ROW_LENGTH, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        checkGlOverlay(texture);
        glDeleteTextures(1, &texture); context.doneCurrent();
        settle(220); snapshot = preview->grab().toImage();
        require(snapshot.pixelColor(snapshot.width()/2, snapshot.height()/4).red() > 240
            && snapshot.pixelColor(snapshot.width()/2, snapshot.height()*3/4).blue() > 240,
            "OpenGL original-screen readback failed");
        if (qEnvironmentVariableIsSet("MELONSTUDIO_TEST_SCREENSHOT"))
            window->grab().save(qEnvironmentVariable("MELONSTUDIO_TEST_SCREENSHOT"));
        require(window->getWindowConfig().GetInt("ScreenSizing") == screenSizing_TopOnly,
            "Editor must preserve Top only");
        StudioDocument loaded;
        loaded.gameId = "test-game-a"; loaded.gameLabel = "Test Game A";
        QString error;
        require(editor->saveOnClose() && loaded.load(firstPath, error), "Document round-trip failed");
        auto before = loaded.toJson();
        auto invalid = before;
        auto invalidStates = invalid["scenes"].toArray();
        auto state = invalidStates[0].toObject();
        auto elements = state["elements"].toArray();
        auto element = elements[0].toObject(); element["width"] = 999;
        elements[0] = element; state["elements"] = elements; invalidStates[0] = state; invalid["scenes"] = invalidStates;
        QString badPath = temporary.path() + "/bad.json";
        QFile bad(badPath); require(bad.open(QIODevice::WriteOnly), "Bad fixture write failed");
        bad.write(QJsonDocument(invalid).toJson()); bad.close();
        require(!loaded.load(badPath, error) && loaded.toJson() == before,
            "Malformed configuration must leave current document intact");
        StudioDocument other; other.gameId = "another-game";
        require(!other.load(firstPath, error), "Cross-game configuration must be rejected");
        // Unhandled editor keystrokes must not reach the emulator hotkey handler.
        Config::GetLocalTable(0).SetInt("Keyboard.HK_RevealBottomScreen", Qt::Key_F9);
        inst->inputLoadConfig();
        name->setFocus(); settle();
        QKeyEvent press(QEvent::KeyPress, Qt::Key_F9, Qt::NoModifier);
        QApplication::sendEvent(window, &press); settle();
        require(!inst->getEmuThread()->isBottomScreenRevealed(), "Inspector typing must not trigger gameplay hotkeys");
        require(window->tabPosition(Qt::RightDockWidgetArea)==QTabWidget::North,"Dock tabs must be at the top");
        auto tree=window->findChild<QTreeWidget*>("StudioProfiles"); require(tree,"Profiles hierarchy missing");
        auto button=[&](const char* id) { auto b=window->findChild<QPushButton*>(id); require(b,"Profile button missing"); b->click(); settle(); };
        auto inlineName=[&](QTreeWidget* tree,const QString& text,bool cancel=false) {
            auto field=tree->findChild<QLineEdit*>(); require(field && field->isVisible(),"Expected inline name editor"); field->setText(text);
            QKeyEvent key(QEvent::KeyPress,cancel ? Qt::Key_Escape : Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(field,&key); settle();
        };
        auto contextMenu=[&](QTreeWidget* tree,const QString& command) {
            QTimer::singleShot(20,[command] {
                auto menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); require(menu,"Expected hierarchy context menu");
                bool found=false; for(auto action:menu->actions()) if(action->text()==command) { require(action->isEnabled(),"Context action disabled"); action->trigger(); found=true; break; }
                require(found,"Context action missing"); menu->close();
            });
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(tree->currentItem()).center())); settle();
        };
        button("StudioNewProfile"); inlineName(tree,"Custom racing profile");
        require(states->count()==3 && outline->topLevelItemCount()==0,"New profile must have independent scenes");
        button("StudioAddScene"); inlineName(tree,"Credits custom scene");
        int customRow=states->currentIndex(); require(states->currentText()=="Credits custom scene","Create custom scene");
        auto doubleClickName=[&](QTreeWidget* tree) {
            auto point=tree->visualItemRect(tree->currentItem()).center();
            QMouseEvent event(QEvent::MouseButtonDblClick,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),QPointF(tree->viewport()->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&press); QApplication::sendEvent(tree->viewport(),&release);
            QApplication::sendEvent(tree->viewport(),&event); QApplication::sendEvent(tree->viewport(),&release); settle();
        };
        doubleClickName(tree); inlineName(tree,"Renamed credits"); require(states->currentText()=="Renamed credits","Scene double-click inline rename UI");
        contextMenu(tree,"Rename"); inlineName(tree,"Cancelled name",true); require(states->currentText()=="Renamed credits","Escape cancels rename");
        contextMenu(tree,"Duplicate"); require(states->count()==5,"Scene duplicate UI");
        button("StudioProfileUp"); require(states->currentIndex()==customRow,"Scene reorder UI");
        contextMenu(tree,"Copy"); contextMenu(tree,"Paste"); require(states->count()==6,"Scene clipboard paste UI");
        QTimer::singleShot(60,[] { auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); require(box,"Expected delete dialog"); box->button(QMessageBox::Yes)->click(); });
        button("StudioDeleteItem"); require(states->count()==5,"Scene delete UI");
        tree->setCurrentItem(tree->currentItem()->parent()); settle();
        require(window->findChild<QLineEdit*>("StudioProfileName")->isVisible() && !name->isVisible(),"Profile selection must show profile Inspector");
        contextMenu(tree,"Duplicate"); contextMenu(tree,"Rename"); inlineName(tree,"Renamed profile");
        require(editor->saveOnClose(),"Named profile save");
        StudioDocument named; require(named.load(editor->configurationPath(),error) && named.gameLabel=="Renamed profile","Named profile persistence");
        QTimer::singleShot(60,[] { auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); require(box,"Expected profile delete dialog"); box->button(QMessageBox::Yes)->click(); });
        button("StudioDeleteItem");
        // Widget menus and drag ordering invoke production handlers, not a model-only stand-in.
        button("StudioNewProfile"); inlineName(tree,"Widget operations profile");
        states->setCurrentIndex(0); click("Add");
        doubleClickName(outline); inlineName(outline,"Inline widget");
        require(outline->currentItem()->text(0)=="Inline widget","Widget inline rename");
        require(editor->saveOnClose(),"Widget identity save");
        StudioDocument widgetBefore; require(widgetBefore.load(editor->configurationPath(),error),"Read widget identity");
        auto originalWidgetId=widgetBefore.elements[0][0].id;
        contextMenu(outline,"Duplicate"); contextMenu(outline,"Copy"); contextMenu(outline,"Paste");
        require(outline->topLevelItemCount()==3,"Widget independent duplicate/copy/paste");
        contextMenu(outline,"Hide/Show");
        StudioDocument widgetAfter; require(widgetAfter.load(editor->configurationPath(),error),"Read pasted widgets");
        require(widgetAfter.elements[0][0].id==originalWidgetId && widgetAfter.elements[0][1].id!=originalWidgetId && widgetAfter.elements[0][2].id!=widgetAfter.elements[0][1].id,"Widget copies must have independent IDs");
        require(!widgetAfter.elements[0][2].enabled,"Hide widget must persist without stale Inspector overwrites");
        auto widgets=static_cast<StudioTree*>(outline); auto lastId=outline->topLevelItem(2)->data(0,Qt::UserRole).toString();
        dragTreeGesture(outline,outline->topLevelItem(2),outline->topLevelItem(0));
        require(outline->topLevelItemCount()==3,"Real widget drag must not delete the moved item");
        require(outline->topLevelItem(0)->data(0,Qt::UserRole).toString()==lastId,"Widget drag order UI refresh");
        require(widgetAfter.load(editor->configurationPath(),error) && widgetAfter.elements[0][0].id==lastId,"Widget drag order must persist");
        contextMenu(outline,"Delete"); require(outline->topLevelItemCount()==2,"Widget menu deletion");
        auto profilesTree=static_cast<StudioTree*>(tree); auto sceneRoot=tree->currentItem()->parent();
        auto sceneLastId=sceneRoot->child(sceneRoot->childCount()-1)->data(0,Qt::UserRole+2).toString();
        require(!profilesTree->moveRelative(sceneRoot->child(0),tree->topLevelItem(0),true),"Drag must reject cross-profile ownership changes");
        dragTreeGesture(tree,sceneRoot->child(sceneRoot->childCount()-1),sceneRoot->child(0));
        require(states->count()==3,"Real scene drag must not delete the moved scene");
        require(named.load(editor->configurationPath(),error) && named.sceneIds[0]==sceneLastId,"Scene drag ordering must preserve identities and persist");
        tree->setCurrentItem(tree->currentItem()->parent()); settle();
        auto profileId=tree->currentItem()->data(0,Qt::UserRole).toString();
        if(tree->currentItem()!=tree->topLevelItem(0)) {
            int count=tree->topLevelItemCount(); dragTreeGesture(tree,tree->currentItem(),tree->topLevelItem(0));
            require(tree->topLevelItemCount()==count,"Real profile drag must not remove the source profile");
        }
        StudioProfiles orderedStore(QFileInfo(editor->configurationPath()).absolutePath());
        require(orderedStore.list().first().id==profileId,"Profile drag order persistence");
        // Vertex tracing uses native source coordinates independent of widget scale.
        StudioPolygon polygon; polygon.resize(800,600); polygon.image=QImage(256,192,QImage::Format_RGB32); polygon.image.fill(Qt::blue);
        auto polygonRect=polygon.canvasRect();
        auto polygonPoint=[&](int x,int y) { return polygonRect.topLeft()+QPoint(x*polygonRect.width()/256,y*polygonRect.height()/192); };
        auto clickPoint=[&](QPoint p) { drag(&polygon,p,p); };
        clickPoint(polygonPoint(20,20)); clickPoint(polygonPoint(200,20)); clickPoint(polygonPoint(120,170));
        require(polygon.valid() && polygon.points.size()==3,"Trace triangle vertices");
        auto beforeVertex=polygon.points[0]; drag(&polygon,polygonPoint(20,20),polygonPoint(30,30));
        require(polygon.points[0]!=beforeVertex,"Move polygon vertex");
        polygon.undo(); require(polygon.points.size()==2 && !polygon.valid(),"Undo polygon point");
        clickPoint(polygonPoint(120,170)); bool confirmed=false; polygon.confirm=[&] { confirmed=true; };
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(&polygon,&enter);
        require(confirmed,"Enter polygon confirmation");
        // The no-ROM test feeds synthetic frames through the real editor mailbox.
        // Pause/capture of a running Mario Kart ROM remains a manual check.
        editor->setGame("mkds-synthetic", "Mario Kart DS synthetic test");
        auto mario=window->findChild<QCheckBox*>("StudioSceneToolsEnabled");
        auto automatic=window->findChild<QCheckBox*>("StudioAutomaticRecognition");
        auto references=window->findChild<QListWidget*>("StudioReferences");
        auto debug=window->findChild<QLabel*>("StudioRecognitionDebug");
        auto screens=static_cast<StudioScreensWidget*>(preview);
        auto canvas=window->findChild<QWidget*>("StudioHudCanvas");
        auto hud=static_cast<StudioHudCanvas*>(canvas);
        // Seed arbitrary user scenes rather than depending on a fixed scene list.
        StudioDocument seed; seed.gameId="mkds-synthetic"; seed.gameLabel="Mario Kart DS synthetic test";
        while(seed.sceneCount()<8) seed.addScene(StudioDocument::legacyStateName(seed.sceneCount()));
        seed.activeState=StudioLegacyRacing; seed.layouts[StudioLegacyRacing]=StudioLayout::Top; seed.sceneToolsEnabled=true;
        StudioDocument originalSeed; originalSeed.gameId="mkds-synthetic"; require(originalSeed.load(editor->configurationPath(),error),"Read seed ID"); seed.profileId=originalSeed.profileId;
        require(seed.save(editor->configurationPath(),error),"Seed custom profile");
        editor->setGame("temporary-profile","Temporary");
        editor->setGame("mkds-synthetic","Mario Kart DS synthetic test");
        mario->setChecked(true);
        require(states->count()==8 && states->currentData().toInt()==StudioLegacyRacing,"Dynamic scenes missing");
        auto feed=[&](quint32 color,int ms) {
            QVector<quint32> pixels(256*192,color); QElapsedTimer timer; timer.start();
            do { editor->captureScreens(pixels.data(),bottom.data(),true); settle(110); } while(timer.elapsed()<ms);
        };
        auto teach=[&](int state,quint32 color,int screen) {
            states->setCurrentIndex(states->findData(state)); require(states->currentData().toInt()==state,"Recognition must follow hierarchy scene"); feed(color,220);
            screens->selecting=true; auto r=screens->screenRect(screen);
            drag(screens,r.topLeft()+QPoint(r.width()/4,r.height()/4),r.topLeft()+QPoint(r.width()/2,r.height()/2));
            require(references->count()>0,"Region teaching failed");
        };
        teach(StudioLegacyMarioFirst,0xffff0000,0); teach(StudioLegacyMarioFirst,0xffffff00,0);
        require(references->count()==2,"Multiple alternatives missing");
        teach(StudioLegacyRacing,0xff00ff00,0);
        auto threshold=window->findChild<QDoubleSpinBox*>("StudioReferenceThreshold"); threshold->setValue(99);
        auto layout=window->findChild<QComboBox*>("StudioSceneLayout");
        states->setCurrentIndex(states->findData(StudioLegacyMarioFirst)); layout->setCurrentIndex(int(StudioLayout::Bottom));
        states->setCurrentIndex(states->findData(StudioLegacyRacing));
        window->findChild<QPushButton*>("StudioAddLiveMap")->click();
        require(outline->topLevelItemCount()==1,"Live map missing from racing Outliner");
        require(hud->elements[0].screen==1 && hud->elements[0].width==256,"Map must use live bottom screen");
        // Move and resize via actual mouse events; destination coordinates persist.
        hud->resize(528,400); auto cr=hud->canvasRect();
        auto point=[&](int x,int y) { return cr.topLeft()+QPoint(x*cr.width()/256,y*cr.height()/192); };
        drag(hud,point(180,130),point(140,110));
        require(hud->elements[0].destination.x()<168,"HUD canvas move failed");
        auto destinationBefore=hud->elements[0].destination;
        drag(hud,point(destinationBefore.right()-2,destinationBefore.bottom()-2),point(destinationBefore.right()-15,destinationBefore.bottom()-10));
        require(hud->elements[0].destination.width()<destinationBefore.width(),"HUD canvas resize failed");
        auto transform=window->panel->studioTransform(); auto viewportBefore=hud->elements[0].destination;
        QPoint from=transform.map(QPointF(viewportBefore.center())).toPoint();
        QPoint to=transform.map(QPointF(viewportBefore.center()+QPoint(-10,-8))).toPoint();
        drag(window->panel,from,to);
        require(hud->elements[0].destination.x()<viewportBefore.x(),"Gameplay viewport HUD drag failed");
        auto viewportMoved=hud->elements[0].destination;
        drag(window->panel,transform.map(QPointF(viewportMoved.bottomRight()-QPoint(2,2))).toPoint(),transform.map(QPointF(viewportMoved.bottomRight()-QPoint(12,10))).toPoint());
        require(hud->elements[0].destination.width()<viewportMoved.width(),"Gameplay viewport transform handle failed");
        automatic->setChecked(true); feed(0xff00ff00,700);
        require(automatic->isChecked() && debug->text().contains("Active: Racing"),"Automatic racing confirmation failed");
        require(window->panel->minimumSize()==QSize(256,192),"Racing top-screen override failed");
        states->setCurrentIndex(StudioLegacyMarioFirst); feed(0xff00ff00,400);
        require(automatic->isChecked() && states->currentIndex()==StudioLegacyMarioFirst && debug->text().contains("Active: Racing"),"Runtime detection must not steal editing selection");
        require(references->count()==2 && outline->topLevelItemCount()==0,"All scene tools must follow editing selection");
        states->setCurrentIndex(StudioLegacyRacing);
        window->findChild<QPushButton*>("StudioManualOverride")->click();
        require(!automatic->isChecked(),"Choosing current scene must manually override detection");
        automatic->setChecked(true); feed(0xff00ff00,700);
        play->setChecked(true); settle(); feed(0xffff0000,700);
        require(debug->text().contains("Active: Main menus"),"Recognition must work in fullscreen Play mode");
        feed(0xff0000ff,700);
        require(debug->text().contains("Active: Unknown - fallback layout"),"Unknown must use fallback");
        play->setChecked(false); settle();
        states->setCurrentIndex(states->findData(StudioLegacyRacing));
        require(automatic->isChecked(),"Editor selection must preserve automatic recognition");
        window->findChild<QPushButton*>("StudioManualOverride")->click();
        require(!automatic->isChecked() && debug->text().contains("Manual override"),"Explicit preview must override detection");
        require(outline->topLevelItemCount()==1,"Scene map was not restored");
        require(editor->saveOnClose(),"Mario profile save failed");
        QString marioPath=editor->configurationPath(); StudioDocument marioDocument; marioDocument.gameId="mkds-synthetic";
        require(marioDocument.load(marioPath,error) && marioDocument.references[StudioLegacyMarioFirst].size()==2
            && marioDocument.references[StudioLegacyRacing][0].threshold==0.99
            && marioDocument.elements[StudioLegacyRacing][0].destination==hud->elements[0].destination,
            "References/settings/live layouts failed persistence");
        screens->selecting=true;
        editor->setGame("other-mkds-profile","Other profile");
        require(!screens->selecting && screens->images[0].isNull(),"Switching games must cancel stale region selection");
        require(outline->topLevelItemCount()==0,"HUD leaked into another game");
        editor->setGame("mkds-synthetic","Mario Kart DS synthetic test");
        require(mario->isChecked() && outline->topLevelItemCount()==1,"Mario profile reload failed");
        if (qEnvironmentVariableIsSet("MELONSTUDIO_TEST_SCREENSHOT"))
        {
            feed(0xff00ff00,220);
            for (auto dock : docks) if (dock->windowTitle()=="Recognition Rules") { dock->show(); dock->raise(); }
            settle(); window->grab().save(qEnvironmentVariable("MELONSTUDIO_TEST_SCREENSHOT")+"-mkds.png");
        }
        std::cout << "Synthetic Mario Kart editor teaching, alternatives, automatic/fullscreen states, unknown fallback, manual override, HUD move/resize, profiles and live GL overlay pixels passed\n";
        QDockWidget* originalDock=nullptr; QDockWidget* outlinerDock=nullptr;
        for(auto dock : docks) { if(dock->windowTitle()=="Original DS Screens") originalDock=dock; if(dock->windowTitle()=="Outliner") outlinerDock=dock; }
        require(originalDock && outlinerDock,"Workspace docks missing");
        QDockWidget* inspectorDock=nullptr;
        for(auto dock:docks) if(dock->windowTitle()=="Inspector") inspectorDock=dock;
        window->resize(1180,850); window->resizeDocks({originalDock,inspectorDock},{330,310},Qt::Horizontal);
        inspectorDock->raise(); settle(150); int originalWidth=originalDock->width(), inspectorWidth=inspectorDock->width();
        require(editor->saveOnClose(),"Docked workspace save");
        deleteAllEmuInstances(); require(createEmuInstance(),"Reopen docked workspace");
        inst=emuInstances[0]; window=inst->getMainWindow(); editor=window->findChild<StudioEditor*>(); settle(150);
        docks=window->findChildren<QDockWidget*>(); originalDock=nullptr; outlinerDock=nullptr; inspectorDock=nullptr;
        for(auto dock:docks) { if(dock->windowTitle()=="Original DS Screens") originalDock=dock; if(dock->windowTitle()=="Outliner") outlinerDock=dock; if(dock->windowTitle()=="Inspector") inspectorDock=dock; }
        require(!originalDock->isFloating() && std::abs(originalDock->width()-originalWidth)<=2 && std::abs(inspectorDock->width()-inspectorWidth)<=2,
            "Docked panel widths/splitter proportions must survive reopening");
        require(window->tabifiedDockWidgets(inspectorDock).size()==3 && !inspectorDock->visibleRegion().isEmpty(),"Tab grouping and active tab persistence");
        originalDock->setFloating(true); originalDock->resize(370,520); originalDock->move(50,60); outlinerDock->hide();
        window->resize(1180,850); settle(100); auto savedSize=window->size(), floatingSize=originalDock->size();
        require(editor->saveOnClose(),"Workspace save failed");
        deleteAllEmuInstances(); require(createEmuInstance(),"Reopen workspace failed");
        inst=emuInstances[0]; window=inst->getMainWindow(); editor=window->findChild<StudioEditor*>(); settle(150);
        docks=window->findChildren<QDockWidget*>(); originalDock=nullptr; outlinerDock=nullptr;
        for(auto dock:docks) { if(dock->windowTitle()=="Original DS Screens") originalDock=dock; if(dock->windowTitle()=="Outliner") outlinerDock=dock; }
        std::cerr << "Workspace sizes: " << savedSize.width() << "x" << savedSize.height() << " -> " << window->width() << "x" << window->height()
            << "; floating " << floatingSize.width() << "x" << floatingSize.height() << " -> " << originalDock->width() << "x" << originalDock->height() << "; visibility " << outlinerDock->isVisible() << "\n";
        require(window->size()==savedSize && originalDock->isFloating() && originalDock->size()==floatingSize && !outlinerDock->isVisible(),"Saved geometry/floating dock size/visibility must survive reopening");
        auto floatingBefore=originalDock->geometry(); editor->setGame("workspace-switch","Game switch"); settle();
        require(originalDock->isFloating() && originalDock->geometry()==floatingBefore,"Changing games must not reset workspace");
        if(!dsi) {
            // Asset-free DS console exercises the real EmuThread controls. This is
            // not a claim to have booted/tested a retail ROM or DSi firmware.
            auto thread=inst->getEmuThread(); int resets=0;
            QObject::connect(thread,&EmuThread::windowEmuReset,editor,[&] { ++resets; });
            thread->emuReset(); thread->emuPause(); settle(220);
            auto pauseButton=window->findChild<QToolButton*>("StudioPauseButton");
            auto resetButton=window->findChild<QToolButton*>("StudioResetGameButton");
            auto gamePlay=window->findChild<QToolButton*>("StudioPlayButton");
            require(pauseButton->isEnabled() && pauseButton->defaultAction()->isChecked(),"Toolbar must reflect paused emulation");
            pauseButton->click(); settle(220); require(thread->emuIsRunning() && !pauseButton->defaultAction()->isChecked(),"Pause toolbar must resume real thread");
            pauseButton->click(); settle(220); require(!thread->emuIsRunning() && pauseButton->defaultAction()->isChecked(),"Pause toolbar must pause real thread");
            gamePlay->click(); settle(220); require(editor->isPlayMode() && window->isFullScreen() && thread->emuIsRunning(),"Play button must resume emulation and fullscreen");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QApplication::sendEvent(window->panel,&escape); settle();
            require(!editor->isPlayMode() && thread->emuIsRunning(),"Exit Play must keep emulation running");
            QString preservedPath=editor->configurationPath(); require(editor->saveOnClose(),"Pre-reset profile save");
            StudioDocument preserved; require(preserved.load(preservedPath,error),"Pre-reset read"); auto preservedJson=preserved.toJson();
            resetButton->click(); settle(220); require(resets==2 && thread->emuIsRunning(),"Reset toolbar must dispatch existing reset operation");
            require(editor->configurationPath()==preservedPath && preserved.load(preservedPath,error) && preserved.toJson()==preservedJson,"Reset must preserve profiles");
            thread->emuStop(true); settle();
        }
        std::cout << "Inline profile/scene/widget naming, copy/paste/duplicate/menu operations, real drag gestures, editing/runtime isolation, transport controls and workspace persistence passed\n";
        std::cout << (dsi ? "DSi" : "DS") << ": editor panels, Inspector, ordering, scenes, mode toggle/fullscreen, per-game persistence, validation, software/OpenGL screen previews, input isolation passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    window->close(); settle();
    SDL_Quit();
    return result;
}
