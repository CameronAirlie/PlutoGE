[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$BuildOnly,
    [switch]$Reconfigure,
    [switch]$ViewportValidation,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$EditorArguments
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repoRoot "out/build/editor"
$cachePath = Join-Path $buildDirectory "CMakeCache.txt"
$editorOutputDirectory = Join-Path $repoRoot "editor/avalonia/bin/$Configuration/net10.0"
$editorExecutable = Join-Path $editorOutputDirectory "PlutoGE.Editor.Avalonia.exe"
$editorAssembly = Join-Path $editorOutputDirectory "PlutoGE.Editor.Avalonia.dll"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Command,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Command $($Arguments -join ' ')"
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found on PATH. Install CMake and open a new terminal."
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw ".NET SDK was not found on PATH. Install the .NET 10 SDK used by the Avalonia editor."
}

$runningEditor = Get-Process -Name "PlutoGE.Editor.Avalonia" -ErrorAction SilentlyContinue
if ($runningEditor) {
    throw "PlutoGE Editor is already running. Close it before rebuilding so the native DLL can be replaced."
}

Push-Location $repoRoot
try {
    if ($Reconfigure -or -not (Test-Path $cachePath)) {
        Write-Host "[PlutoGE] Configuring editor build..." -ForegroundColor Cyan
        $configureArguments = @(
            "-S", $repoRoot,
            "-B", $buildDirectory,
            "-DPLUTO_BUILD_EDITOR=ON",
            "-DPLUTO_BUILD_RUNTIME=OFF",
            "-DPLUTO_BUILD_SAMPLES=OFF",
            "-DBUILD_TESTING=OFF"
        )
        if ($env:OS -eq "Windows_NT") {
            $configureArguments += @("-A", "x64")
        }
        Invoke-Checked -Command "cmake" -Arguments $configureArguments
    }

    Write-Host "[PlutoGE] Building native engine and Avalonia editor ($Configuration)..." -ForegroundColor Cyan
    Invoke-Checked -Command "cmake" -Arguments @(
        "--build", $buildDirectory,
        "--config", $Configuration,
        "--target", "PlutoGEAvaloniaEditor",
        "--parallel"
    )

    if ($BuildOnly) {
        Write-Host "[PlutoGE] Build complete." -ForegroundColor Green
        exit 0
    }

    $launchArguments = @()
    if ($ViewportValidation) {
        $launchArguments += "--viewport-validation"
    }
    if ($EditorArguments) {
        $launchArguments += $EditorArguments
    }

    Write-Host "[PlutoGE] Launching editor..." -ForegroundColor Green
    if (Test-Path $editorExecutable) {
        & $editorExecutable @launchArguments
    }
    elseif (Test-Path $editorAssembly) {
        Invoke-Checked -Command "dotnet" -Arguments (@($editorAssembly) + $launchArguments)
    }
    else {
        throw "Build succeeded but the editor output was not found in $editorOutputDirectory."
    }

    if ($LASTEXITCODE -ne 0) {
        throw "PlutoGE Editor exited with code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
