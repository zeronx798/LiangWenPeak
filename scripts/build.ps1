[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Architecture = 'x64',

    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $context = Get-BuildContext -Configuration $Configuration -Architecture $Architecture
    $environment = Get-BuildEnvironment -Architecture $Architecture -RepositoryRoot $context.RepositoryRoot
    Write-BuildEnvironmentSummary -Context $context -Environment $environment

    if (-not (Test-Path -LiteralPath $context.SolutionPath -PathType Leaf)) {
        throw "ERROR: solution was not found: $($context.SolutionPath)"
    }

    $nugetConfiguration = Join-Path $context.RepositoryRoot 'NuGet.config'
    $applicationPackages = Join-Path $context.RepositoryRoot 'src\LiangWenPeak.App\packages.config'
    foreach ($requiredRestoreFile in @($nugetConfiguration, $applicationPackages)) {
        if (-not (Test-Path -LiteralPath $requiredRestoreFile -PathType Leaf)) {
            throw "ERROR: NuGet restore input was not found: $requiredRestoreFile"
        }
    }

    if ($Clean) {
        Write-Host '[CLEAN] Removing selected build outputs...'
        Clear-BuildConfiguration -Context $context
    }

    $commonArguments = @(
        $context.SolutionPath,
        '/nologo',
        '/nr:false',
        "/p:Configuration=$Configuration",
        "/p:Platform=$Architecture",
        '/v:minimal'
    )

    Write-Host '[RESTORE] Restoring NuGet and Windows App SDK packages...'
    try {
        Invoke-CheckedProcess `
            -FilePath $environment.MSBuildPath `
            -Arguments ($commonArguments + @('/t:Restore', '/p:RestorePackagesConfig=true')) `
            -Label 'NuGet restore'
    } catch {
        throw "ERROR: NuGet/Windows App SDK package restore failed. Verify package sources in NuGet.config and network/package availability.`n$($_.Exception.Message)"
    }

    Write-Host '[BUILD] Building Launcher, App, Core, and Tests...'
    Invoke-CheckedProcess `
        -FilePath $environment.MSBuildPath `
        -Arguments ($commonArguments + @('/m', '/t:Build')) `
        -Label 'Solution build'

    Assert-PackageBuildOutputs -Context $context
    $testExecutable = Join-Path $context.BuildOutput 'LiangWenPeak.Tests.exe'
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
        throw "ERROR: test build output is missing: $testExecutable"
    }

    Write-Host '[BUILD] PASS'
} catch {
    throw
}
