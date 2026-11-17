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

`TerniWorld` has the same calls as in the other plugins, in snake_case: `load`,
`start(options)` / `begin_start(options)`, `tick` / `begin_tick` / `tick_ready` /
`end_tick`, `read_field`, `write_field`, `write_value`, `subscribe`, `get_subscribed`,
`create_entity`, `add_modifier` (a handle is a `Vector2i`), `save` / `load_save`,
`get_state_hash`, `get_devices`, `find_renderer_device`, `move_to_device`. Calls that
return `bool` return true when they worked; after false, `get_last_error()` says why,
and the error is also pushed to the debugger. Start options are a dictionary:
`device`, `ignore_renderer`, `preference`, `cache_folder`, `cache_only`, `validation`.

By default the simulation runs on the GPU the engine renders with, if it can: it keeps
a laptop's second GPU asleep and follows the player's choice of GPU. The node's
**Device** names one instead; **Preference** decides when the renderer's GPU cannot run
the simulation.

Rules are compiled by `terni_compile_rules`, which comes with the addon, into the folder
`spirv` inside the game folder. A world starts from that folder and runs the compiler
only for rules that are new or have changed.

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
of the running game with its device, tick, time per tick and entity counts, and the
values of any field you pick, refreshed twice a second. The game runs in its own
process, so the simulations send this over the debugger connection; an exported game
sends nothing.

## Exported games

Exporting needs no setup beyond the **Terni** plugin being enabled. Godot leaves game
folders out of the exported package (their `.gdignore` says so), so the plugin's export
step compiles each game folder's rules and copies the folder, with its `spirv` folder,
beside the exported program, at the same path relative to it. A `TerniSimulation` with
`res://Galaxy` finds `Galaxy` beside the program. A game folder is any folder of the
project with a `schema` and a `rules` folder in it, outside `addons`.

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
GODOT_TEMPLATES=/path/to/templates godot/test.sh --export   # export for Linux, change a rule, test there
GODOT_MONO_TEMPLATES=/path/to/templates godot/test.sh --export-csharp   # the same with the C# example
```

The first command-line import of a fresh project with the extension makes Godot 4.7
crash as it quits, after the import is done; opening the editor is not affected, and
`test.sh` imports twice.

`--export` needs Godot's export templates: installed through the editor, or the folder
holding `linux_release.x86_64` (from the official templates archive) in `GODOT_TEMPLATES`.

## Not done yet

- Shipping a software Vulkan driver, for machines whose GPUs cannot run the simulation.
