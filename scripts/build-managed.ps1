<#
.SYNOPSIS
    Builds the managed half of the tree (Cartograph.Interop, and from Phase 13
    the WPF shell) and runs the interop boundary tests.

.DESCRIPTION
    CMake owns the native tree; MSBuild owns the managed one. See the Phase 12
    DECISIONS entry for why. The split means there is an order to respect:

        1. cmake --build --preset x64-debug     (produces Cartograph.Core.lib
                                                 and cartograph-native.props)
        2. Cartograph.Interop.vcxproj           (links against both)
        3. Cartograph.Interop.Tests.csproj      (references the assembly from 2)

    This script does 2 and 3, and by default checks that 1 has happened. It
    must run from a Developer PowerShell for VS, like every other build command
    in this project - MSBuild is not on a plain shell's PATH.

.PARAMETER Configuration
    Debug (default) or Release. Selects the matching native build directory.

.PARAMETER SkipTests
    Build only; do not run the boundary tests.

.EXAMPLE
    scripts\build-managed.ps1
    scripts\build-managed.ps1 -Configuration Release
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$suffix = "x64-$($Configuration.ToLowerInvariant())"
$nativeDir = Join-Path $repoRoot "build\$suffix"
$managedDir = Join-Path $repoRoot "build\managed\$suffix"

if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    throw "msbuild is not on PATH. Run this from a Developer PowerShell for VS (see CLAUDE.md)."
}

$props = Join-Path $nativeDir 'cartograph-native.props'
if (-not (Test-Path $props)) {
    throw "Native build not found: $props is missing. Build it first with " +
          "``cmake --preset $suffix; cmake --build --preset $suffix``."
}

$projects = @(
    'Cartograph.Interop\Cartograph.Interop.vcxproj',
    'Cartograph.Interop.Tests\Cartograph.Interop.Tests.csproj'
)

foreach ($project in $projects) {
    Write-Host "==> $project ($Configuration)" -ForegroundColor Cyan
    # -restore matters only for the SDK-style C# projects, but it is harmless
    # on the C++ one and keeps this loop from needing a special case.
    msbuild (Join-Path $repoRoot $project) -p:Configuration=$Configuration -p:Platform=x64 -restore -v:minimal -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $project"
    }
}

if ($SkipTests) {
    Write-Host "Built to $managedDir (tests skipped)." -ForegroundColor Green
    exit 0
}

$tests = Join-Path $managedDir 'Cartograph.Interop.Tests.exe'
Write-Host "==> $tests" -ForegroundColor Cyan

# GDAL writes its own diagnostics to stderr even for failures Core catches and
# turns into an exception - and the boundary tests deliberately provoke a few of
# those. With ErrorActionPreference still 'Stop', PowerShell would treat that
# chatter as a terminating error and report a failure the test run did not have.
# The exit code is the real verdict.
$ErrorActionPreference = 'Continue'
& $tests
exit $LASTEXITCODE
