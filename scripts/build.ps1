param([switch]$DesktopTests)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-env.ps1')
$crossdeskProject = Split-Path -Parent $PSScriptRoot
$crossdeskBuildProject = $crossdeskProject
# Qt's Windows moc and some MinGW tools do not consistently accept non-ASCII
# argv paths. A junction keeps both source and generated paths ASCII without
# relocating the user's checkout or changing their files.
if ($crossdeskProject -match '[^\x00-\x7F]') {
    $crossdeskHasher = [Security.Cryptography.SHA256]::Create()
    try {
        $crossdeskHash = [BitConverter]::ToString($crossdeskHasher.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($crossdeskProject))).Replace('-', '').Substring(0, 12)
    } finally { $crossdeskHasher.Dispose() }
    $crossdeskAliasRoot = Join-Path $env:CROSSDESK_TOOLCHAIN_ROOT 'workspaces'
    New-Item -ItemType Directory -Path $crossdeskAliasRoot -Force | Out-Null
    $crossdeskBuildProject = Join-Path $crossdeskAliasRoot $crossdeskHash
    if (-not (Test-Path -LiteralPath $crossdeskBuildProject)) {
        New-Item -ItemType Junction -Path $crossdeskBuildProject -Target $crossdeskProject | Out-Null
    } elseif ((Get-Item -LiteralPath $crossdeskBuildProject).Target -ne $crossdeskProject) {
        throw "Build alias points at an unexpected location: $crossdeskBuildProject"
    }
}
$crossdeskBuild = Join-Path $crossdeskBuildProject 'build-desktop'

cmake -S $crossdeskBuildProject -B $crossdeskBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug -DCROSSDESK_BUILD_GUI=ON `
    "-DCMAKE_CXX_COMPILER=$crossdeskCompiler/g++.exe"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $crossdeskBuild --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
if ($DesktopTests) {
    ctest --test-dir $crossdeskBuild --output-on-failure
} else {
    ctest --test-dir $crossdeskBuild -LE interactive-desktop --output-on-failure
}
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
