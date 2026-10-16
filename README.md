# TyMod

A no-clip mod for **TY the Tasmanian Tiger** (Steam HD, 2016) that bakes into the
game as a DLL proxy, with an in-game ImGui menu.

## How it works

TY.exe imports `OpenAL32.dll` from its own folder. TyMod replaces that DLL with a
small proxy that:

1. **Forwards all 157 OpenAL exports to `soft_oal.dll`** (the real OpenAL-Soft
   binary that ships in the game folder), so audio is unaffected.
2. Hooks GDI's `SwapBuffers` (the game's frame present) and draws an **ImGui**
   overlay on top of the game each frame.
3. Provides the **No-Clip** feature.

No separate launcher or injector is needed — Steam launches the game as usual.

## Menu

Press **INSERT** in-game to toggle the menu.

- **No-Clip** checkbox — fly through everything.
- **Infinite Health** checkbox — keeps Ty's hit counter full (4), so you never
  run out of hits or lose a life.
- **Speed** slider — movement speed (units/sec).

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

## Layout

```
TyMod/
  proxy/            C++ mod source (dllmain, mod, overlay, noclip, camera, health, log)
  vendor/           imgui + minhook (git submodules)
  tools/            build.ps1, install.ps1, uninstall.ps1, dist.ps1, source.ps1
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
