@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

rem ============================================================
rem  VT驱动 + WinUI 前端：一键构建 + 冒烟验证
rem  构建目标：UnrealDbgProNativeWinUI.vcxproj (Release x64)
rem        构建完成后执行 --ui-smoke-test 验证启动。
rem ============================================================

set "ROOT=%~dp0"
set "PROJ=%ROOT%UnrealDbgProNative\UnrealDbgProNativeWinUI.vcxproj"
set "OUTDIR=%ROOT%x64\WinUI\Release"
set "EXE=%OUTDIR%\UnrealDbgProNativeWinUI.exe"

where msbuild >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
    )
) else (
    for /f "delims=" %%i in ('where msbuild') do set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [错误] 未找到 MSBuild，请确保已安装 Visual Studio 2019/2022 含 C++ 桌面开发工作负载。
    pause
    exit /b 1
)

echo [1/3] 正在构建 Release x64 ...
"%MSBUILD%" "%PROJ%" /p:Configuration=Release /p:Platform=x64 /m /v:m
if errorlevel 1 (
    echo.
    echo [失败] 构建失败，请查看上方错误信息。
    pause
    exit /b 1
)

echo.
echo [2/3] 构建成功，产物：%EXE%

echo [3/3] 正在执行 UI 冒烟验证 ...
pushd "%OUTDIR%"
"%EXE%" --ui-smoke-test
set "RC=%ERRORLEVEL%"
popd

echo.
echo 冒烟测试退出码：%RC%  ^(0 = 通过^)
if not "%RC%"=="0" (
    echo [警告] 冒烟验证未通过，请查看上方日志。
    pause
    exit /b %RC%
)

echo.
echo 全部通过，现在可以直接双击项目目录下的 启动GUI.bat 查看界面。
pause
exit /b 0