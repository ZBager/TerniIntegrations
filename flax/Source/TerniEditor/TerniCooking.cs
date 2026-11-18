// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using System;
using System.Collections.Generic;
using System.IO;
using FlaxEditor;
using FlaxEngine;
using Terni;

namespace TerniEditor
{
    /// <summary>
    /// Puts Terni game folders into a cooked game. A game folder is a folder of the project
    /// with a "schema" and a "rules" folder in it. Each one has its rules compiled and is
    /// copied to the same place in the cooked game, with its "spirv" folder, so the path a
    /// TerniSimulation script has works there too and players need no compiler. A folder
    /// with a descriptor.mod is a mod, not a game: mods are not cooked (players put them
    /// beside the game).
    /// </summary>
    public static class TerniCooking
    {
        // Top-level folders of a project that never hold game data.
        private static readonly string[] Skipped = { "Binaries", "Cache", "Content", "Logs", "Output", "Plugins", "Source" };
        private const int MaxDepth = 4;

        /// <summary>
        /// The game folders of the project, as full paths.
        /// </summary>
        /// <param name="except">A folder not to look in (the cooked game), or null.</param>
        public static List<string> FindGameFolders(string except = null)
        {
            var found = new List<string>();
            var root = Path.GetFullPath(Globals.ProjectFolder);
            except = except != null ? Path.GetFullPath(except) : null;
            foreach (var folder in Directory.GetDirectories(root))
            {
                if (Array.IndexOf(Skipped, Path.GetFileName(folder)) < 0)
                    Find(folder, except, 1, found);
            }
            found.Sort(StringComparer.Ordinal);
            return found;
        }

        private static void Find(string folder, string except, int depth, List<string> found)
        {
            var name = Path.GetFileName(folder);
            if (name.StartsWith(".") || (except != null && Path.GetFullPath(folder) == except) || File.Exists(Path.Combine(folder, "descriptor.mod")))
                return;
            if (Directory.Exists(Path.Combine(folder, "schema")) && Directory.Exists(Path.Combine(folder, "rules")))
            {
                found.Add(folder);
                return;
            }
            if (depth >= MaxDepth)
                return;
            foreach (var child in Directory.GetDirectories(folder))
                Find(child, except, depth + 1, found);
        }

        internal static void OnDeployFiles()
        {
            var output = GameCooker.CurrentData.DataOutputPath;
            var root = Path.GetFullPath(Globals.ProjectFolder);
            foreach (var folder in FindGameFolders(output))
            {
                var relative = Path.GetRelativePath(root, folder);
                if (!TerniWorld.CompileRules(folder, Path.Combine(folder, "spirv"), out var text))
                {
                    // The cooked game would not start this world: say so where it is seen.
                    Debug.LogError($"[Terni] {relative} is not in the cooked game, its rules did not compile:\n{text}");
                    continue;
                }
                var target = Path.Combine(output, relative);
                if (Directory.Exists(target))
                    Directory.Delete(target, true);
                Copy(folder, target);
                Debug.Log($"[Terni] {relative} is in the cooked game ({text})");
            }
        }

        internal static void OnPostProcessFiles()
        {
            // The rule compiler ships with the game, for rules that players change. Copying
            // can drop its permission to run.
            var path = Path.Combine(GameCooker.CurrentData.NativeCodeOutputPath, "terni_compile_rules");
            if (File.Exists(path) && !OperatingSystem.IsWindows())
                File.SetUnixFileMode(path, File.GetUnixFileMode(path) | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
        }

        private static void Copy(string from, string to)
        {
            Directory.CreateDirectory(to);
            foreach (var file in Directory.GetFiles(from))
                File.Copy(file, Path.Combine(to, Path.GetFileName(file)), true);
            foreach (var folder in Directory.GetDirectories(from))
                Copy(folder, Path.Combine(to, Path.GetFileName(folder)));
        }
    }
}
