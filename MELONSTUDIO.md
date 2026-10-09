# MelonStudio: basic Qt 6 editor

MelonStudio is built into this fork's existing Qt frontend. The executable is
still named `melonDS.exe`, and the central game viewport uses melonDS's original
rendering, input, DS/DSi emulation, screen layout, and save files.

## Windows download and launch

1. Open this fork's **Actions → Windows** page and select the successful run for
   the `ci/melonstudio-editor` branch (or its pull request).
2. In **Artifacts**, click **melonDS-windows-x86_64**. Sign into GitHub if the
   download link is unavailable.
3. Extract the ZIP into a writable folder and run `melonDS.exe`. The Windows
   presets link Qt, SDL, supporting libraries, and the C/C++ runtime statically;
   no separate Qt installation or dependency DLL download is required.
4. Open your game with **File → Open ROM**. Existing BIOS/firmware configuration
   still applies. DSi emulation requires the same BIOS, firmware, and NAND setup
   as this fork did before the editor.

Windows Actions only compiles and uploads the executable. It does **not** launch
the GUI or wait on `--help`. Artifact upload and dependency-cache saving run
independently after a successful build.

## Interface

- **Central viewport:** the existing playable game display. Click it to give
  keyboard input to the emulator. Touch input continues to use the existing
  bottom-screen coordinate transformation.
- **Original DS Screens:** view-only live previews of both original screens,
  updated five times per second, independent of Top only and hold-to-reveal.
  Software and OpenGL rendering are supported. The previews are hidden in Play
  mode, avoiding further GPU readback work while playing.
- **Outliner:** add, remove, select, rename, and reorder HUD element records.
  Each scene has its own list. Up/Down determines the saved element order.
- **Inspector:** edit the selected record's name, source screen, enabled flag,
  and rectangular bounds in original DS pixels (256 × 192). Bounds stay inside
  their source screen. These are **metadata only** in this milestone: changing
  them does not yet crop, mask, overlay, or move graphics in the viewport.
- **Scene States:** manually select Gameplay, Menus, or Cutscenes. This changes
  the editor's active element list; it does not detect or change game scenes.
- **Play mode / Editor mode:** hide and restore dock panels while preserving
  their visibility. The toggle focuses the viewport and leaves the emulator's
  running/paused state unchanged. Use the existing System menu to pause.
- **Save configuration / Load configuration:** save or reload the current game's
  editor data. Reload asks before discarding unsaved changes. Dirty profiles
  save automatically when switching games or closing the editor. Save errors
  are shown and prevent closing without saving.

The existing **View → Screen sizing → Top only** and **Config → Input and
hotkeys → Reveal bottom screen (hold, Top only)** continue to work. Gamepad
input stays available in both modes. Typing in editor panels does not trigger
emulator keyboard controls; focusing an editor panel releases held keyboard
controls to avoid stuck buttons.

## Per-game profiles

Each cartridge is identified by SHA-256 of its ROM content, so moving or renaming
an identical ROM retains its profile. Different ROM contents get separate
profiles. DS/DSi firmware sessions have their own profiles. The main window owns
the editor; additional emulator display windows keep the existing screen UI.

Profiles are versioned JSON documents below Qt's application configuration
location, in `MelonStudio/games/<ROM-SHA256>.json`. Hover over the toolbar's game
label to see the exact path on your computer. These files are separate from
melonDS settings, cartridge saves, and savestates. Invalid, wrong-game, or
unsupported-version files are rejected without replacing the loaded document.
Saves use an atomic file replacement. A star beside the game label means the
profile has unsaved changes.

Freeform alpha masking, HUD compositing, automatic scene detection, and
controller-to-touchscreen mappings are not implemented in this milestone.

## Developer verification

Use a Qt 6 Linux Ninja development build with exported compile commands and
without LTO. The frontend harness reuses the production objects and requires
GNU `objcopy`, Python, and an X session (or `xvfb-run`).

```sh
python3 tests/frontend/run_single_screen_input.py build/cloud --studio
python3 tests/frontend/run_single_screen_input.py build/cloud
c++ -std=c++17 tests/frontend/single_screen_layout.cpp src/frontend/ScreenLayout.cpp -o build/layout-test
build/layout-test
```

The editor harness checks panels and central viewport, Inspector bounds,
Outliner ordering/deletion, independent scenes, mode and visibility restoration,
per-game save/load, invalid-file rejection, live software and OpenGL preview pixels / GL state restoration, and
keyboard isolation in DS and DSi modes. The existing controller harness checks
Top only and hold/release behavior with an SDL virtual controller. These tests
need no commercial ROM or BIOS assets; actual games and physical controllers
still need manual Windows testing.
