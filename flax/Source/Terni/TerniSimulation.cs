// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.IO;
using FlaxEngine;

namespace Terni
{
    /// <summary>
    /// Runs a Terni game folder from a scene: loads it, starts it on a device and ticks it
    /// at a steady rate without holding up frames. Other scripts use <see cref="World"/>
    /// to read and write the simulation, and the events to follow it.
    /// </summary>
    public class TerniSimulation : Script
    {
        /// <summary>
        /// The game folder (schema, common, history, rules). A relative path starts at the project folder.
        /// </summary>
        [EditorOrder(0), Tooltip("The game folder (schema, common, history, rules). A relative path starts at the project folder.")]
        public string GameFolder = "Sim";

        /// <summary>
        /// Mod folders loaded on top of the game folder, in order. A relative path starts at the project folder.
        /// </summary>
        [EditorOrder(5), Tooltip("Mod folders loaded on top of the game folder, in order. A relative path starts at the project folder.")]
        public string[] ModFolders = Array.Empty<string>();

        /// <summary>
        /// The folder of the compiled rules. Empty: "spirv" inside the game folder.
        /// </summary>
        [EditorOrder(10), Tooltip("The folder of the compiled rules. Empty: \"spirv\" inside the game folder.")]
        public string CacheFolder = "";

        /// <summary>
        /// Load and start when the script is enabled.
        /// </summary>
        [EditorOrder(20), Tooltip("Load and start when the script is enabled.")]
        public bool AutoStart = true;

        /// <summary>
        /// Simulation ticks per second while not paused. 0 ticks only on Step().
        /// </summary>
        [EditorOrder(30), Limit(0, 1000), Tooltip("Simulation ticks per second while not paused. 0 ticks only on Step().")]
        public float TicksPerSecond = 10;

        /// <summary>
        /// No ticks while paused, except by Step().
        /// </summary>
        [EditorOrder(40), Tooltip("No ticks while paused, except by Step().")]
        public bool Paused;

        /// <summary>
        /// Ticks run in the background and are collected in a later frame. Off: each tick finishes within its frame.
        /// </summary>
        [EditorOrder(50), Tooltip("Ticks run in the background and are collected in a later frame. Off: each tick finishes within its frame.")]
        public bool Background = true;

        /// <summary>
        /// Device index from TerniWorld.GetDevices(), or -1 to choose by Preference.
        /// </summary>
        [EditorOrder(60), Tooltip("Device index from TerniWorld.GetDevices(), or -1 to choose: the renderer's GPU with Match Renderer, otherwise by Preference.")]
        public int Device = -1;

        /// <summary>
        /// Run on the GPU the engine renders with (when Device is -1), if it can run the simulation.
        /// </summary>
        [EditorOrder(65), Tooltip("Run on the GPU the engine renders with (when Device is -1), if it can run the simulation.")]
        public bool MatchRenderer = true;

        /// <summary>
        /// Which device to choose when Device is -1.
        /// </summary>
        [EditorOrder(70), Tooltip("Which device to choose when Device is -1 and the renderer's GPU is not used.")]
        public TerniDevicePreference Preference = TerniDevicePreference.Discrete;

        /// <summary>
        /// Compile rules that are not in the cache yet. Turn off in a shipped game whose cache is complete.
        /// </summary>
        [EditorOrder(80), Tooltip("Compile rules that are not in the cache yet. Turn off in a shipped game whose cache is complete.")]
        public bool CompileRules = true;

        /// <summary>
        /// Start in the background: the world starts on a worker thread while frames go on,
        /// and Started comes in a later frame. Off: StartWorld() returns once it has started.
        /// </summary>
        [EditorOrder(90), Tooltip("Start in the background: frames go on while the world starts, and Started comes in a later frame.")]
        public bool StartInBackground = true;

        /// <summary>
        /// Waits for a tick spin for up to this many microseconds before they sleep: worth it only
        /// for ticks well under a millisecond that the frame waits for (Background off), and it
        /// keeps a CPU core busy meanwhile. 0: waits sleep at once.
        /// </summary>
        [EditorOrder(100), Limit(0, 10000), Tooltip("Waits for a tick spin for up to this many microseconds before they sleep: worth it only for ticks well under a millisecond that the frame waits for (Background off). 0: waits sleep at once.")]
        public int SpinMicroseconds;

        /// <summary>
        /// Why loading, starting or the last tick failed; empty if nothing failed. Visual
        /// scripts, which cannot bind the events of this script, can check it instead of Failed.
        /// </summary>
        [NoSerialize]
        public string LastError { get; private set; } = "";

        /// <summary>
        /// The world started and its starting state is on the device.
        /// </summary>
        public event Action<TerniSimulation> Started;

        /// <summary>
        /// A tick was collected: subscribed fields hold its results.
        /// </summary>
        public event Action<TerniSimulation> TickCompleted;

        /// <summary>
        /// Loading, starting or a tick failed. The text is also in the log. A lost device
        /// is DeviceLost instead.
        /// </summary>
        public event Action<TerniSimulation, string> Failed;

