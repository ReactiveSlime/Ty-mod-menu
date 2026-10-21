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

**Controller:** **R3 + L3** (both stick clicks) toggles the menu. While it is
open the game sees a neutral pad, so pad input never leaks into the game.
Navigate with the **D-pad or left stick** (they act as arrow keys — tabs,
checkboxes, sliders and buttons all respond), **A** activates and **B** backs
out. The mouse keeps working normally alongside the pad. **RB** held gives the
3x flight speed, same as Shift.

While flying in No-Clip the pad flies too: **left stick** = forward/strafe and
the **triggers** = down/up (right trigger up, left trigger down).

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

(On a controller: **RB** held = 3x speed, same as Shift.)

Movement is camera-relative: the game is still reading the keyboard and moving Ty
toward where the camera points; TyMod measures that direction and flies along it.

## World (level warp)

The **World** section can teleport between all 19 levels (Rainbow Cliffs,
Two Up, Ship Rex, ...) with a dropdown + **Warp** button. The combo shows your
pending pick and syncs with the level when the game changes it.

## Visuals / FOV

A **Set FOV** checkbox + 40–120° vertical-FOV slider rescales the game's
projection matrix each frame. The same section has an FPS limiter
(**Limit FPS** checkbox + 15–240 target slider; defaults to the display's
refresh rate).

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
(logarithmic slider, up to 100,000), and per-category on/off toggles + colors.
The struct layout (position field) is auto-detected at runtime per level,
retrying until the category's arrays resolve.

## Hotkeys & persistence

- The **Hotkeys** section binds the menu toggle plus No-Clip, ESP and
  Super Speed toggles to any key (click a bind, press a key; ESC clears).
- All settings (hotkeys, FOV, FPS cap, cheats, No-Clip speed, ESP options and
  colors) persist automatically to `tymod.ini` next to TY.exe and are reloaded
  at startup.

## Known issues

**Game-side (memory) issues**
- **No-Clip water: splash SFX spam vs. water grab** (2026-10-21): the game
  forces a swim hero-state (`+0x27158C`, e.g. `UnderwaterSwimming=17`,
  `IdleUnderwater=39`) while Ty is in water and the swim physics re-clamp his
  position to the surface ("water grabs Ty"). Overwriting those water states
  with `Gliding` (9) each frame fixes the grab but makes the game re-detect
  water entry every frame → **splash SFX spams constantly**. Holding a stable
  in-water state (`TreadingWater=38`) silences the splash but the water grabs
  again. Net: you can have the grab fixed *or* the splash fixed, not both.
  Currently landed on the glide fix (grab fixed, splash spams) and written off
  as a sound-only issue. Next step: find the game's water-entry detection (a
  "was underwater" bool / surface-Y compare) and suppress just that, or hook the
  splash `alSourcePlay`; needs the `NoClip heroState=` diagnostic to know what
  state values actually occur.
- **Infinite Health fails in Outback Safari (level 10)** (2026-10-18): health now
  keys off the `IsBull` bool (`+0x27E544`) or level id 10 → `0x273814` (MTP's
  `SetOutbackHealth`), but it doesn't hold there. The bull-ride health counter
  may live elsewhere or be a different type. Next step: re-enable the
  exact-value scanner (`health.cpp`) and scan while riding.
- **FOV override is cull-broken.** At wide FOV the game doesn't load geometry
  its own unmodified frustum would never see, so level parts at the screen edges
  appear/disappear (see the Visuals/FOV note above). Game-side limitation.

**Code issues (maintainer)**
- `col*` INI colours never load — `settings.cpp` colour branch has a
  `sscanf_s` format/count mismatch.
- A few fragile spots: bare `__except {}` fault handlers can hide real crashes,
  and the health scanner dereferences stale candidates without SEH.

## Roadmap / backlog

Future mod menu items and reverse-engineering notes, grouped by effort.

### Known addresses (this build)
- Module base is ASLR'd per launch; use **module-relative offsets**.
- Player position: `TY.exe + 0x270B78` (X, Y, Z floats, Y-up; mirror copy at `+0x10`).
- Ty hit counter (max 4, int32): `TY.exe + 0x2737CC`. Infinite Health writes `4` every frame.
- Addresses were found with the exact-value memory scanner in `health.cpp`:
  scan for a value, get it to change, filter for drops, pick. The scanner is
  built into the DLL but not reachable from the menu in this build.

### Shipped
- [x] No-Clip (camera-relative flight)
- [x] No-Clip chase camera (keeps Ty centred; camera jitter fixed)
- [x] Infinite Health
- [x] Speed slider
- [x] Health exact-value scanner (fallback if a patch moves the address)

### Cheap (addresses/patterns already known or one scan away)
- [ ] **Teleport bookmarks**: save/recall player spots. Store a few float[3]
      copies of `posAddr`; a menu button per slot to save/recall. Pairs perfectly
      with No-Clip.
- [x] **Infinite Lives**: the lives counter sits in the game's save struct
      (pointer at `0x2888D8`, display offset `+0xAD0`); the mod tops it up to 20
      whenever it drops below that.
- [ ] **Super Speed (on foot)**: walk-speed multiplier while No-Clip is OFF.
      Find the player speed/velocity field (near posAddr) or scale the per-frame
      move. Toggle + slider.
- [ ] **Game speed slider**: scale the engine's time delta (slow-mo / fast
      forward). Might be a float global near the update code.

### Medium
- [ ] **Unlock All Moves / Abilities**: unlock bite, tail whip, etc.
- [x] **Infinite Breath** (underwater): refill the air value (`+0x2737F0`)
      every frame.
- [ ] **Freeze timers**: mission / race countdowns.
- [ ] **Super Jump / No Fall Damage**: poke vertical velocity or the fall-damage
      check.
- [x] **FOV slider**: find the projection/FOV value.

### Larger
- [x] **Collectible ESP / waypoints**: draw markers to collectibles (needs a
      list/struct scan).
- [ ] **One-hit kill enemies** / **disable enemy AI**.

### Quality of life
- [x] Configurable menu hotkey (currently INSERT).
- [x] Persist settings (enabled features, speed) to a config file next to the DLL.
- [x] Per-feature hotkeys.

## Layout

```
TyMod/
  proxy/            C++ mod source (dllmain, mod, overlay, noclip, camera, health, cheats, esp, settings, gamepad, log)
  vendor/           imgui + minhook (git submodules)
  tools/            build.ps1, dist.ps1, release.ps1, sync-src.ps1, install.ps1, uninstall.ps1, source.ps1
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
