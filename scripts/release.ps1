[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string]$Architecture = 'x64',

    [switch]$SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $context = Get-BuildContext -Configuration 'Release' -Architecture $Architecture

    & (Join-Path $PSScriptRoot 'build.ps1') `
        -Configuration 'Release' `
        -Architecture $Architecture `
        -Clean
    if (-not $?) {
        throw 'ERROR: build.ps1 failed.'
    }

    # A failed test must not leave a stale artifact for the current target.
    Clear-PackageArtifacts -Context $context

    if (-not $SkipTests) {
        Write-Host '[TEST] Running native unit tests...'
        Invoke-TestExecutable -TestExecutable (Join-Path $context.BuildOutput 'LiangWenPeak.Tests.exe')

        Write-Host '[TEST] Running release pipeline negative tests...'
        & (Join-Path $context.RepositoryRoot 'tests\LiangWenPeak.Build.Tests\Test-BuildPipeline.ps1')
        if (-not $?) {
            throw 'ERROR: release pipeline tests failed.'
        }
        Write-Host '[TEST] PASS'
    } else {
        Write-Host '[TEST] SKIPPED'
    }

    $packageArguments = @{
        Configuration = 'Release'
        Architecture = $Architecture
        SkipBuild = $true
    }
    if (-not $SkipTests) {
        $packageArguments.FullLauncherTests = $true
    }
    $packageResult = & (Join-Path $PSScriptRoot 'package.ps1') @packageArguments
    if (-not $?) {
        throw 'ERROR: package.ps1 failed.'
    }
    $packageResult = @($packageResult | Where-Object { $_ -is [PSCustomObject] }) | Select-Object -Last 1
    if ($null -eq $packageResult) {
        throw 'ERROR: package.ps1 did not return release metadata.'
    }

    Write-Host ''
    Write-Host '========================================'
    Write-Host ' LiangWenPeak Release Complete'
    Write-Host '========================================'
    Write-Host ''
    Write-Host ("Version : {0}" -f $context.Version)
    Write-Host ("Target  : {0}" -f $context.Target)
    Write-Host ''
    Write-Host 'Launcher:'
    Write-Host ("  {0}" -f $context.PackageDirectory)
    Write-Host ''
    Write-Host 'Portable:'
    Write-Host ("  {0}" -f $context.PackageArchive)
    Write-Host ''
    Write-Host ("Uncompressed : {0}" -f (Format-ByteSize $packageResult.UncompressedSize))
    Write-Host ("ZIP Size     : {0}" -f (Format-ByteSize $packageResult.ZipSize))
    Write-Host ("SHA256       : {0}" -f $packageResult.Sha256)
} catch {
    Write-Host ''
    Write-Host '[RELEASE] FAIL'
    throw
}
