@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Runs the example's self-test: the galaxy example on every Vulkan device, checked
rem against the state hash Terni gives on its own. Build first (build.bat).
rem   set GODOT=C:\path\to\Godot_v4.7.2-stable_win64.exe
rem   test.bat            headless
rem   test.bat window     in a window, so the renderer's GPU is checked too; more arguments
rem                       go to Godot: test.bat window --rendering-driver d3d12
rem   set GODOT_MONO=C:\path\to\Godot_v4.7.2-stable_mono_win64.exe
rem   test.bat csharp     the C# example, headless
setlocal
cd /d "%~dp0"
set RESULT=%TEMP%\terni_godot_test.txt
del "%RESULT%" 2>nul
set TERNI_EXAMPLE_TEST=%RESULT%
if /i "%1"=="csharp" goto csharp
if "%GODOT%"=="" (echo Set GODOT to the Godot program & exit /b 2)
cd example
rem The first import of a project registers the extension; Godot may crash as it quits
rem after that, with the import done. A second run checks it.
if not exist .godot (
    "%GODOT%" --headless --path . --import > nul 2>&1
    "%GODOT%" --headless --path . --import > nul 2>&1
)
if /i "%1"=="window" (
    "%GODOT%" --path . %2 %3 %4 %5
) else (
    "%GODOT%" --headless --path .
)
goto result

:csharp
if "%GODOT_MONO%"=="" (echo Set GODOT_MONO to the Godot .NET program & exit /b 2)
cd example-csharp
if not exist .godot "%GODOT_MONO%" --headless --path . --import > nul 2>&1
"%GODOT_MONO%" --headless --path . --build-solutions --quit > "%TEMP%\terni_csharp_build.txt" 2>&1
if not exist .godot\mono\temp\bin\Debug\TerniExampleCs.dll (
    type "%TEMP%\terni_csharp_build.txt"
    echo The C# project did not build. It needs the .NET 8 SDK or newer: dotnet --list-sdks
    exit /b 1
)
"%GODOT_MONO%" --headless --path . --import > nul 2>&1
"%GODOT_MONO%" --headless --path .

:result
if not exist "%RESULT%" (echo The self-test did not run & exit /b 1)
type "%RESULT%"
findstr /x /c:"PASSED" "%RESULT%" > nul || exit /b 1
