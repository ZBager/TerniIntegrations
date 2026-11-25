// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.IO;
using System.Linq;
using FlaxEngine;
using Terni;

namespace Game
{
    /// <summary>
    /// Checks the plugin against what Terni gives on its own: the state hash of the galaxy
    /// example after 200 ticks, on every device, with blocking and background ticks, and a
    /// save that continues to the same state. Then hashes on the device, stage times, a lost
    /// device and a mod. Writes the result to the file named by TERNI_EXAMPLE_TEST.
    /// </summary>
    public static class SelfTest
    {
        /// <summary>State hash of the galaxy example after 200 ticks, as terni_run prints it.</summary>
        public const ulong Expected = 0xad93f051a2965dfb;

        /// <summary>A mod for the galaxy example: other growth, a new bundle and a new entity type.</summary>
        public const string Mod = "Mods/FasterGrowth";

        public static void Run(TerniSimulation simulation, string resultFile)
        {
            var log = new StringWriter();
            var ok = true;
            void Check(bool condition, string what)
            {
                log.WriteLine((condition ? "ok    " : "FAIL  ") + what);
                ok &= condition;
            }

            try
            {
                var folder = simulation.ResolveFolder(simulation.GameFolder);
                var devices = TerniWorld.GetDevices();
                Check(devices.Any(d => d.Usable), $"{devices.Length} devices, at least one usable");
                Check(devices.Where(d => d.Usable).All(d => Enum.IsDefined(d.Robustness)),
                      "buffer robustness per device: " + string.Join(", ", devices.Where(d => d.Usable).Select(d => d.Robustness)));
                if (Environment.GetEnvironmentVariable("TERNI_EXPECT_SOFTWARE_DRIVER") == "1")
                    Check(TerniWorld.SoftwareDriver.Length > 0 && devices.All(d => d.IsCpu),
                          $"no GPU: the software driver shipped with the game is added ({TerniWorld.SoftwareDriver})");
                TerniHash[] firstHashes = null;
                string firstDevice = null;

                foreach (var device in devices.Where(d => d.Usable))
                {
                    var world = new TerniWorld();
                    Check(world.Load(folder), $"[{device.Name}] load");
                    // In the background; frames would go on meanwhile.
                    Check(world.BeginStart(new TerniStartOptions { UseDevice = true, Device = device.Index }), $"[{device.Name}] begin start");
                    var polls = 0;
                    while (!world.StartReady())
                        polls++;
                    Check(world.IsRunning && !world.IsStarting, $"[{device.Name}] started in the background ({polls} polls): {world.LastError}");
                    if (!world.IsRunning)
                        continue; // the rest would only repeat that
                    var planet = world.Entity("planet");
                    var pop = world.Subscribe(planet, "pop");

                    // 100 blocking ticks, a save, then 100 background ticks.
                    for (var i = 0; i < 100; i++)
                        world.Tick();
                    var save = world.Save();
                    var middle = world.GetStateHash();
                    for (var i = 0; i < 100; i++)
                    {
                        world.BeginTick();
                        while (!world.TickReady())
                        {
                        }
                        world.EndTick();
                    }
                    var end = world.GetStateHash();
                    Check(world.Ticks == 200, $"[{device.Name}] 200 ticks");
                    Check(end == Expected, $"[{device.Name}] state hash {end:x16}, expected {Expected:x16}");

                    // Hashes computed on the device: the same on every device.
                    var hashes = world.GetFieldHashes();
                    var entityHashes = world.GetEntityHashes();
                    Check(hashes.Any(h => h.Name == "planet.pop") && entityHashes.Any(h => h.Name == "planet") && entityHashes.Any(h => h.Name == "terni"),
                          $"[{device.Name}] hashes on the device: {hashes.Length} fields, {entityHashes.Length} entity types");
                    if (firstHashes == null)
                    {
                        firstHashes = hashes;
                        firstDevice = device.Name;
                    }
                    else
                        Check(hashes.SequenceEqual(firstHashes), $"[{device.Name}] the same field hashes as on {firstDevice}");
                    var stages = world.GetStageTimes();
                    var growth = stages.FirstOrDefault(t => t.Name == "planet_growth");
                    Check(growth.Ran && growth.Runs == 200, $"[{device.Name}] GPU time of {stages.Length} stages (planet_growth: {growth.LastMs:0.0000} ms)");

                    // Subscribed data is what a read from the device gives.
                    var read = world.ReadField(planet, "pop");
                    var subscribed = world.Subscribed(pop).ToArray();
                    Check(subscribed.Length == world.GetCount(planet) && subscribed.SequenceEqual(read.Take(subscribed.Length)),
                          $"[{device.Name}] subscribed pop equals read pop ({subscribed.Length} rows)");

                    // The save continues to the same state.
                    Check(world.LoadSave(save) && world.Ticks == 100 && world.GetStateHash() == middle, $"[{device.Name}] save restores tick 100");
                    for (var i = 0; i < 100; i++)
                        world.Tick();
                    Check(world.GetStateHash() == Expected && world.GetFieldHashes().SequenceEqual(hashes), $"[{device.Name}] the save continues to the same hash");

                    // Host writes and modifiers reach the simulation; bad calls fail politely.
                    var sicilia = world.FindRow(planet, "sicilia");
                    Check(sicilia >= 0 && world.GetRowId(planet, sicilia) == "sicilia", $"[{device.Name}] rows by id");
                    Check(world.WriteValue(planet, "pop", sicilia, TerniValue.FromDouble(1234.5, TerniFieldType.Quantity)), $"[{device.Name}] write");
                    var key = world.FindModifierKey("pop_growth");
                    var before = world.GetModifierTotal(key, planet, sicilia);
                    var modifier = world.AddModifier(world.FindBundle("good_harvest"), planet, sicilia);
                    world.Tick();
                    Check(world.ReadField(planet, "pop", sicilia, 1)[0] != 0 && world.IsModifierAlive(modifier) &&
                          world.GetModifierTotal(key, planet, sicilia) != before &&
                          world.GetModifierBreakdown(key, planet, sicilia).Length > 0,
                          $"[{device.Name}] modifier added and counted");
                    Check(!world.WriteValue(planet, "no_such_field", 0, 1) && world.LastError.Length > 0, $"[{device.Name}] a bad field is an error, not a crash");
                    Check(world.ReadField(-1, "pop").Length == 0, $"[{device.Name}] a bad entity is an error, not a crash");

                    world.Stop();
                    Check(!world.IsRunning && !world.Tick(), $"[{device.Name}] stopped");
                    FlaxEngine.Object.Destroy(world);
                }

                // The GPU the engine renders with: found, used by default, and not when told otherwise.
                var gpu = GPUDevice.Instance;
                var renderer = TerniWorld.FindRendererDevice();
                log.WriteLine($"      the engine renders with {gpu?.RendererType} on {gpu?.Adapter?.Description}: Terni device {renderer}");
                var expectRenderer = gpu != null && gpu.RendererType != RendererType.Null && devices.Any(d => d.Usable && d.Name == gpu.Adapter.Description);
                Check(!expectRenderer || renderer >= 0, "the renderer's GPU is found among Terni's devices");
                if (renderer >= 0)
                {
                    var name = devices.First(d => d.Index == renderer).Name;
                    var other = devices.FirstOrDefault(d => d.Usable && d.Index != renderer);
                    var world = new TerniWorld();
                    // A preference for another kind of device must not win over the renderer's GPU.
                    var preference = devices.First(d => d.Index == renderer).IsCpu ? TerniDevicePreference.Discrete : TerniDevicePreference.Cpu;
                    Check(world.Load(folder) && world.Start(new TerniStartOptions { Preference = preference }) && world.DeviceName == name,
                          $"by default the simulation runs on the renderer's GPU ({world.DeviceName})");
                    if (other.Name != null)
                    {
                        var cpu = devices.FirstOrDefault(d => d.Usable && d.IsCpu && d.Index != renderer);
                        if (cpu.Name != null)
                            Check(world.Start(new TerniStartOptions { IgnoreRenderer = true, Preference = TerniDevicePreference.Cpu }) && world.DeviceName == cpu.Name,
                                  $"IgnoreRenderer lets the preference decide ({world.DeviceName})");
                        Check(world.Start(new TerniStartOptions { UseDevice = true, Device = other.Index }) && world.DeviceName == other.Name,
                              $"UseDevice wins over the renderer's GPU ({world.DeviceName})");
                    }
                    world.Stop();
                    FlaxEngine.Object.Destroy(world);
                }

                // A call made while a start is in flight waits for it.
                var eager = new TerniWorld();
                Check(eager.Load(folder) && eager.BeginStart(new TerniStartOptions()) && eager.Tick() && eager.Ticks == 1,
                      "a tick right after BeginStart waits for the start");
                eager.Stop();
                FlaxEngine.Object.Destroy(eager);

                // Waits that spin change how long a tick's results take to arrive, never the results.
                var spinning = new TerniWorld();
                Check(spinning.Load(folder) && spinning.Start(new TerniStartOptions { SpinMicroseconds = 200 }), "a world starts with SpinMicroseconds");
                for (var t = 0; t < 200; t++)
                    spinning.Tick();
                Check(spinning.GetStateHash() == Expected, "waits that spin: the same state hash");
                spinning.Stop();
                FlaxEngine.Object.Destroy(spinning);

                // A folder with a mistake reports file:line and does not start.
                var broken = new TerniWorld();
                Check(!broken.Load(folder + "_missing") && broken.LastError.Length > 0, "a missing folder is an error");
                Check(!broken.LoadWithMods(folder, new[] { simulation.ResolveFolder(Mod) + "_missing" }) && broken.LastError.Length > 0, "a missing mod folder is an error");
                FlaxEngine.Object.Destroy(broken);

                CheckDeviceLost(folder, Check);
                CheckMod(folder, simulation.ResolveFolder(Mod), Check);
                CheckStartState(folder, Check);
                CheckOrderAndCache(folder, Check);
            }
            catch (Exception e)
            {
                Check(false, "exception: " + e);
            }

            log.WriteLine(ok ? "PASSED" : "FAILED");
            File.WriteAllText(resultFile, log.ToString());
            Debug.Log("Terni self-test:\n" + log);
        }

