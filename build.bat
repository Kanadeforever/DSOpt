@echo off
:: build.bat — Build all Dead Space Fix ASI plugins
:: Requires: Visual Studio 2019 (Community/Pro/Enterprise) with C++ workload

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="

:: Find VS installation
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        set "VSPATH=%%i"
    )
)

:: Fallback paths
if not defined VSPATH (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VSPATH=C:\Program Files\Microsoft Visual Studio\18\Community"
    )
)
if not defined VSPATH (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VSPATH=C:\Program Files\Microsoft Visual Studio\18\Professional"
    )
)
if not defined VSPATH (
    echo [ERROR] Visual Studio not found!
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 (
    echo [ERROR] Failed to set up VS environment (vcvarsall.bat x86)
    exit /b 1
)

echo.
echo ============================================
echo  Building Dead Space Fix ASI plugins (x86)
echo ============================================

set "OUTDIR=%~dp0build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set "COMMON=%~dp0src\common"
set "CFLAGS=/nologo /O2 /MT /LD /GS- /std:c++17 /I"%COMMON%" /Zc:strictStrings-"
set "LFLAGS=/link /SUBSYSTEM:WINDOWS /MACHINE:X86"

:: --- CPUFix ---
echo.
echo [1/2] Building CPUFix.asi...
cl.exe %CFLAGS% "%~dp0src\cpu_fix\cpu_fix.cpp" %LFLAGS% /OUT:"%OUTDIR%\CPUFix.asi" /Fe:"%OUTDIR%\CPUFix.asi"
if errorlevel 1 (
    echo [ERROR] CPUFix build failed!
    exit /b 1
)
copy /Y "%~dp0src\cpu_fix\cpu_fix.ini" "%OUTDIR%\CPUFix.ini" >nul
echo   -> %OUTDIR%\CPUFix.asi [OK]

:: --- WindowFix ---
echo.
echo [2/2] Building WindowFix.asi...
cl.exe %CFLAGS% "%~dp0src\window_fix\window_fix.cpp" %LFLAGS% /OUT:"%OUTDIR%\WindowFix.asi" /Fe:"%OUTDIR%\WindowFix.asi"
if errorlevel 1 (
    echo [ERROR] WindowFix build failed!
    exit /b 1
)
copy /Y "%~dp0src\window_fix\window_fix.ini" "%OUTDIR%\WindowFix.ini" >nul
echo   -> %OUTDIR%\WindowFix.asi [OK]

echo.
echo ============================================
echo  Build complete!
echo  Output: %OUTDIR%
echo ============================================
