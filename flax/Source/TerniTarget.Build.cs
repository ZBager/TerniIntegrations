// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
// Copyright (C) 2026 Rafał Zając
using Flax.Build;

public class TerniTarget : GameProjectTarget
{
    /// <inheritdoc />
    public override void Init()
    {
        base.Init();
        Modules.Add("Terni");
    }
}
