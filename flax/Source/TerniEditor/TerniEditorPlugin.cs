// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
// Copyright (C) 2026 Rafał Zając
using FlaxEditor;
using FlaxEditor.GUI.ContextMenu;
using Terni;

namespace TerniEditor
{
    /// <summary>
    /// Adds Tools > Terni to the editor, and stops worlds that play mode left running.
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
        }

        /// <inheritdoc />
        public override void DeinitializeEditor()
        {
            Editor.PlayModeEnd -= TerniWorld.StopAll;
            TerniWorld.StopAll();
            _button?.Dispose();
            _button = null;
            base.DeinitializeEditor();
        }
    }
}
