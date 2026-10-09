// Exercises the actual Qt editor and persisted documents without game/BIOS assets.
#include "main.h"
#include "StudioEditor.h"
#include "StudioViews.h"
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
    glBindFramebuffer(GL_FRAMEBUFFER,0); glUseProgram(0); glBindVertexArray(0);
    glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&output);
    glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
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
        auto states = window->findChild<QComboBox*>("StudioSceneStates");
        auto x = window->findChild<QSpinBox*>("StudioX");
        auto width = window->findChild<QSpinBox*>("StudioWidth");
        auto click = [&](const QString& text) {
            for (auto b : window->findChildren<QPushButton*>()) if (b->text() == text) { b->click(); return; }
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
        for (auto a : actions) if (a->text() == "Play mode") play = a;
        require(play, "Play toggle missing");
        docks[0]->hide();
        play->setChecked(true);
        require(editor->isPlayMode(), "Play mode failed");
        for (auto d : docks) require(!d->isVisible(), "Play mode must hide all editor panels");
        play->setChecked(false);
        require(!docks[0]->isVisible() && docks[1]->isVisible(), "Editor mode must restore dock visibility");
        auto toolbar = window->findChild<QToolBar*>("MelonStudio.Toolbar");
        window->toggleFullscreen(); settle();
        require(window->isFullScreen() && !toolbar->isVisible(), "Fullscreen must hide the editor toolbar");
        for (auto d : docks) require(!d->isVisible(), "Fullscreen must hide all editor panels");
        require(window->panel->size() == window->contentsRect().size(), "Fullscreen viewport must fill the window");
        window->toggleFullscreen(); settle();
        require(!window->isFullScreen() && toolbar->isVisible() && !docks[0]->isVisible()
            && docks[1]->isVisible(), "Leaving fullscreen must restore editor visibility");
        play->setChecked(true);
        window->toggleFullscreen(); settle();
        require(window->isFullScreen() && !toolbar->isVisible(), "Fullscreen Play mode must hide the toolbar");
        window->toggleFullscreen(); settle();
        require(editor->isPlayMode() && toolbar->isVisible(), "Leaving fullscreen must retain Play mode");
        for (auto d : docks) require(!d->isVisible(), "Play mode must remain free of editor panels");
        play->setChecked(false);
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
        auto invalidStates = invalid["states"].toArray();
        auto state = invalidStates[0].toObject();
        auto elements = state["elements"].toArray();
        auto element = elements[0].toObject(); element["width"] = 999;
        elements[0] = element; state["elements"] = elements; invalidStates[0] = state; invalid["states"] = invalidStates;
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
        // The no-ROM test feeds synthetic frames through the real editor mailbox.
        // Pause/capture of a running Mario Kart ROM remains a manual check.
        editor->setGame("mkds-synthetic", "Mario Kart DS synthetic test");
        auto mario=window->findChild<QCheckBox*>("StudioMarioEnabled");
        auto automatic=window->findChild<QCheckBox*>("StudioAutomaticRecognition");
        auto refState=window->findChild<QComboBox*>("StudioReferenceState");
        auto references=window->findChild<QListWidget*>("StudioReferences");
        auto debug=window->findChild<QLabel*>("StudioRecognitionDebug");
        auto screens=static_cast<StudioScreensWidget*>(preview);
        auto canvas=window->findChild<QWidget*>("StudioHudCanvas");
        auto hud=static_cast<StudioHudCanvas*>(canvas);
        mario->setChecked(true);
        require(states->count()==5 && states->currentData().toInt()==StudioRacing,"Mario Kart scenes missing");
        auto feed=[&](quint32 color,int ms) {
            QVector<quint32> pixels(256*192,color); QElapsedTimer timer; timer.start();
            do { editor->captureScreens(pixels.data(),bottom.data(),true); settle(110); } while(timer.elapsed()<ms);
        };
        auto teach=[&](int state,quint32 color,int screen) {
            refState->setCurrentIndex(refState->findData(state)); feed(color,220);
            screens->selecting=true; auto r=screens->screenRect(screen);
            drag(screens,r.topLeft()+QPoint(r.width()/4,r.height()/4),r.topLeft()+QPoint(r.width()/2,r.height()/2));
            require(references->count()>0,"Region teaching failed");
        };
        teach(StudioMarioFirst,0xffff0000,0); teach(StudioMarioFirst,0xffffff00,0);
        require(references->count()==2,"Multiple alternatives missing");
        teach(StudioRacing,0xff00ff00,0);
        auto threshold=window->findChild<QDoubleSpinBox*>("StudioReferenceThreshold"); threshold->setValue(99);
        auto layout=window->findChild<QComboBox*>("StudioSceneLayout");
        states->setCurrentIndex(states->findData(StudioMarioFirst)); layout->setCurrentIndex(int(StudioLayout::Bottom));
        states->setCurrentIndex(states->findData(StudioRacing));
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
        automatic->setChecked(true); feed(0xff00ff00,700);
        require(automatic->isChecked() && debug->text().contains("Active: Racing"),"Automatic racing confirmation failed");
        require(window->panel->minimumSize()==QSize(256,192),"Racing top-screen override failed");
        QMetaObject::invokeMethod(states,"activated",Qt::DirectConnection,Q_ARG(int,states->currentIndex()));
        require(!automatic->isChecked(),"Choosing current scene must manually override detection");
        automatic->setChecked(true); feed(0xff00ff00,700);
        play->setChecked(true); window->toggleFullscreen(); settle(); feed(0xffff0000,700);
        require(debug->text().contains("Active: Main menus"),"Recognition must work in fullscreen Play mode");
        feed(0xff0000ff,700);
        require(debug->text().contains("Active: Unknown - fallback layout"),"Unknown must use fallback");
        window->toggleFullscreen(); play->setChecked(false); settle();
        states->setCurrentIndex(states->findData(StudioRacing));
        require(!automatic->isChecked() && debug->text().contains("Manual override"),"Scene selection must override detection");
        require(outline->topLevelItemCount()==1,"Scene map was not restored");
        require(editor->saveOnClose(),"Mario profile save failed");
        QString marioPath=editor->configurationPath(); StudioDocument marioDocument; marioDocument.gameId="mkds-synthetic";
        require(marioDocument.load(marioPath,error) && marioDocument.references[StudioMarioFirst].size()==2
            && marioDocument.references[StudioRacing][0].threshold==0.99
            && marioDocument.elements[StudioRacing][0].destination==hud->elements[0].destination,
            "References/settings/live layouts failed persistence");
        editor->setGame("other-mkds-profile","Other profile");
        require(!mario->isChecked(),"Mario tools leaked into another game");
        editor->setGame("mkds-synthetic","Mario Kart DS synthetic test");
        require(mario->isChecked() && outline->topLevelItemCount()==1,"Mario profile reload failed");
        if (qEnvironmentVariableIsSet("MELONSTUDIO_TEST_SCREENSHOT"))
        {
            feed(0xff00ff00,220);
            for (auto dock : docks) if (dock->windowTitle()=="Recognition Rules") { dock->show(); dock->raise(); }
            settle(); window->grab().save(qEnvironmentVariable("MELONSTUDIO_TEST_SCREENSHOT")+"-mkds.png");
        }
        std::cout << "Synthetic Mario Kart editor teaching, alternatives, automatic/fullscreen states, unknown fallback, manual override, HUD move/resize, profiles and live GL overlay pixels passed\n";
        std::cout << (dsi ? "DSi" : "DS") << ": editor panels, Inspector, ordering, scenes, mode toggle/fullscreen, per-game persistence, validation, software/OpenGL screen previews, input isolation passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    window->close(); settle();
    SDL_Quit();
    return result;
}
