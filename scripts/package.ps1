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
$bootstrapPath = Join-Path $buildPath "bin\dinput8.dll"
$installReadme = Join-Path $repoRoot "packaging\README_INSTALL.txt"
$zipPath = Join-Path $outputPath "GZFX-SkyrimSE-x64.zip"

foreach ($required in @($runtimePath, $bootstrapPath, $installReadme)) {
    if (-not (Test-Path $required)) {
        throw "Required package input was not found: $required"
    }
}

if (Test-Path $outputPath) {
    Remove-Item $outputPath -Recurse -Force
}

New-Item -ItemType Directory -Path $stagePath -Force | Out-Null

Copy-Item $runtimePath (Join-Path $stagePath "GZFX.dll")
Copy-Item $bootstrapPath (Join-Path $stagePath "dinput8.dll")
Copy-Item $installReadme (Join-Path $stagePath "README_INSTALL.txt")

Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Created package: $zipPath"
