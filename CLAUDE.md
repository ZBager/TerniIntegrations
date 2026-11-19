# CLAUDE.md

Guidance for Claude Code (and other assistants) working in this repository.

TerniIntegrations holds the engine plugins for [Terni](https://github.com/ZBager/Terni),
a C++20 library that runs the tick of a strategy game as Vulkan compute shaders with
bit-identical results on every GPU and CPU. Each plugin lets a game use Terni through an
engine's own scripting and editor. Read `README.md` and each plugin's README first.
`ROADMAP.md` is the plan for the plugins: work in its order unless asked otherwise, and
update it when an item is done or the plan changes. The larger steps are in Terni's own
`docs/roadmap.md` (section "Engine integrations"): Flax and Godot are done; next is a C
API in Terni itself, then Unreal Engine and Unity.

## Hard rules

- **Terni stays engine-agnostic; engine code lives here.** Never put engine-specific
  code into Terni. Change Terni only when the change helps every host, and say so.
- **Determinism is the product.** The plugins must give the same results as Terni on
  its own: every self-test checks the galaxy example's state hash after 200 ticks,
  `ad93f051a2965dfb`, on every Vulkan device. Never convert simulation values through
  floating point except for display (`TerniValue` / `TerniWorld.to_float`).
- **Licence header on every new source file** (C++, C#, GDScript, CMake, scripts), as
  the first two lines, with the comment syntax of the file:
  ```
  // SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
  // Copyright (C) 2026 Rafał Zając
  ```
  Batch files use `rem` and ASCII only (`Rafal Zajac`). Do not change `LICENSE` or
  `LICENSE-EXCEPTION`; they are copies of Terni's.
- **No private names.** The repository is public. Never add absolute paths with a user
  name in them, email addresses, or names of private projects that use Terni (this file
  may name `~/Projects/...` for the local workflow). Before committing, grep the diff for
  `/home/`, `C:\Users` and project names.
- **Windows and Linux.** Every plugin must build and pass its tests on both. Code is
  standard C++ (no GCC-only extensions such as `, ##__VA_ARGS__`); batch files are
  ASCII.

## Layout

| Path | Contents |
| --- | --- |
| `native/` | CMake: Terni (pinned to one commit, `TERNI_COMMIT`), `terni_bridge`, `terni_compile_rules`, `notices/` (`Terni-NOTICES.txt`, and `lavapipe-NOTICES.txt`, appended when the software driver ships) |
| `native/lavapipe/` | `build.sh` / `build.bat`: Mesa's lavapipe with LLVM inside, from pinned, checksummed sources, into `build/lavapipe`; the plugins' build scripts ship it from there. `build.bat` unpacks with `unpack.py` (Windows' `tar.exe` hung on the LLVM sources in CI) |
| `native/bridge/` | `terni_bridge`: Terni behind a C++14 interface where no call throws, and the lookup of the renderer's GPU (`renderer_gpu.cpp`: Vulkan UUID, DXGI LUID, vendor and name) |
| `flax/` | Flax Engine 1.13 plugin: `Source/Terni` (TerniWorld, TerniSimulation), `Source/TerniEditor` (Terni window, cooking, reload), `Example/` with the self-test |
| `godot/` | Godot 4.5+ GDExtension: `src/` (TerniWorld, TerniSimulation), `addons/terni` (editor plugin, debugger tab, export plugin, C# wrapper), `example/` (GDScript) and `example-csharp/` with self-tests |
| `build/` | All build output, ignored |

Every plugin links Terni::runtime (no shader compiler in the engine process: engines
bundle their own glslang, which clashes with shaderc) and runs `terni_compile_rules`
as a separate program, cache first. The compiler ships with exported games, so mods can
change rules.

## Build and test (Linux)

```sh
native/lavapipe/build.sh                                 # the software Vulkan driver; once, long (LLVM)
TERNI_DIR=~/Projects/Terni flax/build-native.sh          # omit TERNI_DIR for the pinned commit
FLAX_EDITOR=$PWD/build/FlaxEditor/Binaries/Editor/Linux/Development/FlaxEditor flax/test.sh
flax/test.sh --reload | --visual | --cooked              # same FLAX_EDITOR

TERNI_DIR=~/Projects/Terni godot/build.sh
godot/test.sh                                            # headless
godot/test.sh --window --gpu-index 0                     # renderer GPU check (here 0 = AMD, 1 = NVIDIA)
godot/test.sh --csharp                                   # godot-mono
GODOT_TEMPLATES=$PWD/build/godot-templates/templates godot/test.sh --export
GODOT_MONO_TEMPLATES=$PWD/build/godot-templates-mono/templates godot/test.sh --export-csharp
godot/test.sh --no-gpu [mode]                            # system Vulkan drivers hidden: the shipped driver
flax/test.sh --no-gpu [mode]                             # (same FLAX_EDITOR; Flax renders with -null)
```

- `build/FlaxEditor` is a private copy of the Flax editor with the Linux platform files
  (needed for cooking), extracted from `~/Projects/FlaxEditorLinux.zip` and
  `~/Projects/Linux.zip`. Switching editor installs needs `flax/Cache` and
  `flax/Example/Cache` deleted (cached bindings hold absolute paths).
- `build/godot-templates*` hold only the Linux x86_64 export templates from the
  official archives.
- Windows: `flax\build-native.bat`, `flax\test.bat [reload|visual]`, `godot\build.bat`,
  `godot\test.bat [nogpu] [window <godot args>|csharp]`. The owner runs these on a separate
  Windows machine; give exact commands, and for editor commands typed at the prompt use
  `start /wait "" "%EXE%" ...` (cmd does not wait for GUI programs otherwise).
- A change is done when the tests pass on every device here, and, for anything that
  touches Windows code paths, when the owner has run the Windows tests.

## Continuous integration

`.github/workflows/ci.yml` builds both plugins and runs their tests on Linux and
Windows runners, which have no GPU: everything runs on lavapipe. The software driver is
built once per version of `native/lavapipe/build.sh` (and `build.bat`) and cached. The
Flax editor for Linux comes from flaxengine.com (pinned package and checksums); the
Windows editor comes only through Flax's launcher, so Windows builds Terni for Flax only.

Try a Linux job in a clean container before pushing (`.github/replay/`, usage at the top
of each script; `build/ci-replay` gets the results). What the runners lacked:
- The Flax editor needs X11 and GLib libraries even headless, game code links against
  their development packages (and zlib's and Wayland's), and play mode (`-play`) opens a
  window: `xvfb-run`.
- git refuses a repository that another user owns (`safe.directory`), as in a container.

## Moving the Terni pin

1. Push the Terni change first (Terni's own rules: it is a public repository).
2. Set `TERNI_COMMIT` in `native/CMakeLists.txt` to the full hash.
3. Delete `build/native`, `build/terni` and `build/godot`, rebuild both plugins from
   the pinned commit (no `TERNI_DIR`), and run their tests.

A Terni API change that `terni_bridge` uses needs the bridge (and both plugins) updated
in the same step.

## Engine pitfalls found so far

Flax 1.13:
- Engine headers do not compile as C++20, and Terni's need it: Flax modules include only
  `terni_bridge.h`. Static libraries go in `options.LinkEnv.InputFiles`.
- `API_STRUCT`s returned in arrays need a `TIsPODType<>` specialization. C# fills structs
  with zeros and ignores C++ defaults, so every `TerniStartOptions` default is zero.
- Visual scripts cannot bind C# events and do not see `[HideInEditor]` members.
- `Editor.UI` is null in a headless editor. A cooked game that quits in its first frame
  crashes in Flax's shutdown; the self-test waits 30 frames.
- Cooked Linux games get all-zero Vulkan device IDs from Flax, hence the vendor-and-name
  fallback in `renderer_gpu.cpp`.
- Flax uses the newest .NET SDK it finds. With the .NET 11 SDK, the 1.13 editor compiles
  the scripts again in play mode and fails an assertion as it reloads them: point
  `DOTNET_ROOT` (and `PATH`) at a .NET 10 install of its own, as CI does.

Godot 4.5+ (godot-cpp `godot-4.5-stable`):
- No global variables of Godot types (`String`, `NodePath`, ...) in extension code: they
  are made before godot-cpp is ready and crash the editor as the library loads.
- The library exports only its entry point (`--exclude-libs,ALL`); on Windows it uses
  the shared C++ runtime (`GODOTCPP_USE_STATIC_CPP OFF`), as Terni's libraries do.
- Game folders need a `.gdignore` (Godot would import the `.glsl` rules), so the export
  plugin copies them beside the program; `res://` resolves there in exported games.
- The game runs in its own process: the editor sees it only through the debugger
  connection (`EngineDebugger` messages, `terni:` prefix).
- GDExtension classes have no generated C# bindings: `addons/terni/csharp/Terni.cs`
  wraps them. A C# project needs its `.csproj` and, to export, its `.sln` committed.
- The first command-line `--import` of a fresh project with the extension crashes Godot
  as it quits (the import itself is done); the test scripts import twice.

The software driver (`native/lavapipe`):
- Built for compute only and self-contained: `readelf -d` must list only libc, libm and
  the dynamic linker (the script fails otherwise). Mesa takes libdrm and libudev from
  pkg-config if it finds them, so the script hides pkg-config and builds libdrm (which
  Mesa requires on Linux) from Mesa's wrap; a version script exports only the three
  `vk_icd*` entry points (without it, all of LLVM's symbols are exported).
- LLVM 23 needs its `libc` folder to configure, and finds Z3 on its own unless told not to.
- The bridge adds it only where the system has no usable software device, so the
  normal tests here use the system's lavapipe; `--no-gpu` (`VK_DRIVER_FILES` pointing
  at nothing) tests the shipped one. Anything a plugin's build copies from `native`
  must be copied on every build, not after a link (a changed file would not be).

General: run one rule compiler at a time per process (worlds starting together share a
cache folder); on Windows a file another process has open cannot be replaced.

## Changes that require other changes

- A plugin's public API: its README.
- What a plugin offers or lacks: the root README's table and the paragraph under it,
  and Terni's `README.md` (Engine plugins) and `docs/roadmap.md`.
- Anything shipped beside games: `native/notices/Terni-NOTICES.txt` if a library is
  added. `lavapipe-NOTICES.txt` covers what is inside the software driver: when Mesa or
  LLVM moves, check the licences of the files it is built from again (`ninja -t deps`
  in `build/lavapipe/mesa-build` lists them; most carry SPDX identifiers) and copy any
  new licence texts from the sources.

## Git

- `main` is the only branch and is public. Commit with the configured identity (the
  GitHub no-reply address). Commit and push only when the owner asks, or as part of
  work the owner asked for in this repository; add commits rather than rewriting
  history.
- Commit messages: a short summary line, then what changed and why.
- `.gitignore` rules for files an engine generates are scoped to that engine's folder
  (`flax/**/...`), so another plugin's own files are never left out.
