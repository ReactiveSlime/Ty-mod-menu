# Building & Publishing TyMod

Three independent outputs: the **DLL** (build), the **ZIP** (players), and the
**GitHub repo** (source). You can produce any of them on their own.

## 1. Build the DLL

Prerequisites: Visual Studio 2022 with the C++ workload (x86 toolset). The path
to `vcvars32.bat` is hard-coded in `tools\build.ps1`.

```powershell
pwsh -File tools\build.ps1
```

- Compiles `proxy\*.cpp` + the vendored **imgui** and **minhook** sources into
  a 32-bit, statically linked (`/MT`) `OpenAL32.dll` proxy.
- Parses the OpenAL export list from `tools\soft_oal_exports.txt` and emits
  forwarding stubs so audio keeps working.
- Output: `build\OpenAL32.dll`

Requires `vendor\imgui` and `vendor\minhook` to be present (they are git
submodules — `git submodule update --init --recursive`).

## 2. Publish the ZIP (for players)

Packages the built DLL with an installer and a plain-text readme.

```powershell
pwsh -File tools\dist.ps1               # build, then package
pwsh -File tools\dist.ps1 -SkipBuild    # package the existing build\OpenAL32.dll
pwsh -File tools\dist.ps1 -Version 1.1  # override the version label
```

Output (`<ver>` defaults to `1.0`):

```
dist\TyMod-<ver>\          OpenAL32.dll, Install.bat, Install.ps1,
                           Uninstall.bat, README.txt
dist\TyMod-<ver>.zip       the above, zipped
```

Share `dist\TyMod-<ver>.zip` (e.g. attach it to a GitHub **Release**).

## 3. Publish the source to GitHub

Assembles a clean, upload-ready git repo — source only, no vendor blobs.

```powershell
pwsh -File tools\source.ps1
```

Output: `dist\TyMod-repo\`

- Copies `proxy\` (`.cpp`/`.h`), `tools\` (minus the local-only `dist.ps1`),
  and the root `*.md` docs; writes a `.gitignore`.
- Adds `vendor\imgui` and `vendor\minhook` as **git submodules**, pinned to the
  commits currently checked out under `vendor\` (so the build matches what was
  tested). URLs are overridable with `-ImguiUrl` / `-MinhookUrl`.
- Runs `git init`, stages everything, and makes an initial commit if a git
  identity is configured.

### First push

```powershell
cd dist\TyMod-repo
gh repo create <name> --public --source=. --remote=origin --push
```

or with plain git (after creating an empty repo on GitHub):

```powershell
git remote add origin https://github.com/<you>/<repo>.git
git branch -M main
git push -u origin main
```

### Later updates

Edit the files **in `dist\TyMod-repo\`** and commit/push as normal:

```powershell
git add -A
git commit -m "..."
git push
```

Re-running `source.ps1` regenerates the folder from scratch — handy for sweeping
changes, but it wipes `.git` and the configured remote, so re-add the remote and
push again afterward.

> Cloning the repo needs the submodules:
> `git clone --recursive <url>`, or `git submodule update --init --recursive`.
