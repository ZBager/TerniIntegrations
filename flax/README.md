# Terni for Flax Engine

A Flax plugin that runs a [Terni](https://github.com/ZBager/Terni) game folder from
C++, C# and Visual Scripting. The simulation runs on a Vulkan device of its own, next
to the renderer, and gives the same results on every machine.

Made for Flax 1.13. Tested on Linux and Windows, in the editor and in cooked games.

## Install

1. Put this repository into your project, for example in `Plugins/Terni`.
2. Build Terni once (needs CMake, a C++20 compiler, the Vulkan headers and shaderc):
   ```sh
   Plugins/Terni/flax/build-native.sh        # Windows: build-native.bat (Visual Studio and the Vulkan SDK)
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
        _simulation.Started += OnStarted;              // each time a world has started
        _simulation.TickCompleted += OnTick;
        if (_simulation.IsRunning)
            OnStarted(_simulation);
    }

    private void OnStarted(TerniSimulation simulation)
    {
        // Subscriptions belong to one world: make them again for every world.
        var world = simulation.World;
        _town = world.Entity("town");
        _pop = world.Subscribe(_town, "pop");          // comes back with every tick
    }

    private void OnTick(TerniSimulation simulation)
    {
        var pop = simulation.World.Subscribed(_pop);   // one raw value per town, no copy
        Debug.Log("First town: " + TerniValue.ToDouble(pop[0], TerniFieldType.Quantity));
    }
}
```

- **`TerniSimulation`** (script): game folder, mod folders, ticks per second, pause,
  `Step()`, device choice; events `Started`, `TickCompleted`, `Failed`, `DeviceLost`. It starts the world in
  the background (compiling changed rules, making the device ready, uploading the
  starting state) while frames go on; `Started` follows a few frames later.
- **`TerniWorld`** (object, also for C++ and Visual Scripting): `Load` /
  `LoadWithMods`, `Start`, `Tick` / `BeginTick` / `TickReady` / `EndTick`, `ReadField`,
  `WriteField`, `WriteValue`, `Subscribe`, `CreateEntity` / `DestroyEntity`,
  `GetAliveBits`, `GetFreeCount`, `AddModifier` and the other modifier calls, `Save` /
  `LoadSave`, `Checksum`, `GetStateHash`, `GetFieldHashes` / `GetEntityHashes`,
  `GetStageTimes`, `GetDevices` (each with its `Robustness`: what a rule that reads or
  writes past the end of a field does there), `MoveToDevice`; event `DeviceLost`. Functions that return `bool` return true when they
  worked; after false, `LastError` says why, and the log has it too. `BeginStart`,
  `StartReady` and `EndStart` start a world without holding up the frame; any other
  call made during a start waits for it.
- **`TerniValue`**: raw values to numbers for display, and back.
- **Script reloads** in the editor free every world (device and game data) before the
  plugin's code is replaced. A script that still holds a `TerniWorld` afterwards finds
  it not loaded; load it again.
- **Tools > Terni** (editor): the devices of the machine, the running worlds with a
  state hash per entity type and the GPU time of each stage, and the values of any
  field, live. Flax's profiler times only work on its own GPU device, so Terni's stage
  times are shown here and not there.

Values are raw integers: a quantity of 1 is 256, a rate of 1 is 65536. Keep them raw
in game logic and in anything sent to other players.

### Which GPU

By default the simulation runs on the GPU the engine renders with, if that GPU can run
it. On a laptop with two GPUs this keeps the second one asleep, and it follows the
player's choice of GPU for the game (in Windows' graphics settings, or Flax's `-nvidia`,
`-amd` and `-intel` switches). Otherwise the script's **Preference** decides (a dedicated
GPU first). **Device** names one device instead, for a settings menu;
`TerniWorld.GetDevices()` lists them and `TerniWorld.FindRendererDevice()` says which
one the engine renders with. Every device gives the same results, so the choice never
matters to multiplayer.

`TerniStartOptions` (for `TerniWorld.Start`) is all zeros by default, so `new
TerniStartOptions()` in C# means the same: `UseDevice` with `Device` names a device,
`IgnoreRenderer` skips the renderer's GPU, `CacheOnly` never compiles rules.

### Visual Scripting

Visual scripts can use `TerniWorld` and `TerniSimulation` like C# scripts can (tried
with a graph that logs the simulation's `Ticks` from **On Update**), with one
difference: they cannot bind the C# events `Started`, `TickCompleted`, `Failed` and
`DeviceLost`. Instead, in **On Update**, compare the simulation's `Ticks` with the value
from the last frame to act on new ticks, and check `IsRunning`, `IsDeviceLost` and
`LastError`. Use
`GetSubscribed` (an array copy) where C# would use `Subscribed` (a span).

### Rules and the SPIR-V cache

Rules are compiled by `terni_compile_rules`, a program that the plugin's build puts
beside its binaries, into the folder `spirv` inside the game folder. A world starts
from that folder; only when a rule is new or has changed does the editor run the
compiler. `TerniWorld.CompileRules` does the same from your own build step.

### Without a usable GPU