        // A lost device, simulated: a real one would reset the GPU.
        private static void CheckDeviceLost(string folder, Action<bool, string> check)
        {
            var world = new TerniWorld();
            var signalled = 0;
            world.DeviceLost += w => signalled++;
            if (!world.Load(folder) || !world.Start(new TerniStartOptions()))
            {
                check(false, "lost device: the world starts: " + world.LastError);
                FlaxEngine.Object.Destroy(world);
                return;
            }
            for (var i = 0; i < 100; i++)
                world.Tick();
            var autosave = world.Save();
            world.SimulateDeviceLost();
            check(!world.Tick() && world.IsDeviceLost && !world.IsRunning && signalled == 1, $"a lost device stops the world and is signalled: {world.LastError}");
            check(!world.Tick() && signalled == 1, "calls after it fail, without another event");
            check(world.Start(new TerniStartOptions()) && !world.IsDeviceLost && world.LoadSave(autosave), "the world starts again and loads the last save");
            for (var i = 0; i < 100; i++)
                world.Tick();
            check(world.GetStateHash() == Expected, "and goes on to the same state");
            // Found while a tick is in flight.
            world.BeginTick();
            world.SimulateDeviceLost();
            while (!world.TickReady())
            {
            }
            check(world.IsDeviceLost && !world.TickInFlight && signalled == 2, "a device lost during a background tick is found");
            world.Stop();
            FlaxEngine.Object.Destroy(world);

            // TerniSimulation: DeviceLost, not Failed.
            var actor = new EmptyActor();
            var simulation = actor.AddScript<TerniSimulation>();
            simulation.GameFolder = folder;
            simulation.StartInBackground = false;
            simulation.Paused = true;
            var events = new System.Collections.Generic.List<string>();
            simulation.DeviceLost += (s, e) => events.Add("DeviceLost");
            simulation.Failed += (s, e) => events.Add("Failed");
            check(simulation.StartWorld(), "a TerniSimulation script starts: " + simulation.LastError);
            simulation.World.SimulateDeviceLost();
            check(!simulation.Step() && simulation.IsDeviceLost && events.SequenceEqual(new[] { "DeviceLost" }) && simulation.LastError.Length > 0,
                  $"TerniSimulation invokes DeviceLost, not Failed ({string.Join(", ", events)})");
            simulation.StopWorld();
            FlaxEngine.Object.Destroy(actor);
        }

