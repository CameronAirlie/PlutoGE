[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [ValidateSet('make', 'package')]
    [string] $ForgeTarget = 'make',

    [string] $BuildDirectory = 'out/build/electron-distribution',
    [string] $StagingDirectory = 'out/package/electron',
    [switch] $SkipNativeBuild,
    [switch] $SkipNpmInstall
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$electronDirectory = Join-Path $repositoryRoot 'editor/electron'

function Resolve-RepositoryPath([string] $Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Assert-RepositoryChild([string] $Path, [string] $Description) {
    $separator = [System.IO.Path]::DirectorySeparatorChar
    $repositoryPrefix = $repositoryRoot.TrimEnd($separator, [System.IO.Path]::AltDirectorySeparatorChar) + $separator
    if (-not $Path.StartsWith($repositoryPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must be inside the repository: $Path"
    }
}

function Invoke-Checked([string] $Description, [scriptblock] $Command) {
    Write-Host $Description
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Copy-RequiredFile([string] $Source, [string] $DestinationDirectory) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required distribution file was not produced: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $DestinationDirectory -Force
}

function Select-VersionDirectory([string] $Root, [int] $MajorVersion) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Required .NET directory was not found: $Root"
    }
    $selection = Get-ChildItem -LiteralPath $Root -Directory |
        Where-Object { $_.Name -match "^$MajorVersion\.\d+\.\d+" } |
        Sort-Object { [version]($_.Name -replace '-.*$', '') } -Descending |
        Select-Object -First 1
    if (-not $selection) {
        throw "No .NET $MajorVersion version was found under $Root."
    }
    return $selection
}

function Get-Net8TargetingPackVersion([string] $SdkDirectory) {
    $bundledVersionsPath = Join-Path $SdkDirectory 'Microsoft.NETCoreSdk.BundledVersions.props'
    if (-not (Test-Path -LiteralPath $bundledVersionsPath -PathType Leaf)) {
        return $null
    }
    [xml] $bundledVersions = Get-Content -LiteralPath $bundledVersionsPath -Raw
    $framework = $bundledVersions.Project.ItemGroup.KnownFrameworkReference |
        Where-Object {
            $_.TargetFramework -eq 'net8.0' -and
            $_.TargetingPackName -eq 'Microsoft.NETCore.App.Ref'
        } |
        Select-Object -First 1
    return $framework.TargetingPackVersion
}

foreach ($command in @('cmake', 'dotnet', 'node', 'npm')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$command is required to package the PlutoGE editor."
    }
}

$nodeVersionText = (& node --version).Trim().TrimStart('v')
if ($LASTEXITCODE -ne 0 -or -not [version]::TryParse($nodeVersionText, [ref] $null)) {
    throw 'Could not determine the installed Node.js version.'
}
$nodeVersion = [version] $nodeVersionText
$minimumNodeVersion = [version] '22.12.0'
if ($nodeVersion -lt $minimumNodeVersion) {
    throw "Node.js $minimumNodeVersion or newer is required by Electron 43. The active version is $nodeVersion. Update Node.js or correct PATH before packaging."
}

$nativeBuildDirectory = Resolve-RepositoryPath $BuildDirectory
$stagingRoot = Resolve-RepositoryPath $StagingDirectory
Assert-RepositoryChild $nativeBuildDirectory 'The native build directory'
Assert-RepositoryChild $stagingRoot 'The staging directory'
$engineBundle = Join-Path $stagingRoot 'engine'

if (-not $SkipNativeBuild) {
    $configureArguments = @(
        '-S', $repositoryRoot,
        '-B', $nativeBuildDirectory,
        '-DPLUTO_BUILD_EDITOR=ON',
        '-DPLUTO_BUILD_RUNTIME=ON',
        '-DPLUTO_BUILD_SAMPLES=OFF',
        '-DBUILD_TESTING=OFF',
        '-DPLUTO_BUILD_ENGINE_SHARED=ON',
        '-DPLUTO_BUILD_ELECTRON_EDITOR_HOST=ON'
    )
    Invoke-Checked 'Configuring the Electron distribution native build...' {
        & cmake @configureArguments
    }
    Invoke-Checked 'Building the native editor host and shipping runtime...' {
        & cmake --build $nativeBuildDirectory --config $Configuration --target PlutoGEEditorHost PlutoGERuntime
    }
}