On a machine whose GPUs cannot run the simulation (an old GPU, a virtual machine, a
server without one), it runs on the CPU with Mesa's lavapipe, a software Vulkan driver:
the same results, more slowly. `native/lavapipe/build.sh` (Windows:
`native\lavapipe\build.bat`) builds it from source, with LLVM inside and nothing else
needed on the player's machine; once it is built, `build-native.sh` puts it into
`ThirdParty/Terni/bin`, and it is deployed and cooked with the rest
(`TERNI_SOFTWARE_DRIVER=none` leaves it out). The first time devices are listed or a
world starts, the plugin adds it to the system's drivers, unless the system has a
software device of its own (most Linux distributions come with Mesa's);
`TerniWorld.SoftwareDriver` says whether it did. The default preference then chooses a
GPU when one can run the simulation, and lavapipe otherwise.

It adds about 73 MB to a game. It needs the Vulkan loader (`vulkan-1.dll`,
`libvulkan.so.1`), version 1.3.234 or newer, which comes with every current GPU
driver; without one, the plugin does not load at all.

### Mods

A mod is a folder laid out like the game's, holding only what it changes; Terni's
[Data format](https://github.com/ZBager/Terni/blob/main/docs/data-format.md#mods) says
how files combine. Give the script's **Mod Folders**, or call
`world.LoadWithMods(gameFolder, new[] { "Mods/FasterGrowth" })`, in order. Mods change
the game checksum, so multiplayer peers with different mods can tell before they play.
Their rules are compiled into the game folder's cache the first time, in cooked games
too. Point modders to Terni's `terni_check`, which loads, compiles and runs a mod the
way the game will. Give each mod folder a `descriptor.mod` file: cooking then knows it
is not a game folder and leaves it out; players put mods beside the game.

### Saves from an earlier version of the game

A patched game, or one with other mods, has another checksum, and `LoadSave(data)`
refuses saves made before. `LoadSave(data, true)` loads them by name: the save is fitted
to this game (new fields and entity types start as the game describes them, removed ones
are dropped) and `FittedLines` says what was fitted. `TerniWorld.GetSaveChecksum(data)`
tells beforehand which game a save was made with.

### A lost device

A rule that runs too long (an endless loop in a mod, say) makes the driver reset the
GPU, and the simulation's state on it is gone. The world stops and fires `DeviceLost`;
the script fires its own `DeviceLost`, and not `Failed`. Start again, on the same device
or another, and load the last save:

```cs
_simulation.DeviceLost += (simulation, error) =>
{
    Debug.LogWarning("The simulation restarts from the autosave: " + error);
    simulation.StartWorld();
    simulation.World.LoadSave(_autosave);
};
```

`SimulateDeviceLost()` makes the next call that uses the device find it lost, to test
this without resetting a GPU.

### Desync checks

`GetFieldHashes()` and `GetEntityHashes()` hash the state on the device and bring back
two words per field: cheap enough for lockstep peers to compare every few ticks, and
equal on every device that ran the same ticks. `GetStateHash()` reads the whole state
back and is much slower.

### Cooked games

Cooking needs no setup. Every game folder of the project (a folder with a `schema` and
a `rules` folder in it, outside `Content`, `Source` and `Plugins`) has its rules
compiled and is copied to the same place in the cooked game, with its `spirv` folder
(mod folders, with a `descriptor.mod`, are left out).
The path a `TerniSimulation` script has works there unchanged. The cook log names each
folder, and reports an error for one whose rules do not compile.

The rule compiler ships with the cooked game, with shaderc: linked into it where the
system has a complete static shaderc (the Vulkan SDK does), otherwise as shared
libraries beside it (about 11 MB). Players need nothing installed. A cooked game starts
from the `spirv` folders and runs the compiler only for rules that were changed after
cooking, so mods can change rules.

### Licence notices

A cooked game gets `Terni-NOTICES.txt` beside its program: the notice that Terni's
licence asks for (that the game uses Terni, its copyright, where its source is, and
links to its licence), followed by the licence texts of shaderc, glslang, SPIRV-Tools
and SPIRV-Headers, which ship with the rule compiler. Keep the file with the game, or
copy its contents into your own credits or notices. Terni's licence exception allows
shipping these libraries with Terni.

## Example and test

`Example/` is a small Flax project: a galaxy of three empires and five planets, a script
that shows the planets' population on screen as the simulation runs, and a self-test.
A cooked game writes its log to `%LOCALAPPDATA%\Terni\Terni Example\Logs` on Windows
and `~/.local/share/Terni/Terni Example/Logs` on Linux. Run the test with

```sh
FLAX_EDITOR=/path/to/FlaxEditor ./test.sh
```

It runs the galaxy for 200 ticks on every Vulkan device of the machine, with blocking
and background ticks and through a save, and compares the state hash with the one
Terni's own tools give. It also checks hashes on the device, stage times, a simulated
lost device, and the galaxy with the example mod (`Mods/FasterGrowth`) on top. On Windows, run `test.bat`. The test makes wrong calls on
purpose, so the editor's log ends with a few `[Terni]` errors even when it passes.

- `./test.sh --cooked` cooks the example for Linux first, changes a rule in the cooked
  game as a mod would, puts the example mod beside it, and runs the same test there. The editor needs Flax's Linux
  platform files for that (they come separately from the editor download).
- `./test.sh --visual` (Windows: `test.bat visual`) applies the editor's own rules for
  what visual scripts can use to every member of the plugin's types, and checks that a
  visual script has what it needs.
- `./test.sh --reload` (Windows: `test.bat reload`) starts a world in the editor, makes
  the editor rebuild and reload the scripts, and checks that the world was freed and a
  new one runs.
- `./test.sh --no-gpu [mode]` runs any of the above with the system's Vulkan drivers
  hidden (and Flax's null renderer): the software driver deployed with the plugin must
  be added and everything must run on it. Build it first (`native/lavapipe/build.sh`).

## Not done yet

- The software Vulkan driver is built and tested on Linux; its Windows build
  (`native\lavapipe\build.bat`) has not been run yet.
