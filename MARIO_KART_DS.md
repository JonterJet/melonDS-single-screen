# Mario Kart DS in MelonStudio

The editor now uses named game profiles and arbitrary custom scenes rather than
a fixed Mario Kart state list. Existing Mario Kart profiles and live rectangular
maps migrate automatically, preserving reference images and display settings.

See [MELONSTUDIO.md](MELONSTUDIO.md) for Windows download, profile management,
recognition training, true polygon HUD tracing, Play mode and workspace settings.

For a console-style Mario Kart setup:

1. Open your ROM, select or create its game profile, and create/rename scenes
   for Main Menu, Character Selection, Kart Selection, Racing, Pause and Results.
2. Set Racing's layout to Top screen only. Assign the appropriate screen to the
   other scenes, and choose a safe unknown-state fallback.
3. Pause and capture distinctive visual reference regions for each scene.
   Add alternatives and tune thresholds/confirmation using Recognition Debug.
4. In Racing click the Outliner + (Add Widget), choose Bottom Screen, and trace a polygon around the
   map. Confirm and move/scale it using the viewport or HUD Layout. Save.
5. Resume the game and enable automatic recognition. Enter Play for fullscreen;
   Escape or the mouse-triggered Exit button restores the editor.

No pretrained references are supplied. Validate detection, map transparency,
controller reveal and input with your own ROM under both renderers. No ROM bytes
or game memory addresses are changed.
