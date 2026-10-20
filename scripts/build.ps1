param([switch]$DesktopTests)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'dev-env.ps1')
$crossdeskProject = Split-Path -Parent $PSScriptRoot
$crossdeskBuild = Join-Path $crossdeskProject 'build-local'

cmake -S $crossdeskProject -B $crossdeskBuild -G Ninja `
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