        // A starting state changed before the start, and the modifier calls on many rows at once.
        private static void CheckStartState(string folder, Action<bool, string> check)
        {
            var world = new TerniWorld();
            try
            {
                check(world.Load(folder), "start state: the game loads");
                var planet = world.Entity("planet");
                var empire = world.Entity("empire");
                var harvest = world.FindBundle("good_harvest");
                var key = world.FindModifierKey("pop_growth");
                check(world.GetDefines().Contains("growth_rate") && world.GetFieldRef(planet, "owner") == empire && world.GetFieldRef(planet, "pop") == -1,
                      "defines by name, and what a ref field refers to");
                var checksum = world.Checksum;
                check(world.SetModifierCapacity(2048) && world.Checksum != checksum && !world.SetModifierCapacity(0),
                      "the modifier store's capacity is set, and is part of the checksum");
                var row = world.GetCount(planet);
                var pop = TerniValue.FromDouble(100, TerniFieldType.Quantity);
                check(!world.AddStartModifier(harvest, planet, row), "a start modifier on a row not in use is an error");
                check(world.SetStartCount(planet, row + 2) && world.GetCount(planet) == row + 2 && !world.SetStartCount(planet, 1 << 30),
                      "two more planets at the start");
                check(world.WriteStart(planet, "owner", row, new[] { 0, 0 }) && world.WriteStart(planet, "pop", row, new[] { pop, pop }) &&
                      world.ReadStart(planet, "pop", row, 2).SequenceEqual(new[] { pop, pop }) && world.ReadStart(planet, "pop").Length == world.GetCapacity(planet),
                      "their starting values are written and read");
                check(!world.WriteStart(empire, "stock", 0, new[] { 1, 2 }) && !world.WriteStart(planet, "no_such_field", 0, new[] { 1 }),
                      "words that are not whole rows, and a bad field, are errors");
                check(world.AddStartModifier(harvest, planet, row), "a modifier at the start");
                check(world.Start(new TerniStartOptions()), "the changed world starts: " + world.LastError);
                if (!world.IsRunning)
                    return;
                check(!world.WriteStart(planet, "pop", row, new[] { pop }), "a running world refuses a starting state");
                var reads = world.SyncReads;
                check(world.ReadField(planet, "pop", row, 2).SequenceEqual(new[] { pop, pop }) && world.SyncReads > reads,
                      "the starting state is on the device, and the read was counted as one that waits");
                var instances = world.ModifierCount;
                var own = world.GetModifierOwnTotal(key, planet, row);
                check(instances >= 1 && own > 0 && world.GetModifierOwnTotal(key, planet, row + 1) == 0, "the start modifier is in its row's own total");

                var added = world.AddModifiers(harvest, planet, new[] { 0, row, row + 1 });
                check(added.Length == 3 && world.ModifierCount == instances + 3 && world.GetModifierOwnTotal(key, planet, row) == 2 * own &&
                      world.GetModifierOwnTotal(key, planet, row + 1) == own, "one bundle on three rows");
                check(world.AddModifiers(harvest, planet, new[] { 0, 1 << 30 }).Length == 0 && world.ModifierCount == instances + 3,
                      "a bad row among many adds none of them");
                var replaced = world.ReplaceModifier(added[2], harvest, planet, row + 1, 2 * TerniValue.RateOne);
                check(!world.IsModifierAlive(added[2]) && world.IsModifierAlive(replaced) && world.ModifierCount == instances + 3 &&
                      world.GetModifierOwnTotal(key, planet, row + 1) == 2 * own, "a modifier replaced by another");
                var kept = world.ReplaceModifier(replaced, -1, planet, row + 1);
                check(kept.Slot == replaced.Slot && kept.Version == replaced.Version && world.IsModifierAlive(replaced), "a failed replacement keeps the old one");
                world.AddModifier(world.FindBundle("war_economy"), empire, 0);
                check(world.GetModifierTotal(key, planet, row + 1) < world.GetModifierOwnTotal(key, planet, row + 1), "the total inherits, the own total does not");

                var ticks = world.Ticks;
                check(world.TickChecked() && world.Ticks == ticks + 1 && world.OrderIssues.Length == 0, "a checked tick finds no order issues");
                world.Stop();
                check(world.SetStartCount(planet, row) && world.GetCount(planet) == row, "rows can be given up again");
            }
            finally
            {
                world.Stop();
                FlaxEngine.Object.Destroy(world);
            }
        }

