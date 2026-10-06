[CmdletBinding()]
param(
    [string]$OutputDirectory = '',
    [string]$ReleaseRoot = '',
    [ValidateRange(1, 100)]
    [int]$MaximumSessionLogs = 12,
    [ValidateRange(1MB, 256MB)]
    [long]$MaximumLogBytesPerFile = 64MB
)

# 此脚本只读取系统、服务、签名、事件和本地日志；绝不安装、启动、停止
# 或卸载驱动，也不改变 Secure Boot、HVCI、VBS 或测试签名设置。
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $scriptRoot '..\..\_artifacts\driver-diagnostics'
}
if ([string]::IsNullOrWhiteSpace($ReleaseRoot)) {
    $ReleaseRoot = Join-Path $scriptRoot '..\..\x64\WinUI\Release'
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$out = Join-Path $OutputDirectory $stamp
$logsOut = Join-Path $out 'logs'
New-Item -ItemType Directory -Force -Path $out, $logsOut | Out-Null

$manifest = [System.Collections.Generic.List[string]]::new()
function Add-Manifest([string]$Text) {
    $manifest.Add("$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')  $Text")
}

function Write-Utf8File(
    [string]$Path,
    [Parameter(ValueFromPipeline = $true)]
    [object]$Value
) {
    begin { $items = [System.Collections.Generic.List[object]]::new() }
    process {
        foreach ($item in @($Value)) {
            $items.Add($item)
        }
    }
    end {
        if ($items.Count -eq 0) { $items.Add('') }
        $items | Out-File -LiteralPath $Path -Encoding utf8 -Width 4096
    }
}

function Get-RegistryDwordText([string]$Path, [string]$Name) {
    try {
        return [string](Get-ItemPropertyValue -LiteralPath $Path -Name $Name -ErrorAction Stop)
    }
    catch {
        return '未读取到'
    }
}

function Copy-DiagnosticLog([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        return
    }
    $item = Get-Item -LiteralPath $Source -ErrorAction Stop
    if ($item.Length -le $MaximumLogBytesPerFile) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        Add-Manifest "已复制日志：$Source ($($item.Length) 字节)"
        return
    }

    # 不让单个异常膨胀的日志吞掉整个诊断包；保留末尾 25000 行，那里通常
    # 含有最后一次失败、服务退出码与收尾状态，并明确记录截断事实。
    $truncated = @(
        "[诊断包截断] 原文件=$Source",
        "[诊断包截断] 原始字节=$($item.Length)，超过限制=$MaximumLogBytesPerFile",
        '[诊断包截断] 以下仅保留末尾 25000 行；原始文件未被修改。',
        ''
    )
    $truncated += Get-Content -LiteralPath $Source -Tail 25000 -Encoding utf8 -ErrorAction Stop
    Write-Utf8File $Destination $truncated
    Add-Manifest "已截断复制日志：$Source ($($item.Length) 字节)"
}

Add-Manifest '驱动诊断采集开始（只读；未执行驱动加载或系统配置变更）'
Add-Manifest "发布目录：$ReleaseRoot"

try { Get-CimInstance Win32_OperatingSystem | Format-List * | Write-Utf8File (Join-Path $out 'os.txt') }
catch { Write-Utf8File (Join-Path $out 'os.txt') "读取 Win32_OperatingSystem 失败：$($_.Exception.Message)" }
try { Get-CimInstance Win32_ComputerSystem | Format-List * | Write-Utf8File (Join-Path $out 'computer.txt') }
catch { Write-Utf8File (Join-Path $out 'computer.txt') "读取 Win32_ComputerSystem 失败：$($_.Exception.Message)" }
try { Get-CimInstance Win32_Processor | Format-List * | Write-Utf8File (Join-Path $out 'cpu.txt') }
catch { Write-Utf8File (Join-Path $out 'cpu.txt') "读取 Win32_Processor 失败：$($_.Exception.Message)" }

$securityState = @(
    "采集时间：$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')",
    "Windows Build：$(Get-RegistryDwordText 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' 'CurrentBuildNumber')",
    "Windows UBR：$(Get-RegistryDwordText 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' 'UBR')",
    "VBS：$(Get-RegistryDwordText 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard' 'EnableVirtualizationBasedSecurity')",
    "HVCI：$(Get-RegistryDwordText 'HKLM:\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity' 'Enabled')",
    "安全启动：$(Get-RegistryDwordText 'HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State' 'UEFISecureBootEnabled')"
)
try {
    $deviceGuard = Get-CimInstance -Namespace 'root\Microsoft\Windows\DeviceGuard' -ClassName Win32_DeviceGuard -ErrorAction Stop
    $securityState += 'DeviceGuard：'
    $securityState += ($deviceGuard | Format-List * | Out-String -Width 4096)
}
catch {
    $securityState += "DeviceGuard WMI 未读取到：$($_.Exception.Message)"
}
Write-Utf8File (Join-Path $out 'security-and-virtualization.txt') $securityState

