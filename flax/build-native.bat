@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception-1.0
rem Copyright (C) 2026 Rafal Zajac
rem
rem Builds Terni with MSVC and puts it into ThirdParty\Terni, where the plugin's build
rem script looks for it. Run once from a "x64 Native Tools Command Prompt" before opening
rem a project that uses the plugin, and again after updating the plugin. Needs CMake and
rem the Vulkan SDK (VULKAN_SDK set). Set TERNI_DIR to build a local Terni checkout.
setlocal
cd /d "%~dp0"
cmake -S ..\native -B ..\build\native -G "Visual Studio 17 2022" -A x64 -DTERNI_DIR="%TERNI_DIR%" || exit /b 1
cmake --build ..\build\native --config Release || exit /b 1
cmake --install ..\build\native --config Release --prefix ThirdParty\Terni || exit /b 1
