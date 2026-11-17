# Terni integrations

Plugins that let a game use [Terni](https://github.com/ZBager/Terni) through a game
engine's own scripting and editor, without touching Vulkan or Terni's C++ API. Terni
runs the tick of a strategy game as compute shaders, with the same results on every
GPU and CPU.

| Folder | Engine | State |
| --- | --- | --- |
| `flax/` | Flax Engine 1.13 | Works on Linux and Windows, in the editor and in cooked games; see [flax/README.md](flax/README.md) for what is missing |
| `godot/` | Godot 4.5+ | Works on Linux: GDScript and C#, the editor, exported games; Windows is next. See [godot/README.md](godot/README.md) |
| | Unreal Engine | Planned |
| | Unity | Planned |

`native/` builds what every plugin links: Terni itself (pinned to one commit),
`terni_bridge` (Terni behind a C++14 interface without exceptions, for engines whose
headers are not C++20) and `terni_compile_rules` (compiles a game's rules without a GPU).

Every plugin offers the same things: load a game folder, tick it (blocking or in the
background), read fields in bulk, write fields, modifiers and entities, save and load,
choose the device, and report errors through the engine's log.

Licensed like Terni: LGPL-2.1-only with the Terni linking exception (`LICENSE`,
`LICENSE-EXCEPTION`).
