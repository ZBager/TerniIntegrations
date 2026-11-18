# Roadmap

What the engine plugins need next, in order of priority. The order is a priority, not a
promise. The larger steps that involve Terni itself (a C API and a shared library build,
then plugins for Unreal Engine and Unity) are planned in
[Terni's roadmap](https://github.com/ZBager/Terni/blob/main/docs/roadmap.md), section
"Engine integrations".

## 1. Verify the latest Terni on Windows

Terni's recent changes touch device creation (robust buffer access, calibrated
timestamps), how ticks are timed, the save format and the built-in stages. The plugins
pass their tests on Linux; continuous integration runs Windows only on lavapipe. Run on
Windows with a discrete GPU:

- Flax: the editor self-test, a cooked game, and the renderer's GPU match.
- Godot: GDScript and C#, the Vulkan and Direct3D 12 renderers, and an exported game.
- Terni's own test suite on the same machine.

## 2. Offer what Terni added

Nothing of the following is in `terni_bridge` or the plugins yet:

- **Mods on top of a game's folder** (`load_game({game, mod, ...})`): let a game load its
  folder with mod folders on top, in cooked and exported games too, and choose them
  (a list of folders, in order). The plugins already ship the rule compiler so mods can
  change rules; this lets them load. Point modders to `terni_check`.
- **Saves from an earlier version of a game**: loading with `SaveMatch::ByName`, with
  the lines it returns, and `save_checksum()` to tell which load a save needs.
- **A lost device as its own event**: today `terni::DeviceLost` arrives as a generic
  error. A signal (Godot) or event (Flax) lets a game start again from its last save,
  on the same device or another.
- **State hashes on the device** (`field_hashes()`, `entity_hashes()`): cheap desync
  checks for lockstep games, and a state hash in the editor panels.
- **GPU time per stage** (`stage_times()`): in the Godot debugger tab and the Flax Terni
  window, and as engine profiler data where the engine has it.
- **Smaller calls**: `alive_bits()`, `entities_free()`, and each device's buffer
  robustness in the device lists.

## 3. Ship a software Vulkan driver

For machines whose GPU cannot run the simulation: build or obtain Mesa's lavapipe for
Windows and Linux, ship it with cooked and exported games, add it with
`add_vulkan_driver()` before devices are listed, and fall back to it when no GPU is
usable. Mesa's licence goes into `native/notices/Terni-NOTICES.txt`. Test on a setup
without a usable GPU.

## 4. Continuous integration

- Linux with lavapipe: build `terni_bridge` and the Godot extension, run the Godot
  self-tests (GDScript and C#, headless).
- Flax: a build-only job if the editor cannot be fetched in CI.
- Windows: build jobs for both plugins.

## 5. Releases

Versioned releases that name the Terni release they pin; prebuilt binaries per
platform, so users need not build Terni; publishing on the Godot Asset Library and in a
form Flax users can install; the licence notices taken from Terni once it ships them.
These follow Terni's own release policy.

## 6. Calls lockstep games use

Calls a lockstep game makes that the plugins lack: `tick_checked()` and its issues,
writing a generated starting state before the start (and reading it back), adding one
modifier bundle to many rows in one call, and compile statistics. Terni's planned C API
is meant to carry these for every plugin at once, so the plugins take only what a game
needs before then.
