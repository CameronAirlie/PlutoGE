[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Project,

    [Parameter(Mandatory = $true, Position = 1)]
    [string] $Output
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

Write-Host 'Configuring the shipping runtime...'
& cmake --preset msvc-shipping -S $repositoryRoot
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

Write-Host 'Building the Release runtime...'
& cmake --build --preset shipping
if ($LASTEXITCODE -ne 0) { throw 'Shipping runtime build failed.' }

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

$runtimePath = Join-Path $repositoryRoot 'out/build/msvc-shipping/runtime/Release/PlutoGERuntime.exe'
if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
    throw "Shipping runtime was not produced at the expected path: $runtimePath"
}

Write-Host 'Cooking assets and assembling the game...'
& $runtimePath --export $projectPath $outputPath
if ($LASTEXITCODE -ne 0) { throw 'Game export failed.' }

Write-Host "Game export is ready: $outputPath"
