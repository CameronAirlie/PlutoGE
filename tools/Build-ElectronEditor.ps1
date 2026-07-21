[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Debug',

    [string] $BuildDirectory = 'out/build/electron-editor',
    [switch] $Reconfigure,
    [switch] $SkipNativeBuild,
    [switch] $SkipInstall,
    [switch] $SkipTypeCheck
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$electronDirectory = Join-Path $repositoryRoot 'editor/electron'
$defaultBuildDirectory = Join-Path $repositoryRoot 'out/build/electron-editor'

function Resolve-RepositoryPath([string] $Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Invoke-Checked([string] $Description, [scriptblock] $Command) {
    Write-Host "==> $Description"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Assert-Command([string] $Name, [string] $InstallHint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name is required. $InstallHint"
    }
}

if (-not $SkipNativeBuild) {
    Assert-Command 'cmake' 'Install CMake and make sure it is available on PATH.'
}
Assert-Command 'node' 'Install Node.js 22.12 or newer.'
Assert-Command 'npm' 'Install Node.js 22.12 or newer.'

$nodeVersionText = (& node --version).Trim().TrimStart('v')
$parsedNodeVersion = $null
if ($LASTEXITCODE -ne 0 -or -not [version]::TryParse($nodeVersionText, [ref] $parsedNodeVersion)) {
    throw 'Could not determine the installed Node.js version.'
}
$minimumNodeVersion = [version] '22.12.0'
if ($parsedNodeVersion -lt $minimumNodeVersion) {
    throw "Node.js $minimumNodeVersion or newer is required by Electron. The active version is $parsedNodeVersion."
}

$nativeBuildDirectory = Resolve-RepositoryPath $BuildDirectory
$usesDefaultBuildDirectory = $nativeBuildDirectory.Equals(
    [System.IO.Path]::GetFullPath($defaultBuildDirectory),
    [System.StringComparison]::OrdinalIgnoreCase
)

if (-not $SkipNativeBuild) {
    if ($usesDefaultBuildDirectory) {
        $cmakeCache = Join-Path $nativeBuildDirectory 'CMakeCache.txt'
        if ($Reconfigure -or -not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
            Invoke-Checked 'Configuring the native editor host' {
                & cmake -S $repositoryRoot --preset electron-editor
            }
        }
        else {
            Write-Host '==> Reusing the existing native configuration (pass -Reconfigure to refresh it)'
        }
        $buildPreset = "electron-editor-$($Configuration.ToLowerInvariant())"
        Invoke-Checked "Building the native editor host ($Configuration)" {
            & cmake --build --preset $buildPreset
        }
    }
    else {
        $configureArguments = @(
            '-S', $repositoryRoot,
            '-B', $nativeBuildDirectory,
            '-DPLUTO_BUILD_EDITOR=ON',
            '-DPLUTO_BUILD_RUNTIME=OFF',
            '-DPLUTO_BUILD_SAMPLES=OFF',
            '-DBUILD_TESTING=OFF',
            '-DPLUTO_BUILD_ENGINE_SHARED=ON',
            '-DPLUTO_BUILD_ELECTRON_EDITOR_HOST=ON'
        )
        Invoke-Checked 'Configuring the native editor host' {
            & cmake @configureArguments
        }
        Invoke-Checked "Building the native editor host ($Configuration)" {
            & cmake --build $nativeBuildDirectory --config $Configuration --target PlutoGEEditorHost
        }
    }
}

$hostExecutable = @(
    (Join-Path $nativeBuildDirectory "bin/$Configuration/PlutoGEEditorHost.exe"),
    (Join-Path $nativeBuildDirectory 'bin/PlutoGEEditorHost.exe')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1

if (-not $hostExecutable) {
    throw "Native editor host was not found under $(Join-Path $nativeBuildDirectory 'bin'). Run without -SkipNativeBuild first."
}

Push-Location $electronDirectory
try {
    $nodeModules = Join-Path $electronDirectory 'node_modules'
    $installedLock = Join-Path $nodeModules '.package-lock.json'
    $sourceLock = Join-Path $electronDirectory 'package-lock.json'
    $forgeCommand = Join-Path $nodeModules '.bin/electron-forge.cmd'
    $electronExecutable = Join-Path $nodeModules 'electron/dist/electron.exe'
    $dependenciesMissing =
        -not (Test-Path -LiteralPath $nodeModules -PathType Container) -or
        -not (Test-Path -LiteralPath $installedLock -PathType Leaf) -or
        -not (Test-Path -LiteralPath $forgeCommand -PathType Leaf)
    $dependenciesStale =
        -not $dependenciesMissing -and
        (Get-Item -LiteralPath $sourceLock).LastWriteTimeUtc -gt
            (Get-Item -LiteralPath $installedLock).LastWriteTimeUtc

    if (-not $SkipInstall -and ($dependenciesMissing -or $dependenciesStale)) {
        Invoke-Checked 'Synchronizing Electron dependencies' {
            & npm ci --no-audit --no-fund
        }
    }

    if (-not (Test-Path -LiteralPath $forgeCommand -PathType Leaf)) {
        throw 'Electron dependencies are incomplete. Run again without -SkipInstall.'
    }

    if (-not (Test-Path -LiteralPath $electronExecutable -PathType Leaf)) {
        if ($SkipInstall) {
            throw 'The Electron runtime is incomplete. Run again without -SkipInstall.'
        }
        Invoke-Checked 'Repairing the Electron runtime' {
            & npm rebuild electron
        }
    }

    if (-not $SkipTypeCheck) {
        Invoke-Checked 'Type-checking the Electron editor' {
            & npm run typecheck
        }
    }
}
finally {
    Pop-Location
}

Write-Host "==> Electron editor build is ready: $hostExecutable" -ForegroundColor Green