$services = @('VT_Driver', 'UnrealDevice')
foreach ($service in $services) {
    $serviceOut = Join-Path $out ("service-$service.txt")
    $serviceText = [System.Collections.Generic.List[string]]::new()
    $serviceText.Add("服务：$service")
    try {
        $serviceText.Add((Get-CimInstance Win32_SystemDriver -Filter "Name='$service'" -ErrorAction Stop |
            Format-List * | Out-String -Width 4096))
    }
    catch {
        $serviceText.Add("Win32_SystemDriver 未读取到：$($_.Exception.Message)")
    }
    # 不调用 sc.exe：其 ANSI 控制台输出会随系统代码页变化，可能把中文
    # 部署路径重新写成乱码。WMI 已提供诊断所需的 PathName、StartMode、
    # State、ExitCode 和 ServiceSpecificExitCode；Get-Service 补充当前状态。
    try {
        $serviceText.Add((Get-Service -Name $service -ErrorAction Stop |
            Select-Object Name, DisplayName, Status, ServiceType | Format-List * | Out-String -Width 4096))
    }
    catch {
        $serviceText.Add("Get-Service 未读取到：$($_.Exception.Message)")
    }
    Write-Utf8File $serviceOut $serviceText
}

try {
    Get-WinEvent -LogName 'Microsoft-Windows-CodeIntegrity/Operational' -MaxEvents 300 -ErrorAction Stop |
        Format-List * | Write-Utf8File (Join-Path $out 'code-integrity-events.txt')
}
catch {
    Write-Utf8File (Join-Path $out 'code-integrity-events.txt') "未读取到 CodeIntegrity/Operational：$($_.Exception.Message)"
}
try {
    Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = (Get-Date).AddDays(-7) } -ErrorAction Stop |
        Where-Object { $_.ProviderName -in @('Service Control Manager', 'Microsoft-Windows-CodeIntegrity') } |
        Select-Object -First 500 | Format-List * |
        Write-Utf8File (Join-Path $out 'system-service-and-code-integrity-events.txt')
}
catch {
    Write-Utf8File (Join-Path $out 'system-service-and-code-integrity-events.txt') "未读取到系统服务/代码完整性事件：$($_.Exception.Message)"
}

if (Test-Path -LiteralPath $ReleaseRoot -PathType Container) {
    $runtimeFiles = Get-ChildItem -LiteralPath (Join-Path $ReleaseRoot 'bin') -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.sys', '.dll' } |
        Sort-Object Name
    if ($runtimeFiles) {
        $hashes = foreach ($runtimeFile in $runtimeFiles) {
            Get-FileHash -LiteralPath $runtimeFile.FullName -Algorithm SHA256
        }
        $hashes | Format-Table -AutoSize |
            Write-Utf8File (Join-Path $out 'runtime-file-sha256.txt')
        $signatures = foreach ($runtimeFile in $runtimeFiles) {
            Get-AuthenticodeSignature -LiteralPath $runtimeFile.FullName
        }
        $signatures |
            Select-Object Path, Status, StatusMessage, @{ Name = 'Signer'; Expression = { if ($_.SignerCertificate) { $_.SignerCertificate.Subject } } } |
            Format-List * | Write-Utf8File (Join-Path $out 'runtime-file-signatures.txt')
        Add-Manifest "已记录运行时 DLL/SYS：$($runtimeFiles.Count) 个"
    }
    else {
        Add-Manifest "发布目录中未发现 bin 下的 DLL/SYS：$ReleaseRoot"
    }

    $logRoot = Join-Path $ReleaseRoot 'Log'
    foreach ($name in @('log.ini', 'log.previous.ini', 'UnrealDbgDll.log', 'UnrealDbgDll.previous.log')) {
        Copy-DiagnosticLog (Join-Path $logRoot $name) (Join-Path $logsOut $name)
    }
    $sessionRoot = Join-Path $logRoot 'Sessions'
    if (Test-Path -LiteralPath $sessionRoot -PathType Container) {
        Get-ChildItem -LiteralPath $sessionRoot -Filter '*.log' -File -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First $MaximumSessionLogs |
            ForEach-Object { Copy-DiagnosticLog $_.FullName (Join-Path $logsOut $_.Name) }
    }
}
else {
    Add-Manifest "发布目录不存在，未采集运行时文件和应用日志：$ReleaseRoot"
}

Add-Manifest '驱动诊断采集完成'
Write-Utf8File (Join-Path $out 'manifest.txt') $manifest
Write-Host "诊断已收集到：$out"
