// SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
// Copyright (C) 2026 Rafał Zając
using System;
using System.IO;
using Flax.Build;
using Flax.Build.NativeCpp;

/// <summary>
/// The Terni simulation for Flax: TerniWorld (C++, with generated C# bindings) and the
/// TerniSimulation script. Links the libraries that build-native puts into
/// ThirdParty/Terni: Terni itself and terni_bridge, the interface TerniWorld calls
/// (Terni's own headers are C++20 with exceptions; the engine's are neither).
/// </summary>
public class Terni : GameModule
{
    /// <inheritdoc />
    public override void Init()
    {
        base.Init();
        BuildNativeCode = true;
    }

    /// <inheritdoc />
    public override void Setup(BuildOptions options)
    {
        base.Setup(options);
        options.ScriptingAPI.IgnoreMissingDocumentationWarnings = true;

        var terni = Path.GetFullPath(Path.Combine(FolderPath, "..", "..", "ThirdParty", "Terni"));
        if (!File.Exists(Path.Combine(terni, "include", "terni_bridge.h")))
            throw new Exception("Terni is not built: run build-native.sh (or build-native.bat) in the plugin folder first.");
        options.PrivateIncludePaths.Add(Path.Combine(terni, "include"));

        // terni_compile_rules, and the shaderc libraries it loads if they are not linked into it.
        foreach (var file in Directory.GetFiles(Path.Combine(terni, "bin")))
            options.DependencyFiles.Add(file);

        // Terni::runtime, the build without the rule compiler: the editor has a shader
        // compiler of its own that the one Terni uses must not meet. Rules are compiled by
        // terni_compile_rules, a separate program, into the SPIR-V cache.
        switch (options.Platform.Target)
        {
        case TargetPlatform.Linux:
            options.LinkEnv.InputFiles.Add(Path.Combine(terni, "lib", "libterni_bridge.a"));
            options.LinkEnv.InputFiles.Add(Path.Combine(terni, "lib", "libterni_runtime.a"));
            options.Libraries.Add("vulkan");
            break;
        case TargetPlatform.Windows:
        {
            var sdk = Environment.GetEnvironmentVariable("VULKAN_SDK");
            if (string.IsNullOrEmpty(sdk))
                throw new Exception("VULKAN_SDK is not set: install the Vulkan SDK.");
            options.LinkEnv.InputLibraries.Add(Path.Combine(terni, "lib", "terni_bridge.lib"));
            options.LinkEnv.InputLibraries.Add(Path.Combine(terni, "lib", "terni_runtime.lib"));
            options.LinkEnv.InputLibraries.Add(Path.Combine(sdk, "Lib", "vulkan-1.lib"));
            break;
        }
        default: throw new InvalidPlatformException(options.Platform.Target);
        }
    }
}
