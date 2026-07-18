@echo off
setlocal EnableExtensions

rem =============================================================================
rem build.bat
rem -----------------------------------------------------------------------------
rem 使用 Visual Studio C++ 工具链构建 32 位 DSOpt.asi。
rem 需要安装“使用 C++ 的桌面开发”工作负载和 x86 编译工具。
rem =============================================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="

rem 优先通过 vswhere 查找最新且包含 x86/x64 C++ 工具的 Visual Studio。
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSPATH=%%i"
    )
)

rem vswhere 不存在或未找到时，检查 Visual Studio 2022 的常见安装目录。
if not defined VSPATH (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VSPATH=C:\Program Files\Microsoft Visual Studio\2022\%%E"
            goto :vs_found
        )
    )
)

:vs_found
if not defined VSPATH (
    echo [错误] 未找到可用的 Visual Studio C++ 工具链。
    exit /b 1
)

rem 明确初始化 x86 环境；死亡空间 1 和 ASI Loader 均为 32 位。
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x86
if errorlevel 1 (
    echo [错误] 初始化 Visual Studio x86 构建环境失败。
    exit /b 1
)

set "ROOT=%~dp0"
set "OUTDIR=%ROOT%build"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

echo.
echo ============================================
echo  正在构建 DSOpt.asi（x86，模块化源码）
echo ============================================

rem /utf-8：让源码中的详细中文注释和日志字符串稳定编译。
rem /W4：启用较严格警告；/permissive-：使用更严格的标准一致性模式。
cl.exe /nologo /utf-8 /O2 /MT /LD /std:c++17 /EHsc /permissive- /W4 ^
    /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
    /I"%ROOT%src\common" /I"%ROOT%src\ds_opt" ^
    "%ROOT%src\common\file_utils.cpp" ^
    "%ROOT%src\common\memory_patch.cpp" ^
    "%ROOT%src\common\pe_scanner.cpp" ^
    "%ROOT%src\ds_opt\app_config.cpp" ^
    "%ROOT%src\ds_opt\ds_opt.cpp" ^
    "%ROOT%src\ds_opt\game_files.cpp" ^
    "%ROOT%src\ds_opt\logging.cpp" ^
    "%ROOT%src\ds_opt\process_fixes.cpp" ^
    "%ROOT%src\ds_opt\subtitle_scaling.cpp" ^
    "%ROOT%src\ds_opt\window_manager.cpp" ^
    /Fe:"%OUTDIR%\DSOpt.asi" ^
    /link kernel32.lib user32.lib shell32.lib ^
    /SUBSYSTEM:WINDOWS /MACHINE:X86 /OUT:"%OUTDIR%\DSOpt.asi"
if errorlevel 1 (
    echo [错误] DSOpt 构建失败。
    exit /b 1
)

copy /Y "%ROOT%src\ds_opt\ds_opt.ini" "%OUTDIR%\DSOpt.ini" >nul

echo.
echo [完成] %OUTDIR%\DSOpt.asi
echo [完成] %OUTDIR%\DSOpt.ini
exit /b 0
