[CmdletBinding()]
param(
    [string] $BuildDirectory = 'out/build/msvc-nvidia',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $Target = 'PlutoGEEditor',
    [ValidateRange(1, 128)] [int] $Jobs = 8,
    [ValidateRange(1, 20)] [int] $Samples = 3
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot $BuildDirectory
}
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    throw 'Choose a configured CMake build directory.'
}
$measurements = @()
for ($sample = 1; $sample -le $Samples; $sample++) {
    $logPath = Join-Path $BuildDirectory "build-measurement-$sample.log"
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & cmake --build $BuildDirectory --config $Configuration --target $Target --parallel $Jobs *> $logPath
    $buildExitCode = $LASTEXITCODE
    $timer.Stop()
    if ($buildExitCode -ne 0) { throw "Build failed; inspect $logPath" }
    $measurements += [pscustomobject]@{
        Sample = $sample
        Target = $Target
        Configuration = $Configuration
        Jobs = $Jobs
        Seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
        Log = $logPath
    }
}
$measurements | Export-Csv -LiteralPath (Join-Path $BuildDirectory 'build-measurements.csv') -NoTypeInformation
$measurements | Format-Table -AutoSize
