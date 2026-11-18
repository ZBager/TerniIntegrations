@echo off
rem SPDX-License-Identifier: LGPL-2.1-only WITH AdditionRef-Terni-linking-exception
rem Copyright (C) 2026 Rafal Zajac
rem
rem Builds Mesa's lavapipe, a Vulkan driver that runs on the CPU, with LLVM inside it, for
rem the plugins to ship with games: on a machine whose GPUs cannot run the simulation, it
rem is the device the simulation runs on (with the same results). Built for compute only,
rem with the C runtime linked in, so it needs nothing on the player's machine but the
rem Vulkan loader. See build.sh, which this follows.
rem
rem   native\lavapipe\build.bat   ->  build\lavapipe\vulkan_lvp.dll and its licences
rem
rem The first run takes long (LLVM: an hour or more); later runs reuse what is built.
rem Needs Visual Studio 2022 or newer with the C++ workload, CMake, Ninja, Python 3 with
rem meson, mako, PyYAML and packaging (pip install meson mako pyyaml packaging),
rem win_flex and win_bison (winflexbison) and glslangValidator (the Vulkan SDK) on PATH.
setlocal
set LLVM_VERSION=23.1.5
set LLVM_SHA256=fb50b1eac93ddeb3aa8dab4cfe324dff74f4a9c2bdee6160c9e36188f8d06a6e
set MESA_VERSION=26.2.7
set MESA_SHA256=04294f75d22e9f61cd1e050206bc48b3d39366e788f1803a3368e3d5a872ce18

for %%i in ("%~dp0..\..\build\lavapipe") do set ROOT=%%~fi
if not exist "%ROOT%\downloads" mkdir "%ROOT%\downloads"
if not exist "%ROOT%\src" mkdir "%ROOT%\src"

rem The compiler: from Visual Studio's own environment, if not set up already.
where cl > nul 2>&1
if errorlevel 1 (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VS=%%i
)
where cl > nul 2>&1
if errorlevel 1 (
    if "%VS%"=="" (echo Visual Studio with the C++ workload was not found & exit /b 1)
    call "%VS%\VC\Auxiliary\Build\vcvars64.bat" > nul || exit /b 1
)

rem ---- LLVM: static libraries for the x86 CPU only, no tools but llvm-config ----
set LLVM_SRC=%ROOT%\src\llvm-project-%LLVM_VERSION%.src
set LLVM_BUILD=%ROOT%\llvm-build
if not exist "%LLVM_SRC%\libc" (
    call :fetch https://github.com/llvm/llvm-project/releases/download/llvmorg-%LLVM_VERSION%/llvm-project-%LLVM_VERSION%.src.tar.xz %LLVM_SHA256% || exit /b 1
    rem The parts an LLVM build reads; mlgo-utils has symbolic links, which need rights to make.
    tar -xf "%ROOT%\downloads\llvm-project-%LLVM_VERSION%.src.tar.xz" -C "%ROOT%\src" --exclude "*/mlgo-utils/*" llvm-project-%LLVM_VERSION%.src/llvm llvm-project-%LLVM_VERSION%.src/cmake llvm-project-%LLVM_VERSION%.src/third-party llvm-project-%LLVM_VERSION%.src/libc || exit /b 1
)
cmake -S "%LLVM_SRC%\llvm" -B "%LLVM_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
    -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS= -DLLVM_ENABLE_RUNTIMES= ^
    -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF ^
    -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF -DLLVM_ENABLE_BINDINGS=OFF ^
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_LIBEDIT=OFF ^
    -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_CURL=OFF -DLLVM_ENABLE_HTTPLIB=OFF -DLLVM_ENABLE_FFI=OFF ^
    -DLLVM_ENABLE_Z3_SOLVER=OFF -DLLVM_ENABLE_DIA_SDK=OFF -DLLVM_ENABLE_RTTI=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF > nul || exit /b 1
cmake --build "%LLVM_BUILD%" --target llvm-libraries llvm-config || exit /b 1

rem ---- Mesa: lavapipe alone ----
set MESA_SRC=%ROOT%\src\mesa-%MESA_VERSION%
set MESA_BUILD=%ROOT%\mesa-build
if not exist "%MESA_SRC%" (
    call :fetch https://archive.mesa3d.org/mesa-%MESA_VERSION%.tar.xz %MESA_SHA256% || exit /b 1
    tar -xf "%ROOT%\downloads\mesa-%MESA_VERSION%.tar.xz" -C "%ROOT%\src" || exit /b 1
)
set LLVM_CONFIG=%LLVM_BUILD:\=/%/bin/llvm-config.exe
> "%ROOT%\llvm.ini" echo [binaries]
>> "%ROOT%\llvm.ini" echo llvm-config = '%LLVM_CONFIG%'
if not exist "%MESA_BUILD%\build.ninja" (
    meson setup "%MESA_BUILD%" "%MESA_SRC%" --native-file "%ROOT%\llvm.ini" ^
        --buildtype=release -Db_ndebug=true -Db_vscrt=mt ^
        -Dplatforms=windows -Dvulkan-drivers=swrast -Dgallium-drivers=llvmpipe ^
        -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled -Dgbm=disabled ^
        -Dllvm=enabled -Dshared-llvm=disabled -Dmicrosoft-clc=disabled ^
        -Dshader-cache=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dzlib=disabled -Dzstd=disabled ^
        -Dspirv-tools=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled ^
        -Dbuild-tests=false -Dtools=[] -Dvulkan-layers=[] -Dvideo-codecs=[] -Dperfetto=false ^
        -Dteflon=false -Dgallium-va=disabled -Dgallium-rusticl=false -Dhtml-docs=disabled || exit /b 1
)
ninja -C "%MESA_BUILD%" src/gallium/targets/lavapipe/vulkan_lvp.dll || exit /b 1

rem ---- the driver and the licences of what is inside it ----
copy /y "%MESA_BUILD%\src\gallium\targets\lavapipe\vulkan_lvp.dll" "%ROOT%\vulkan_lvp.dll" > nul || exit /b 1
if not exist "%ROOT%\licences" mkdir "%ROOT%\licences"
copy /y "%MESA_SRC%\docs\license.rst" "%ROOT%\licences\Mesa-license.rst" > nul
copy /y "%LLVM_SRC%\llvm\LICENSE.TXT" "%ROOT%\licences\LLVM-LICENSE.TXT" > nul
echo Built %ROOT%\vulkan_lvp.dll, Mesa %MESA_VERSION% with LLVM %LLVM_VERSION%
exit /b 0

rem Downloads a file once and checks it against its pinned SHA-256: call :fetch url sha256
:fetch
set FILE=%ROOT%\downloads\%~nx1
if not exist "%FILE%" (
    echo downloading %1
    curl -fL --retry 3 -o "%FILE%.part" %1 || exit /b 1
    move /y "%FILE%.part" "%FILE%" > nul
)
set HASH=
for /f "usebackq tokens=*" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%FILE%').Hash"`) do set HASH=%%h
if /i not "%HASH%"=="%2" (echo %FILE% does not match its checksum & exit /b 1)
exit /b 0
