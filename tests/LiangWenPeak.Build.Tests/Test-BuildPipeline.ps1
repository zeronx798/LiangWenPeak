[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')

function Assert-ThrowsLike {
    param(
        [Parameter(Mandatory)][scriptblock]$Action,
        [Parameter(Mandatory)][string]$ExpectedPattern,
        [Parameter(Mandatory)][string]$Name
    )

    try {
        & $Action
    } catch {
        if ($_.Exception.Message -notlike $ExpectedPattern) {
            throw "$Name returned the wrong error. Expected '$ExpectedPattern', got '$($_.Exception.Message)'."
        }
        Write-Host "PASS: $Name"
        return
    }
    throw "$Name did not fail as expected."
}

function Write-Utf8WithoutBom {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Contents
    )
    [IO.File]::WriteAllText($Path, $Contents, [Text.UTF8Encoding]::new($false))
}

function New-RequiredPortableFixture {
    param(
        [Parameter(Mandatory)][string]$Root,
        [Parameter(Mandatory)][string]$Version
    )

    $applicationDirectory = Join-Path $Root "app-$Version"
    New-Item -ItemType Directory -Path (Join-Path $applicationDirectory 'Microsoft.UI.Xaml') -Force | Out-Null
    foreach ($relativePath in @(
        'LiangWenPeak.exe',
        'current.txt',
        "app-$Version\LiangWenPeak.App.exe",
        "app-$Version\App.xbf",
        "app-$Version\MainWindow.xbf",
        "app-$Version\LiangWenPeak.pri",
        "app-$Version\Microsoft.WindowsAppRuntime.dll",
        "app-$Version\Microsoft.ui.xaml.dll"
    )) {
        $path = Join-Path $Root $relativePath
        if ($relativePath -eq 'current.txt') {
            Write-Utf8WithoutBom -Path $path -Contents "$Version`r`n"
        } else {
            [IO.File]::WriteAllBytes($path, [byte[]]@())
        }
    }
}

$testRoot = Join-Path $repositoryRoot 'build\pipeline-tests'
$buildRoot = Join-Path $repositoryRoot 'build'
Remove-GeneratedDirectory -Path $testRoot -AllowedParent $buildRoot
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

try {
    $fakeVsWhere = Join-Path $testRoot 'empty-vswhere.cmd'
    Write-Utf8WithoutBom -Path $fakeVsWhere -Contents "@echo off`r`necho []`r`n"
    Assert-ThrowsLike `
        -Name 'missing Visual Studio has an actionable error' `
        -ExpectedPattern '*Desktop development with C++*' `
        -Action { Find-VisualStudio -VSWherePath $fakeVsWhere }

    $emptyVisualStudio = [PSCustomObject]@{ installationPath = (Join-Path $testRoot 'empty-vs') }
    New-Item -ItemType Directory -Path $emptyVisualStudio.installationPath -Force | Out-Null
    Assert-ThrowsLike `
        -Name 'missing MSBuild has an actionable error' `
        -ExpectedPattern '*MSBuild.exe was not found*' `
        -Action { Find-MSBuild -VisualStudio $emptyVisualStudio }

    $invalidVersionRoot = Join-Path $testRoot 'invalid-version'
    New-Item -ItemType Directory -Path $invalidVersionRoot -Force | Out-Null
    Write-Utf8WithoutBom `
        -Path (Join-Path $invalidVersionRoot 'Version.props') `
        -Contents '<Project><PropertyGroup><LiangWenPeakVersion>not-a-version</LiangWenPeakVersion></PropertyGroup></Project>'
    Assert-ThrowsLike `
        -Name 'invalid source version is rejected' `
        -ExpectedPattern '*invalid LiangWenPeakVersion*' `
        -Action { Get-SourceVersion -RepositoryRoot $invalidVersionRoot }

    $conflictingVersionRoot = Join-Path $testRoot 'conflicting-version'
    New-Item -ItemType Directory -Path $conflictingVersionRoot -Force | Out-Null
    Write-Utf8WithoutBom `
        -Path (Join-Path $conflictingVersionRoot 'Version.props') `
        -Contents '<Project><PropertyGroup><LiangWenPeakVersion>1.0.0</LiangWenPeakVersion></PropertyGroup></Project>'
    Write-Utf8WithoutBom `
        -Path (Join-Path $conflictingVersionRoot 'Other.props') `
        -Contents '<Project><PropertyGroup><Version>2.0.0</Version></PropertyGroup></Project>'
    Assert-ThrowsLike `
        -Name 'conflicting source versions are rejected' `
        -ExpectedPattern '*conflicting release version definitions*' `
        -Action { Get-SourceVersion -RepositoryRoot $conflictingVersionRoot }

    $failingTest = Join-Path $testRoot 'failing-test.cmd'
    Write-Utf8WithoutBom -Path $failingTest -Contents "@echo off`r`nexit /b 23`r`n"
    $packageReached = $false
    try {
        Invoke-TestExecutable -TestExecutable $failingTest
        $packageReached = $true
    } catch {
        if ($_.Exception.Message -notlike '*exit code 23*') {
            throw
        }
    }
    if ($packageReached) {
        throw 'a failed test did not stop the pipeline before packaging.'
    }
    Write-Host 'PASS: failed tests stop the pipeline before packaging'

    $missingAppOutput = Join-Path $testRoot 'missing-app-output'
    New-Item -ItemType Directory -Path $missingAppOutput -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $missingAppOutput 'LiangWenPeak.exe'), [byte[]]@())
    $missingAppContext = [PSCustomObject]@{ BuildOutput = $missingAppOutput }
    Assert-ThrowsLike `
        -Name 'missing App executable blocks packaging' `
        -ExpectedPattern '*LiangWenPeak.App.exe*' `
        -Action { Assert-PackageBuildOutputs -Context $missingAppContext }

    $forbiddenPackageRoot = Join-Path $testRoot 'forbidden-package'
    New-RequiredPortableFixture -Root $forbiddenPackageRoot -Version '1.0.0'
    [IO.File]::WriteAllBytes((Join-Path $forbiddenPackageRoot 'debug.pdb'), [byte[]]@())
    $forbiddenContext = [PSCustomObject]@{
        Version = '1.0.0'
        PackageDirectory = $forbiddenPackageRoot
    }
    Assert-ThrowsLike `
        -Name 'forbidden artifacts block ZIP creation' `
        -ExpectedPattern '*forbidden artifacts*' `
        -Action { Assert-PortablePackage -Context $forbiddenContext }

    Write-Host 'All release pipeline negative tests passed.'
} finally {
    Remove-GeneratedDirectory -Path $testRoot -AllowedParent $buildRoot
    $global:LASTEXITCODE = 0
}
