[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot 'out/build/msvc-distribution'
$packageDirectory = Join-Path $repositoryRoot 'out/packages'

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found on PATH.'
}
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'The .NET 8 SDK was not found. Install it and ensure dotnet is on PATH.'
}

$dotnetVersion = & dotnet --version
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to query the installed .NET SDK.'
}
$dotnetMajorVersion = 0
if (-not [int]::TryParse(($dotnetVersion -split '\.')[0], [ref] $dotnetMajorVersion) -or $dotnetMajorVersion -lt 8) {
    throw "PlutoGE requires a .NET 8 or newer SDK capable of targeting net8.0. Found: $dotnetVersion"
}

Write-Host 'Configuring the Windows distribution build...'
& cmake -S $repositoryRoot -B $buildDirectory -A x64 `
    -DPLUTO_BUILD_EDITOR=ON `
    -DPLUTO_BUILD_RUNTIME=ON `
    -DPLUTO_BUILD_SAMPLES=OFF `
    -DBUILD_TESTING=$(-not $SkipTests)
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

Write-Host 'Building PlutoGE...'
& cmake --build $buildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Engine build failed.' }

if (-not $SkipTests) {
    Write-Host 'Running tests...'
    & ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}

New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null

Write-Host 'Generating the portable ZIP...'
& cpack --config (Join-Path $buildDirectory 'CPackConfig.cmake') -C $Configuration -G ZIP -B $packageDirectory
if ($LASTEXITCODE -ne 0) { throw 'Portable package generation failed.' }

$makeNsis = Get-Command makensis -ErrorAction SilentlyContinue
if (-not $makeNsis) {
    $standardMakeNsisPath = Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe'
    if (Test-Path -LiteralPath $standardMakeNsisPath -PathType Leaf) {
        $makeNsis = Get-Item -LiteralPath $standardMakeNsisPath
    }
}
if ($makeNsis) {
    Write-Host 'Generating the NSIS installer...'
    & cpack --config (Join-Path $buildDirectory 'CPackConfig.cmake') -C $Configuration -G NSIS64 -B $packageDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Installer generation failed.' }
} else {
    Write-Warning 'makensis was not found. The portable ZIP was created, but the NSIS installer was skipped.'
}

$packageFiles = @(Get-ChildItem -LiteralPath $packageDirectory -File |
    Where-Object { $_.Extension -in @('.exe', '.zip') })
$checksumLines = foreach ($packageFile in $packageFiles) {
    $hash = Get-FileHash -LiteralPath $packageFile.FullName -Algorithm SHA256
    "$($hash.Hash)  $($packageFile.Name)"
}
$checksumLines | Set-Content -LiteralPath (Join-Path $packageDirectory 'SHA256SUMS.txt') -Encoding ascii

Write-Host "Distribution packages are ready in: $packageDirectory"
