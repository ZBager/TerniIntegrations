// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.IO;
using System.Linq;
using System.Text;
using Godot;
using Terni;

namespace TerniExample;

/// <summary>
/// The galaxy from C#: shows the planets' population as the Simulation node runs it. With
/// the environment variable TERNI_EXAMPLE_TEST set to a result file, runs a self-test of
/// the C# wrapper instead and quits.
/// </summary>
public partial class Main : Control
{
    /// <summary>State hash of the galaxy example after 200 ticks, as terni_run prints it.</summary>
    private const ulong Expected = 0xad93f051a2965dfb;

    private TerniSimulation _simulation;
    private TerniWorld _subscribed;
    private int _planet, _pop;
    private Label _label;

    public override void _Ready()
    {
        _simulation = new TerniSimulation(GetNode("Simulation"));
        _label = GetNode<Label>("Galaxy");
        var test = System.Environment.GetEnvironmentVariable("TERNI_EXAMPLE_TEST");
        if (!string.IsNullOrEmpty(test))
        {
            _simulation.Paused = true;
            var passed = SelfTest(test);
            GetTree().CreateTimer(0.5).Timeout += () => GetTree().Quit(passed ? 0 : 1);
            return;
        }
        // Every time a world starts: subscriptions belong to one world.
        _simulation.Started += Setup;
        _simulation.TickCompleted += OnTick;
        _simulation.Failed += error => _label.Text = "The simulation stopped:\n" + error;
        if (_simulation.IsRunning)
            Setup();
    }

    private void Setup()
    {
        var world = _simulation.World;
        _subscribed = world;
        _planet = world.Entity("planet");
        _pop = world.Subscribe(_planet, "pop");
        GD.Print($"Galaxy runs on {world.DeviceName}: {world.GetCount(_planet)} planets.");
    }

    private void OnTick()
    {
        var world = _simulation.World;
        if (world != _subscribed)
            Setup();
        var pop = world.GetSubscribed(_pop);
        var text = new StringBuilder();
        text.AppendLine($"Terni galaxy on {world.DeviceName} (C#)");
        text.AppendLine($"Tick {world.Ticks}, {world.LastTickGpuMs:0.000} ms on the device per tick");
        text.AppendLine();
        for (var row = 0; row < pop.Length; row++)
            text.AppendLine($"{world.GetRowId(_planet, row)}: {TerniValue.ToDouble(pop[row], TerniFieldType.Quantity):0.0}");
        _label.Text = text.ToString();
    }