        /// <summary>
        /// The device was lost (a rule ran too long and the driver reset the GPU, or the GPU
        /// went away) and the world has stopped. Call StartWorld() and load the last save
        /// into World to go on. Visual scripts check IsDeviceLost instead.
        /// </summary>
        public event Action<TerniSimulation, string> DeviceLost;

        /// <summary>
        /// The device was lost; see DeviceLost.
        /// </summary>
        [NoSerialize]
        public bool IsDeviceLost => World != null && World.IsDeviceLost;

        /// <summary>
        /// The world, or null before StartWorld() and after StopWorld().
        /// </summary>
        [NoSerialize]
        public TerniWorld World { get; private set; }

        /// <summary>
        /// The world is running.
        /// </summary>
        [NoSerialize]
        public bool IsRunning => World != null && World.IsRunning;

        /// <summary>
        /// Completed ticks.
        /// </summary>
        [NoSerialize]
        public ulong Ticks => World != null ? World.Ticks : 0;

        private double _due;

        /// <summary>
        /// The game folder as a full path.
        /// </summary>
        public string ResolveFolder(string folder)
        {
            return Path.IsPathRooted(folder) ? folder : Path.Combine(Globals.ProjectFolder, folder);
        }

        /// <summary>
        /// The world is starting in the background.
        /// </summary>
        [NoSerialize]
        public bool IsStarting => World != null && World.IsStarting;

        /// <summary>
        /// Loads the game folder and starts the world. Stops a running one first. With
        /// StartInBackground the start goes on after this returns; Started or Failed follows.
        /// </summary>
        /// <returns>True if the world is running, or starting in the background.</returns>
        public bool StartWorld()
        {
            StopWorld();
            LastError = "";
            World = new TerniWorld();
            World.DeviceLost += OnDeviceLost;
            var options = new TerniStartOptions
            {
                UseDevice = Device >= 0,
                Device = Math.Max(Device, 0),
                IgnoreRenderer = !MatchRenderer,
                Preference = Preference,
                CacheFolder = string.IsNullOrEmpty(CacheFolder) ? "" : ResolveFolder(CacheFolder),
                CacheOnly = !CompileRules,
                SpinMicroseconds = SpinMicroseconds,
            };
            var mods = Array.ConvertAll(ModFolders ?? Array.Empty<string>(), ResolveFolder);
            var ok = World.LoadWithMods(ResolveFolder(GameFolder), mods);
            if (ok && StartInBackground)
                ok = World.BeginStart(options);
            else if (ok)
                ok = World.Start(options);
            if (!ok)
            {
                FailStart();
                return false;
            }
            if (!StartInBackground)
                OnStarted();
            return true;
        }

        private void OnStarted()
        {
            _due = 0;
            Started?.Invoke(this);
        }

        private void OnDeviceLost(TerniWorld world)
        {
            LastError = world.LastError;
            DeviceLost?.Invoke(this, LastError);
        }

        private void FailStart()
        {
            var error = World.LastError;
            LastError = error;
            StopWorld();
            Failed?.Invoke(this, error);
        }

        /// <summary>
        /// Stops the world and frees its device.
        /// </summary>
        public void StopWorld()
        {
            if (World == null)
                return;
            World.DeviceLost -= OnDeviceLost;
            World.Stop();
            Destroy(World);
            World = null;
        }

        /// <summary>
        /// Runs one tick now and waits for it, paused or not.
        /// </summary>
        /// <returns>True if the tick ran.</returns>
        public bool Step()
        {
            if (!IsRunning)
                return false;
            if (!World.EndTick() || !World.Tick())
                return Fail();
            TickCompleted?.Invoke(this);
            return true;
        }

        /// <inheritdoc />
        public override void OnEnable()
        {
            if (AutoStart)
                StartWorld();
        }

        /// <inheritdoc />
        public override void OnDisable()
        {
            StopWorld();
        }

        /// <inheritdoc />
        public override void OnUpdate()
        {
            if (IsStarting)
            {
                if (!World.StartReady())
                    return;
                if (!World.IsRunning)
                {
                    FailStart();
                    return;
                }
                OnStarted();
            }
            if (!IsRunning)
                return;

            // Collect the tick begun in an earlier frame, once the device is done with it.
            if (World.TickInFlight)
            {
                if (!World.TickReady())
                    return;
                if (!World.EndTick())
                {
                    Fail();
                    return;
                }
                TickCompleted?.Invoke(this);
                if (!IsRunning)
                    return;
            }

            if (Paused || TicksPerSecond <= 0)
            {
                _due = 0;
                return;
            }
            // At most one tick per frame: a slow frame must not queue a burst of ticks.
            _due = Math.Min(_due + Time.UnscaledDeltaTime * TicksPerSecond, 1.0);
            if (_due < 1.0)
                return;
            _due -= 1.0;
            if (Background)
            {
                if (!World.BeginTick())
                    Fail();
            }
            else if (World.Tick())
                TickCompleted?.Invoke(this);
            else
                Fail();
        }

        private bool Fail()
        {
            if (IsDeviceLost)
                return false; // DeviceLost was invoked instead
            var error = World != null ? World.LastError : "";
            LastError = error;
            Paused = true;
            Failed?.Invoke(this, error);
            return false;
        }
    }
}
