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
    /// save that continues to the same state. Writes the result to the file named by
    /// TERNI_EXAMPLE_TEST.
    /// </summary>
    public static class SelfTest
    {
        /// <summary>State hash of the galaxy example after 200 ticks, as terni_run prints it.</summary>
        public const ulong Expected = 0xad93f051a2965dfb;

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

                foreach (var device in devices.Where(d => d.Usable))
                {
                    var world = new TerniWorld();
                    Check(world.Load(folder), $"[{device.Name}] load");
                    // In the background; frames would go on meanwhile.
                    Check(world.BeginStart(new TerniStartOptions { Device = device.Index, CompileRules = true }), $"[{device.Name}] begin start");
                    var polls = 0;
                    while (!world.StartReady())
                        polls++;
                    Check(world.IsRunning && !world.IsStarting, $"[{device.Name}] started in the background ({polls} polls): {world.LastError}");
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

                    // Subscribed data is what a read from the device gives.
                    var read = world.ReadField(planet, "pop");
                    var subscribed = world.Subscribed(pop).ToArray();
                    Check(subscribed.Length == world.GetCount(planet) && subscribed.SequenceEqual(read.Take(subscribed.Length)),
                          $"[{device.Name}] subscribed pop equals read pop ({subscribed.Length} rows)");

                    // The save continues to the same state.
                    Check(world.LoadSave(save) && world.Ticks == 100 && world.GetStateHash() == middle, $"[{device.Name}] save restores tick 100");
                    for (var i = 0; i < 100; i++)
                        world.Tick();
                    Check(world.GetStateHash() == Expected, $"[{device.Name}] the save continues to the same hash");

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

                // A call made while a start is in flight waits for it.
                var eager = new TerniWorld();
                Check(eager.Load(folder) && eager.BeginStart(new TerniStartOptions()) && eager.Tick() && eager.Ticks == 1,
                      "a tick right after BeginStart waits for the start");
                eager.Stop();
                FlaxEngine.Object.Destroy(eager);

                // A folder with a mistake reports file:line and does not start.
                var broken = new TerniWorld();
                Check(!broken.Load(folder + "_missing") && broken.LastError.Length > 0, "a missing folder is an error");
                FlaxEngine.Object.Destroy(broken);
            }
            catch (Exception e)
            {
                Check(false, "exception: " + e);
            }

            log.WriteLine(ok ? "PASSED" : "FAILED");
            File.WriteAllText(resultFile, log.ToString());
            Debug.Log("Terni self-test:\n" + log);
        }
    }
}
