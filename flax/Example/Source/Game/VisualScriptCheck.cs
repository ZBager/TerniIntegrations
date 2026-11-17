// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
#if FLAX_EDITOR
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using FlaxEditor;
using FlaxEditor.Scripting;
using FlaxEngine;

namespace Game
{
    /// <summary>
    /// With the environment variable TERNI_VISUAL_CHECK set to a result file: lists which
    /// members of the plugin's types Visual Scripting can use, by the editor's own rules
    /// (the ones its node library applies), and checks the ones a visual script needs.
    /// </summary>
    public class VisualScriptCheck : EditorPlugin
    {
        // What a visual script needs to run a simulation, follow it and act on it.
        private static readonly string[] Required =
        {
            "TerniSimulation.World", "TerniSimulation.IsRunning", "TerniSimulation.Ticks",
            "TerniSimulation.LastError", "TerniSimulation.StartWorld", "TerniSimulation.StopWorld", "TerniSimulation.Step",
            "TerniWorld.Entity", "TerniWorld.FindRow", "TerniWorld.ReadField", "TerniWorld.WriteValue",
            "TerniWorld.Subscribe", "TerniWorld.GetSubscribed", "TerniWorld.AddModifier", "TerniWorld.FindBundle",
            "TerniWorld.GetModifierTotal", "TerniWorld.Save", "TerniWorld.LoadSave", "TerniWorld.Ticks",
            "TerniWorld.LastError", "TerniWorld.GetDevices", "TerniValue.ToDouble", "TerniValue.FromDouble",
        };

        /// <inheritdoc />
        public override void InitializeEditor()
        {
            base.InitializeEditor();
            var result = Environment.GetEnvironmentVariable("TERNI_VISUAL_CHECK");
            if (string.IsNullOrEmpty(result))
                return;
            var text = new StringBuilder();
            var ok = true;
            try
            {
                var utils = typeof(Editor).Assembly.GetType("FlaxEditor.Surface.SurfaceUtils", true);
                var invoke = utils.GetMethod("IsValidVisualScriptInvokeMethod", BindingFlags.NonPublic | BindingFlags.Static);
                var field = utils.GetMethod("IsValidVisualScriptField", BindingFlags.NonPublic | BindingFlags.Static);
                var usable = new System.Collections.Generic.HashSet<string>();
                foreach (var type in new[] { typeof(Terni.TerniWorld), typeof(Terni.TerniSimulation), typeof(Terni.TerniValue) })
                {
                    var scriptType = new ScriptType(type);
                    foreach (var member in scriptType.GetMembers(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly))
                    {
                        bool valid;
                        if (member.IsMethod)
                        {
                            var args = new object[] { member, null };
                            valid = (bool)invoke.Invoke(null, args);
                        }
                        else if (member.IsField)
                            valid = (bool)field.Invoke(null, new object[] { member });
                        else if (member.IsProperty)
                            valid = member.HasGet && !member.HasAttribute(typeof(HideInEditorAttribute), true);
                        else
                            continue;
                        var name = type.Name + "." + member.Name;
                        if (valid)
                            usable.Add(name);
                        text.AppendLine((valid ? "usable      " : "not usable  ") + name);
                    }
                }
                text.AppendLine();
                foreach (var name in Required)
                {
                    var found = usable.Contains(name) || usable.Contains(name.Replace(".", ".Get"));
                    text.AppendLine((found ? "ok    " : "FAIL  ") + "visual scripts can use " + name);
                    ok &= found;
                }
            }
            catch (Exception e)
            {
                text.AppendLine("exception: " + e);
                ok = false;
            }
            text.AppendLine(ok ? "PASSED" : "FAILED");
            File.WriteAllText(result, text.ToString());
            Engine.RequestExit();
        }
    }
}
#endif
