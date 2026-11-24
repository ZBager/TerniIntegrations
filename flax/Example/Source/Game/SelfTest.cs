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