$nativeOutputDirectory = Join-Path $nativeBuildDirectory "bin/$Configuration"
if (-not (Test-Path -LiteralPath $nativeOutputDirectory -PathType Container)) {
    $nativeOutputDirectory = Join-Path $nativeBuildDirectory 'bin'
}
if (-not (Test-Path -LiteralPath $nativeOutputDirectory -PathType Container)) {
    throw "Native output directory was not found under $nativeBuildDirectory."
}

if (Test-Path -LiteralPath $engineBundle) {
    Remove-Item -LiteralPath $engineBundle -Recurse -Force
}
New-Item -ItemType Directory -Path $engineBundle -Force | Out-Null

Copy-RequiredFile (Join-Path $nativeOutputDirectory 'PlutoGEEditorHost.exe') $engineBundle
Copy-RequiredFile (Join-Path $nativeOutputDirectory 'PlutoGERuntime.exe') $engineBundle
Copy-RequiredFile (Join-Path $nativeOutputDirectory 'PlutoGE.dll') $engineBundle

$openAlLibrary = Get-ChildItem -LiteralPath $nativeOutputDirectory -Filter 'OpenAL32.dll' -File |
    Select-Object -First 1
if (-not $openAlLibrary) {
    throw "OpenAL32.dll was not found in $nativeOutputDirectory."
}
Copy-RequiredFile $openAlLibrary.FullName $engineBundle

$editorResources = Join-Path $repositoryRoot 'editor/resources'
if (Test-Path -LiteralPath $editorResources -PathType Container) {
    Copy-Item -LiteralPath $editorResources -Destination (Join-Path $engineBundle 'resources') -Recurse -Force
}

$sdkEntries = & dotnet --list-sdks
if ($LASTEXITCODE -ne 0) {
    throw 'Could not enumerate installed .NET SDKs.'
}
$sdkCandidates = $sdkEntries |
    ForEach-Object {
        if ($_ -match '^(\d+\.\d+\.\d+(?:-[^ ]+)?) \[(.+)\]$') {
            $version = [version]($Matches[1] -replace '-.*$', '')
            if ($version.Major -ge 8) {
                $sdkPath = Join-Path $Matches[2] $Matches[1]
                $targetingPackVersion = Get-Net8TargetingPackVersion $sdkPath
                [pscustomobject]@{
                    Version = $Matches[1]
                    ParsedVersion = $version
                    Directory = $Matches[2]
                    TargetingPackVersion = $targetingPackVersion
                }
            }
        }
    } |
    Sort-Object ParsedVersion
$selectedSdk = $sdkCandidates |
    Where-Object {
        $candidateDotnetRoot = Split-Path -Parent $_.Directory
        $packVersion = $_.TargetingPackVersion
        $packVersion -and
        (Test-Path -LiteralPath (Join-Path $candidateDotnetRoot "packs/Microsoft.NETCore.App.Ref/$packVersion")) -and
        (Test-Path -LiteralPath (Join-Path $candidateDotnetRoot "packs/Microsoft.AspNetCore.App.Ref/$packVersion")) -and
        (Test-Path -LiteralPath (Join-Path $candidateDotnetRoot "packs/Microsoft.WindowsDesktop.App.Ref/$packVersion"))
    } |
    Select-Object -First 1
if (-not $selectedSdk) {
    throw 'A .NET 8 or newer SDK is required to package editor scripting support.'
}

$dotnetRoot = Split-Path -Parent $selectedSdk.Directory
$dotnetBundle = Join-Path $engineBundle 'DotnetRuntime'
New-Item -ItemType Directory -Path $dotnetBundle -Force | Out-Null
Copy-RequiredFile (Join-Path $dotnetRoot 'dotnet.exe') $dotnetBundle
foreach ($noticeFile in @('LICENSE.txt', 'ThirdPartyNotices.txt')) {
    $noticePath = Join-Path $dotnetRoot $noticeFile
    if (Test-Path -LiteralPath $noticePath -PathType Leaf) {
        Copy-Item -LiteralPath $noticePath -Destination $dotnetBundle -Force
    }
}

$hostFxr = Select-VersionDirectory (Join-Path $dotnetRoot 'host/fxr') $selectedSdk.ParsedVersion.Major
$net8Runtime = Select-VersionDirectory (Join-Path $dotnetRoot 'shared/Microsoft.NETCore.App') 8
$sdkRuntime = Select-VersionDirectory (Join-Path $dotnetRoot 'shared/Microsoft.NETCore.App') $selectedSdk.ParsedVersion.Major
$referencePack = Get-Item -LiteralPath (Join-Path $dotnetRoot "packs/Microsoft.NETCore.App.Ref/$($selectedSdk.TargetingPackVersion)")
$aspnetReferencePack = Get-Item -LiteralPath (Join-Path $dotnetRoot "packs/Microsoft.AspNetCore.App.Ref/$($selectedSdk.TargetingPackVersion)")
$windowsDesktopReferencePack = Get-Item -LiteralPath (Join-Path $dotnetRoot "packs/Microsoft.WindowsDesktop.App.Ref/$($selectedSdk.TargetingPackVersion)")

