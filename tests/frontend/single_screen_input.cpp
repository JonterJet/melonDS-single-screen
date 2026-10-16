// Integration test using the production frontend, an SDL virtual controller,
// and the real paused emulation thread; no game or BIOS assets are required.
#include "main.h"
#include "StudioEditor.h"
#include <QPushButton>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QStyle>
#include <functional>
#include <iostream>
#include <stdexcept>

extern EmuInstance* emuInstances[];

template<class Panel> class ProbePanel : public Panel
{
public:
    explicit ProbePanel(MainWindow* parent) : Panel(parent)
    {
        this->resize(1024, 768);
        reload();
    }
    void reload()
    {
        QMetaObject::invokeMethod(this, "onScreenLayoutChanged", Qt::DirectConnection);
    }
    bool displays(int kind) const { return this->numScreens == 1 && this->screenKind[0] == kind; }
    int count() const { return this->numScreens; }
    void beginTouch() { this->touching = true; this->emuInstance->touchScreen(128, 96); }
    bool isTouching() const { return this->touching; }
};

static void require(bool ok, const char* what)
{
    if (!ok) throw std::runtime_error(what);
}

static void await(const std::function<bool()>& condition, const char* what)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QApplication::processEvents();
        if (condition()) return;
        QThread::msleep(10);
    } while (timer.elapsed() < 3000);
    require(false, what);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir config;
    require(config.isValid(), "Temporary config directory unavailable");
    qputenv("XDG_CONFIG_HOME",(config.path()+"/config").toUtf8());
    emuDirectory = config.path();
    QString theme = QApplication::style()->objectName();
    systemThemeName = &theme;
    sysTimer.start();
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) == 0, SDL_GetError());
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 2, 0);
    require(device >= 0, "SDL virtual controller unavailable");
    SDL_Joystick* joystick = SDL_JoystickOpen(device);
    require(joystick != nullptr, SDL_GetError());
    bool dsi = argc > 1 && std::string(argv[1]) == "--dsi";
    Config::GetGlobalTable().SetInt("Emu.ConsoleType", dsi ? 1 : 0);
    Config::Table instcfg = Config::GetLocalTable(0);
    instcfg.SetInt("JoystickID", device);
    instcfg.SetInt("Joystick.HK_RevealBottomScreen", 0);
    instcfg.SetInt("Keyboard.HK_RevealBottomScreen", Qt::Key_F9);
    instcfg.SetInt("Window0.ScreenSizing", screenSizing_TopOnly);
    setMPInterface(melonDS::MPInterface_Local);
    require(createEmuInstance(), "Cannot create emulator instance");
    auto* inst = emuInstances[0];
    auto* window = inst->getMainWindow();
    auto* thread = inst->getEmuThread();
    int cases = 0;
    try
    {
        {
            ProbePanel<ScreenPanelNative> native(window);
            ProbePanel<ScreenPanelGL> gl(window);
            QObject::connect(thread, &EmuThread::bottomScreenRevealChange,
                             &native, &ScreenPanel::onBottomScreenRevealChanged);
            QObject::connect(thread, &EmuThread::bottomScreenRevealChange,
                             &gl, &ScreenPanel::onBottomScreenRevealChanged);
            auto& cfg = window->getWindowConfig();
            auto press = [&](bool down)
            {
                SDL_LockMutex(inst->getJoyMutex().get());
                int result = SDL_JoystickSetVirtualButton(joystick, 0, down);
                SDL_UnlockMutex(inst->getJoyMutex().get());
                require(result == 0, SDL_GetError());
            };
            for (int arrangement = 0; arrangement < screenLayout_MAX; ++arrangement)
            {
                cfg.SetInt("ScreenLayout", arrangement);
                native.reload(); gl.reload();
                require(native.displays(0) && gl.displays(0), "Initial top-only view failed");
                // Let the initial Qt layout/show events settle before measuring.
                QApplication::processEvents();
                QThread::msleep(100);
                QApplication::processEvents();
                QSize size = window->size();
                press(true);
                await([&] { return native.displays(1) && gl.displays(1); }, "Controller hold did not reveal bottom");
                require(cfg.GetInt("ScreenSizing") == screenSizing_TopOnly, "Hold changed saved mode");
                require(window->size() == size, "Hold resized the window");
                // Panels recreated while held must inherit transient state.
                ProbePanel<ScreenPanelNative> recreated(window);
                require(recreated.displays(1), "Recreated panel lost held state");
                native.beginTouch(); gl.beginTouch();
                press(false);
                await([&] { return native.displays(0) && gl.displays(0); }, "Controller release did not restore top");
                require(!native.isTouching() && !gl.isTouching(), "Hidden touchscreen stayed pressed");
                ++cases;
            }
            // A scene override must reveal bottom even when saved View sizing is dual-screen.
            cfg.SetInt("ScreenSizing", screenSizing_Even); native.reload(); gl.reload();
            StudioPresentation presentation; presentation.sizing=screenSizing_TopOnly;
            StudioElement map; presentation.overlays.append(map);
            native.setStudioPresentation(presentation); gl.setStudioPresentation(presentation);
            require(native.displays(0) && gl.displays(0), "Scene top override failed");
            press(true);
            await([&] { return native.displays(1) && gl.displays(1); }, "Scene override lost controller reveal");
            require(cfg.GetInt("ScreenSizing")==screenSizing_Even,"Scene override modified saved layout");
            native.beginTouch(); gl.beginTouch(); press(false);
            await([&] { return native.displays(0) && gl.displays(0); }, "Scene reveal release failed");
            require(!native.isTouching() && !gl.isTouching(),"Scene override left hidden touch pressed");
            presentation.sizing=screenSizing_BotOnly; native.setStudioPresentation(presentation); gl.setStudioPresentation(presentation);
            require(native.displays(1) && gl.displays(1),"Scene bottom override failed");
            presentation.sizing=screenSizing_Even; native.setStudioPresentation(presentation); gl.setStudioPresentation(presentation);
            require(native.count()==3 && gl.count()==3,"Scene Both override lost Hybrid");
            native.setStudioPresentation({}); gl.setStudioPresentation({});
            cfg.SetInt("ScreenSizing",screenSizing_TopOnly); native.reload(); gl.reload(); ++cases;
            // Ordinary physical-controller events must not expose Play's mouse-only Exit control.
            auto editor=window->findChild<StudioEditor*>(); QAction* play=nullptr;
            for(auto action:window->findChildren<QAction*>()) if(action->text()=="Play") play=action;
            require(editor && play,"Play controls unavailable"); play->setChecked(true); QApplication::processEvents();
            auto exit=window->findChild<QPushButton*>("StudioExitPlay");
            press(true); await([&] { return thread->isBottomScreenRevealed(); },"Controller reveal must work in Play");
            require(editor->isPlayMode() && exit && !exit->isVisible(),"Gamepad button must not reveal Exit Play");
            SDL_LockMutex(inst->getJoyMutex().get());
            int axisResult=SDL_JoystickSetVirtualAxis(joystick,0,16384);
            SDL_UnlockMutex(inst->getJoyMutex().get()); require(axisResult==0,SDL_GetError());
            QThread::msleep(150); QApplication::processEvents();
            require(editor->isPlayMode() && !exit->isVisible(),"Ordinary gamepad movement must not reveal Exit Play");
            SDL_LockMutex(inst->getJoyMutex().get()); SDL_JoystickSetVirtualAxis(joystick,0,0); SDL_UnlockMutex(inst->getJoyMutex().get());

            press(false); await([&] { return !thread->isBottomScreenRevealed(); },"Play reveal release");
            require(!exit->isVisible(),"Gamepad release must not reveal Exit Play"); play->setChecked(false); ++cases;
            // Both bindings contribute to the held state; releasing one must
            // not hide the bottom screen while the other remains pressed.
            QKeyEvent keyDown(QEvent::KeyPress, Qt::Key_F9, Qt::NoModifier);
            QKeyEvent keyUp(QEvent::KeyRelease, Qt::Key_F9, Qt::NoModifier);
            QApplication::sendEvent(window, &keyDown);
            await([&] { return native.displays(1); }, "Keyboard mapping failed");
            press(true);
            QThread::msleep(100);
            QApplication::sendEvent(window, &keyUp);
            QApplication::processEvents();
            require(native.displays(1), "Keyboard release ignored held gamepad button");
            press(false);
            await([&] { return native.displays(0); }, "Combined input release failed");
            ++cases;
            QApplication::sendEvent(window, &keyDown);
            await([&] { return native.displays(1); }, "Keyboard reveal failed");
            window->onAppStateChanged(Qt::ApplicationInactive);
            await([&] { return native.displays(0); }, "Lost-focus keyboard reset failed");
            ++cases;
            cfg.SetInt("ScreenSizing", screenSizing_Even);
            native.reload(); gl.reload();
            press(true);
            await([&] { return thread->isBottomScreenRevealed(); }, "Hold state missing");
            require(native.count() == 3 && gl.count() == 3, "Reveal changed existing Hybrid dual-screen mode");
            cfg.SetInt("ScreenSizing", screenSizing_TopOnly);
            native.reload(); gl.reload();
            require(native.displays(1) && gl.displays(1), "Changing to Top only while held failed");
            ++cases;
            SDL_LockMutex(inst->getJoyMutex().get());
            int detached = SDL_JoystickDetachVirtual(device);
            SDL_UnlockMutex(inst->getJoyMutex().get());
            require(detached == 0, SDL_GetError());
            await([&] { return native.displays(0) && gl.displays(0); }, "Disconnect left bottom revealed");
            ++cases;
        }
        // Config::Load replaces table storage; stop users of those tables first.
        deleteAllEmuInstances();
        Config::Save();
        require(Config::Load(), "Saved config failed to reload");
        require(Config::GetLocalTable(0).GetInt("Window0.ScreenSizing") == screenSizing_TopOnly,
                "Transient reveal persisted as bottom-only");
        require(Config::GetLocalTable(0).GetInt("Joystick.HK_RevealBottomScreen") == 0,
                "Controller binding did not persist");
        ++cases;
        SDL_JoystickClose(joystick);
        SDL_Quit();
        std::cout << cases << " frontend input integration cases passed ("
                  << (dsi ? "DSi" : "DS") << " configuration)\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        deleteAllEmuInstances();
        SDL_JoystickClose(joystick);
        SDL_Quit();
        return 1;
    }
}
