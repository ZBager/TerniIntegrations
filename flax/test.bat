@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Runs the example's self-test in a headless editor: the galaxy example on every Vulkan
rem device, checked against the state hash Terni gives on its own.
rem   set FLAX_EDITOR=C:\path\to\FlaxEditor.exe
rem   test.bat
setlocal
if "%FLAX_EDITOR%"=="" (echo Set FLAX_EDITOR to FlaxEditor.exe & exit /b 2)
cd /d "%~dp0Example"
set TERNI_EXAMPLE_TEST=%TEMP%\terni_selftest.txt
del "%TERNI_EXAMPLE_TEST%" 2>nul
"%FLAX_EDITOR%" -std -headless -mute "-project=%CD%" -play 7e2a1c0041d05b6f8a3c9d1e2f4b6a80
if not exist "%TERNI_EXAMPLE_TEST%" (echo The self-test did not run: see Example\Logs & exit /b 1)
type "%TERNI_EXAMPLE_TEST%"
findstr /x /c:"PASSED" "%TERNI_EXAMPLE_TEST%" >nul || exit /b 1
