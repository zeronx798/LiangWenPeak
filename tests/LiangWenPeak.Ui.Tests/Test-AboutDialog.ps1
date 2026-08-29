[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet(96, 120, 144, 192)]
    [int]$ExpectedDpi = 96
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')

if (-not ('LiangWenPeakUiTests.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace LiangWenPeakUiTests
{
    public static class NativeMethods
    {
        public delegate bool MonitorEnumProc(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data);

        [StructLayout(LayoutKind.Sequential)]
        public struct Rect
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr window, out Rect rect);

        [DllImport("user32.dll")]
        public static extern uint GetDpiForWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

        public static IntPtr EnterPerMonitorV2Awareness()
        {
            return SetThreadDpiAwarenessContext(new IntPtr(-4));
        }

        public static void RestoreThreadDpiAwareness(IntPtr context)
        {
            if (context != IntPtr.Zero)
            {
                SetThreadDpiAwarenessContext(context);
            }
        }

        [DllImport("user32.dll")]
        private static extern bool EnumDisplayMonitors(
            IntPtr dc,
            IntPtr clip,
            MonitorEnumProc callback,
            IntPtr data);

        [DllImport("shcore.dll")]
        private static extern int GetDpiForMonitor(IntPtr monitor, int dpiType, out uint dpiX, out uint dpiY);

        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(
            IntPtr window,
            IntPtr insertAfter,
            int x,
            int y,
            int width,
            int height,
            uint flags);

        public static bool GetPhysicalWindowRect(IntPtr window, out Rect rect)
        {
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                return GetWindowRect(window, out rect);
            }
            finally
            {
                if (previous != IntPtr.Zero)
                {
                    SetThreadDpiAwarenessContext(previous);
                }
            }
        }

        public static bool MoveWindowToDpi(IntPtr window, uint expectedDpi)
        {
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                Rect target = new Rect();
                bool found = false;
                EnumDisplayMonitors(
                    IntPtr.Zero,
                    IntPtr.Zero,
                    delegate(IntPtr monitor, IntPtr dc, ref Rect rect, IntPtr data)
                    {
                        uint dpiX;
                        uint dpiY;
                        if (GetDpiForMonitor(monitor, 0, out dpiX, out dpiY) == 0 && dpiX == expectedDpi)
                        {
                            target = rect;
                            found = true;
                            return false;
                        }
                        return true;
                    },
                    IntPtr.Zero);

                const uint noSizeNoZOrderNoActivate = 0x0001 | 0x0004 | 0x0010;
                return found && SetWindowPos(
                    window,
                    IntPtr.Zero,
                    target.Left + 80,
                    target.Top + 80,
                    0,
                    0,
                    noSizeNoZOrderNoActivate);
            }
            finally
            {
                if (previous != IntPtr.Zero)
                {
                    SetThreadDpiAwarenessContext(previous);
                }
            }
        }
    }
}
'@
}

Add-Type -AssemblyName UIAutomationClient

function Get-WindowRectangle([IntPtr]$WindowHandle) {
    $rectangle = [LiangWenPeakUiTests.NativeMethods+Rect]::new()
    if (-not [LiangWenPeakUiTests.NativeMethods]::GetPhysicalWindowRect($WindowHandle, [ref]$rectangle)) {
        throw 'GetWindowRect failed.'
    }
    return $rectangle
}

function Test-SameRectangle($Left, $Right, [int]$Tolerance = 0) {
    return [Math]::Abs($Left.Left - $Right.Left) -le $Tolerance -and
        [Math]::Abs($Left.Top - $Right.Top) -le $Tolerance -and
        [Math]::Abs($Left.Right - $Right.Right) -le $Tolerance -and
        [Math]::Abs($Left.Bottom - $Right.Bottom) -le $Tolerance
}