$dotnetCopies = @(
    @{ Source = $hostFxr.FullName; Destination = Join-Path $dotnetBundle 'host/fxr' },
    @{ Source = $net8Runtime.FullName; Destination = Join-Path $dotnetBundle 'shared/Microsoft.NETCore.App' },
    @{ Source = $sdkRuntime.FullName; Destination = Join-Path $dotnetBundle 'shared/Microsoft.NETCore.App' },
    @{ Source = (Join-Path $selectedSdk.Directory $selectedSdk.Version); Destination = Join-Path $dotnetBundle 'sdk' },
    @{ Source = $referencePack.FullName; Destination = Join-Path $dotnetBundle 'packs/Microsoft.NETCore.App.Ref' },
    @{ Source = $aspnetReferencePack.FullName; Destination = Join-Path $dotnetBundle 'packs/Microsoft.AspNetCore.App.Ref' },
    @{ Source = $windowsDesktopReferencePack.FullName; Destination = Join-Path $dotnetBundle 'packs/Microsoft.WindowsDesktop.App.Ref' }
)
foreach ($copy in $dotnetCopies) {
    New-Item -ItemType Directory -Path $copy.Destination -Force | Out-Null
    Copy-Item -LiteralPath $copy.Source -Destination $copy.Destination -Recurse -Force
}

$scriptCoreProject = Join-Path $repositoryRoot 'engine/scripting/managed/PlutoGE.ScriptCore/PlutoGE.ScriptCore.csproj'
$previousDotnetRoot = $env:DOTNET_ROOT
$previousMultilevelLookup = $env:DOTNET_MULTILEVEL_LOOKUP
$previousFirstTimeExperience = $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE
$previousTelemetry = $env:DOTNET_CLI_TELEMETRY_OPTOUT
$previousCliHome = $env:DOTNET_CLI_HOME
$previousNugetPackages = $env:NUGET_PACKAGES
try {
    $env:DOTNET_ROOT = $dotnetBundle
    $env:DOTNET_MULTILEVEL_LOOKUP = '0'
    $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $env:DOTNET_CLI_HOME = Join-Path $stagingRoot 'dotnet-cli-home'
    $env:NUGET_PACKAGES = Join-Path $stagingRoot 'nuget-packages'
    New-Item -ItemType Directory -Path $env:DOTNET_CLI_HOME -Force | Out-Null
    New-Item -ItemType Directory -Path $env:NUGET_PACKAGES -Force | Out-Null
    Invoke-Checked 'Building ScriptCore with the bundled .NET SDK...' {
        & (Join-Path $dotnetBundle 'dotnet.exe') build $scriptCoreProject -c Release -f net8.0
    }
}
finally {
    $env:DOTNET_ROOT = $previousDotnetRoot
    $env:DOTNET_MULTILEVEL_LOOKUP = $previousMultilevelLookup
    $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = $previousFirstTimeExperience
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = $previousTelemetry
    $env:DOTNET_CLI_HOME = $previousCliHome
    $env:NUGET_PACKAGES = $previousNugetPackages
}

$scriptCoreOutput = Join-Path (Split-Path -Parent $scriptCoreProject) 'bin/Release/net8.0'
$scriptCoreBundle = Join-Path $engineBundle 'ScriptCore'
New-Item -ItemType Directory -Path $scriptCoreBundle -Force | Out-Null
foreach ($fileName in @('PlutoGE.ScriptCore.dll', 'PlutoGE.ScriptCore.deps.json', 'PlutoGE.ScriptCore.runtimeconfig.json')) {
    Copy-RequiredFile (Join-Path $scriptCoreOutput $fileName) $scriptCoreBundle
}
$scriptCorePdb = Join-Path $scriptCoreOutput 'PlutoGE.ScriptCore.pdb'
if (Test-Path -LiteralPath $scriptCorePdb -PathType Leaf) {
    Copy-Item -LiteralPath $scriptCorePdb -Destination $scriptCoreBundle -Force
}