        // A rule whose result depends on the order of its rows, and where its SPIR-V came from.
        private static void CheckOrderAndCache(string folder, Action<bool, string> check)
        {
            var mod = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "terni_order_test_" + Guid.NewGuid().ToString("N"));
            var cache = System.IO.Path.Combine(mod, "spirv");
            var world = new TerniWorld();
            try
            {
                System.IO.Directory.CreateDirectory(System.IO.Path.Combine(mod, "rules"));
                System.IO.File.WriteAllText(System.IO.Path.Combine(mod, "rules", "planet_growth.glsl"),
                    "void main() {\n    FOR_EACH_PLANET(p) {\n        planet_pop[p] += planet_pop[(p + 1u) % terni.planet_count] + int(p);\n" +
                    "        if (p < 3u) moon_phase_set(p, 300u);\n    }\n}\n");
                System.IO.Directory.CreateDirectory(System.IO.Path.Combine(mod, "schema"));
                System.IO.File.WriteAllText(System.IO.Path.Combine(mod, "schema", "moons.txt"),  // small fields
                    "moon = { capacity = 8 owner = ref8:empire phase = count8 tilt = int16 }\n");
                check(world.LoadWithMods(folder, new[] { mod }) && world.Warnings.Length > 0, "a rule that depends on order is a warning when it loads");
                var moon = world.FindEntity("moon");
                check(world.GetFieldType(moon, "phase") == TerniFieldType.Count8 && world.GetFieldType(moon, "tilt") == TerniFieldType.Int16 &&
                      world.GetFieldType(moon, "owner") == TerniFieldType.Ref8 && world.GetFieldRef(moon, "owner") == world.FindEntity("empire"), "small fields");
                check(world.SetStartCount(moon, 3), "moons to start with");
                var options = new TerniStartOptions { CacheFolder = cache };
                check(world.Start(options), "it starts with an empty cache: " + world.LastError);
                var stats = world.CompileStats;
                check(stats.Compiled >= 3 && stats.Cached == 0 && stats.Ms > 0, $"its rules were compiled ({stats.Compiled} compiled, {stats.Cached} cached)");
                check(world.TickChecked() && world.OrderIssues.Any(i => i.Stage == "planet_growth" && i.Field == "planet.pop" && i.Rows > 0),
                      "a checked tick names the stage and field: " + string.Join("; ", world.OrderIssues.Select(i => $"{i.Stage} {i.Field} from row {i.FirstRow}")));
                var misfits = world.OrderIssues.Where(i => i.Misfit).ToArray();
                check(misfits.Length == 1 && misfits[0].Stage == "planet_growth" && misfits[0].Field == "moon.phase" && misfits[0].Rows == 3,
                      "and values that do not fit a small field");
                check(world.ReadField(moon, "phase", 0, 3).SequenceEqual(new[] { 255, 255, 255 }), "the nearest values are stored");
                check(!world.WriteValue(moon, "phase", 0, 256) && world.LastError.Contains("does not fit"), "a value that does not fit is refused");
                check(world.WriteValue(moon, "owner", 1, -1) && world.Tick() && world.ReadField(moon, "owner", 1, 1).SequenceEqual(new[] { -1 }), "a small ref to no row");
                check(world.Start(options) && world.CompileStats.Compiled == 0 && world.CompileStats.Cached >= 3, "the second start takes them from the cache");
                world.Stop();
                check(!world.TickChecked() && world.CompileStats.Cached == 0, "a stopped world cannot be checked");
            }
            finally
            {
                world.Stop();
                FlaxEngine.Object.Destroy(world);
                System.IO.Directory.Delete(mod, true);
            }
        }

