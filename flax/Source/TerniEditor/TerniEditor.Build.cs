// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
// Copyright (C) 2026 Rafał Zając
using Flax.Build;
using Flax.Build.NativeCpp;

/// <summary>
/// Editor tools for Terni: the Terni window (devices, running worlds, field values).
/// </summary>
public class TerniEditor : GameEditorModule
{
    /// <inheritdoc />
    public override void Init()
    {
        base.Init();
        BuildNativeCode = false;
    }

    /// <inheritdoc />
    public override void Setup(BuildOptions options)
    {
        base.Setup(options);
        options.ScriptingAPI.IgnoreMissingDocumentationWarnings = true;
        options.PublicDependencies.Add("Terni");
    }
}
