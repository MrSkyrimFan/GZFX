param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$stagePath = Join-Path $outputPath "stage"
$dllPath = Join-Path $buildPath "bin\GZFX.dll"
$installReadme = Join-Path $repoRoot "packaging\README_INSTALL.txt"
$zipPath = Join-Path $outputPath "GZFX-SkyrimSE-x64.zip"

if (-not (Test-Path $dllPath)) {
    throw "GZFX.dll was not found at expected path: $dllPath"
}

if (Test-Path $outputPath) {
    Remove-Item $outputPath -Recurse -Force
}

New-Item -ItemType Directory -Path $stagePath -Force | Out-Null

Copy-Item $dllPath (Join-Path $stagePath "GZFX.dll")
Copy-Item $installReadme (Join-Path $stagePath "README_INSTALL.txt")

Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Created package: $zipPath"
