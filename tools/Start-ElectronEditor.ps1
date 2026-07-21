[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Debug',

    [string] $BuildDirectory = 'out/build/electron-editor',
    [switch]$Reconfigure,
    [switch]$SkipNativeBuild,
    [switch]$SkipInstall,
    [switch]$SkipTypeCheck,

    [ValidateRange(1024, 65535)]
    [int] $DevServerPort = 3000
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$electronDirectory = Join-Path $repositoryRoot 'editor/electron'
$nativeBuildDirectory = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    [System.IO.Path]::GetFullPath($BuildDirectory)
}
else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $BuildDirectory))
}

function Test-TcpPortAvailable([int] $Port) {
    $socket = [System.Net.Sockets.Socket]::new(
        [System.Net.Sockets.AddressFamily]::InterNetworkV6,
        [System.Net.Sockets.SocketType]::Stream,
        [System.Net.Sockets.ProtocolType]::Tcp
    )
    try {
        $socket.DualMode = $true
        $socket.Bind([System.Net.IPEndPoint]::new([System.Net.IPAddress]::IPv6Any, $Port))
        return $true
    }
    catch [System.Net.Sockets.SocketException] {
        return $false
    }
    finally {
        $socket.Dispose()
    }
}

function Find-DevServerPort([int] $PreferredPort) {
    foreach ($candidate in $PreferredPort..([Math]::Min($PreferredPort + 99, 65535))) {
        if (Test-TcpPortAvailable $candidate) {
            return $candidate
        }
    }
    throw "No available development port was found between $PreferredPort and $([Math]::Min($PreferredPort + 99, 65535))."
}

$buildArguments = @{
    Configuration = $Configuration
    BuildDirectory = $BuildDirectory
    Reconfigure = $Reconfigure
    SkipNativeBuild = $SkipNativeBuild
    SkipInstall = $SkipInstall
    SkipTypeCheck = $SkipTypeCheck
}
try {
    & (Join-Path $PSScriptRoot 'Build-ElectronEditor.ps1') @buildArguments
}
catch {
    Write-Host "Electron editor build failed: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$hostExecutable = @(
    (Join-Path $nativeBuildDirectory "bin/$Configuration/PlutoGEEditorHost.exe"),
    (Join-Path $nativeBuildDirectory 'bin/PlutoGEEditorHost.exe')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1

if (-not $hostExecutable) {
    throw "Native editor host was not found under $(Join-Path $nativeBuildDirectory 'bin'). Run without -SkipNativeBuild first."
}

Push-Location $electronDirectory
$previousHost = $env:PLUTOGE_ENGINE_HOST
$previousDevServerPort = $env:PLUTOGE_DEV_SERVER_PORT
$editorExitCode = 0
try {
    $selectedPort = Find-DevServerPort $DevServerPort
    if ($selectedPort -ne $DevServerPort) {
        Write-Host "==> Port $DevServerPort is busy; using $selectedPort for the Electron dev server." -ForegroundColor Yellow
    }
    else {
        Write-Host "==> Starting the Electron editor on development port $selectedPort"
    }
    $env:PLUTOGE_ENGINE_HOST = $hostExecutable
    $env:PLUTOGE_DEV_SERVER_PORT = [string] $selectedPort
    & npm start
    $editorExitCode = $LASTEXITCODE
}
finally {
    $env:PLUTOGE_ENGINE_HOST = $previousHost
    $env:PLUTOGE_DEV_SERVER_PORT = $previousDevServerPort
    Pop-Location
}

if ($editorExitCode -ne 0) {
    Write-Host "Electron development process exited with code $editorExitCode. The underlying error is shown above." -ForegroundColor Red
    exit $editorExitCode
}
