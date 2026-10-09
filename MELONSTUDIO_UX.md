# Editor UX overhaul — Milestone A

Development branch: `ci/melonstudio-ux`, based on the completed
`ci/melonstudio-freeform` build. The emulator backend and ROM/save file behavior
are unchanged. This document records completed work and unfinished milestones.

## Implemented

- Profile/scene hierarchy with inline creation/naming, Enter commit, Escape
  cancellation and double-click renaming.
- Profile/scene context menus and compact add/delete/arrow controls.
- Compatible profile, scene and widget copy/paste; independent duplicate UUIDs.
- Sibling drag/drop ordering for profiles, scenes and widgets; no drag changes
  object ownership. Copy/paste is the explicit cross-profile scene transfer.
- Blender-style widget list with a narrow vertical +/minus/arrow toolbar,
  context menus, inline names and persistent Hide/Show.
- Hierarchy selection controls all scene editors. Redundant scene selectors and
  Teach state dropdown are removed. Recognition cannot steal editing selection.
- Profile selection shows its name and ROM association in Inspector.
- Separate Editing / Automatic runtime / Manual override status; explicit
  Preview selected scene and recognition-resume controls.
- Centered Play/Pause/Reset transport. Play uses Qt fullscreen, explicitly hides
  the menu, toolbar and docks, resumes paused emulation and removes editor gizmos.
- Escape and the fullscreen hotkey restore the editor without pausing the console. Real mouse movement
  temporarily exposes Exit Play and the cursor; synthetic events and ordinary
  controller inputs do not. Other mouse clicks are consumed in Play.
- Existing workspace persistence retained, all dock tabs forced to the top,
  and Reset Workspace Layout available in Studio. Custom saved layouts survive
  startup, profile changes, Play transitions and reopening.
- Existing v1/v2/v3 profiles retained. Widget UUID is an optional v3 field;
  older widgets receive an ID without changing their content or transforms.
  Early v3 files using `marioEnabled` also load without losing settings.
  Profile ordering uses a separate atomic JSON registry.

## Validation

Synthetic Qt integration tests exercise actual hierarchy selection, inline
editors (including cancellation/double-click), menus, real X11 drag gestures and
on-disk persistence, live software/OpenGL overlays, runtime/editing separation,
Play/Escape/cursor timeout and real docked/floating workspace restoration.
An asset-free DS console exercises the real thread's Pause/Resume/Reset controls.
DSi UI and SDL virtual-controller checks do not require proprietary firmware.
Headless CTest validates recognition, migration, masks and profile registries.

Windows CI builds static Qt 6 executables and uploads them immediately after
compilation. Only synthetic QCoreApplication tests run in Windows CI, with a
15-second limit; no GUI executable startup check is used. Dependency cache saving
remains independent of later verification failures.

Manual Windows checks: title bar disappears in Play; Pause/Reset operate your
loaded ROM; your gamepad and reveal binding work; high-DPI/multiple-monitor dock
restoration is correct; references and live cutouts work with your ROM. Neither
real-ROM recognition accuracy nor Windows GUI execution is established by CI.

## Remaining

| Milestone | Remaining work |
| --- | --- |
| B — selection/widgets | Shared two-screen polygon/rectangle/ellipse editor, eight resize handles, aspect locking, independent source sampling and four-corner UV deformation in both renderers. |
| C — recognition | Masked shape comparison, editable modal references, automatic pause/select/confirm/resume, removal of the legacy Resume and quick-map controls. Scene selection synchronization is already implemented in A. |
| D — library | Scanned games directory, banner extraction, tiles/search/sorting, separate properties/covers/profile metadata, library/editor/fullscreen navigation and games-folder Windows packaging. |
| Editor history | Undo/redo for scene/widget operations beyond the existing tracer's point undo. |

The old rectangular-reference and polygon-only widget workflows remain usable
until their replacements are implemented. No placeholder library or inactive UV
buttons were added. See [MELONSTUDIO.md](MELONSTUDIO.md) for the current user guide.
