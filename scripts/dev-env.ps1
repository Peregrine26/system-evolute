param(
    [string]$ToolchainRoot = $(if ($env:CROSSDESK_TOOLCHAIN_ROOT) {
        $env:CROSSDESK_TOOLCHAIN_ROOT
    } else {
        Join-Path $env:USERPROFILE '.codex/tools/crossdesk'
    })
)

$crossdeskScripts = Join-Path $ToolchainRoot 'python/Scripts'
$crossdeskCompiler = Join-Path $ToolchainRoot 'Qt/Tools/mingw1310_64/bin'
$crossdeskQt = Join-Path $ToolchainRoot 'Qt/6.8.3/mingw_64'

foreach ($crossdeskRequired in @(
    (Join-Path $crossdeskScripts 'cmake.exe'),
    (Join-Path $crossdeskScripts 'ninja.exe'),
    (Join-Path $crossdeskCompiler 'g++.exe'),
    (Join-Path $crossdeskQt 'bin/Qt6Widgets.dll')
)) {
    if (-not (Test-Path -LiteralPath $crossdeskRequired)) {
        throw "Missing toolchain component: $crossdeskRequired. See docs/a-platform-integration.md."
    }
}

# Only this PowerShell session changes; do not write the system/user PATH.
$env:PATH = "$crossdeskCompiler;$crossdeskQt/bin;$crossdeskScripts;$env:PATH"
$env:CMAKE_PREFIX_PATH = "$crossdeskQt;$env:CMAKE_PREFIX_PATH"
$env:CROSSDESK_TOOLCHAIN_ROOT = $ToolchainRoot
