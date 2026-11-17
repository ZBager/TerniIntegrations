// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.IO;
using FlaxEngine;
using Terni;

namespace Game
{
    /// <summary>
    /// Follows the galaxy simulation that the TerniSimulation script on the same actor
    /// runs: subscribes to the planets' population, prints it now and then, and applies a
    /// modifier from the host. With the environment variable TERNI_EXAMPLE_TEST set, runs
    /// a self-test instead and closes the engine.
    /// </summary>
    public class GalaxyExample : Script
    {
        private TerniSimulation _simulation;
        private int _planet, _pop;

        /// <inheritdoc />
        public override void OnStart()
        {
            _simulation = Actor.GetScript<TerniSimulation>();
            if (_simulation == null)
            {
                Debug.LogError("GalaxyExample needs a TerniSimulation script on the same actor.");
                return;
            }
            _simulation.TickCompleted += OnTick;
            _simulation.Failed += (s, error) => Debug.LogError("The simulation stopped: " + error);

            var test = Environment.GetEnvironmentVariable("TERNI_EXAMPLE_TEST");
            if (!string.IsNullOrEmpty(test))
            {
                _simulation.Paused = true;
                SelfTest.Run(_simulation, test);
                Engine.RequestExit();
                return;
            }
            if (_simulation.IsRunning)
                Setup();
            else
                _simulation.Started += s => Setup();
        }

        private void Setup()
        {
            var world = _simulation.World;
            _planet = world.Entity("planet");
            // Subscribed fields come back with every tick, at no extra cost.
            _pop = world.Subscribe(_planet, "pop");

            // A host action: ten years of good harvests on Sicilia.
            var sicilia = world.FindRow(_planet, "sicilia");
            world.AddModifier(world.FindBundle("good_harvest"), _planet, sicilia, TerniValue.RateOne, 120);
            Debug.Log($"Galaxy runs on {world.DeviceName}: {world.GetCount(_planet)} planets.");
        }

        private void OnTick(TerniSimulation simulation)
        {
            var world = simulation.World;
            if (world.Ticks % 20 != 0)
                return;
            var pop = world.Subscribed(_pop);
            var text = $"tick {world.Ticks}:";
            for (var row = 0; row < pop.Length; row++)
                text += $" {world.GetRowId(_planet, row)} {TerniValue.ToDouble(pop[row], TerniFieldType.Quantity):0.0}";
            Debug.Log(text);
        }
    }
}
