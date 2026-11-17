@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Builds Terni with Visual Studio and puts it into ThirdParty\Terni, where the plugin's
rem build script looks for it. Run once before opening a project that uses the plugin,
rem and again after updating the plugin. Needs CMake, Git, Visual Studio 2022 or newer
rem with the C++ workload, and the Vulkan SDK (VULKAN_SDK set). Set TERNI_DIR to build a
rem local Terni checkout instead of the pinned commit.
setlocal
cd /d "%~dp0"
cmake -S ..\native -B ..\build\native -A x64 -DTERNI_DIR="%TERNI_DIR%" || exit /b 1
cmake --build ..\build\native --config Release || exit /b 1
cmake --install ..\build\native --config Release --prefix ThirdParty\Terni || exit /b 1
echo Terni is in %~dp0ThirdParty\Terni
