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

function Invoke-ExistingInstanceLaunch(
    $profile,
    [Diagnostics.Process]$primary,
    [string]$launcherPath,
    [string]$applicationPath,
    [string]$workingDirectory,
    [IntPtr]$expectedForegroundWindow) {

    $launcher = Start-IsolatedTestProcess `
        -Profile $profile `
        -FilePath $launcherPath `
        -WorkingDirectory $workingDirectory
    if (-not $launcher.WaitForExit(10000)) {
        Stop-IsolatedTestProcess -Profile $profile -Process $launcher
        throw 'Conditional singleton launch did not let the launcher exit.'
    }
    [void]$profile.OwnedProcessIds.Remove($launcher.Id)
    if ($launcher.ExitCode -ne 0) {
        throw "Conditional singleton launcher exit code was $($launcher.ExitCode)."
    }

    $secondaryCandidates = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            [int]$_.ProcessId -ne $primary.Id -and
            -not [string]::IsNullOrWhiteSpace($_.ExecutablePath) -and
            [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                [IO.Path]::GetFullPath($applicationPath),
                [StringComparison]::OrdinalIgnoreCase)
        })
    foreach ($candidate in $secondaryCandidates) {
        try {
            $secondary = [Diagnostics.Process]::GetProcessById([int]$candidate.ProcessId)
            [void]$profile.OwnedProcessIds.Add($secondary.Id)
            if (-not $secondary.WaitForExit(10000)) {
                throw 'Redirected secondary application process did not exit.'
            }
            [void]$profile.OwnedProcessIds.Remove($secondary.Id)
        } catch [ArgumentException] {
            # The redirected secondary already exited.
        }
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $primary.Refresh()
        $matchingProcesses = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
            Where-Object {
                -not [string]::IsNullOrWhiteSpace($_.ExecutablePath) -and
                [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                    [IO.Path]::GetFullPath($applicationPath),
                    [StringComparison]::OrdinalIgnoreCase)
            })
        $foreground = [LiangWenPeakLauncherTests.NativeMethods]::GetForegroundWindow()
    } while ((
            $primary.HasExited -or
            [LiangWenPeakLauncherTests.NativeMethods]::IsIconic($primary.MainWindowHandle) -or
            $foreground -ne $expectedForegroundWindow -or
            $matchingProcesses.Count -ne 1) -and
        [DateTime]::UtcNow -lt $deadline)

    if ($primary.HasExited -or $matchingProcesses.Count -ne 1 -or
        [int]$matchingProcesses[0].ProcessId -ne $primary.Id) {
        throw 'Same-data-root launch created a second lasting application process.'
    }
    if ([LiangWenPeakLauncherTests.NativeMethods]::IsIconic($primary.MainWindowHandle)) {
        throw 'Secondary activation did not restore the existing MainWindow.'
    }
    if ($foreground -ne $expectedForegroundWindow) {
        throw 'Secondary activation did not foreground the existing active window.'
    }
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
if (-not ('LiangWenPeakLauncherTests.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace LiangWenPeakLauncherTests
{
    public static class NativeMethods
    {
        [DllImport("user32.dll")]
        public static extern bool ShowWindow(IntPtr window, int command);

        [DllImport("user32.dll")]
        public static extern bool IsIconic(IntPtr window);

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
        public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    }
}
'@
}

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
    $parallelProfile = $null
    $parallelApplication = $null
    try {
        New-Item -Path $normalProfile.RegistryPath -Force | Out-Null
        New-ItemProperty `
            -Path $normalProfile.RegistryPath `
            -Name AlwaysOnTop `
            -PropertyType DWord `
            -Value 0 `
            -Force | Out-Null
        $application = Invoke-SuccessScenario `
            $normalProfile `
            'normal staged launch' `
            $launcherSource `
            $applicationSource `
            $normalProfile.PortableRoot

        [void][LiangWenPeakLauncherTests.NativeMethods]::ShowWindow(
            $application.MainWindowHandle,
            6)
        Invoke-ExistingInstanceLaunch `
            $normalProfile `
            $application `
            $launcherSource `
            $applicationSource `
            $normalProfile.PortableRoot `
            $application.MainWindowHandle
        if ((Get-ItemPropertyValue `
                -Path $normalProfile.RegistryPath `
                -Name AlwaysOnTop) -ne 0) {
            throw 'Secondary activation changed the persisted always-on-top preference.'
        }
        Write-Host 'PASS: same data root redirects, restores, and foregrounds one MainWindow'

        $alternateVersionDirectory = Join-Path $normalProfile.PortableRoot 'app-1.1.2'
        New-Item `
            -ItemType Junction `
            -Path $alternateVersionDirectory `
            -Target (Split-Path -Parent $applicationSource) `
            -Force | Out-Null
        $alternateApplication = Join-Path $alternateVersionDirectory 'LiangWenPeak.App.exe'
        $alternate = Start-IsolatedTestProcess `
            -Profile $normalProfile `
            -FilePath $alternateApplication `
            -WorkingDirectory $normalProfile.PortableRoot
        if (-not $alternate.WaitForExit(10000)) {
            Stop-IsolatedTestProcess -Profile $normalProfile -Process $alternate
            throw 'Different app-version path did not redirect to the same data-root instance.'
        }
        [void]$normalProfile.OwnedProcessIds.Remove($alternate.Id)
        $application.Refresh()
        if ($application.HasExited) {
            throw 'Cross-version activation terminated the primary instance.'
        }
        Write-Host 'PASS: different app version path shares the canonical data-root instance'

        $mainRoot = [Windows.Automation.AutomationElement]::FromHandle(
            $application.MainWindowHandle)
        $more = $mainRoot.FindFirst(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::AutomationIdProperty,
                'MoreButton'))
        $more.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
        Start-Sleep -Milliseconds 300
        $settingsLabel = ([char]0x8BBE).ToString() + [char]0x7F6E + '...'
        $settingsItems = [Windows.Automation.AutomationElement]::RootElement.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.AndCondition]::new(
                [Windows.Automation.PropertyCondition]::new(
                    [Windows.Automation.AutomationElement]::NameProperty,
                    $settingsLabel),
                [Windows.Automation.PropertyCondition]::new(
                    [Windows.Automation.AutomationElement]::ProcessIdProperty,
                    $application.Id)))
        $settingsItem = $null
        for ($itemIndex = 0; $itemIndex -lt $settingsItems.Count; ++$itemIndex) {
            $candidate = $settingsItems.Item($itemIndex)
            if ($candidate.Current.IsEnabled -and -not $candidate.Current.IsOffscreen) {
                $settingsItem = $candidate
                break
            }
        }
        if ($null -eq $settingsItem) {
            throw 'Conditional singleton test could not open the settings menu item.'
        }
        $settingsItem.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
        $settingsTitle = ([char]0x8BBE).ToString() + [char]0x7F6E
        $settingsDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            Start-Sleep -Milliseconds 100
            $settingsWindow = [Windows.Automation.AutomationElement]::RootElement.FindFirst(
                [Windows.Automation.TreeScope]::Descendants,
                [Windows.Automation.AndCondition]::new(
                    [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::NameProperty,
                        $settingsTitle),
                    [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [Windows.Automation.ControlType]::Window)))
        } while ($null -eq $settingsWindow -and [DateTime]::UtcNow -lt $settingsDeadline)
        if ($null -eq $settingsWindow -or $settingsWindow.Current.ProcessId -ne $application.Id) {
            throw 'Conditional singleton test settings window did not open.'
        }
        $settingsHandle = [IntPtr]$settingsWindow.Current.NativeWindowHandle
        Invoke-ExistingInstanceLaunch `
            $normalProfile `
            $application `
            $launcherSource `
            $applicationSource `
            $normalProfile.PortableRoot `
            $settingsHandle
        $settingsWindows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.AndCondition]::new(
                [Windows.Automation.AndCondition]::new(
                    [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::NameProperty,
                        $settingsTitle),
                    [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [Windows.Automation.ControlType]::Window)),
                [Windows.Automation.PropertyCondition]::new(
                    [Windows.Automation.AutomationElement]::ProcessIdProperty,
                    $application.Id)))
        if ($settingsWindows.Count -ne 1) {
            throw 'Secondary activation duplicated the owned settings window.'
        }
        Write-Host 'PASS: secondary activation preserves and foregrounds the owned settings window'

        $parallelProfile = New-IsolatedTestProfile `
            -RepositoryRoot $repositoryRoot `
            -TestArea 'launcher-parallel-root'
        Copy-DirectoryContents $distributionRoot $parallelProfile.PortableRoot
        $parallelLauncher = Join-Path $parallelProfile.PortableRoot 'LiangWenPeak.exe'
        $parallelApplicationPath = Join-Path $parallelProfile.PortableRoot "app-$version\LiangWenPeak.App.exe"
        $parallelApplication = Invoke-SuccessScenario `
            $parallelProfile `
            'different data root parallel launch' `
            $parallelLauncher `
            $parallelApplicationPath `
            $parallelProfile.PortableRoot
        $parallelTopmostBefore = [LiangWenPeakLauncherTests.NativeMethods]::GetWindowLongPtr(
            $parallelApplication.MainWindowHandle,
            -20).ToInt64() -band 0x8
        Invoke-ExistingInstanceLaunch `
            $parallelProfile `
            $parallelApplication `
            $parallelLauncher `
            $parallelApplicationPath `
            $parallelProfile.PortableRoot `
            $parallelApplication.MainWindowHandle
        $parallelTopmostAfter = [LiangWenPeakLauncherTests.NativeMethods]::GetWindowLongPtr(
            $parallelApplication.MainWindowHandle,
            -20).ToInt64() -band 0x8
        if ($parallelTopmostBefore -eq 0 -or $parallelTopmostAfter -eq 0) {
            throw 'Secondary activation changed an enabled always-on-top window.'
        }
        $application.Refresh()
        if ($application.HasExited -or $parallelApplication.HasExited -or
            $application.Id -eq $parallelApplication.Id) {
            throw 'Different canonical data roots did not remain independently runnable.'
        }
        Write-Host 'PASS: different data roots allow independent instances'
    } finally {
        if ($null -ne $parallelApplication) {
            Stop-IsolatedTestProcess -Profile $parallelProfile -Process $parallelApplication
        }
        if ($null -ne $parallelProfile) {
            Remove-IsolatedTestProfile -Profile $parallelProfile -RepositoryRoot $repositoryRoot
        }
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
