<#
.SYNOPSIS
Builds CursorUsageMonitor.dll (TrafficMonitor plugin) with the Visual Studio C++ toolset.

.EXAMPLE
.\build.ps1
.\build.ps1 -PluginDir "D:\Tools\TrafficMonitor\plugins"
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    # Must match the TrafficMonitor build: x64, Win32, or ARM64EC.
    [ValidateSet('x64', 'Win32', 'ARM64EC')]
    [string]$Platform = 'x64',

    # Optional TrafficMonitor plugins folder; the built DLL is copied there.
    [string]$PluginDir
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$buildDir = Join-Path $root "build\$Platform"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'vswhere.exe not found. Install Visual Studio 2019+ or Build Tools with the "Desktop development with C++" workload.'
}

$vcComponent = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
if ($Platform -eq 'ARM64EC') {
    $vcComponent = 'Microsoft.VisualStudio.Component.VC.Tools.ARM64EC'
}
$vs = & $vswhere -latest -products * -requires $vcComponent -property installationPath -utf8
$vsVersion = & $vswhere -latest -products * -requires $vcComponent -property installationVersion -utf8
if (-not $vs) {
    if ($Platform -eq 'ARM64EC') {
        throw 'No Visual Studio install with the ARM64EC toolset was found. Add "MSVC ARM64/ARM64EC build tools" (Microsoft.VisualStudio.Component.VC.Tools.ARM64EC).'
    }
    throw 'No Visual Studio install with the MSVC toolset was found. Add the "Desktop development with C++" workload.'
}

$major = ([version]$vsVersion).Major
$years = @{ 16 = '2019'; 17 = '2022'; 18 = '2026' }
if (-not $years.ContainsKey($major)) {
    throw "Unsupported Visual Studio version $vsVersion at $vs"
}
$generator = "Visual Studio $major $($years[$major])"

$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) {
        throw 'cmake.exe not found. Install the "C++ CMake tools for Windows" component or put CMake on PATH.'
    }
}

Write-Host "Visual Studio: $vs ($generator)"
& $cmake -S $root -B $buildDir -G $generator -A $Platform "-DCMAKE_GENERATOR_INSTANCE=$vs"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

& $cmake --build $buildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

$dll = Join-Path $buildDir "$Configuration\CursorUsageMonitor.dll"
Write-Host "Built: $dll"

if ($PluginDir) {
    New-Item -ItemType Directory -Force $PluginDir | Out-Null
    Copy-Item $dll $PluginDir -Force
    Write-Host "Copied to: $PluginDir"
}
