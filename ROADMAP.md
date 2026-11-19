# Roadmap

What the engine plugins need next, in order of priority. The order is a priority, not a
promise. The larger steps that involve Terni itself (a C API and a shared library build,
then plugins for Unreal Engine and Unity) are planned in
[Terni's roadmap](https://github.com/ZBager/Terni/blob/main/docs/roadmap.md), section
"Engine integrations".

## 1. Verify the latest Terni on Windows

Terni's recent changes touch device creation (robust buffer access, calibrated
timestamps), how ticks are timed, the save format and the built-in stages, and the
plugins now offer mods, saves loaded by name, lost devices, hashes on the device and
stage times. The plugins pass their tests on Linux, and continuous integration
(`.github/workflows/ci.yml`) runs them on every push: on Linux all of them, Flax's
included; on Windows it builds both plugins and the software driver and runs Godot's
GDScript, C# and no-GPU tests, all on lavapipe, since no runner has a GPU. Run on
Windows with a discrete GPU:

- Flax: the editor self-test, a cooked game (with the example mod beside it), visual
  scripts, and the renderer's GPU match.
- Godot: GDScript and C#, the Vulkan and Direct3D 12 renderers, an exported game (with
  the example mod beside it), the Terni debugger tab and the Monitors tab.
- Terni's own test suite on the same machine.
- The software Vulkan driver (CI builds it and runs Godot on it): the Flax editor and a
  cooked Flax game, and an exported Godot game, without a usable GPU. Run `godot\test.bat
  nogpu` from a prompt that is not elevated, since an elevated one makes the Vulkan
  loader ignore `VK_DRIVER_FILES`; `flax\test.bat` has no such mode yet.
- Godot's `test.bat` always ended with exit code 1 before CI found why (`findstr /x`
  and LF line endings): earlier Windows runs that printed PASSED did pass.

## 2. Releases

Versioned releases that name the Terni release they pin; prebuilt binaries per
platform, so users need not build Terni; publishing on the Godot Asset Library and in a
form Flax users can install; the licence notices taken from Terni once it ships them.
These follow Terni's own release policy.

Linux binaries for a release must be built on an older distribution: built on a current
one they need its glibc (here 2.43 for the Godot extension and 2.44 for the software
driver). The software driver adds about 73 MB to a game; an LLVM built for size, or
with link-time optimisation, could make it smaller.

CI's slowest step is the C# example's build on Windows (about 10 minutes, the first
.NET restore and build on a fresh runner); a cache of NuGet packages may shorten it.

## 3. Calls lockstep games use

Calls a lockstep game makes that the plugins lack: `tick_checked()` and its issues,
writing a generated starting state before the start (and reading it back), adding one
modifier bundle to many rows in one call, and compile statistics. Terni's planned C API
is meant to carry these for every plugin at once, so the plugins take only what a game
needs before then.
