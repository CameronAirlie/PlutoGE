[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Project,

    [Parameter(Mandatory = $true, Position = 1)]
    [string] $Output,

    [string] $RuntimePath,

    [switch] $RebuildRuntime
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$projectPath = [System.IO.Path]::GetFullPath($Project, (Get-Location).Path)
$outputPath = [System.IO.Path]::GetFullPath($Output, (Get-Location).Path)

if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "Project manifest was not found: $projectPath"
}

if ([System.IO.Path]::GetExtension($outputPath) -ne '.exe') {
    $outputPath += '.exe'
}

if ($RebuildRuntime -and $RuntimePath) {
    throw 'Use either -RuntimePath for a prebuilt runtime or -RebuildRuntime for the engine shipping runtime.'
}

if (-not $RuntimePath) {
    $RuntimePath = Join-Path $repositoryRoot 'out/build/msvc-shipping/runtime/Release/PlutoGERuntime.exe'
} else {
    $RuntimePath = [System.IO.Path]::GetFullPath($RuntimePath, (Get-Location).Path)
}

if ($RebuildRuntime) {
    Write-Host 'Configuring the shipping runtime...'
    & cmake --preset msvc-shipping -S $repositoryRoot
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    Write-Host 'Building the Release runtime...'
    & cmake --build (Join-Path $repositoryRoot 'out/build/msvc-shipping') --config Release --target PlutoGERuntime
    if ($LASTEXITCODE -ne 0) { throw 'Shipping runtime build failed.' }
}

if (-not (Test-Path -LiteralPath $RuntimePath -PathType Leaf)) {
    throw "Prebuilt runtime was not found: $RuntimePath. Build it once with -RebuildRuntime, or specify -RuntimePath."
}

$projectDirectory = Split-Path -Parent $projectPath
$scriptProjectCandidates = @(Get-ChildItem -LiteralPath $projectDirectory -Filter '*.Scripts.csproj' -File)
if ($scriptProjectCandidates.Count -gt 1) {
    throw "Multiple script projects were found beside the manifest. Build scripts explicitly before export: $projectDirectory"
}
if ($scriptProjectCandidates.Count -eq 1) {
    Write-Host 'Building project scripts...'
    & dotnet build $scriptProjectCandidates[0].FullName -c Release -f net8.0
    if ($LASTEXITCODE -ne 0) { throw 'Project script build failed.' }
}

Write-Host 'Cooking assets and assembling the game...'
& $runtimePath --export $projectPath $outputPath
if ($LASTEXITCODE -ne 0) { throw 'Game export failed.' }

Write-Host "Game export is ready: $outputPath"
