// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#if FLAX_EDITOR
using System;
using System.IO;
using FlaxEditor;
using FlaxEngine;
using Terni;

namespace Game
{
    /// <summary>
    /// With the environment variable TERNI_RELOAD_TEST set to a result file: starts a world
    /// in the editor, keeps it referenced, and makes the editor rebuild and reload the
    /// scripts (the Terni plugin's too). After the reload a new world must run the galaxy
    /// to the expected hash. test.sh --reload checks the result and the editor's log.
    /// </summary>
    public class ReloadTest : EditorPlugin
    {
        // Held across the reload on purpose, as a careless script would.
        private static TerniWorld _held;
        private int _frames;

        private static string Result => Environment.GetEnvironmentVariable("TERNI_RELOAD_TEST");
        private static string Marker => Result + ".reloading";

        /// <inheritdoc />
        public override void InitializeEditor()
        {
            base.InitializeEditor();
            if (!string.IsNullOrEmpty(Result))
                Scripting.Update += OnUpdate;
        }

        /// <inheritdoc />
        public override void DeinitializeEditor()
        {
            Scripting.Update -= OnUpdate;
            base.DeinitializeEditor();
        }

        private void OnUpdate()
        {
            // Let the editor finish starting (or reloading) first.
            if (++_frames < 30 || ScriptsBuilder.IsCompiling || !Editor.Instance.StateMachine.IsEditMode)
                return;
            Scripting.Update -= OnUpdate;
            var folder = Path.Combine(Globals.ProjectFolder, "Galaxy");
            try
            {
                if (!File.Exists(Marker))
                {
                    _held = new TerniWorld();
                    if (!_held.Load(folder) || !_held.Start(new TerniStartOptions()))
                        throw new Exception("the first world did not start: " + _held.LastError);
                    for (var i = 0; i < 10; i++)
                        _held.Tick();
                    File.WriteAllText(Marker, _held.DeviceName);
                    // Make both the native module and the scripts out of date, then rebuild:
                    // the editor reloads them when the build is done.
                    var now = DateTime.UtcNow;
                    var plugin = Path.GetFullPath(Path.Combine(Globals.ProjectFolder, "..", "Source"));
                    File.SetLastWriteTimeUtc(Path.Combine(plugin, "Terni", "TerniWorld.cpp"), now);
                    File.SetLastWriteTimeUtc(Path.Combine(plugin, "Terni", "TerniWorld.cs"), now);
                    File.SetLastWriteTimeUtc(Path.Combine(Globals.ProjectFolder, "Source", "Game", "ReloadTest.cs"), now);
                    Debug.Log("[ReloadTest] world started, reloading scripts");
                    ScriptsBuilder.Compile();
                    return;
                }

                File.Delete(Marker);
                var worlds = TerniWorld.GetWorlds().Length;
                var world = new TerniWorld();
                var ok = world.Load(folder) && world.Start(new TerniStartOptions());
                for (var i = 0; ok && i < 200; i++)
                    ok = world.Tick();
                var hash = world.GetStateHash();
                world.Stop();
                FlaxEngine.Object.Destroy(world);
                var passed = ok && hash == SelfTest.Expected && worlds == 0;
                File.WriteAllText(Result, $"worlds left after reload: {worlds}\nhash after reload: {hash:x16}\n" + (passed ? "PASSED\n" : "FAILED\n"));
            }
            catch (Exception e)
            {
                File.WriteAllText(Result, "exception: " + e + "\nFAILED\n");
            }
            Engine.RequestExit();
        }
    }
}
#endif
