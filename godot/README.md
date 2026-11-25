# Terni for Godot

A GDExtension that runs a [Terni](https://github.com/ZBager/Terni) game folder from
GDScript and C#: the `TerniSimulation` node and the `TerniWorld` object. The simulation runs
on a Vulkan device of its own, next to the renderer, and gives the same results on
every machine.

Made for Godot 4.5 and later (built with godot-cpp 4.5, tested with 4.7). Tested on
Linux and Windows, from GDScript and C#, with Vulkan and Direct3D 12, in the editor
and in exported games.

## Build

```sh
godot/build.sh        # needs CMake, Ninja, Python 3, Git, a C++20 compiler, the Vulkan headers and shaderc
godot\build.bat       # Windows: CMake, Git, Python 3, Visual Studio 2022 or newer, the Vulkan SDK
```

On Windows the library uses the shared C++ runtime (as Terni's libraries do), so a
game needs the Visual C++ runtime, which most Windows machines have.

This puts the addon in `addons/terni` (the library and the rule compiler in `bin/`) and
copies it into the two example projects. To use it in a project, copy `addons/terni` into
the project's `addons` folder and enable the **Terni** plugin.

## Use

Put the game folder (schema, common, history, rules) in the project with an empty
`.gdignore` file in it, so that Godot does not try to import the rules, add a
**TerniSimulation** node and set its **Game Folder**.

```gdscript
extends Node

@onready var simulation: TerniSimulation = $Simulation
var town := -1
var pop := -1

func _ready() -> void:
	simulation.started.connect(_on_started)  # each time a world has started
	simulation.tick_completed.connect(_on_tick)

func _on_started() -> void:
	# Subscriptions belong to one world: make them again for every world.
	var world := simulation.get_world()
	town = world.entity("town")
	pop = world.subscribe(town, "pop")  # comes back with every tick

func _on_tick() -> void:
	var values := simulation.get_world().get_subscribed(pop)
	print("First town: ", TerniWorld.to_float(values[0], TerniWorld.FIELD_QUANTITY))
```

`TerniWorld` has the same calls as in the other plugins, in snake_case: `load(folder,
mods)`, `start(options)` / `begin_start(options)`, `tick` / `begin_tick` / `tick_ready` /
`end_tick`, `read_field`, `write_field`, `write_value`, `subscribe`, `get_subscribed`,
`create_entity`, `get_alive_bits`, `get_free_count`, `add_modifier` (a handle is a
`Vector2i`), `add_modifiers` (one bundle on many rows), `replace_modifier`, `save` / `load_save(data, by_name)`, `get_state_hash`, `get_field_hashes`,
`get_entity_hashes`, `get_stage_times`, `get_compile_stats`, `get_sync_reads`, `get_devices`, `find_renderer_device`,
`move_to_device`. Calls that return `bool` return true when they worked; after false,
`get_last_error()` says why, and the error is also pushed to the debugger. Start
options are a dictionary: `device`, `ignore_renderer`, `preference`, `cache_folder`,
`cache_only`, `validation`, and `spin_us`, which lets waits for a tick spin for that many
microseconds before they sleep (Terni's
[Embedding](https://github.com/ZBager/Terni/blob/main/docs/embedding.md#waiting-for-small-ticks)
says when that is worth it; the node has it as **Spin Us**). Each device in `get_devices()` has `robustness`: what a
rule that reads or writes past the end of a field does there (`ROBUSTNESS_DEFINED`, on
every current desktop driver: reads give 0 and writes are dropped, the same everywhere).

By default the simulation runs on the GPU the engine renders with, if it can: it keeps
a laptop's second GPU asleep and follows the player's choice of GPU. The node's
**Device** names one instead; **Preference** decides when the renderer's GPU cannot run
the simulation.

Rules are compiled by `terni_compile_rules`, which comes with the addon, into the folder
`spirv` inside the game folder. A world starts from that folder and runs the compiler
only for rules that are new or have changed.

### A generated starting state

Between `load` and `start`, a game can change what its history files describe, for a map
that a match generates: `set_capacity`, `set_modifier_capacity`, `set_start_count` (rows in
use), `write_start` / `read_start` (whole rows of a field, as raw words) and
`add_start_modifier`:

```gdscript
world.load("res://Galaxy")
var planet := world.entity("planet")
world.set_start_count(planet, 500)
world.write_start(planet, "pop", 0, generated_pop)  # PackedInt32Array, one value per row
world.start()
```

Every peer of a multiplayer game must make the same calls. `get_checksum()` covers the
capacities but not the starting rows, values and modifiers, so compare
`get_entity_hashes()` after the start.

### Without a usable GPU

On a machine whose GPUs cannot run the simulation (an old GPU, a virtual machine, a
server without one), it runs on the CPU with Mesa's lavapipe, a software Vulkan driver:
the same results, more slowly. `native/lavapipe/build.sh` (Windows:
`native\lavapipe\build.bat`) builds it from source, with LLVM inside and nothing else
needed on the player's machine; once it is built, `build.sh` puts it into
`addons/terni/bin`, and exported games get it with the rest
(`TERNI_SOFTWARE_DRIVER=none` leaves it out). The first time devices are listed or a
world starts, the addon adds it to the system's drivers, unless the system has a
software device of its own (most Linux distributions come with Mesa's);
`TerniWorld.get_software_driver()` says whether it did. The default preference then
chooses a GPU when one can run the simulation, and lavapipe otherwise.

It adds about 73 MB to a game. It needs the Vulkan loader (`vulkan-1.dll`,
`libvulkan.so.1`), version 1.3.234 or newer, which comes with every current GPU
driver; without one, the extension does not load at all.

### Mods

A mod is a folder laid out like the game's, holding only what it changes; Terni's
[Data format](https://github.com/ZBager/Terni/blob/main/docs/data-format.md#mods) says
how files combine. Give the node's **Mod Folders**, or load them yourself, in order:

```gdscript
world.load("res://Galaxy", ["user://mods/faster_growth", "res://Mods/more_planets"])
```

Mods change the game checksum (`get_checksum()`), so multiplayer peers with different
mods can tell before they play. Their rules are compiled into the game folder's cache
the first time, in exported games too. Point modders to Terni's `terni_check`, which
loads, compiles and runs a mod the way the game will. Give each mod folder a
`descriptor.mod` file: the export step then knows it is not a game folder and leaves it
out; players put mods beside the game (`res://Mods/x` finds `Mods/x` beside the program).

### Saves from an earlier version of the game

A patched game, or one with other mods, has another checksum, and `load_save(data)`
refuses saves made before. Load them by name: `load_save(data, true)` fits the save to
this game (new fields and entity types start as the game describes them, removed ones
are dropped) and `get_fitted_lines()` says what was fitted.
`TerniWorld.save_checksum(data)` tells beforehand which game a save was made with.

### A lost device

A rule that runs too long (an endless loop in a mod, say) makes the driver reset the
GPU, and the simulation's state on it is gone. The world stops and emits `device_lost`;
the node emits `device_lost` too, and not `failed`. Start again, on the same device or
another, and load the last save:

```gdscript
simulation.device_lost.connect(func(error: String):
	push_warning("The simulation restarts from the autosave: " + error)
	simulation.start_world()
	simulation.get_world().load_save(autosave))
```

`simulate_device_lost()` makes the next call that uses the device find it lost, to test
this without resetting a GPU.

### Desync checks

`get_field_hashes()` and `get_entity_hashes()` hash the state on the device and bring
back two words per field: cheap enough for lockstep peers to compare every few ticks
(each is a dictionary of name to hash, equal on every device that ran the same ticks).
`get_state_hash()` reads the whole state back and is much slower.

To find rules that can make peers drift apart before players do, run `tick_checked()` in a
test: a tick that also finds stages whose result depends on the order of their rows.
`get_order_issues()` then names each stage and field. An issue with `misfit` is no order
problem: a rule wrote values that do not fit a small field (`count8`, ...). Every stage runs three times, so use
a small world.

## C#

Godot makes no C# classes for extension classes, so the addon brings its own:
`addons/terni/csharp/Terni.cs`, which Godot compiles into any C# project that has the
addon. `Terni.TerniWorld` and `Terni.TerniSimulation` wrap the extension's objects with
PascalCase methods, properties and C# events:

```cs
using Godot;
using Terni;

public partial class Towns : Node
{
    private TerniSimulation _simulation;
    private int _town, _pop;

    public override void _Ready()
    {
        _simulation = new TerniSimulation(GetNode("Simulation"));
        _simulation.Started += OnStarted;       // each time a world has started
        _simulation.TickCompleted += OnTick;
    }

    private void OnStarted()
    {
        var world = _simulation.World;
        _town = world.Entity("town");
        _pop = world.Subscribe(_town, "pop");
    }

    private void OnTick()
    {
        int[] pop = _simulation.World.GetSubscribed(_pop);
        GD.Print("First town: ", TerniValue.ToDouble(pop[0], TerniFieldType.Quantity));
    }
}
```

`example-csharp/` is the galaxy in C#, with a self-test of the wrapper. Like Godot's own
C# projects it targets .NET 8, and needs the .NET 8 SDK or newer. Exporting a C# project
needs its solution file (`.sln`), which Godot makes with **Project > Tools > C# > Create
C# solution**; the plugin adds nothing to that.

## The Terni tab

While the game runs from the editor, the **Debugger** panel has a **Terni** tab: the
devices of the machine (the one the editor renders with marked), each `TerniSimulation`
of the running game with its device, tick, time per tick, entity counts, a state hash
per entity type, and the GPU time of each stage, and the values of any field you pick,
refreshed twice a second. The game runs in its own process, so the simulations send
this over the debugger connection; an exported game sends nothing. The **Monitors**
tab has the GPU time of each simulation's tick and stages, under **Terni** and the
node's name.

## Exported games

Exporting needs no setup beyond the **Terni** plugin being enabled. Godot leaves game
folders out of the exported package (their `.gdignore` says so), so the plugin's export
step compiles each game folder's rules and copies the folder, with its `spirv` folder,
beside the exported program, at the same path relative to it. A `TerniSimulation` with
`res://Galaxy` finds `Galaxy` beside the program. A game folder is any folder of the
project with a `schema` and a `rules` folder in it, outside `addons` and without a
`descriptor.mod`.

The rule compiler (`terni_compile_rules`, with shaderc where it is not linked in) and
`Terni-NOTICES.txt` go beside the program too. An exported game starts from the `spirv`
folders and compiles only rules changed after exporting, so mods can change rules.
Keep `Terni-NOTICES.txt` with the game: it is the notice Terni's licence asks for, and
the licences of the libraries that come with the compiler.

## Test

On Windows: `test.bat`, `test.bat window` (further arguments go to Godot, e.g.
`--rendering-driver d3d12`) and `test.bat csharp`, with `GODOT` and `GODOT_MONO` set to
the Godot programs. Exporting on Windows is done from the editor.

```sh
godot/test.sh                          # headless
godot/test.sh --window --gpu-index 0   # in a window, with Godot on its GPU 0
godot/test.sh --csharp                 # the C# example, with godot-mono
GODOT_TEMPLATES=/path/to/templates godot/test.sh --export   # export for Linux, change a rule, add the mod, test there
GODOT_MONO_TEMPLATES=/path/to/templates godot/test.sh --export-csharp   # the same with the C# example
godot/test.sh --no-gpu [mode]          # any of the above with the system's Vulkan drivers hidden
```

`--no-gpu` checks that the software driver shipped with the addon is added and that
everything runs on it; build it first (`native/lavapipe/build.sh`).

The first command-line import of a fresh project with the extension makes Godot 4.7
crash as it quits, after the import is done; opening the editor is not affected, and
`test.sh` imports twice.

`--export` needs Godot's export templates: installed through the editor, or the folder
holding `linux_release.x86_64` (from the official templates archive) in `GODOT_TEMPLATES`.

## Not done yet

- The software Vulkan driver is built and tested on Linux; its Windows build
  (`native\lavapipe\build.bat`) has not been run yet.
