param(
    [Parameter(Mandatory = $true)]
    [string]$DllPath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $DllPath)) {
    throw "DLL not found: $DllPath"
}

$vswhere = Join-Path \${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found."
}

$installRoot = (& $vswhere `n    -latest `n    -products * `n    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `n    -property installationPath).Trim()

if ([string]::IsNullOrWhiteSpace($installRoot)) {
    throw "Visual Studio C++ toolchain was not found."
}

$dumpbin = Get-ChildItem `n    -Path (Join-Path $installRoot "VC\Tools\MSVC") `n    -Filter "dumpbin.exe" `n    -Recurse |
    Where-Object {
        $_.FullName -match "Hostx64\\x64\\dumpbin\.exe$"
    } |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if ($null -eq $dumpbin) {
    throw "dumpbin.exe was not found in the Visual Studio toolchain."
}

$dependents = (& $dumpbin.FullName /DEPENDENTS $DllPath 2>&1) -join "\`n"
$exports = (& $dumpbin.FullName /EXPORTS $DllPath 2>&1) -join "\`n"

Write-Host "=== GZFX DLL dependencies ==="
Write-Host $dependents

$forbiddenPatterns = @(
    "(?im)^\s*MSVCP\d+[^\s]*\.dll\s*$",
    "(?im)^\s*VCRUNTIME\d+[^\s]*\.dll\s*$",
    "(?im)^\s*api-ms-win-crt-[^\s]*\.dll\s*$"
)

foreach ($pattern in $forbiddenPatterns) {
    if ($dependents -match $pattern) {
        throw "GZFX.dll still depends on the dynamic Microsoft C/C++ runtime."
    }
}

$requiredExports = @(
    "SKSEPlugin_Version",
    "SKSEPlugin_Preload",
    "SKSEPlugin_Load",
    "GZFX_Initialize",
    "GZFX_Shutdown"
)

foreach ($name in $requiredExports) {
    if ($exports -notmatch "(?m)\b$([regex]::Escape($name))\b") {
        throw "Required export is missing: $name"
    }
}

Write-Host "PE import/export contract passed."
