# Terni for Flax Engine

A Flax plugin that runs a [Terni](https://github.com/ZBager/Terni) game folder from
C++, C# and Visual Scripting. The simulation runs on a Vulkan device of its own, next
to the renderer, and gives the same results on every machine.

Made for Flax 1.13 on Linux and Windows. **Only Linux has been run so far.**

## Install

1. Put this repository into your project, for example in `Plugins/Terni`.
2. Build Terni once (needs CMake, a C++20 compiler, the Vulkan headers and shaderc):
   ```sh
   Plugins/Terni/flax/build-native.sh        # Windows: build-native.bat, from a VS command prompt
   ```
3. Reference the plugin in your `.flaxproj`:
   ```json
   "References": [
       { "Name": "$(EnginePath)/Flax.flaxproj" },
       { "Name": "$(ProjectPath)/Plugins/Terni/flax/Terni.flaxproj" }
   ]
   ```
4. In your game module's `Game.Build.cs`: `options.PublicDependencies.Add("Terni");`
5. Open the project. The editor builds the plugin with your scripts.

## Use

Put your game folder (schema, common, history, rules; see Terni's
[data format](https://github.com/ZBager/Terni/blob/main/docs/data-format.md)) in the
project, add a **Terni Simulation** script to an actor and set its **Game Folder**.
It loads the folder, starts it and ticks it ten times a second, in the background.

```cs
using FlaxEngine;
using Terni;

public class Towns : Script
{
    private TerniSimulation _simulation;
    private int _town, _pop;

    public override void OnStart()
    {
        _simulation = Actor.GetScript<TerniSimulation>();
        var world = _simulation.World;
        _town = world.Entity("town");
        _pop = world.Subscribe(_town, "pop");          // comes back with every tick
        _simulation.TickCompleted += OnTick;
    }

    private void OnTick(TerniSimulation simulation)
    {
        var pop = simulation.World.Subscribed(_pop);   // one raw value per town, no copy
        Debug.Log("First town: " + TerniValue.ToDouble(pop[0], TerniFieldType.Quantity));
    }
}
```

- **`TerniSimulation`** (script): game folder, ticks per second, pause, `Step()`,
  device choice; events `Started`, `TickCompleted`, `Failed`.
- **`TerniWorld`** (object, also for C++ and Visual Scripting): `Load`, `Start`,
  `Tick` / `BeginTick` / `TickReady` / `EndTick`, `ReadField`, `WriteField`,
  `WriteValue`, `Subscribe`, `CreateEntity` / `DestroyEntity`, `AddModifier` and
  the other modifier calls, `Save` / `LoadSave`, `Checksum`, `GetStateHash`,
  `GetDevices`, `MoveToDevice`. Functions that return `bool` return true when they
  worked; after false, `LastError` says why, and the log has it too.
- **`TerniValue`**: raw values to numbers for display, and back.
- **Tools > Terni** (editor): the devices of the machine, the running worlds, and the
  values of any field, live.

Values are raw integers: a quantity of 1 is 256, a rate of 1 is 65536. Keep them raw
in game logic and in anything sent to other players.

### Rules and the SPIR-V cache

Rules are compiled by `terni_compile_rules`, a program that the plugin's build puts
beside its binaries, into the folder `spirv` inside the game folder. A world starts
from that folder; only when a rule is new or has changed does the editor run the
compiler. `TerniWorld.CompileRules` does the same from your own build step.

### Cooked games

Cooking needs no setup. Every game folder of the project (a folder with a `schema` and
a `rules` folder in it, outside `Content`, `Source` and `Plugins`) has its rules
compiled and is copied to the same place in the cooked game, with its `spirv` folder.
The path a `TerniSimulation` script has works there unchanged. The cook log names each
folder, and reports an error for one whose rules do not compile.

The rule compiler ships with the cooked game, with shaderc: linked into it where the
system has a complete static shaderc (the Vulkan SDK does), otherwise as shared
libraries beside it (about 11 MB). Players need nothing installed. A cooked game starts
from the `spirv` folders and runs the compiler only for rules that were changed after
cooking, so mods can change rules.

shaderc, glslang and SPIRV-Tools have their own licences (Apache 2.0 and BSD-style).
Terni's licence exception allows shipping them with Terni; include their licence texts
and notices with a game that does.

## Example and test

`Example/` is a small Flax project: a galaxy of three empires and five planets, a script
that follows the population, and a self-test. Run the test with

```sh
FLAX_EDITOR=/path/to/FlaxEditor ./test.sh
```

It runs the galaxy for 200 ticks on every Vulkan device of the machine, with blocking
and background ticks and through a save, and compares the state hash with the one
Terni's own tools give. `./test.sh --cooked` cooks the example for Linux first, changes
a rule in the cooked game as a mod would, and runs the same test there.

## Not done yet

- Windows: the build scripts are written and have never been run.
- Stopping worlds when scripts reload in the editor is in place and has not been tested.
- Starting a world blocks the frame it is called in.
- Sharing the renderer's GPU by name, and shipping a software Vulkan driver as a fallback.
- Visual Scripting: `TerniWorld` and `TerniSimulation` are open to it like any scripting
  type; no graph has been made with them yet.
