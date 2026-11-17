# Terni for Godot

A GDExtension that runs a [Terni](https://github.com/ZBager/Terni) game folder from
GDScript: the `TerniSimulation` node and the `TerniWorld` object. The simulation runs
on a Vulkan device of its own, next to the renderer, and gives the same results on
every machine.

Made for Godot 4.5 and later (built with godot-cpp 4.5, tested with 4.7). **Work in
progress**: it runs in the editor and from the command line on Linux; the editor dock,
exported games, C# and Windows are next.

## Build

```sh
godot/build.sh        # needs CMake, Ninja, Python 3, Git, a C++20 compiler, the Vulkan headers and shaderc
```

This puts the addon in `addons/terni` (the library in `bin/`) and copies it into the
example project. To use it in a project, copy `addons/terni` into the project's
`addons` folder and enable the **Terni** plugin.

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

By default the simulation runs on the GPU the engine renders with, if it can.

## Test

```sh
godot/test.sh                          # headless
godot/test.sh --window --gpu-index 0   # in a window, with Godot on its GPU 0
```
