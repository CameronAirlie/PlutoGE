[CmdletBinding()]
param(
    [string] $PackageDirectory
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$electronOutput = Join-Path $repositoryRoot 'editor/electron/out'

if ($PackageDirectory) {
    $resolvedPackage = (Resolve-Path $PackageDirectory).Path
}
else {
    $resolvedPackage = Get-ChildItem -LiteralPath $electronOutput -Directory -Recurse -ErrorAction SilentlyContinue |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.FullName 'resources/app.asar') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'resources/engine') -PathType Container)
        } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -ExpandProperty FullName -First 1
}

if (-not $resolvedPackage) {
    throw "No packaged Electron application was found under $electronOutput."
}

$engineDirectory = Join-Path $resolvedPackage 'resources/engine'
$requiredFiles = @(
    'resources/app.asar',
    'resources/engine/PlutoGEEditorHost.exe',
    'resources/engine/PlutoGERuntime.exe',
    'resources/engine/PlutoGE.dll',
    'resources/engine/OpenAL32.dll',
    'resources/engine/ScriptCore/PlutoGE.ScriptCore.dll',
    'resources/engine/ScriptCore/PlutoGE.ScriptCore.runtimeconfig.json',
    'resources/engine/DotnetRuntime/dotnet.exe'
)

$applicationExecutable = Join-Path $resolvedPackage 'PlutoGEEditor.exe'
if (-not (Test-Path -LiteralPath $applicationExecutable -PathType Leaf)) {
    throw "Packaged editor executable is missing: $applicationExecutable"
}

foreach ($relativePath in $requiredFiles) {
    $filePath = Join-Path $resolvedPackage $relativePath
    $file = Get-Item -LiteralPath $filePath -ErrorAction SilentlyContinue
    if (-not $file -or $file.Length -eq 0) {
        throw "Required packaged file is missing or empty: $filePath"
    }
}

$requiredPatterns = @(
    'DotnetRuntime/host/fxr/*/hostfxr.dll',
    'DotnetRuntime/shared/Microsoft.NETCore.App/*/coreclr.dll',
    'DotnetRuntime/sdk/*/MSBuild.dll',
    'DotnetRuntime/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0/System.Runtime.dll',
    'DotnetRuntime/packs/Microsoft.AspNetCore.App.Ref/*/ref/net8.0/Microsoft.AspNetCore.dll',
    'DotnetRuntime/packs/Microsoft.WindowsDesktop.App.Ref/*/ref/net8.0/WindowsBase.dll'
)
foreach ($pattern in $requiredPatterns) {
    if (-not (Get-ChildItem -Path (Join-Path $engineDirectory $pattern) -File -ErrorAction SilentlyContinue | Select-Object -First 1)) {
        throw "The packaged .NET SDK is incomplete; no file matched: $pattern"
    }
}

Write-Host "Verified packaged PlutoGE Editor: $resolvedPackage"
