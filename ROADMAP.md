# Roadmap

What the engine plugins need next, in order of priority. The order is a priority, not a
promise. The larger steps that involve Terni itself (a C API and a shared library build,
then plugins for Unreal Engine and Unity) are planned in
[Terni's roadmap](https://github.com/ZBager/Terni/blob/main/docs/roadmap.md), section
"Engine integrations".

## 1. Ship a software Vulkan driver

For machines whose GPU cannot run the simulation: build or obtain Mesa's lavapipe for
Windows and Linux, ship it with cooked and exported games, add it with
`add_vulkan_driver()` before devices are listed, and fall back to it when no GPU is
usable. Mesa's licence goes into `native/notices/Terni-NOTICES.txt`. Test on a setup
without a usable GPU.

## 2. Continuous integration

- Linux with lavapipe: build `terni_bridge` and the Godot extension, run the Godot
  self-tests (GDScript and C#, headless).
- Flax: a build-only job if the editor cannot be fetched in CI.
- Windows: build jobs for both plugins.

## 3. Verify the latest Terni on Windows

Terni's recent changes touch device creation (robust buffer access, calibrated
timestamps), how ticks are timed, the save format and the built-in stages, and the
plugins now offer mods, saves loaded by name, lost devices, hashes on the device and
stage times. The plugins pass their tests on Linux. Continuous integration (item 2)
builds both plugins on Windows, and Terni's runs its tests there on lavapipe, but
neither has a GPU. Run on Windows with a discrete GPU:

- Flax: the editor self-test, a cooked game (with the example mod beside it), visual
  scripts, and the renderer's GPU match.
- Godot: GDScript and C#, the Vulkan and Direct3D 12 renderers, an exported game (with
  the example mod beside it), the Terni debugger tab and the Monitors tab.
- Terni's own test suite on the same machine.

## 4. Releases

Versioned releases that name the Terni release they pin; prebuilt binaries per
platform, so users need not build Terni; publishing on the Godot Asset Library and in a
form Flax users can install; the licence notices taken from Terni once it ships them.
These follow Terni's own release policy.

## 5. Calls lockstep games use

Calls a lockstep game makes that the plugins lack: `tick_checked()` and its issues,
writing a generated starting state before the start (and reading it back), adding one
modifier bundle to many rows in one call, and compile statistics. Terni's planned C API
is meant to carry these for every plugin at once, so the plugins take only what a game
needs before then.
