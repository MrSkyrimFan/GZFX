param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$stagePath = Join-Path $outputPath "stage"
$runtimePath = Join-Path $buildPath "bin\GZFX.dll"
$pluginPath = Join-Path $stagePath "Data\SKSE\Plugins"
$zipPath = Join-Path $outputPath "GZFX-SkyrimSE-x64.zip"

if (-not (Test-Path $runtimePath)) {
    throw "Required package input was not found: $runtimePath"
}

if (Test-Path $outputPath) {
    Remove-Item $outputPath -Recurse -Force
}

New-Item -ItemType Directory -Path $pluginPath -Force | Out-Null
Copy-Item $runtimePath (Join-Path $pluginPath "GZFX.dll")

Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Created one-DLL SKSE package: $zipPath"
