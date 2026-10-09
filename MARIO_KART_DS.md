# Mario Kart DS scene tools

This version adds user-trained visual recognition and live rectangular overlays.
It contains **no pretrained Mario Kart references**. Train and validate the five
states with your own Mario Kart DS ROM. No ROM bytes are changed, no game memory
addresses are used, and matching runs locally without an AI service.

## Download and run on Windows

Open the successful **Windows** Actions run for `ci/melonstudio-mkds` or its pull
request in **JonterJet/melonDS-single-screen**. Under **Artifacts**, download
**melonDS-windows-x86_64** (sign into GitHub), extract the ZIP to a writable
folder, and run **melonDS.exe**. Qt, SDL, supporting libraries, and the C/C++
runtime are linked statically. Use your existing melonDS BIOS/firmware settings,
then **File → Open ROM** to open your Mario Kart DS copy.

The workflow uploads the emulator immediately after compilation. It then runs
headless synthetic model tests with a 15-second test timeout and saves the
vcpkg cache even if verification fails. It never launches the Windows GUI.

## Teach the scenes

1. In **Scene States**, enable **Mario Kart DS tools for this game**. This is
   opt-in for the current ROM's profile. Select the scene you want to edit:
   Main menus, Character / kart selection, Racing, Pause menus, or Race results.
2. Choose its **Scene layout**: Top screen only, Bottom screen only, Both
   screens, or Existing emulator layout. Racing defaults to Top; the other
   scenes default to Both. Both honors the emulator's existing screen
   arrangement. Choose whichever original screen contains that scene's UI.
3. Open the **Recognition Rules** tab beside Inspector. In **Teach state**,
   choose the state you are currently seeing in the running game. Click
   **Pause & select reference region**. The emulator pauses and captures fresh
   original screens. Drag a distinctive region on either **Original DS Screens**
   preview. Escape cancels selection. The region becomes a saved reference.
4. Name the reference and set its **Match threshold** (96% initially). Add
   alternatives for different menus, character/kart pages, tracks, and results
   screens as needed. References for one state are alternatives: any may match.
   Disable or remove references that match the wrong state.
5. Click **Resume game**. Set **Unknown-state fallback**, **Confirmation period**
   (350 ms initially), and **Ambiguity margin** (3 percentage points initially).
   Enable **Automatic recognition** in Scene States and test your transitions.
   **Recognition Debug** shows the active state, candidate, best similarity,
   confirmation time, and each state's score.
6. Use **Save configuration**. References, thresholds, scene layouts, overlays,
   automatic mode, and fallback settings belong to this game's profile.

Matching samples the selected rectangle at a fixed 32 × 24 grid and measures
normalized RGB agreement. A displayed percentage is a similarity score, not a
probability of correctness. Choose small, stable, distinctive labels or icons;
avoid moving racers, animated backgrounds, and regions that remain unchanged
between racing and pause. Match position matters. Keep language/ROM revision
and display-content settings consistent with your captured references.

The matcher checks fresh original frames around ten times per second, including
in Play mode and fullscreen. A candidate must persist for the confirmation
period. Close competing states are treated as ambiguous. Unrecognized or
persistently unstable frames settle on the configured fallback, with no HUD;
missing fresh frames for half a second immediately clears stale recognition.
The confirmation period provides brief grace during transitions.

**Manual override:** select a state in Scene States to disable automatic
recognition and use its layout immediately. Editing HUD elements also selects
manual mode. Re-enable Automatic recognition to resume detection. Teaching
pauses recognition so changing scenes cannot interrupt a region selection.

## Add a live racing map

1. Select **Racing** manually. In Recognition Rules click **Add live bottom-screen
   map to selected scene**. Initially it shows the whole bottom screen as an
   80 × 60 rectangle. This is a live image of the current game frame.
2. Select it in **Outliner**. Use Inspector's source screen and source bounds to
   crop the map. Or click **Pause & select HUD source** and drag its map region
   on Original DS Screens. Resume the game afterward.
3. Open **HUD Layout**. Drag the selected overlay to move it; drag its lower-right
   corner to resize it. Inspector's destination bounds provide exact coordinates
   in a 256 × 192 logical screen. Those coordinates scale with the viewport.
4. Toggle the element's enabled flag to hide/show it in that scene. Other scenes
   have independent Outliner lists; add an overlay only where you want it. Up/Down
   changes composition order. In a two-screen layout, overlays sit on the first
   displayed screen. For a racing console view, use Top screen only.

Software and OpenGL display paths both compose current framebuffer pixels.
Saved recognition screenshots are never used as the live HUD. The editor canvas
updates around ten times per second; the game viewport composes overlays at its
normal render rate. Freeform alpha masks and controller-to-touch mappings are
not implemented.

The existing **Reveal bottom screen (hold, Top only)** keyboard/gamepad binding
works with the Racing scene's top-only override, even if the saved View sizing
is different. While held, the full bottom screen appears and overlays are hidden;
release restores the scene view. Existing gamepad input, touchscreen transforms,
fullscreen, DS/DSi core behavior, saves, and savestates are preserved.

## Profiles and compatibility

Profiles remain in `MelonStudio/games/<ROM-SHA256>.json` beneath Qt's application
configuration directory. Hover over the toolbar game label for the exact path.
Identical ROM content keeps the same profile after renaming or moving the ROM.
Different content gets a separate profile. Version 1 editor profiles load with
recognition disabled; their original file is backed up as `.v1.bak` on the first
version 2 save. Disabling Mario tools restores existing emulator layout behavior.
Other games retain the original basic editor scenes and metadata-only elements.

## Verification and limits

Automated tests use synthetic frames, not a Mario Kart ROM. They exercise all
five states, multiple references, thresholds, ambiguity, confirmation, flicker,
unknown/stale/unstable fallback, profile round trips/migration, live overlay
pixels, Qt region teaching, HUD movement/resizing, fullscreen recognition,
manual overrides, and DS/DSi input regressions. OpenGL tests use the production
screen shader and overlay vertices with changing array-texture pixels.

Build model tests with `-DMELONSTUDIO_BUILD_TESTS=ON`, then run:

```sh
ctest --test-dir build/cloud --output-on-failure --timeout 15
python3 tests/frontend/run_single_screen_input.py build/cloud --studio
python3 tests/frontend/run_single_screen_input.py build/cloud
c++ -std=c++17 tests/frontend/single_screen_layout.cpp src/frontend/ScreenLayout.cpp -o build/layout-test
build/layout-test
```

The Qt frontend tests require Linux Ninja, exported compile commands, GNU
objcopy, Qt/SDL libraries, no LTO, and an X session or xvfb-run. They do not need a
ROM or BIOS. `--build-only` prepares the integration executable without running
it; this allows compilation outside the virtual X session.

**Still requires your manual Windows test:** capture from an actually running
Mario Kart ROM; tune scene references; test each intended menu/selection,
tracks/laps/items, pause transitions, and results screen; confirm map content and
input under software/OpenGL rendering, fullscreen, and your physical controller.
There is no claim of reliable real-game recognition before this training and
validation. DSi core regressions use synthetic tests; Mario Kart DS is a DS game.
