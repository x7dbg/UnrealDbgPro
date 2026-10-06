[CmdletBinding()]
param(
    [string]$ReleaseRoot = ''
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ([string]::IsNullOrWhiteSpace($ReleaseRoot)) {
    $ReleaseRoot = Join-Path $scriptRoot '..\..\x64\WinUI\Release'
}
$resolvedRoot = (Resolve-Path -LiteralPath $ReleaseRoot).Path
$exe = Join-Path $resolvedRoot 'UnrealDbgNativeWinUI.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "找不到 WinUI 自检程序：$exe；请先构建 x64 Release。"
}

Write-Host '=== UnrealDbg 兼容性预检（不会加载驱动） ===' -ForegroundColor Cyan
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Write-Host ("系统：{0}；版本：{1}；架构：{2}" -f $os.Caption,$os.Version,$os.OSArchitecture)
Write-Host ("CPU：{0}" -f $cpu.Name)

foreach ($driver in 'VT_Driver.sys','DbgkSysWin10.sys','DbgkSysWin11.sys') {
    $path = Join-Path (Join-Path $resolvedRoot 'bin') $driver
    if (-not (Test-Path -LiteralPath $path)) { Write-Warning "缺少运行驱动：$path"; continue }
    $sig = Get-AuthenticodeSignature -LiteralPath $path
    Write-Host ("签名：{0} -> {1}" -f $driver,$sig.Status)
}

Get-CimInstance Win32_SystemDriver -Filter "Name='VT_Driver' OR Name='UnrealDevice'" |
    Select-Object Name,State,StartMode,PathName | Format-Table -AutoSize

function Invoke-GuiSelfTest([string]$argument) {
    $process = Start-Process -FilePath $exe -ArgumentList $argument -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "$argument 失败，退出码=$($process.ExitCode)" }
}
Invoke-GuiSelfTest '--self-test'
Invoke-GuiSelfTest '--symbol-cache-check'
Write-Host '预检完成：未调用驱动加载接口。真实加载必须在快照虚拟机按矩阵手工执行。' -ForegroundColor Green
