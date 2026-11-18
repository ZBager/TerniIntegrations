@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Builds Terni with Visual Studio and puts it into ThirdParty\Terni, where the plugin's
rem build script looks for it. Run once before opening a project that uses the plugin,
rem and again after updating the plugin. Needs CMake, Git, Visual Studio 2022 or newer
rem with the C++ workload, and the Vulkan SDK (VULKAN_SDK set). Set TERNI_DIR to build a
rem local Terni checkout instead of the pinned commit. The software Vulkan driver that
rem ..\native\lavapipe\build.bat builds ships with the plugin once it is built;
rem TERNI_SOFTWARE_DRIVER names another (a vulkan_lvp.dll), or none.
setlocal
cd /d "%~dp0"
if not defined TERNI_SOFTWARE_DRIVER set TERNI_SOFTWARE_DRIVER=%~dp0..\build\lavapipe\vulkan_lvp.dll
if /i "%TERNI_SOFTWARE_DRIVER%"=="none" set TERNI_SOFTWARE_DRIVER=
if defined TERNI_SOFTWARE_DRIVER if not exist "%TERNI_SOFTWARE_DRIVER%" set TERNI_SOFTWARE_DRIVER=
rem none left from an earlier build
if exist ThirdParty\Terni\bin\vulkan_lvp.dll del ThirdParty\Terni\bin\vulkan_lvp.dll
cmake -S ..\native -B ..\build\native -A x64 -DTERNI_DIR="%TERNI_DIR%" -DTERNI_SOFTWARE_DRIVER="%TERNI_SOFTWARE_DRIVER%" || exit /b 1
cmake --build ..\build\native --config Release || exit /b 1
cmake --install ..\build\native --config Release --prefix ThirdParty\Terni || exit /b 1
echo Terni is in %~dp0ThirdParty\Terni
