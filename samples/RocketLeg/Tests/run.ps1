param([string]$BuildDirectory = 'out/build/gcc-nvidia')
$ErrorActionPreference = 'Stop'
$rocketRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
Push-Location $rocketRoot
try {
    $build = (Resolve-Path $BuildDirectory).Path
    $compilerLine = Get-Content (Join-Path $build 'CMakeCache.txt') |
        Where-Object { $_ -match '^CMAKE_CXX_COMPILER:[^=]+=' } | Select-Object -First 1
    if (!$compilerLine) { throw 'Configure the GCC engine build first.' }
    $compiler = ($compilerLine -split '=', 2)[1]
    $output = Join-Path $rocketRoot 'out/rocketleg-validation'
    New-Item -ItemType Directory -Force $output | Out-Null
    $compileArgs = @(
        '-std=c++20', '-O2', '-shared', '-static-libgcc', '-static-libstdc++',
        'samples/RocketLeg/Tests/BulletHost.cpp',
        'engine/scene/src/components/ColliderComponent.cpp',
        '-Iengine/scene/include', "-I$build/_deps/glm-src", "-I$build/_deps/bullet3-src/src"
    )
    foreach ($library in @('BulletDynamics', 'BulletCollision', 'LinearMath')) {
        $archive = Get-ChildItem -LiteralPath "$build/_deps/bullet3-build/src/$library" -Filter "lib$library*.a" |
            Select-Object -First 1
        if (!$archive) { throw "Build the engine's $library target first." }
        $compileArgs += $archive.FullName
    }
    $compileArgs += @('-o', "$output/RocketLegBullet.dll")
    & $compiler @compileArgs
    if ($LASTEXITCODE) { throw 'Bullet test host compilation failed.' }
    dotnet run --project samples/RocketLeg/Tests/RocketLeg.PhysicsTests.csproj -c Release -- samples/RocketLeg/Assets/Models
    if ($LASTEXITCODE) { throw 'RocketLeg driving regression failed.' }
}
finally { Pop-Location }
