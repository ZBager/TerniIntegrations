# Terni integrations

[![CI](https://github.com/ZBager/TerniIntegrations/actions/workflows/ci.yml/badge.svg)](https://github.com/ZBager/TerniIntegrations/actions/workflows/ci.yml)

Plugins that let a game use [Terni](https://github.com/ZBager/Terni) through a game
engine's own scripting and editor, without touching Vulkan or Terni's C++ API. Terni
runs the tick of a strategy game as compute shaders, with the same results on every
GPU and CPU.

| Folder | Engine | State |
| --- | --- | --- |
| `flax/` | Flax Engine 1.13 | Works on Linux and Windows: C++, C# and Visual Scripting, the editor, cooked games. See [flax/README.md](flax/README.md) |
| `godot/` | Godot 4.5+ | Works on Linux and Windows: GDScript and C#, the editor, Vulkan and Direct3D 12, exported games. See [godot/README.md](godot/README.md) |
| | Unreal Engine | Planned |
| | Unity | Planned |

`native/` builds what every plugin links and ships: Terni itself (pinned to one commit),
`terni_bridge` (Terni behind a C++14 interface without exceptions, for engines whose
headers are not C++20 or that build without exceptions, the lookup of the GPU the
engine renders with, and the software driver's setup), `terni_compile_rules` (compiles
a game's rules, with mod folders on top, without a GPU, and ships with games so mods can
change rules), `lavapipe/` (builds Mesa's lavapipe, a software Vulkan driver, from
source) and `Terni-NOTICES.txt` (the licence notices a game that ships them needs).

Every plugin offers the same things: load a game folder with mod folders on top, start
and tick it (blocking or in the background), read fields in bulk, write fields,
modifiers and entities, save and load (saves of an earlier version of the game by
name), compare state hashes computed on the device to find a desync, start again from a
save when the device is lost, run on the GPU the engine renders with or a chosen one
(or, where no GPU can run the simulation, on the CPU with the software driver shipped
with the game), report errors through the engine's log, show the simulation in the
editor (with the GPU time of each stage), and put game folders, the rule compiler and
the software driver into exported games. What comes next is in
[ROADMAP.md](ROADMAP.md).

Licensed like Terni: LGPL-2.1-only with the Terni linking exception (`LICENSE`,
`LICENSE-EXCEPTION`).