function Find-ElementByProperty(
    [Windows.Automation.AutomationElement]$Root,
    [Windows.Automation.AutomationProperty]$Property,
    [object]$Value,
    [int]$TimeoutMilliseconds = 5000) {

    $condition = [Windows.Automation.PropertyCondition]::new($Property, $Value)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $element = $Root.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
        if ($null -ne $element) {
            return $element
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    return $null
}

function Find-ElementByNameAndControlType(
    [Windows.Automation.AutomationElement]$Root,
    [string]$Name,
    [Windows.Automation.ControlType]$ControlType,
    $WindowRectangle,
    [int]$TimeoutMilliseconds = 5000) {

    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::NameProperty,
            $Name),
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::ControlTypeProperty,
            $ControlType))
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        if ($elements.Count -gt 0) {
            return $elements.Item(0)
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    return $null
}

function Invoke-AutomationElement([Windows.Automation.AutomationElement]$Element) {
    $pattern = $Element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Assert-ElementInsideWindow(
    [Windows.Automation.AutomationElement]$Element,
    $WindowRectangle,
    [string]$Name) {

    $bounds = $Element.Current.BoundingRectangle
    if ($bounds.IsEmpty -or $bounds.Width -le 0 -or $bounds.Height -le 0) {
        throw "About element '$Name' is clipped or has empty bounds."
    }

    if ($bounds.Left -lt $WindowRectangle.Left -or
        $bounds.Top -lt $WindowRectangle.Top -or
        $bounds.Right -gt $WindowRectangle.Right -or
        $bounds.Bottom -gt $WindowRectangle.Bottom) {
        throw "About element '$Name' bounds $($bounds.Left),$($bounds.Top),$($bounds.Right),$($bounds.Bottom) extend outside window $($WindowRectangle.Left),$($WindowRectangle.Top),$($WindowRectangle.Right),$($WindowRectangle.Bottom)."
    }
}

function Stop-TestApplication([Diagnostics.Process]$Process) {
    Stop-IsolatedTestProcess -Profile $TestProfile -Process $Process
}

$context = Get-BuildContext -Configuration $Configuration
$sourceApplicationPath = Join-Path $context.BuildOutput 'LiangWenPeak.App.exe'
if (-not (Test-Path -LiteralPath $sourceApplicationPath -PathType Leaf)) {
    throw "Application build output was not found: $sourceApplicationPath"
}

$TestProfile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'about-ui'
$applicationDirectory = Join-Path $TestProfile.PortableRoot "app-$($context.Version)"
New-Item -ItemType Directory -Path $applicationDirectory -Force | Out-Null
Get-ChildItem -LiteralPath $context.BuildOutput -Force | Copy-Item `
    -Destination $applicationDirectory `
    -Recurse `
    -Force
$applicationPath = Join-Path $applicationDirectory 'LiangWenPeak.App.exe'
Copy-Item `
    -LiteralPath (Join-Path $context.BuildOutput 'LiangWenPeak.exe') `
    -Destination $TestProfile.ExpectedLauncherPath `
    -Force
[IO.File]::WriteAllText(
    (Join-Path $TestProfile.PortableRoot 'current.txt'),
    "$($context.Version)`r`n",
    [Text.UTF8Encoding]::new($false))
New-Item -Path $TestProfile.RegistryPath -Force | Out-Null
New-ItemProperty `
    -Path $TestProfile.RegistryPath `
    -Name ApiFeatureEnabled `
    -PropertyType DWord `
    -Value 0 `
    -Force | Out-Null
Write-Host "ISOLATED_TEST_RUN_ID=$($TestProfile.RunId)"

$previousDpiContext = [LiangWenPeakUiTests.NativeMethods]::EnterPerMonitorV2Awareness()
$application = Start-IsolatedTestProcess `
    -Profile $TestProfile `
    -FilePath $applicationPath `
    -WorkingDirectory $applicationDirectory
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $application.Refresh()
    } while (-not $application.HasExited -and
        $application.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $deadline)

    if ($application.HasExited -or $application.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'LiangWenPeak main window did not become visible.'
    }

    $windowHandle = $application.MainWindowHandle
    $actualDpi = [LiangWenPeakUiTests.NativeMethods]::GetDpiForWindow($windowHandle)
    if ($actualDpi -ne $ExpectedDpi) {
        if (-not [LiangWenPeakUiTests.NativeMethods]::MoveWindowToDpi($windowHandle, $ExpectedDpi)) {
            throw "No monitor with DPI $ExpectedDpi is available; initial window DPI is $actualDpi."
        }

        $dpiDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 100
            $actualDpi = [LiangWenPeakUiTests.NativeMethods]::GetDpiForWindow($windowHandle)
        } while ($actualDpi -ne $ExpectedDpi -and [DateTime]::UtcNow -lt $dpiDeadline)

        if ($actualDpi -ne $ExpectedDpi) {
            throw "Expected DPI $ExpectedDpi after moving the window, but the window is running at DPI $actualDpi."
        }
    }

    Start-Sleep -Milliseconds 500
    $before = Get-WindowRectangle $windowHandle
    $root = [Windows.Automation.AutomationElement]::FromHandle($windowHandle)
    $moreButton = Find-ElementByProperty `
        -Root $root `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'MoreButton'
    if ($null -eq $moreButton) {
        throw 'The More button was not found.'
    }
    Invoke-AutomationElement $moreButton

    $aboutLabel = ([char]0x5173).ToString() + [char]0x4E8E
    $desktop = [Windows.Automation.AutomationElement]::RootElement
    $aboutMenuItem = Find-ElementByProperty `
        -Root $desktop `
        -Property ([Windows.Automation.AutomationElement]::NameProperty) `
        -Value $aboutLabel
    if ($null -eq $aboutMenuItem) {
        throw 'The About menu item was not found.'
    }
    Invoke-AutomationElement $aboutMenuItem

    $sourceVersion = Get-SourceVersion $repositoryRoot
    $versionLabel = (([char]0x7248).ToString() + [char]0x672C + ' ' + $sourceVersion)
    $expectedElements = @(
        [PSCustomObject]@{ Name = $aboutLabel; Type = [Windows.Automation.ControlType]::Text },
        [PSCustomObject]@{ Name = 'LiangWenPeak'; Type = [Windows.Automation.ControlType]::Text },
        [PSCustomObject]@{
            Name = 'zeronx798/LiangWenPeak'
            Type = [Windows.Automation.ControlType]::Hyperlink
        },
        [PSCustomObject]@{
            Name = 'Licensed under Apache License 2.0'
            Type = [Windows.Automation.ControlType]::Text
        },
        [PSCustomObject]@{
            Name = $versionLabel
            Type = [Windows.Automation.ControlType]::Text
        },
        [PSCustomObject]@{
            Name = (([char]0x5173).ToString() + [char]0x95ED)
            Type = [Windows.Automation.ControlType]::Button
        }
    )

    $during = Get-WindowRectangle $windowHandle
    if (-not (Test-SameRectangle $before $during -Tolerance 2)) {
        $expectedObservationGrowth = [int][Math]::Floor((13 * $actualDpi + 48) / 96)
        $observationBecameVisible =
            $before.Left -eq $during.Left -and
            $before.Top -eq $during.Top -and
            $before.Right -eq $during.Right -and
            ($during.Bottom - $before.Bottom) -eq $expectedObservationGrowth
        if (-not $observationBecameVisible) {
            throw "Opening About changed the main window rectangle from $($before.Left),$($before.Top),$($before.Right),$($before.Bottom) to $($during.Left),$($during.Top),$($during.Right),$($during.Bottom)."
        }
        $before = $during
    }

    $scalePercent = [int][Math]::Round($actualDpi / 96.0 * 100)
    $screenshotName = "about-dialog-$scalePercent`pct"
    $screenshotPath = & (Join-Path $repositoryRoot 'scripts\ui-validation\Capture-Window.ps1') `
        -WindowHandle $windowHandle `
        -Name $screenshotName

    $foundElements = @{}
    foreach ($expectedElement in $expectedElements) {
        $element = Find-ElementByNameAndControlType `
            -Root $root `
            -Name $expectedElement.Name `
            -ControlType $expectedElement.Type `
            -WindowRectangle $during
        if ($null -eq $element) {
            throw "About element '$($expectedElement.Name)' was not found."
        }
        if ($expectedElement.Name -ne $versionLabel) {
            Assert-ElementInsideWindow -Element $element -WindowRectangle $during -Name $expectedElement.Name
        }
        $foundElements[$expectedElement.Name] = $element
    }

    $removedText = @(
        (([char]0x6881).ToString() + [char]0x6587 + [char]0x5CF0 + [char]0x65F6 + [char]0x949F),
        (([char]0x5317).ToString() + [char]0x4EAC + [char]0x65F6 + [char]0x95F4 + [char]0x5CF0 + [char]0x8C37 + [char]0x72B6 + [char]0x6001 + [char]0x4EEA + [char]0x8868)
    )
    foreach ($removedName in $removedText) {
        $removedCondition = [Windows.Automation.AndCondition]::new(
            [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::NameProperty,
                $removedName),
            [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::ControlTypeProperty,
                [Windows.Automation.ControlType]::Text))
        if ($null -ne $root.FindFirst(
                [Windows.Automation.TreeScope]::Descendants,
                $removedCondition)) {
            throw "Removed About text '$removedName' is still exposed."
        }
    }

    $repositoryLink = $foundElements['zeronx798/LiangWenPeak']
    $invokePattern = $null
    if (-not $repositoryLink.TryGetCurrentPattern(
            [Windows.Automation.InvokePattern]::Pattern,
            [ref]$invokePattern)) {
        throw 'The GitHub repository hyperlink does not expose the native Invoke pattern.'
    }

    $closeLabel = ([char]0x5173).ToString() + [char]0x95ED
    $closeBounds = $foundElements[$closeLabel].Current.BoundingRectangle
    foreach ($entry in $foundElements.GetEnumerator()) {
        if ($entry.Key -eq $closeLabel -or $entry.Key -eq $versionLabel) {
            continue
        }

        $bounds = $entry.Value.Current.BoundingRectangle
        $overlapsClose = $bounds.Left -lt $closeBounds.Right -and
            $bounds.Right -gt $closeBounds.Left -and
            $bounds.Top -lt $closeBounds.Bottom -and
            $bounds.Bottom -gt $closeBounds.Top
        if ($overlapsClose) {
            throw "About element '$($entry.Key)' overlaps the Close button."
        }
    }

    Invoke-AutomationElement $foundElements[$closeLabel]
    Start-Sleep -Milliseconds 300

    $after = Get-WindowRectangle $windowHandle
    if (-not (Test-SameRectangle $before $after -Tolerance 2)) {
        throw "Closing About changed the main window rectangle from $($before.Left),$($before.Top),$($before.Right),$($before.Bottom) to $($after.Left),$($after.Top),$($after.Right),$($after.Bottom)."
    }

    Write-Host "PASS: About semantic UI Automation at $scalePercent% DPI; version visibility is covered by final-ZIP manual visual acceptance"
    Write-Host "Window rectangle: $($before.Left),$($before.Top) $($before.Right - $before.Left)x$($before.Bottom - $before.Top)"
    Write-Host "Screenshot: $screenshotPath"
} finally {
    Stop-TestApplication $application
    [LiangWenPeakUiTests.NativeMethods]::RestoreThreadDpiAwareness($previousDpiContext)
    Remove-IsolatedTestProfile -Profile $TestProfile -RepositoryRoot $repositoryRoot
}
