# TyMod

A mod menu for **TY the Tasmanian Tiger** (Steam HD, 2016) that loads as a DLL
proxy, with an in-game ImGui menu.

## How it works

TY.exe imports `OpenAL32.dll` from its own folder. TyMod replaces that DLL with a
small proxy that:

1. **Forwards all 157 OpenAL exports to `soft_oal.dll`** (the real OpenAL-Soft
   binary that ships in the game folder), so audio is unaffected.
2. Hooks GDI's `SwapBuffers` (the game's frame present) and draws an **ImGui**
   overlay on top of the game each frame.
3. Provides the **No-Clip** feature.

No separate launcher or injector is needed; Steam launches the game as usual.

## Menu

Press **INSERT** in-game to toggle the menu (configurable in the Hotkeys
section). While it is open TyMod captures the mouse and keyboard (including
left/right/middle click) so you can click menu buttons without the game
reacting. **Hold Shift or Alt** to hand control back to the game while the menu
stays open.

- **No-Clip** checkbox: fly through everything.
- **Speed** slider: movement speed (units/sec).
- **Infinite Lives**: tops the lives counter back up to 20.
- **Infinite Breath**: refills Ty's air supply every frame.
- **Invincible**: flips the game's own invulnerability flag.
- **Super Speed**: multiplies the hero's run/swim/air/glide speed fields
  (1x–10x slider); the original values are restored when toggled off.

While flying:

| Key | Action |
|-----|--------|
| `W` / `S` | forward / back (relative to where the camera looks) |
| `A` / `D` | strafe left / right |
| `Space` / `Ctrl` | up / down |
| `Shift` | 3x speed |
| `+` / `-` | adjust speed |

Movement is camera-relative: the game is still reading the keyboard and moving Ty
toward where the camera points; TyMod measures that direction and flies along it.

## World (level warp)

The **World** section can teleport between all 19 levels (Rainbow Cliffs,
Two Up, Ship Rex, ...) with a dropdown + **Warp** button. The combo shows your
pending pick and syncs with the level when the game changes it.

## Visuals / FOV

A **Set FOV** checkbox + 40–120° vertical-FOV slider rescales the game's
projection matrix each frame.

> **Known issue:** the FOV override is buggy. On wide settings the game does not
> load the world geometry that a normal FOV camera would never see, so level
> parts at the edges of the view are missing/hidden until they move toward the
> screen center (the game culls with its own, unmodified frustum).

## Collectible ESP

Colored on-screen markers for every collectible, with distance-scaled dots,
name/distance labels, and edge arrows that sweep around the screen border for
off-screen or behind-camera items:

- **Opals** — works in playable levels; 300 object slots at the MTP sync array.
- **Crates** and **Invisible Crates**.
- **Caged Bilbies**.
- **Golden Cogs**.
- **Thunder Eggs** — same struct layout as Golden Cogs.
- **Rainbow Scales** — Rainbow Cliffs hub only (level 0), same array as opals.

Toggles for labels, distance text, off-screen markers, a **Max Dist** filter
(logarithmic slider, up to 100,000), and per-category colors. The struct layout
(position field) is auto-detected at runtime per level, retrying until the
category's arrays resolve.

## Hotkeys & persistence

- The **Hotkeys** section binds the menu toggle plus No-Clip, ESP and
  Super Speed toggles to any key (click a bind, press a key; ESC clears).
- All settings (hotkeys, FOV, cheats, No-Clip speed, ESP options and colors)
  persist automatically to `tymod.ini` next to TY.exe and are reloaded at
  startup.

## Layout

```
TyMod/
  proxy/            C++ mod source (dllmain, mod, overlay, noclip, camera, health, cheats, esp, settings, log)
  vendor/           imgui + minhook (git submodules)
  tools/            build.ps1, install.ps1, uninstall.ps1, source.ps1
  build/            build output (OpenAL32.dll)
  backup/           pristine OpenAL32.dll.orig
```

`tools\source.ps1` assembles a clean, GitHub-ready repo in `dist\TyMod-repo\`
(proxy + tools + docs, no build artifacts) with imgui and minhook added as git
submodules and an initial commit made.

## Build

Requires Visual Studio 2022 with the C++ workload (x86 toolset).

Clone with the submodules, then build:

```powershell
git clone --recursive https://github.com/ReactiveSlime/Ty-mod-menu.git
cd Ty-mod-menu
pwsh -File tools\build.ps1
```

Already cloned without `--recursive`? Run `git submodule update --init --recursive`
first. The C++ toolset must also be on `PATH` (or `vcvars32.bat` sourced).

## Install / Uninstall

```powershell
pwsh -File tools\install.ps1      # auto-detects the Steam install, backs up, copies proxy
pwsh -File tools\uninstall.ps1    # restores the original OpenAL32.dll
```

`install.ps1` accepts `-GameDir "D:\...\TY the Tasmanian Tiger"` if auto-detect fails.

## Runtime log

`tymod.log` is written next to `TY.exe` and records load + hook + GL info. Useful
if something goes wrong.

## Credits

The in-game cheats (Infinite Lives, Infinite Breath, Invincibility, Super Speed,
Level Warp) and the collectible ESP use reverse-engineered memory addresses and
game logic from the open-source
[Mul-Ty-Player](https://github.com/xMcacutt/Mul-Ty-Player) project by
**xMcacutt** — the cooperative multiplayer mod for TY the Tasmanian Tiger. Huge
thanks to that project's research.

## Notes

- 32-bit game, so the mod is built x86.
- The proxy is statically linked (no external CRT dependency) and its only real
  import besides the forwarded OpenAL functions is KERNEL32.
- Player position is at `TY.exe + 0x270B78` (X, Y, Z floats, Y-up), with a mirror
  copy at `+0x10`.
- Ty's hit counter (max 4) is an int32 at `TY.exe + 0x2737CC`. Infinite Health
  rewrites it to 4 every frame.
- `health.cpp` still contains an exact-value memory scanner used to locate such
  addresses during development, but it is no longer exposed in the menu.
