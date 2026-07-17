[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'out/build/electron-editor',
    [switch]$SkipNativeBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$electronDirectory = Join-Path $repositoryRoot 'editor/electron'
$nativeBuildDirectory = Join-Path $repositoryRoot $BuildDirectory

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake is required to build the PlutoGE editor host.'
}
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
    throw 'Node.js and npm are required to run the Electron editor.'
}

if (-not $SkipNativeBuild) {
    cmake -S $repositoryRoot -B $nativeBuildDirectory `
        -DPLUTO_BUILD_EDITOR=ON `
        -DPLUTO_BUILD_RUNTIME=OFF `
        -DPLUTO_BUILD_SAMPLES=OFF `
        -DBUILD_TESTING=OFF `
        -DCMAKE_BUILD_TYPE=$Configuration `
        -DPLUTO_BUILD_ENGINE_SHARED=ON `
        -DPLUTO_BUILD_ELECTRON_EDITOR_HOST=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }

    cmake --build $nativeBuildDirectory --config $Configuration --target PlutoGEEditorHost
    if ($LASTEXITCODE -ne 0) { throw 'Native editor host build failed.' }
}

$hostExecutable = @(
    (Join-Path $nativeBuildDirectory "bin/$Configuration/PlutoGEEditorHost.exe"),
    (Join-Path $nativeBuildDirectory 'bin/PlutoGEEditorHost.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $hostExecutable) {
    throw "Native editor host was not found under $(Join-Path $nativeBuildDirectory 'bin')."
}

Push-Location $electronDirectory
$previousHost = $env:PLUTOGE_ENGINE_HOST
try {
    if (-not $SkipInstall -and -not (Test-Path 'node_modules')) {
        npm install
        if ($LASTEXITCODE -ne 0) {
            throw 'npm install failed. Check your organization certificate/proxy configuration; TLS verification should remain enabled.'
        }
    }

    $env:PLUTOGE_ENGINE_HOST = $hostExecutable
    npm start
    if ($LASTEXITCODE -ne 0) { throw 'Electron editor exited with an error.' }
}
finally {
    $env:PLUTOGE_ENGINE_HOST = $previousHost
    Pop-Location
}
