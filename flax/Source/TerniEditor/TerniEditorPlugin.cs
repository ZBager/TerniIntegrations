// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using FlaxEditor;
using FlaxEditor.GUI.ContextMenu;
using Terni;

namespace TerniEditor
{
    /// <summary>
    /// Adds Tools > Terni to the editor, stops worlds that play mode left running, and
    /// puts the project's game folders into cooked games.
    /// </summary>
    public class TerniEditorPlugin : EditorPlugin
    {
        private ContextMenuButton _button;

        /// <inheritdoc />
        public override void InitializeEditor()
        {
            base.InitializeEditor();
            // A headless editor has no menus.
            var tools = Editor.UI?.MenuTools;
            if (tools != null)
                _button = tools.ContextMenu.AddButton("Terni", () => new TerniWindow().Show());
            Editor.PlayModeEnd += TerniWorld.StopAll;
            GameCooker.DeployFiles += TerniCooking.OnDeployFiles;
            GameCooker.PostProcessFiles += TerniCooking.OnPostProcessFiles;
        }

        /// <inheritdoc />
        public override void DeinitializeEditor()
        {
            GameCooker.PostProcessFiles -= TerniCooking.OnPostProcessFiles;
            GameCooker.DeployFiles -= TerniCooking.OnDeployFiles;
            Editor.PlayModeEnd -= TerniWorld.StopAll;
            TerniWorld.StopAll();
            _button?.Dispose();
            _button = null;
            base.DeinitializeEditor();
        }
    }
}
