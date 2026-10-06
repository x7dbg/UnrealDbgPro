$ErrorActionPreference = 'Stop'

# WinUI 3 的 C++ 投影和 Windows App SDK 包不放进 Git；脚本把它们恢复到
# _artifacts，项目文件会从那里生成投影头和链接。重复执行是安全的。
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $repoRoot '_artifacts\winui_packages'
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

$packages = @(
    @{ Id = 'Microsoft.WindowsAppSDK'; Version = '1.8.250907003'; Folder = 'sdk' },
    @{ Id = 'Microsoft.WindowsAppSDK.Foundation'; Version = '1.8.250906002'; Folder = 'foundation' },
    @{ Id = 'Microsoft.WindowsAppSDK.InteractiveExperiences'; Version = '1.8.250906004'; Folder = 'interactive' },
    @{ Id = 'Microsoft.WindowsAppSDK.WinUI'; Version = '1.8.250906003'; Folder = 'winui' },
    @{ Id = 'Microsoft.WindowsAppSDK.Runtime'; Version = '1.8.250907003'; Folder = 'runtime' },
    @{ Id = 'Microsoft.Windows.CppWinRT'; Version = '2.0.240405.15'; Folder = 'cppwinrt' },
    @{ Id = 'Microsoft.Web.WebView2'; Version = '1.0.2792.45'; Folder = 'webview2' }
)

foreach ($package in $packages) {
    $id = $package.Id.ToLowerInvariant()
    $fileName = "$id.$($package.Version).nupkg"
    $archive = Join-Path $packageRoot $fileName
    if (-not (Test-Path -LiteralPath $archive)) {
        $url = "https://api.nuget.org/v3-flatcontainer/$id/$($package.Version.ToLowerInvariant())/$fileName"
        Write-Host "下载 $($package.Id) $($package.Version)"
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive
    }
    $destination = Join-Path $packageRoot $package.Folder
    if (-not (Test-Path -LiteralPath $destination)) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
    }
}

# Windows App SDK 1.8 是拆分包；将编译所需组件叠加到一个 sdk 目录，
# 与项目文件中的 WindowsAppSDKPackageDir 保持一致。
$sdk = Join-Path $packageRoot 'sdk'
foreach ($folder in 'foundation', 'interactive', 'winui', 'runtime') {
    Copy-Item -Path (Join-Path $packageRoot "$folder\*") -Destination $sdk -Recurse -Force
}
$webViewMetadata = Join-Path $packageRoot 'webview2\lib\Microsoft.Web.WebView2.Core.winmd'
if (Test-Path -LiteralPath $webViewMetadata) {
    New-Item -ItemType Directory -Force -Path (Join-Path $sdk 'metadata') | Out-Null
    Copy-Item -LiteralPath $webViewMetadata -Destination (Join-Path $sdk 'metadata') -Force
}

Write-Host "WinUI 3 依赖已恢复到 $packageRoot"
