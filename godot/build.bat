@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Builds Terni and the GDExtension into addons\terni\bin, and copies the addon into the
rem example projects. Needs CMake, Git, Python 3, Visual Studio 2022 or newer with the C++
rem workload, and the Vulkan SDK (VULKAN_SDK set). Set TERNI_DIR to build a local Terni
rem checkout instead of the pinned commit.
setlocal
cd /d "%~dp0"
set BUILD=%~dp0..\build
cmake -S ..\native -B "%BUILD%\native" -A x64 -DTERNI_DIR="%TERNI_DIR%" || exit /b 1
cmake --build "%BUILD%\native" --config Release || exit /b 1
cmake --install "%BUILD%\native" --config Release --prefix "%BUILD%\terni" || exit /b 1
cmake -S . -B "%BUILD%\godot" -A x64 -DTERNI_PREFIX="%BUILD%\terni" || exit /b 1
cmake --build "%BUILD%\godot" --config Release || exit /b 1
for %%p in (example example-csharp) do (
    if exist "%%p\addons\terni" rmdir /s /q "%%p\addons\terni"
    xcopy /e /i /q addons\terni "%%p\addons\terni" > nul
)
echo Built %~dp0addons\terni, copied into the example projects
