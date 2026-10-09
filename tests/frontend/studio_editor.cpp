// Exercises the actual Qt editor and persisted documents without game/BIOS assets.
#include "main.h"
#include "StudioEditor.h"
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
        require(docks.size() == 4, "Expected all four editor panels");
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
        std::cout << (dsi ? "DSi" : "DS") << ": editor panels, Inspector, ordering, scenes, mode toggle/fullscreen, per-game persistence, validation, software/OpenGL screen previews, input isolation passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    window->close(); settle();
    SDL_Quit();
    return result;
}