        // The game's folder with a mod on top: its rules compiled, saves of the game alone
        // loaded by name, and the mod's entity type, whose rows come and go.
        private static void CheckMod(string folder, string mod, Action<bool, string> check)
        {
            var game = new TerniWorld();
            var modded = new TerniWorld();
            try
            {
                if (!game.Load(folder) || !game.Start(new TerniStartOptions()))
                {
                    check(false, "mod: the game starts: " + game.LastError);
                    return;
                }
                for (var i = 0; i < 100; i++)
                    game.Tick();
                var save = game.Save();
                check(modded.LoadWithMods(folder, new[] { mod }) && modded.ModFolders.Length == 1 && modded.Checksum != game.Checksum &&
                      modded.FindBundle("festival") >= 0 && modded.FindEntity("fleet") >= 0,
                      "a mod loads on top of the game: its bundle, its entity type, another checksum");
                check(modded.Start(new TerniStartOptions()), "the game with the mod starts, the mod's rules compiled: " + modded.LastError);
                if (!modded.IsRunning)
                    return;
                check(TerniWorld.GetSaveChecksum(save) == game.Checksum && TerniWorld.GetSaveChecksum(new byte[] { 1, 2, 3 }) == 0,
                      "GetSaveChecksum names the game a save was made with");
                check(!modded.LoadSave(save), "a save of the game alone is refused as it is");
                check(modded.LoadSave(save, true) && modded.Ticks == 100, "and loads by name: " + string.Join("; ", modded.FittedLines));
                for (var i = 0; i < 100; i++)
                {
                    game.Tick();
                    modded.Tick();
                }
                check(game.GetStateHash() == Expected && modded.GetStateHash() != game.GetStateHash(), "the mod's rules run");

                var fleet = modded.Entity("fleet");
                var free = modded.GetFreeCount(fleet);
                var row = modded.CreateEntity(fleet);
                var bits = modded.GetAliveBits(fleet);
                check(free == 64 && row == 0 && modded.GetFreeCount(fleet) == 63 && bits.Length > 0 && (bits[0] & 1) == 1,
                      $"free rows and alive bits of a dynamic entity type ({free} free, row {row})");
                modded.DestroyEntity(fleet, row);
                modded.Tick();
                check((modded.GetAliveBits(fleet)[0] & 1) == 0 && modded.GetFreeCount(fleet) == 64, "a destroyed row is free again a tick later");
                check(modded.GetAliveBits(modded.Entity("planet")).Length == 0 && modded.LastError.Length > 0,
                      "alive bits of a type that is not dynamic are an error, not a crash");
            }
            finally
            {
                game.Stop();
                modded.Stop();
                FlaxEngine.Object.Destroy(game);
                FlaxEngine.Object.Destroy(modded);
            }
        }
    }
}
