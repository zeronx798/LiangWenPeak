[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$SkipStage,

    [switch]$SmokeOnly,

    [string]$DistributionRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-SafeTestDirectory([string]$path, [string]$repositoryRoot) {
    $fullPath = [IO.Path]::GetFullPath($path).TrimEnd('\', '/')
    $fullRoot = [IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\', '/')
    $requiredPrefix = (Join-Path $fullRoot 'build\launcher-tests').TrimEnd('\', '/')
    $prefixWithSeparator = $requiredPrefix + [IO.Path]::DirectorySeparatorChar

    if ($fullPath.Equals($requiredPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($prefixWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
        return
    }

    throw "Refusing to replace a launcher test directory outside build/launcher-tests: $fullPath"
}

function Copy-DirectoryContents([string]$source, [string]$destination) {
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $source -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse -Force
    }
}

function Write-Utf8WithoutBom([string]$path, [string]$contents) {
    [IO.File]::WriteAllText($path, $contents, [Text.UTF8Encoding]::new($false))
}

function Invoke-SuccessScenario(
    $profile,
    [string]$name,
    [string]$launcherPath,
    [string]$applicationPath,
    [string]$workingDirectory) {

    $fullApplicationPath = [IO.Path]::GetFullPath($applicationPath)
    $launcher = Start-IsolatedTestProcess `
        -Profile $profile `
        -FilePath $launcherPath `
        -WorkingDirectory $workingDirectory
    $childProcessId = $null
    $childDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $candidate = Get-CimInstance Win32_Process `
            -Filter "ParentProcessId = $($launcher.Id)" `
            -ErrorAction SilentlyContinue |
            Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.ExecutablePath) -and
                [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                    $fullApplicationPath,
                    [StringComparison]::OrdinalIgnoreCase)
            } |
            Select-Object -First 1
        if ($null -ne $candidate) {
            $childProcessId = [int]$candidate.ProcessId
            break
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $childDeadline)

    if (-not $launcher.WaitForExit(10000)) {
        Stop-IsolatedTestProcess -Profile $profile -Process $launcher
        throw "$name failed: the launcher did not exit immediately."
    }
    [void]$profile.OwnedProcessIds.Remove($launcher.Id)
    if ($launcher.ExitCode -ne 0) {
        throw "$name failed: launcher exit code $($launcher.ExitCode)."
    }

    if ($null -eq $childProcessId) {
        throw "$name failed: LiangWenPeak.App.exe did not start from the expected directory."
    }
    $application = [Diagnostics.Process]::GetProcessById($childProcessId)
    [void]$profile.OwnedProcessIds.Add($application.Id)

    $windowDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $application.Refresh()
    } while (-not $application.HasExited -and
        $application.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $windowDeadline)

    if ($application.HasExited -or $application.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "$name failed: the WinUI main window did not become visible."
    }

    Write-Host "PASS: $name"
    return $application
}

function Get-WindowAutomationText([IntPtr]$windowHandle) {
    $root = [Windows.Automation.AutomationElement]::FromHandle($windowHandle)
    $elements = $root.FindAll(
        [Windows.Automation.TreeScope]::Descendants,
        [Windows.Automation.Condition]::TrueCondition)

    $names = @()
    for ($index = 0; $index -lt $elements.Count; ++$index) {
        $name = $elements.Item($index).Current.Name
        if (-not [string]::IsNullOrWhiteSpace($name)) {
            $names += $name
        }
    }
    return $names -join "`n"
}

function Invoke-ErrorScenario(
    $profile,
    [string]$name,
    [string]$fixtureRoot,
    [string]$launcherSource,
    [scriptblock]$arrange,
    [string]$expectedMessage) {

    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $fixtureRoot 'LiangWenPeak.exe') -Force
    & $arrange $fixtureRoot

    $process = Start-IsolatedTestProcess `
        -Profile $profile `
        -FilePath (Join-Path $fixtureRoot 'LiangWenPeak.exe') `
        -WorkingDirectory 'C:\'
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 50
            $process.Refresh()
        } while (-not $process.HasExited -and
            $process.MainWindowHandle -eq [IntPtr]::Zero -and
            [DateTime]::UtcNow -lt $deadline)

        if ($process.HasExited -or $process.MainWindowHandle -eq [IntPtr]::Zero) {
            throw "$name failed: the expected native error dialog did not appear."
        }

        $dialogText = Get-WindowAutomationText $process.MainWindowHandle
        if ($dialogText -notlike "*$expectedMessage*") {
            throw "$name failed: expected '$expectedMessage', got '$dialogText'."
        }
    } finally {
        Stop-IsolatedTestProcess -Profile $profile -Process $process
    }

    if ($process.ExitCode -eq 0) {
        throw "$name failed: an error path returned exit code 0."
    }
    Write-Host "PASS: $name"
}

Add-Type -AssemblyName UIAutomationClient

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')
$context = Get-BuildContext -Configuration $Configuration -Architecture $Platform -RepositoryRoot $repositoryRoot
if (-not $SkipStage) {
    $packageResult = & (Join-Path $repositoryRoot 'scripts\package.ps1') `
        -Configuration $Configuration `
        -Architecture $Platform
    if (-not $?) {
        throw 'Portable packaging failed.'
    }
}

$distributionRoot = if ([string]::IsNullOrWhiteSpace($DistributionRoot)) {
    $context.PackageDirectory
} else {
    [IO.Path]::GetFullPath($DistributionRoot)
}
$distributionLauncher = Join-Path $distributionRoot 'LiangWenPeak.exe'
$currentSource = Join-Path $distributionRoot 'current.txt'
if (-not (Test-Path -LiteralPath $distributionLauncher -PathType Leaf) -or
    -not (Test-Path -LiteralPath $currentSource -PathType Leaf)) {
    throw 'The staged launcher layout was not found. Run scripts\package.ps1 first.'
}

$version = [IO.File]::ReadAllText($currentSource).Trim()
if ($version -ne $context.Version) {
    throw "The staged current.txt version '$version' does not match source version '$($context.Version)'."
}
$normalProfile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'launcher-smoke'
Write-Host "ISOLATED_TEST_RUN_ID=$($normalProfile.RunId)"
try {
    Copy-DirectoryContents $distributionRoot $normalProfile.PortableRoot
    $launcherSource = Join-Path $normalProfile.PortableRoot 'LiangWenPeak.exe'
    $applicationSource = Join-Path $normalProfile.PortableRoot "app-$version\LiangWenPeak.App.exe"
    $application = $null
    try {
        $application = Invoke-SuccessScenario `
            $normalProfile `
            'normal staged launch' `
            $launcherSource `
            $applicationSource `
            $normalProfile.PortableRoot
    } finally {
        if ($null -ne $application) {
            Stop-IsolatedTestProcess -Profile $normalProfile -Process $application
        }
    }
} finally {
    Remove-IsolatedTestProfile -Profile $normalProfile -RepositoryRoot $repositoryRoot
}

if ($SmokeOnly) {
    Write-Host 'Launcher staging smoke test passed.'
    return
}

$fullProfile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'launcher-full'
$spaceRoot = Join-Path $fullProfile.RunRoot "Test Apps\LiangWenPeak $($fullProfile.RunId)"
$fullProfile.PortableRoot = [IO.Path]::GetFullPath($spaceRoot)
$fullProfile.ExpectedLauncherPath = Join-Path $fullProfile.PortableRoot 'LiangWenPeak.exe'
Write-Host "ISOLATED_TEST_RUN_ID=$($fullProfile.RunId)"
try {
    Copy-DirectoryContents $distributionRoot $spaceRoot
    $spaceCurrent = Join-Path $spaceRoot 'current.txt'
    [IO.File]::WriteAllText($spaceCurrent, " `t$version`r`n", [Text.UTF8Encoding]::new($true))
    $spaceApplication = Join-Path $spaceRoot "app-$version\LiangWenPeak.App.exe"
    $application = $null
    try {
        $application = Invoke-SuccessScenario `
            $fullProfile `
            'path with spaces and arbitrary working directory' `
            (Join-Path $spaceRoot 'LiangWenPeak.exe') `
            $spaceApplication `
            'C:\'
    } finally {
        if ($null -ne $application) {
            Stop-IsolatedTestProcess -Profile $fullProfile -Process $application
        }
    }

    $errorRoot = Join-Path $fullProfile.RunRoot 'errors'
    Invoke-ErrorScenario `
        $fullProfile `
        'missing current.txt' `
        (Join-Path $errorRoot 'missing-current') `
        $distributionLauncher `
        { param($root) } `
        'current.txt was not found.'

    Invoke-ErrorScenario `
        $fullProfile `
        'invalid version traversal' `
        (Join-Path $errorRoot 'invalid-version') `
        $distributionLauncher `
        { param($root) Write-Utf8WithoutBom (Join-Path $root 'current.txt') "..\other`r`n" } `
        'current.txt contains an invalid version.'

    Invoke-ErrorScenario `
        $fullProfile `
        'missing application directory' `
        (Join-Path $errorRoot 'missing-directory') `
        $distributionLauncher `
        { param($root) Write-Utf8WithoutBom (Join-Path $root 'current.txt') "9.9.9`r`n" } `
        'Application version directory not found:'

    Invoke-ErrorScenario `
        $fullProfile `
        'missing application executable' `
        (Join-Path $errorRoot 'missing-executable') `
        $distributionLauncher `
        {
            param($root)
            Write-Utf8WithoutBom (Join-Path $root 'current.txt') "1.0.0`r`n"
            New-Item -ItemType Directory -Path (Join-Path $root 'app-1.0.0') -Force | Out-Null
        } `
        'Application executable not found:'

    Invoke-ErrorScenario `
        $fullProfile `
        'CreateProcessW failure' `
        (Join-Path $errorRoot 'invalid-executable') `
        $distributionLauncher `
        {
            param($root)
            Write-Utf8WithoutBom (Join-Path $root 'current.txt') "1.0.0`r`n"
            $appDirectory = Join-Path $root 'app-1.0.0'
            New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null
            Write-Utf8WithoutBom (Join-Path $appDirectory 'LiangWenPeak.App.exe') 'not an executable'
        } `
        'Could not start LiangWenPeak.App.exe.'
} finally {
    Remove-IsolatedTestProfile -Profile $fullProfile -RepositoryRoot $repositoryRoot
}
Write-Host 'All launcher integration tests passed.'
