# Single-screen controls

1. Choose **View → Screen sizing → Top only**.
2. Open **Config → Input and hotkeys → General hotkeys**. Bind **Reveal bottom screen (hold, Top only)** in the joystick column to a spare gamepad button. A keyboard binding is also available. Both start unassigned.
3. Hold that binding to replace the top view with the complete live bottom screen at the current window size. Release it to return to the top screen. The bottom screen accepts the usual mouse, pen, and touch input while visible; hiding it releases an active touch.
4. Bind **Toggle fullscreen** in the same dialog to use the existing fullscreen mode. The screen retains the selected aspect ratio; select the window aspect option if you want it stretched to fill the display.

The reveal control works while paused, in DS and DSi modes, with software or OpenGL presentation. Controller disconnection restores the top view. It does not change the saved sizing selection or resize the window. Other screen sizing modes keep their existing behavior. Hybrid arrangement now honors single-screen sizing.

A binding can also trigger a DS button if assigned to both; use a spare button for reveal if you want no game input alongside it. The input configuration retains the existing button, hat, and axis mapping support. No HUD masks, overlays, or gamepad-to-touch coordinate mappings are implemented here.

## Relevant frontend systems

- `src/frontend/ScreenLayout.cpp` computes screen transforms and the inverse bottom-screen transform for touchscreen coordinates.
- `src/frontend/qt_sdl/Screen.cpp` presents both emulated framebuffers using QPainter or OpenGL and handles mouse, tablet, and touch events. Reveal changes only the selected presentation transform, not the emulator's framebuffers or rendering engines.
- `src/frontend/qt_sdl/EmuInstanceInput.cpp` combines keyboard/SDL joystick mappings into DS input and hotkey masks. The new hotkey is appended to preserve existing hotkey IDs; it uses the normal unassigned default and TOML configuration.
- `src/frontend/qt_sdl/EmuThread.cpp` polls held input even while paused and sends reveal changes to the UI thread. New or recreated screen panels inherit the held state.
- `src/frontend/qt_sdl/InputConfig/InputConfigDialog.h` registers the new binding in the existing hotkey configuration dialog.

The DS/DSi emulation core, timing, audio, saves, and game input semantics are unchanged.

## Linux build and tests

In this prepared cloud environment:

```bash
/workspace/.melonds-env/build.sh
source /workspace/.melonds-env/activate.sh
cd /workspace/melonDS-single-screen
c++ -std=c++17 tests/frontend/single_screen_layout.cpp src/frontend/ScreenLayout.cpp -o build/single_screen_layout_test
build/single_screen_layout_test
SDL_AUDIODRIVER=dummy QT_QPA_PLATFORM=xcb xvfb-run -a python3 tests/frontend/run_single_screen_input.py build/cloud
/workspace/.melonds-env/smoke.sh
```

For another Linux machine follow BUILD.md, configure a Ninja development build with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, and pass its build directory to the input test runner. That runner requires GNU objcopy, SDL 2.0.14 or newer for virtual joysticks, and a working X display. It reuses production frontend objects and renames a copy of the application's main symbol; it does not modify source or production objects. Run it against a non-LTO development build.

The layout test covers single-screen framebuffer selection and touchscreen transforms over all arrangements/rotations, swapping, integer scaling, and native/widescreen aspects. The integration test exercises the real paused emulation thread and SDL virtual gamepad, both panel implementations, hold/release, simultaneous keyboard/gamepad input, focus reset, disconnect, panel recreation, unchanged other layouts, and saved configuration. These tests require no ROM. Actual DS/DSi gameplay and physical-controller testing still require your own hardware/assets.

## Obtaining a Windows executable

The Linux binary `build/cloud/melonDS` is not a Windows executable. Windows compilation and execution must be verified separately.

### Existing GitHub Actions build

Commit these changes and push them to this repository's `master` branch or a branch whose name starts with `ci/` (or open a pull request targeting `master`). With Actions enabled, the existing `.github/workflows/build-windows.yml` builds the Windows presets using vcpkg/static dependencies. Once its **Windows / x86_64** job succeeds, download the **melonDS-windows-x86_64** artifact from that workflow run and extract `melonDS.exe`. Use the ARM64 artifact only for ARM64 Windows. An artifact from an older commit will not contain this change.

The workflow checks that the executable starts successfully with `--help` before uploading it, and fails if the executable is missing. Its Windows presets statically link the dependency libraries; this artifact is intended to run without the DLL bundle required by the MSYS2 dynamic build below. Check the actual workflow result before treating the Windows build as verified.

### Build locally with MSYS2 UCRT64

Install MSYS2, open its **UCRT64** terminal, and update it with `pacman -Syu` (reopen/update again if instructed). Install the dynamic Qt 6 dependencies:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,SDL2,libarchive,enet,zstd,faad2,qt6-base,qt6-svg,qt6-multimedia}
cd /path/to/melonDS-single-screen
cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows --parallel 4
./build/windows/melonDS.exe
```

The executable is `build/windows/melonDS.exe`. Run it from UCRT64 initially so it can find its DLLs. To run outside that terminal, deploy the Qt plugins with `/ucrt64/bin/windeployqt.exe --release build/windows/melonDS.exe` and bundle the non-Qt runtime DLLs (SDL2, libarchive, ENet, zstd, FAAD and their transitive dependencies) from `/ucrt64/bin`. Use `ldd build/windows/melonDS.exe` to inspect dependencies and check the package on Windows outside MSYS2. Copying just this dynamic-build EXE is insufficient; the Actions static artifact is the simpler distribution route.

## Validation in this cloud task

- Full Linux Qt 6 build passed with GCC 14.2.0 and Qt 6.8.2; no compiler or linker errors remain.
- 388 layout regression cases passed.
- 9 input integration cases passed under DS configuration and 9 under DSi configuration. These use a virtual controller and no running game.
- Real application startup and graceful shutdown passed with software presentation and OpenGL presentation (Mesa llvmpipe, OpenGL 4.5). The OpenGL check used Top only with Hybrid arrangement.
- Windows compilation/execution, physical controllers, and actual ROM gameplay were not tested in this Linux environment.