    private static bool SelfTest(string resultFile)
    {
        var log = new StringBuilder();
        var ok = true;
        void Check(bool condition, string what)
        {
            log.AppendLine((condition ? "ok    " : "FAIL  ") + what);
            ok &= condition;
        }
        try
        {
            var devices = TerniWorld.GetDevices();
            Check(devices.Any(d => d.Usable), $"{devices.Length} devices, at least one usable");
            Check(devices.Where(d => d.Usable).All(d => Enum.IsDefined(d.Robustness)),
                  "buffer robustness per device: " + string.Join(", ", devices.Where(d => d.Usable).Select(d => d.Robustness)));
            if (System.Environment.GetEnvironmentVariable("TERNI_EXPECT_SOFTWARE_DRIVER") == "1")
                Check(TerniWorld.SoftwareDriver.Length > 0 && devices.All(d => d.IsCpu),
                      $"no GPU: the software driver shipped with the game is added ({TerniWorld.SoftwareDriver})");
            TerniHash[] firstHashes = null;
            foreach (var device in devices.Where(d => d.Usable))
            {
                var world = new TerniWorld();
                Check(world.Load("res://Galaxy"), $"[{device.Name}] load");
                Check(world.BeginStart(new TerniStartOptions { Device = device.Index }), $"[{device.Name}] begin start");
                while (!world.StartReady())
                {
                }
                Check(world.IsRunning, $"[{device.Name}] started: {world.LastError}");
                if (!world.IsRunning)
                    continue; // the rest would only repeat that
                var planet = world.Entity("planet");
                var pop = world.Subscribe(planet, "pop");
                for (var i = 0; i < 100; i++)
                    world.Tick();
                var save = world.Save();
                for (var i = 0; i < 100; i++)
                {
                    world.BeginTick();
                    while (!world.TickReady())
                    {
                    }
                    world.EndTick();
                }
                Check(world.GetStateHash() == Expected, $"[{device.Name}] state hash {world.GetStateHash():x16}, expected {Expected:x16}");
                var hashes = world.GetFieldHashes();
                Check(hashes.Any(h => h.Name == "planet.pop") && world.GetEntityHashes().Any(h => h.Name == "planet") &&
                      (firstHashes == null || hashes.SequenceEqual(firstHashes)), $"[{device.Name}] hashes on the device, the same on every device");
                firstHashes ??= hashes;
                Check(world.GetStageTimes().Any(t => t.Name == "planet_growth" && t.Ran && t.Runs == 200), $"[{device.Name}] GPU time per stage");
                Check(world.GetSubscribed(pop).SequenceEqual(world.ReadField(planet, "pop").Take(world.GetCount(planet))),
                      $"[{device.Name}] subscribed pop equals read pop");
                Check(world.LoadSave(save) && world.Ticks == 100, $"[{device.Name}] save restores tick 100");
                var sicilia = world.FindRow(planet, "sicilia");
                var key = world.FindModifierKey("pop_growth");
                var before = world.GetModifierTotal(key, planet, sicilia);
                var modifier = world.AddModifier(world.FindBundle("good_harvest"), planet, sicilia);
                world.Tick();
                Check(world.IsModifierAlive(modifier) && world.GetModifierTotal(key, planet, sicilia) != before &&
                      world.GetModifierBreakdown(key, planet, sicilia).Length > 0, $"[{device.Name}] modifier added and counted");
                Check(!world.WriteValue(planet, "no_such_field", 0, 1) && world.LastError.Length > 0, $"[{device.Name}] a bad field is an error");
                world.Stop();
            }
            Check(TerniWorld.FindRendererDevice() >= -1, "find the renderer's device");

            // Events: a handler added and removed again is not called.
            var simulationNode = (Node)ClassDB.Instantiate("TerniSimulation");
            var simulation = new TerniSimulation(simulationNode);
            var calls = 0;
            Action counter = () => calls++;
            simulation.TickCompleted += counter;
            simulationNode.EmitSignal("tick_completed");
            simulation.TickCompleted -= counter;
            simulationNode.EmitSignal("tick_completed");
            Check(calls == 1, $"events can be removed ({calls} call)");
            simulationNode.Free();

            // Waits that spin change how long a tick's results take to arrive, never the results.
            var spinning = new TerniWorld();
            Check(spinning.Load("res://Galaxy") && spinning.Start(new TerniStartOptions { SpinMicroseconds = 200 }), "a world starts with SpinMicroseconds");
            for (var t = 0; t < 200; t++)
                spinning.Tick();
            Check(spinning.GetStateHash() == Expected, "waits that spin: the same state hash");
            spinning.Stop();

            // A lost device, simulated.
            var lost = new TerniWorld();
            var signalled = 0;
            lost.DeviceLost += error => signalled++;
            Check(lost.Load("res://Galaxy") && lost.Start(), "lost device: the world starts");
            lost.Tick();
            var autosave = lost.Save();
            lost.SimulateDeviceLost();
            Check(!lost.Tick() && lost.IsDeviceLost && !lost.IsRunning && signalled == 1, $"a lost device stops the world and is signalled: {lost.LastError}");
            Check(lost.Start() && lost.LoadSave(autosave) && lost.Ticks == 1, "the world starts again and loads the last save");
            lost.Stop();

            // A mod on top of the game's folder, and a save of the game alone loaded by name.
            var mod = "res://Mods/FasterGrowth";
            var modded = new TerniWorld();
            Check(modded.Load("res://Galaxy", mod) && modded.ModFolders.Length == 1 && modded.Checksum != lost.Checksum &&
                  modded.FindEntity("fleet") >= 0 && modded.Start(), $"a mod loads and starts: {modded.LastError}");
            Check(TerniWorld.SaveChecksum(autosave) == lost.Checksum && !modded.LoadSave(autosave) && modded.LoadSave(autosave, true) &&
                  modded.FittedLines.Length > 0, "a save of the game alone loads by name: " + string.Join("; ", modded.FittedLines));
            var fleet = modded.Entity("fleet");
            var row = modded.CreateEntity(fleet);
            Check(row == 0 && (modded.GetAliveBits(fleet)[0] & 1) == 1 && modded.GetFreeCount(fleet) == 63, "alive bits and free rows");
            modded.Stop();

            // A starting state changed before the start, and modifiers on many rows at once.
            var start = new TerniWorld();
            Check(start.Load("res://Galaxy") && start.GetDefines().Contains("growth_rate") &&
                  start.GetFieldRef(start.Entity("planet"), "owner") == start.Entity("empire"), "defines, and what a ref field refers to");
            var planets = start.Entity("planet");
            var harvest = start.FindBundle("good_harvest");
            var growth = start.FindModifierKey("pop_growth");
            var first = start.GetCount(planets);
            Check(start.SetModifierCapacity(2048) && start.SetStartCount(planets, first + 1) && start.WriteStart(planets, "owner", first, new[] { 0 }) &&
                  start.WriteStart(planets, "pop", first, new[] { 25600 }) && start.ReadStart(planets, "pop", first, 1).SequenceEqual(new[] { 25600 }) &&
                  start.AddStartModifier(harvest, planets, first), "a starting state is written and read: " + start.LastError);
            Check(start.Start() && start.ReadField(planets, "pop", first, 1)[0] == 25600 && start.SyncReads > 0, "and the world starts with it");
            var own = start.GetModifierOwnTotal(growth, planets, first);
            var instances = start.ModifierCount;
            var added = start.AddModifiers(harvest, planets, new[] { 0, first });
            Check(own > 0 && added.Length == 2 && start.ModifierCount == instances + 2 && start.GetModifierOwnTotal(growth, planets, first) == 2 * own,
                  "one bundle on two rows");
            var replaced = start.ReplaceModifier(added[1], harvest, planets, first, 2 * TerniValue.RateOne);
            Check(!start.IsModifierAlive(added[1]) && start.IsModifierAlive(replaced) && start.ModifierCount == instances + 2, "a modifier replaced by another");
            Check(start.TickChecked() && start.OrderIssues.Length == 0 && start.Ticks == 1, "a checked tick finds no order issues");
            Check(start.CompileStats.Compiled + start.CompileStats.Cached >= 3, $"compile statistics: {start.CompileStats}");
            start.Stop();
        }
        catch (Exception e)
        {
            Check(false, "exception: " + e);
        }
        log.AppendLine(ok ? "PASSED" : "FAILED");
        File.WriteAllText(resultFile, log.ToString());
        GD.Print("Terni C# self-test:\n" + log);
        return ok;
    }
}
