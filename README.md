# RE1 HD Remaster - True 4:3 Fullscreen (v0.1, untested in-game)

A Direct3D 9 proxy (`d3d9.dll`) that takes the finished frame, cuts the 4:3
picture out of the game's 16:9 canvas and scales it uniformly to fill the
screen. Menus, inventory and videos are never stretched or squeezed. If the
screen is not exactly 4:3 (e.g. 1280x1024) it trims a little off the sides
instead of adding bars.

## Install with Fluffy Mod Manager

1. **Keep your current d3d9.dll.** In the game folder, copy your existing
   `d3d9.dll` (the g-sync/borderless proxy) and name the copy `d3d9_chain.dll`.
   This mod loads it underneath itself, so you keep its features.
2. Extract the `RE1HD True 4-3 Fullscreen` folder into Fluffy's `Mods` folder
   (next to the manager .exe). It must contain `modinfo.ini`, `d3d9.dll`
   and `RE1_True43.ini`.
3. Start Fluffy, select the game, tick the mod, press **Launch game** (or the
   install button). Fluffy copies the files into the game folder.
4. In the game: Options -> set the **resolution to a 4:3 one** (e.g. 1280x960)
   and set the display mode to the **built-in 4:3 mode**.

To remove: untick the mod in Fluffy and install/restore; your old `d3d9.dll`
comes back. Delete `d3d9_chain.dll` if you no longer want it.

## In-game keys

| Key | Action |
|-----|--------|
| F9  | Fix on / off (compare before and after) |
| F10 | Reload `RE1_True43.ini` without restarting |
| F11 | Cycle the canvas guess 16:9 -> 16:10 -> 4:3 -> 21:9 |

## If it does not look right

The mod writes `RE1_True43.log` next to the game exe. It records the back
buffer size and the exact crop rectangles. Send me that log plus a screenshot
of the menu with F9 on and off.

## Notes

* `MulderConfig` deletes/replaces `d3d9.dll` when you re-apply its options.
  If you re-apply them, reinstall this mod afterwards.
* The mod disables MSAA on the back buffer (needed for the fix). The game's own
  post-process anti-aliasing is unaffected.
* Scaling a 16:9-canvas picture up to a 4:3 screen is about 1.33x (1.42x on
  5:4), so it is slightly softer than native. `Filter=point` gives crisp pixels.

## Build from source

`./build.sh` (needs clang, lld, llvm-dlltool; no Windows SDK). Unit test for
the geometry: `gcc -Isrc tests/test_layout.c -o t && ./t`.

## Source map

| File | Purpose |
|------|---------|
| `src/layout.h` | Crop/scale math, no Windows code, unit-tested |
| `src/re1_true43.c` | Hooks, settings, hotkeys, forwarding |
| `src/win_minimal.h`, `src/d3d9_minimal.h` | Hand-written Win32 / D3D9 declarations |
| `src/d3d9.def` | Export table (same 12 names as the real d3d9.dll) |
