# MelonStudio: profiles, scenes and live polygon HUD

MelonStudio lives inside the existing Qt 6 melonDS frontend. The Windows program
is still `melonDS.exe`. DS/DSi core emulation, ROMs, saves, savestates, physical
controller input, touchscreen transforms, and hold-to-reveal are preserved.

## Windows download

In [this fork's Windows Actions](https://github.com/JonterJet/melonDS-single-screen/actions/workflows/build-windows.yml),
open the successful build for **ci/melonstudio-ux** or its pull request.
Under **Artifacts**, download **melonDS-windows-x86_64** while signed into GitHub.
Extract the ZIP into a writable folder and run **melonDS.exe**. Qt, SDL,
supporting libraries, and the C/C++ runtime are linked statically; no separate
Qt or dependency-DLL installation is needed. Your existing BIOS/firmware setup
still applies; DSi requires the same BIOS, firmware, and NAND as before.

Windows CI uploads the executable after compilation and runs only headless
synthetic tests, with a 15-second timeout per test. It never launches the emulator
GUI. Dependency cache saving runs even if a later verification test fails.

## Workspace

The default layout follows the editor reference: Original DS Screens above Game
Profiles on the left, a large playable viewport in the center, and Outliner above
the Inspector / Recognition Rules / HUD Layout / Recognition Debug group on the
right. All dock tabs are at the top. Drag, resize, tabify, close, or float panels
as usual. **Studio** lists panel visibility controls.

Window geometry and Qt dock state are saved in **MelonStudio/workspace.ini**
under the application configuration directory, separately from profiles. The
saved state includes dock positions, splitter proportions, dimensions, floating
windows, selected tabs and visibility. It survives reopening and changing games.
Qt may reposition or shrink windows to fit a different monitor. **Studio → Reset
Workspace Layout** restores the default arrangement.

## Named game profiles and custom scenes

Open your ROM with **File → Open ROM**. Its SHA-256 content hash selects the last
chosen profile associated with that ROM. Renaming/moving identical ROM content
keeps the association; different ROM contents get separate associations.

- **New Profile** creates an item directly in the hierarchy and begins inline
  naming. **+ Scene** does the same for a scene. Enter commits a name; Escape
  cancels renaming. Double-click a name or use Rename in its context menu.
- Select a profile to view its name and ROM association in Inspector and edit
  its global settings; select one of its scenes to edit
  its Inspector, references, HUD Layout and screen layout. The hierarchy is the
  only scene selection control. Selecting another scene does not disable
  automatic recognition or move the runtime scene to the editing selection.
- **Preview selected scene (manual override)** makes the selected scene the
  displayed runtime scene. **Automatic recognition** resumes detection. The
  panel status distinguishes Editing from Automatic runtime / Manual override.
- Right-click a profile or scene for Rename, Duplicate, Copy, Paste and Delete.
  Ctrl+C/Ctrl+V copy compatible objects within the editor; Ctrl+D duplicates;
  Delete removes the selected item after confirmation. A scene can be pasted
  into another profile. Copies preserve references, layouts and widgets but get
  independent object identifiers.
- The compact minus / arrow controls delete or reorder the selected profile or
  scene. Drag above or below a sibling to reorder; scenes remain in their
  profile. Profile order is persisted separately in `profiles/order.json`.
- A profile context menu offers **Associate with open ROM**. Open the desired
  ROM first, select the profile, then associate it using the emulator's existing
  content identity. No ROM bytes are changed. Deleted profiles are archived in
  `MelonStudio/profiles/deleted` for recovery.
- Selecting a profile for the currently open ROM makes it the automatic choice
  next time. Another ROM's profile can be edited offline, but cannot change the
  running game's display or capture references/widgets until that ROM opens.
- New profiles contain three freely editable scenes. There is no fixed Mario
  scene list or fixed scene count. Deleting the last scene creates a blank scene
  so the editor always has somewhere to place widgets.

**Save configuration** (Ctrl+S) saves the active named profile. **Load configuration**
reloads it, asking before discarding unsaved changes. Profiles autosave when
switching games/profiles or closing. A star in the toolbar label marks unsaved
changes; hover over it for the active file path.

Version 3 profiles are UUID-named JSON files in `MelonStudio/profiles`, with ROM
associations in `associations.json`. Version 1/2 profiles are imported when their
ROM opens; the original files remain unchanged and a `.pre-v3.bak` copy is made.
All old stored scenes, PNG references, thresholds, display settings, enabled
flags and rectangular HUD elements are retained. A version 2 profile includes
its three older basic scenes as well as the five Mario scenes; unused scenes can
be removed afterward. Scene and profile identifiers persist through renaming
and reordering. Duplicates get new identities at the appropriate level. Early v3 profiles using
the older `marioEnabled` flag remain readable; saving uses `sceneToolsEnabled`.

## Train recognition

Enable **scene layouts and HUD**, then assign the selected scene's **Scene
layout**: Existing emulator layout, Top screen only, Bottom screen only or Both
screens. Both honors the existing View screen arrangement. For Mario Kart DS,
create the scenes you want, such as separate Character Selection and Kart
Selection, then assign Racing to Top screen only.

Select the target scene in **Game Profiles**. In **Recognition Rules**, click
**Pause & select reference region**, and drag a distinctive region on either original screen. The emulator
pauses and captures a fresh game frame. Name the reference, adjust its matching
threshold, and add alternatives as needed. **Resume game** continues play.
Enable **Automatic recognition** to test switching.

Matching is local normalized RGB agreement on a 32 × 24 sample grid at a fixed
source region. No AI service, pretrained game screenshots, game memory addresses,
or ROM modifications are used. Similarity percentages are not probabilities.
Choose stable labels/icons that differ between scenes, avoiding moving racers
and regions shared by gameplay and pause. Use **Recognition Debug** for scores,
active/candidate scene, ambiguity and confirmation progress.

References for one scene are alternatives. Close competing accepted matches are
ambiguous; a candidate must persist for the configurable confirmation period
(default 350 ms, ambiguity margin 3 percentage points). Unknown/stale/unstable
matches use the configured fallback (Both initially), without a HUD. Missing
fresh frames for half a second clears stale recognition. Detection continues
around ten times per second in Play and fullscreen. The detected runtime scene is separate from the editing selection. To force a
scene, use **Preview selected scene (manual override)**; re-enable automatic
recognition to resume detection. The existing capture workflow still pauses
detection while teaching/tracing. Its shared modal replacement is Milestone C.

## Trace a live HUD cutout

1. Select the scene that should contain the HUD, then click the **+** (**Add Widget**) in Outliner.
2. Choose Top Screen or Bottom Screen. The game pauses and that source appears
   in a large tracing dialog.
3. Click points around the desired element. The editor shows connected lines
   and vertices. Drag existing vertices to adjust them. Backspace / Ctrl+Z or
   **Undo point** removes the last point. **Close / adjust shape** previews the
   closed shape; **Clear** restarts it. A circle can be approximated with many
   points. A valid shape needs at least three points and nonzero area.
4. Confirm with Enter, a double-click, or **Confirm**. Cancel/Escape leaves the
   profile unchanged. The game resumes if it was running before tracing; a
   previously paused game stays paused.
5. The new object appears in Outliner. Pixels outside its polygon are transparent.
   The cutout uses **live source pixels**, not the tracing screenshot, on both
   software and OpenGL renderers.
6. In Editor mode, drag the visible cutout on the gameplay viewport to move it.
   Drag its lower-right yellow handle to resize it. **HUD Layout** offers the
   same operations using a live preview; the main viewport remains full-rate.
7. Inspector provides exact destination position/size in a 256 × 192 logical
   screen. Source bounds resize/move the polygon with the source coordinates;
   **Edit polygon mask** opens the tracing dialog to adjust vertices. Renaming,
   enabling/hiding, duplication, deletion and composition order are independent
   for each scene. Up/Down changes the order; later elements draw above earlier
   ones. The compact vertical arrows and sibling drag/drop reorder widgets;
   right-click for Rename, Duplicate, Copy, Paste, Delete and Hide/Show.
   Double-click names to edit inline. Copies have independent identifiers.
   Management commands persist immediately; save configuration after transforms.

Existing rectangular overlays remain rectangles until a polygon is traced for
one. **Recognition Rules → Add live bottom-screen map to selected scene** remains
available as a quick full-bottom-screen rectangular overlay. Set Racing to Top
screen only, then edit its polygon to trace the map or another HUD object.
Overlays use the first displayed screen's transform, including rotation/aspect
ratio/fullscreen scaling. Transparent portions continue to show the base game.
The lower-right handle is a bounding-box scale control; source vertices retain
correct coordinates and transparency while scaling.

The existing **Reveal bottom screen (hold, Top only)** keyboard/gamepad mapping
works with scene overrides. While held, the full bottom screen appears and HUD
objects/handles are hidden. Release restores the configured scene. Touchscreen
input outside editor HUD transforms uses the existing emulator transformation.
Controller-to-touchscreen mappings are not added by this change.

## Play

Click the prominent centered **Play** triangle to enter fullscreen gameplay.
Docks, toolbar and editing handles disappear, while the current layout, live HUD,
recognition and emulation continue. Play resumes a paused active console. The
centered transport also offers **Pause/Resume** (using the real thread state) and
**Reset game** (the existing melonDS reset, retaining save data and profiles).
Gamepad and keyboard gameplay input remain available.

**Escape** returns to the editor. The existing fullscreen hotkey also exits Play
through the same restoration path. The Escape press and release are consumed by
Play mode, avoiding emulator hotkeys. Mouse movement reveals a small **Exit Play
(Esc)** button; it hides again after about two seconds of inactivity. Ordinary
keyboard/gamepad input and synthesized touch/application mouse events do not
show it. The viewport cursor starts hidden and hides again with the Exit control.
In Play, mouse clicks only operate the visible Exit control; other clicks do not
become touchscreen presses. Editor touchscreen behavior is preserved. Exiting restores geometry, docking, floating panels,
visibility and active tabs without restarting the game. The existing standalone
fullscreen command also remains available.

## Developer tests and manual validation

Automated tests use synthetic frames and SDL virtual controllers, with no ROM
or BIOS assets. Enable `-DMELONSTUDIO_BUILD_TESTS=ON` for bounded model tests:

```sh
ctest --test-dir build/cloud --output-on-failure --timeout 15
python3 tests/frontend/run_single_screen_input.py build/cloud --studio
python3 tests/frontend/run_single_screen_input.py build/cloud
c++ -std=c++17 tests/frontend/single_screen_layout.cpp src/frontend/ScreenLayout.cpp -o build/layout-test
build/layout-test
```

The integration runner requires a Linux Ninja development build without LTO,
exported compile commands, Qt/SDL, GNU objcopy and an X session or xvfb-run.
`--build-only` prepares the integration binary outside the virtual X session.

Coverage includes profile/scene/widget inline creation and renaming, cancellation,
context-menu copy/paste/duplicate/delete/hide, persisted sibling ordering,
editing/runtime scene isolation, asset-free DS thread Pause/Play/Reset,
reordering, dynamic recognition, v1/v2 migration, PNG/profile round trips, polygon
alpha generation, native/production-GL live mask pixels and scaling, vertex
tracing/movement/undo, viewport and HUD-canvas transforms, Play/Escape, mouse Exit
visibility/timeout, physical-controller-style SDL input, and real window/dock
persistence across closing/reopening. Existing editor, DS/DSi input, fullscreen
and layout regressions are retained.

**Manual Windows validation still required:** capture/trace from your running
Mario Kart DS copy; confirm live HUD movement/transparency under software and
OpenGL, fullscreen, high DPI/multiple monitors, your physical controller and
bottom reveal; train and validate each intended menu, selection, race, pause and
results transition. No real-ROM recognition accuracy is claimed by the synthetic
suite. Your screenshot is an interface reference, not a supplied ROM.

## UX overhaul delivery scope

This branch completes **Milestone A** of the requested UX overhaul. It retains
the previous working polygon widget tracing, one-corner resizing and rectangular
recognition capture. It does **not** implement the later shared rectangle/ellipse
selection dialog, shaped recognition, eight-handle/aspect-locked gizmos, UV warp
controls, general editor undo/redo or Steam-style library. No library startup or
`games` directory packaging is claimed for this milestone. See
[MELONSTUDIO_UX.md](MELONSTUDIO_UX.md) for the milestone checklist.