Push-Location $electronDirectory
$previousEngineBundle = $env:PLUTOGE_ENGINE_BUNDLE_DIR
$previousElectronZipDirectory = $env:PLUTOGE_ELECTRON_ZIP_DIR
$previousDistributionBuild = $env:PLUTOGE_DISTRIBUTION_BUILD
try {
    $electronModuleDirectory = Join-Path $electronDirectory 'node_modules/electron'
    $electronDistDirectory = Join-Path $electronModuleDirectory 'dist'
    $electronPackagePath = Join-Path $electronModuleDirectory 'package.json'
    $npmCache = Join-Path $stagingRoot 'npm-cache'
    New-Item -ItemType Directory -Path $npmCache -Force | Out-Null

    if (-not $SkipNpmInstall -or -not (Test-Path -LiteralPath $electronPackagePath -PathType Leaf)) {
        if ($SkipNpmInstall) {
            Write-Host 'Electron dependencies are missing; installing them with npm ci...'
        }
        Invoke-Checked 'Installing Electron dependencies...' {
            & npm ci --include=dev --ignore-scripts=false --cache $npmCache
        }
    }

    if (-not (Test-Path -LiteralPath $electronPackagePath -PathType Leaf)) {
        throw 'The Electron npm package is missing after npm ci.'
    }
    $electronVersion = (Get-Content -LiteralPath $electronPackagePath -Raw | ConvertFrom-Json).version
    $electronZipDirectory = Join-Path $stagingRoot 'electron-runtime'
    $electronZipPath = Join-Path $electronZipDirectory "electron-v$electronVersion-win32-x64.zip"
    New-Item -ItemType Directory -Path $electronZipDirectory -Force | Out-Null
    $electronDistAvailable = Test-Path -LiteralPath $electronDistDirectory -PathType Container
    $electronZipAvailable = Test-Path -LiteralPath $electronZipPath -PathType Leaf
    if (-not $electronDistAvailable -and -not $electronZipAvailable) {
        $electronArchiveName = Split-Path -Leaf $electronZipPath
        $electronCacheRoots = @(
            $env:ELECTRON_CACHE,
            (Join-Path $env:LOCALAPPDATA 'electron/Cache')
        ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) }
        $cachedElectronArchive = $electronCacheRoots |
            ForEach-Object {
                Get-ChildItem -LiteralPath $_ -Recurse -Filter $electronArchiveName -File -ErrorAction SilentlyContinue
            } |
            Select-Object -First 1
        if ($cachedElectronArchive) {
            Write-Host "Reusing cached Electron runtime: $($cachedElectronArchive.FullName)"
            Copy-Item -LiteralPath $cachedElectronArchive.FullName -Destination $electronZipPath -Force
            $electronZipAvailable = $true
        }
    }
    if (-not $electronDistAvailable -and -not $electronZipAvailable) {
        Write-Host 'The Electron binary payload is missing; repairing only the Electron package...'
        Invoke-Checked 'Installing the Electron runtime...' {
            & npm rebuild electron --ignore-scripts=false --cache $npmCache
        }
        $electronDistAvailable = Test-Path -LiteralPath $electronDistDirectory -PathType Container
    }
    if (-not $electronDistAvailable -and -not $electronZipAvailable) {
        throw 'The Electron runtime is unavailable after repair. Check ELECTRON_SKIP_BINARY_DOWNLOAD and your npm proxy/certificate configuration.'
    }
    if (-not (Test-Path -LiteralPath $electronZipPath -PathType Leaf)) {
        Write-Host "Creating the local Electron $electronVersion runtime archive..."
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::CreateFromDirectory(
            $electronDistDirectory,
            $electronZipPath,
            [System.IO.Compression.CompressionLevel]::Optimal,
            $false
        )
    }

    $env:PLUTOGE_ENGINE_BUNDLE_DIR = $engineBundle
    $env:PLUTOGE_ELECTRON_ZIP_DIR = $electronZipDirectory
    $env:PLUTOGE_DISTRIBUTION_BUILD = '1'
    Invoke-Checked "Running Electron Forge $ForgeTarget..." { & npm run $ForgeTarget }
}
finally {
    $env:PLUTOGE_ENGINE_BUNDLE_DIR = $previousEngineBundle
    $env:PLUTOGE_ELECTRON_ZIP_DIR = $previousElectronZipDirectory
    $env:PLUTOGE_DISTRIBUTION_BUILD = $previousDistributionBuild
    Pop-Location
}

& (Join-Path $PSScriptRoot 'Verify-ElectronPackage.ps1')
if ($LASTEXITCODE -ne 0) {
    throw 'Electron package verification failed.'
}

Write-Host "PlutoGE Editor distribution is ready under $(Join-Path $electronDirectory 'out')."
