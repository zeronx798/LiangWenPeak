[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet(96, 120, 144, 192)]
    [int]$ExpectedDpi = 96,

    [switch]$RequireObservedBalance
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')

if ($RequireObservedBalance) {
    throw 'RequireObservedBalance is disabled: isolated UI tests never use or query a production API Key.'
}

if (-not ('LiangWenPeakBalanceUiTests.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace LiangWenPeakBalanceUiTests
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

        [StructLayout(LayoutKind.Sequential)]
        public struct Point
        {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct MonitorInfo
        {
            public int Size;
            public Rect Monitor;
            public Rect Work;
            public uint Flags;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetWindowRect(IntPtr window, out Rect rect);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetClientRect(IntPtr window, out Rect rect);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ClientToScreen(IntPtr window, ref Point point);

        [DllImport("user32.dll")]
        private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

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

        public static bool GetPhysicalClientRect(IntPtr window, out Rect rect)
        {
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                return GetClientRect(window, out rect);
            }
            finally
            {
                if (previous != IntPtr.Zero)
                {
                    SetThreadDpiAwarenessContext(previous);
                }
            }
        }

        public static bool GetPhysicalClientBottom(IntPtr window, out Point point)
        {
            point = new Point();
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                Rect rect;
                if (!GetClientRect(window, out rect))
                {
                    return false;
                }
                point.X = rect.Right;
                point.Y = rect.Bottom;
                return ClientToScreen(window, ref point);
            }
            finally
            {
                if (previous != IntPtr.Zero)
                {
                    SetThreadDpiAwarenessContext(previous);
                }
            }
        }

        [DllImport("user32.dll")]
        private static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);

        public static bool GetPhysicalMonitorWorkArea(IntPtr window, out Rect workArea)
        {
            workArea = new Rect();
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                IntPtr monitor = MonitorFromWindow(window, 2);
                if (monitor == IntPtr.Zero)
                {
                    return false;
                }

                MonitorInfo info = new MonitorInfo();
                info.Size = Marshal.SizeOf(typeof(MonitorInfo));
                if (!GetMonitorInfo(monitor, ref info))
                {
                    return false;
                }
                workArea = info.Work;
                return true;
            }
            finally
            {
                if (previous != IntPtr.Zero)
                {
                    SetThreadDpiAwarenessContext(previous);
                }
            }
        }

        [DllImport("user32.dll")]
        public static extern uint GetDpiForWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern bool EnumDisplayMonitors(
            IntPtr dc,
            IntPtr clip,
            MonitorEnumProc callback,
            IntPtr data);

        [DllImport("shcore.dll")]
        private static extern int GetDpiForMonitor(
            IntPtr monitor,
            int dpiType,
            out uint dpiX,
            out uint dpiY);

        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(
            IntPtr window,
            IntPtr insertAfter,
            int x,
            int y,
            int width,
            int height,
            uint flags);

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
                        if (GetDpiForMonitor(monitor, 0, out dpiX, out dpiY) == 0
                            && dpiX == expectedDpi)
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

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWindowEnabled(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWindow(IntPtr window);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindowVisible(IntPtr window);

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetWindowAttribute(
            IntPtr window,
            int attribute,
            out int value,
            int valueSize);

        public static bool IsWindowReadyForInput(IntPtr window)
        {
            int cloaked;
            return IsWindowVisible(window)
                && DwmGetWindowAttribute(window, 14, out cloaked, sizeof(int)) == 0
                && cloaked == 0;
        }

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool PostMessage(
            IntPtr window,
            uint message,
            IntPtr wParam,
            IntPtr lParam);

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
        public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);
    }
}
'@
}

Add-Type -AssemblyName UIAutomationClient

$apiBalanceLabel = 'API ' + [char]0x4F59 + [char]0x989D
$apiBurnLabel = 'API ' + [char]0x6D88 + [char]0x8017
$etaLabel = ([char]0x9884).ToString() + [char]0x8BA1 + [char]0x89E6 + [char]0x5E95
$settingsTitle = ([char]0x8BBE).ToString() + [char]0x7F6E
$settingsMenuLabel = $settingsTitle + '...'
$fluentThemeLabel = 'Fluent ' + [char]0x4E3B + [char]0x9898
$settingsScrollRegionLabel = $settingsTitle + [char]0x6EDA + [char]0x52A8 +
    [char]0x533A + [char]0x57DF
$undoLabel = ([char]0x64A4).ToString() + [char]0x9500
$clearLabel = ([char]0x6E05).ToString() + [char]0x9664
$resetConfirmationLabel = ([char]0x91CD).ToString() + [char]0x65B0 + [char]0x5F00 +
    [char]0x59CB + [char]0x7EDF + [char]0x8BA1 + [char]0xFF1F
$cancelLabel = ([char]0x53D6).ToString() + [char]0x6D88
$settingsCancelLabel = $cancelLabel + ([char]0x8BBE).ToString() + [char]0x7F6E

function Get-WindowRectangle([IntPtr]$WindowHandle) {
    $rectangle = [LiangWenPeakBalanceUiTests.NativeMethods+Rect]::new()
    if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::GetPhysicalWindowRect($WindowHandle, [ref]$rectangle)) {
        throw 'GetWindowRect failed.'
    }
    return $rectangle
}

function Get-ClientRectangle([IntPtr]$WindowHandle) {
    $rectangle = [LiangWenPeakBalanceUiTests.NativeMethods+Rect]::new()
    if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::GetPhysicalClientRect(
            $WindowHandle,
            [ref]$rectangle)) {
        throw 'GetClientRect failed.'
    }
    return $rectangle
}

function Get-ClientBottom([IntPtr]$WindowHandle) {
    $point = [LiangWenPeakBalanceUiTests.NativeMethods+Point]::new()
    if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::GetPhysicalClientBottom(
            $WindowHandle,
            [ref]$point)) {
        throw 'Unable to map the client-area bottom edge to screen coordinates.'
    }
    return $point
}

function Get-MonitorWorkArea([IntPtr]$WindowHandle) {
    $rectangle = [LiangWenPeakBalanceUiTests.NativeMethods+Rect]::new()
    if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::GetPhysicalMonitorWorkArea(
            $WindowHandle,
            [ref]$rectangle)) {
        throw 'Unable to read the settings window monitor work area.'
    }
    return $rectangle
}

function Test-SameRectangle($Left, $Right) {
    return $Left.Left -eq $Right.Left -and
        $Left.Top -eq $Right.Top -and
        $Left.Right -eq $Right.Right -and
        $Left.Bottom -eq $Right.Bottom
}

function Find-Element(
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

function Invoke-Element([Windows.Automation.AutomationElement]$Element) {
    $pattern = $Element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Find-InvokableElementByName(
    [Windows.Automation.AutomationElement]$Root,
    [string]$Name,
    [int]$TimeoutMilliseconds = 5000) {

    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::NameProperty,
        $Name)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        $offscreenCandidate = $null
        for ($index = 0; $index -lt $elements.Count; ++$index) {
            $candidate = $elements.Item($index)
            $invokePattern = $null
            if ($candidate.Current.IsEnabled -and
                $candidate.TryGetCurrentPattern(
                    [Windows.Automation.InvokePattern]::Pattern,
                    [ref]$invokePattern)) {
                if (-not $candidate.Current.IsOffscreen) {
                    return $candidate
                }
                if ($null -eq $offscreenCandidate) {
                    $offscreenCandidate = $candidate
                }
            }
        }

        if ($null -ne $offscreenCandidate) {
            return $offscreenCandidate
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Find-ToggleElementByName(
    [Windows.Automation.AutomationElement]$Root,
    [string]$Name,
    [int]$TimeoutMilliseconds = 5000) {

    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::NameProperty,
        $Name)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        for ($index = 0; $index -lt $elements.Count; ++$index) {
            $candidate = $elements.Item($index)
            $togglePattern = $null
            if ($candidate.Current.IsEnabled -and
                -not $candidate.Current.IsOffscreen -and
                $candidate.TryGetCurrentPattern(
                    [Windows.Automation.TogglePattern]::Pattern,
                    [ref]$togglePattern)) {
                return $candidate
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Find-WindowByName([string]$Name, [int]$TimeoutMilliseconds = 5000) {
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::NameProperty,
            $Name),
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::ControlTypeProperty,
            [Windows.Automation.ControlType]::Window))
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $window = [Windows.Automation.AutomationElement]::RootElement.FindFirst(
            [Windows.Automation.TreeScope]::Descendants,
            $condition)
        if ($null -ne $window) {
            return $window
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Find-ToggleElementByAutomationId(
    [Windows.Automation.AutomationElement]$Root,
    [string]$AutomationId,
    [int]$TimeoutMilliseconds = 5000) {

    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::AutomationIdProperty,
        $AutomationId)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        for ($index = 0; $index -lt $elements.Count; ++$index) {
            $candidate = $elements.Item($index)
            $togglePattern = $null
            if ($candidate.Current.IsEnabled -and
                -not $candidate.Current.IsOffscreen -and
                $candidate.TryGetCurrentPattern(
                    [Windows.Automation.TogglePattern]::Pattern,
                    [ref]$togglePattern)) {
                return $candidate
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Assert-ElementInsideWindow(
    [Windows.Automation.AutomationElement]$Element,
    $WindowRectangle,
    [string]$Name) {

    $scrollPattern = $null
    if ($Element.TryGetCurrentPattern(
            [Windows.Automation.ScrollItemPattern]::Pattern,
            [ref]$scrollPattern)) {
        $scrollPattern.ScrollIntoView()
        Start-Sleep -Milliseconds 100
    }
    $bounds = $Element.Current.BoundingRectangle
    if ($Element.Current.IsOffscreen -or $bounds.IsEmpty -or
        $bounds.Left -lt $WindowRectangle.Left -or
        $bounds.Top -lt $WindowRectangle.Top -or
        $bounds.Right -gt $WindowRectangle.Right -or
        $bounds.Bottom -gt $WindowRectangle.Bottom) {
        throw "Element '$Name' is clipped or outside its settings window. Element=$($bounds.Left),$($bounds.Top),$($bounds.Right),$($bounds.Bottom); Window=$($WindowRectangle.Left),$($WindowRectangle.Top),$($WindowRectangle.Right),$($WindowRectangle.Bottom); Offscreen=$($Element.Current.IsOffscreen)."
    }
}

function Assert-ElementRightGutter(
    [Windows.Automation.AutomationElement]$Element,
    [Windows.Automation.AutomationElement]$ScrollRegion,
    [int]$Dpi,
    [double]$CoordinateScale,
    [string]$Name) {

    $scrollPattern = $null
    if ($Element.TryGetCurrentPattern(
            [Windows.Automation.ScrollItemPattern]::Pattern,
            [ref]$scrollPattern)) {
        $scrollPattern.ScrollIntoView()
        Start-Sleep -Milliseconds 100
    }

    $elementBounds = $Element.Current.BoundingRectangle
    $scrollBounds = $ScrollRegion.Current.BoundingRectangle
    $minimumGutter = [int][Math]::Floor(
        (12.0 * $Dpi / 96.0) / $CoordinateScale)
    $actualGutter = $scrollBounds.Right - $elementBounds.Right
    if ($actualGutter -lt $minimumGutter) {
        throw "Settings control '$Name' has only $actualGutter physical pixels before the scrollbar region; expected at least $minimumGutter."
    }
}

function Get-ExpectedMainWindowClientHeight(
    [bool]$ApiEnabled,
    [bool]$ForecastEnabled,
    [bool]$UpdateTimeVisible) {

    $height = 173
    if ($ApiEnabled) {
        $height += 23 + 4
        if ($ForecastEnabled) {
            $height += 46
        }
        if ($UpdateTimeVisible) {
            $height += 13
        }
    }
    return $height
}

function Assert-UpdateTimeBottomPadding(
    [Windows.Automation.AutomationElement]$UpdateTime,
    [Windows.Automation.AutomationElement]$Window,
    [IntPtr]$WindowHandle,
    [int]$Dpi) {

    $textBounds = $UpdateTime.Current.BoundingRectangle
    $automationWindowBounds = $Window.Current.BoundingRectangle
    $physicalWindowBounds = Get-WindowRectangle $WindowHandle
    $clientBottom = Get-ClientBottom $WindowHandle
    $requiredPadding = [int][Math]::Floor((7 * $Dpi + 48) / 96)
    $automationHeight = $automationWindowBounds.Bottom - $automationWindowBounds.Top
    if ($automationHeight -le 0) {
        throw 'Main window has an invalid UI Automation bounding rectangle.'
    }
    $coordinateScale =
        ($physicalWindowBounds.Bottom - $physicalWindowBounds.Top) / $automationHeight
    $physicalTextBottom = $physicalWindowBounds.Top +
        (($textBounds.Bottom - $automationWindowBounds.Top) * $coordinateScale)
    $actualPadding = $clientBottom.Y - $physicalTextBottom
    if ($UpdateTime.Current.IsOffscreen -or $textBounds.IsEmpty -or
        $actualPadding -lt $requiredPadding) {
        throw "UpdateStatusText is clipped or lacks bottom padding. TextBottom=$physicalTextBottom; ClientBottom=$($clientBottom.Y); Padding=$actualPadding; Required=$requiredPadding."
    }
}

function Set-TestSettings([bool]$ApiEnabled, [bool]$ForecastEnabled) {
    $key = $TestProfile.RegistryPath
    New-Item -Path $key -Force | Out-Null
    New-ItemProperty -Path $key -Name ApiFeatureEnabled -PropertyType DWord -Value ([int]$ApiEnabled) -Force | Out-Null
    New-ItemProperty -Path $key -Name BalanceForecastEnabled -PropertyType DWord -Value ([int]$ForecastEnabled) -Force | Out-Null
    New-ItemProperty -Path $key -Name NotificationEnabled -PropertyType DWord -Value 0 -Force | Out-Null
    New-ItemProperty -Path $key -Name NotificationAdvanceEnabled -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $key -Name NotificationAdvanceMinutes -PropertyType DWord -Value 10 -Force | Out-Null
}

function Start-TestApplication([string]$ApplicationPath, [string]$WorkingDirectory) {
    $process = Start-IsolatedTestProcess `
        -Profile $TestProfile `
        -FilePath $ApplicationPath `
        -WorkingDirectory $WorkingDirectory
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    } while (-not $process.HasExited -and
        $process.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or $process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'LiangWenPeak main window did not become visible.'
    }

    $visibleDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 25
    } while (-not [LiangWenPeakBalanceUiTests.NativeMethods]::IsWindowReadyForInput(
            $process.MainWindowHandle) -and
        [DateTime]::UtcNow -lt $visibleDeadline)
    if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::IsWindowReadyForInput(
            $process.MainWindowHandle)) {
        throw 'LiangWenPeak main window did not finish its first-frame reveal.'
    }

    $actualDpi = [LiangWenPeakBalanceUiTests.NativeMethods]::GetDpiForWindow($process.MainWindowHandle)
    if ($actualDpi -ne $ExpectedDpi) {
        if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::MoveWindowToDpi(
                $process.MainWindowHandle,
                $ExpectedDpi)) {
            throw "No monitor with DPI $ExpectedDpi is available; initial window DPI is $actualDpi."
        }

        $dpiDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 100
            $actualDpi = [LiangWenPeakBalanceUiTests.NativeMethods]::GetDpiForWindow(
                $process.MainWindowHandle)
        } while ($actualDpi -ne $ExpectedDpi -and [DateTime]::UtcNow -lt $dpiDeadline)
        if ($actualDpi -ne $ExpectedDpi) {
            throw "Expected DPI $ExpectedDpi after moving the window, but the window is running at DPI $actualDpi."
        }
    }

    $stableRectangle = Get-WindowRectangle $process.MainWindowHandle
    $stableSince = [DateTime]::UtcNow
    $stabilityDeadline = [DateTime]::UtcNow.AddSeconds(4)
    do {
        Start-Sleep -Milliseconds 50
        $currentRectangle = Get-WindowRectangle $process.MainWindowHandle
        if (Test-SameRectangle $stableRectangle $currentRectangle) {
            $stableForOneTick = ([DateTime]::UtcNow - $stableSince).TotalMilliseconds -ge 1100
        } else {
            $stableRectangle = $currentRectangle
            $stableSince = [DateTime]::UtcNow
            $stableForOneTick = $false
        }
    } while (-not $stableForOneTick -and [DateTime]::UtcNow -lt $stabilityDeadline)
    if (-not $stableForOneTick) {
        throw 'Main window did not settle after its DPI transition.'
    }
    return $process
}

function Stop-TestApplication([Diagnostics.Process]$Process) {
    Stop-IsolatedTestProcess -Profile $TestProfile -Process $Process
}

function Assert-MainWindowState(
    [string]$ApplicationPath,
    [string]$WorkingDirectory,
    [bool]$ApiEnabled,
    [bool]$ForecastEnabled,
    [bool]$RequireUpdateTime) {

    Set-TestSettings -ApiEnabled $ApiEnabled -ForecastEnabled $ForecastEnabled
    $process = Start-TestApplication $ApplicationPath $WorkingDirectory
    try {
        $handle = $process.MainWindowHandle
        $dpi = [LiangWenPeakBalanceUiTests.NativeMethods]::GetDpiForWindow($handle)
        if ($dpi -ne $ExpectedDpi) {
            throw "Expected DPI $ExpectedDpi, but the main window is running at DPI $dpi."
        }
        $root = [Windows.Automation.AutomationElement]::FromHandle($handle)
        $layoutDeadline = [DateTime]::UtcNow.AddSeconds($(if ($RequireUpdateTime) { 20 } else { 5 }))
        do {
            $updateTime = Find-Element `
                $root `
                ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
                'UpdateStatusText' `
                0
            $updateTimeVisible = $null -ne $updateTime -and -not $updateTime.Current.IsOffscreen
            $expectedClientHeightDips = Get-ExpectedMainWindowClientHeight `
                $ApiEnabled `
                $ForecastEnabled `
                $updateTimeVisible
            $expectedClientHeight = [int][Math]::Floor(
                ($expectedClientHeightDips * $dpi + 48) / 96)
            $clientRectangle = Get-ClientRectangle $handle
            $actualClientHeight = $clientRectangle.Bottom - $clientRectangle.Top
            $layoutReady = $actualClientHeight -eq $expectedClientHeight -and
                (-not $RequireUpdateTime -or $updateTimeVisible)
            if (-not $layoutReady) {
                Start-Sleep -Milliseconds 50
            }
        } while (-not $layoutReady -and [DateTime]::UtcNow -lt $layoutDeadline)

        if (-not $layoutReady) {
            throw "Main window client height did not settle for API=$ApiEnabled forecast=$ForecastEnabled updateRequired=$RequireUpdateTime. Actual=$actualClientHeight; Expected=$expectedClientHeight; UpdateVisible=$updateTimeVisible."
        }

        $rectangle = Get-WindowRectangle $handle
        $expectedWidth = [int][Math]::Floor((256 * $dpi + 48) / 96)
        if (($rectangle.Right - $rectangle.Left) -ne $expectedWidth) {
            throw "Dynamic height changed the main window width for API=$ApiEnabled forecast=${ForecastEnabled}: $($rectangle.Right - $rectangle.Left), expected $expectedWidth."
        }

        $apiBalance = Find-Element $root ([Windows.Automation.AutomationElement]::NameProperty) $apiBalanceLabel 300
        $apiBurn = Find-Element $root ([Windows.Automation.AutomationElement]::NameProperty) $apiBurnLabel 300
        $eta = Find-Element $root ([Windows.Automation.AutomationElement]::NameProperty) $etaLabel 300
        if ($ApiEnabled -ne ($null -ne $apiBalance)) {
            throw 'API section visibility does not match the persisted feature state.'
        }
        if (($ApiEnabled -and $ForecastEnabled) -ne ($null -ne $apiBurn -and $null -ne $eta)) {
            throw 'Forecast section visibility does not match the persisted feature state.'
        }
        if ($updateTimeVisible) {
            Assert-UpdateTimeBottomPadding $updateTime $root $handle $dpi
        }
        return $rectangle
    } finally {
        Stop-TestApplication $process
    }
}

$context = Get-BuildContext -Configuration $Configuration -RepositoryRoot $repositoryRoot
$sourceApplicationPath = Join-Path $context.BuildOutput 'LiangWenPeak.App.exe'
if (-not (Test-Path -LiteralPath $sourceApplicationPath -PathType Leaf)) {
    throw "Application build output was not found: $sourceApplicationPath"
}
$TestProfile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'balance-ui'
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
Write-Host "ISOLATED_TEST_RUN_ID=$($TestProfile.RunId)"

try {
    $offRectangle = Assert-MainWindowState $applicationPath $applicationDirectory $false $false $false
    $balanceRectangle = Assert-MainWindowState `
        $applicationPath `
        $applicationDirectory `
        $true `
        $false `
        $RequireObservedBalance.IsPresent
    $forecastRectangle = Assert-MainWindowState `
        $applicationPath `
        $applicationDirectory `
        $true `
        $true `
        $RequireObservedBalance.IsPresent
    if (($offRectangle.Right - $offRectangle.Left) -ne ($balanceRectangle.Right - $balanceRectangle.Left) -or
        ($offRectangle.Right - $offRectangle.Left) -ne ($forecastRectangle.Right - $forecastRectangle.Left)) {
        throw 'Dynamic height states changed the main window width.'
    }

    Set-TestSettings -ApiEnabled $true -ForecastEnabled $false
    Remove-ItemProperty `
        -Path $TestProfile.RegistryPath `
        -Name FluentThemeEnabled `
        -ErrorAction SilentlyContinue
    $application = Start-TestApplication $applicationPath $applicationDirectory
    try {
        $mainHandle = $application.MainWindowHandle
        $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($mainHandle)
        if ($RequireObservedBalance) {
            $observationDeadline = [DateTime]::UtcNow.AddSeconds(20)
            do {
                $updateTime = Find-Element `
                    $mainRoot `
                    ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
                    'UpdateStatusText' `
                    0
                $clientRectangle = Get-ClientRectangle $mainHandle
                $observedLayoutReady = $null -ne $updateTime -and
                    -not $updateTime.Current.IsOffscreen -and
                    ($clientRectangle.Bottom - $clientRectangle.Top) -eq
                        [int][Math]::Floor((213 * $ExpectedDpi + 48) / 96)
                if (-not $observedLayoutReady) {
                    Start-Sleep -Milliseconds 50
                }
            } while (-not $observedLayoutReady -and [DateTime]::UtcNow -lt $observationDeadline)
            if (-not $observedLayoutReady) {
                throw 'The balance-only settings test did not reach its observed layout state.'
            }
        }
        $before = Get-WindowRectangle $mainHandle
        # XamlControlsResources finishes wiring a Button-owned flyout just after
        # the first stable frame; invoking it earlier is accepted but does not
        # open the menu on a cold self-contained launch.
        Start-Sleep -Seconds 1
        $more = Find-Element $mainRoot ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'MoreButton'
        if ($null -eq $more) {
            throw 'More menu button was not found.'
        }
        Invoke-Element $more
        $windowsBuild = [int](Get-ItemPropertyValue `
            -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' `
            -Name CurrentBuildNumber)
        $fluentTheme = Find-ToggleElementByName `
            ([Windows.Automation.AutomationElement]::RootElement) `
            $fluentThemeLabel `
            $(if ($windowsBuild -ge 22000) { 5000 } else { 500 })
        if ($windowsBuild -ge 22000) {
            if ($null -eq $fluentTheme) {
                $menuItems = [Windows.Automation.AutomationElement]::RootElement.FindAll(
                    [Windows.Automation.TreeScope]::Descendants,
                    [Windows.Automation.PropertyCondition]::new(
                        [Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [Windows.Automation.ControlType]::MenuItem))
                for ($menuIndex = 0; $menuIndex -lt $menuItems.Count; ++$menuIndex) {
                    $menuCandidate = $menuItems.Item($menuIndex)
                    if ($menuCandidate.Current.ProcessId -eq $application.Id -and
                        -not $menuCandidate.Current.IsOffscreen) {
                        Write-Host "VISIBLE_TEST_MENU_ITEM: Name='$($menuCandidate.Current.Name)' Id='$($menuCandidate.Current.AutomationId)'"
                    }
                }
                throw 'Windows 11 did not expose the Fluent theme menu item.'
            }
            $toggle = $fluentTheme.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
            if ($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::On) {
                throw 'Fluent theme was not enabled by default on Windows 11.'
            }
            $toggle.Toggle()
            Start-Sleep -Milliseconds 150
            if ((Get-ItemPropertyValue `
                    -Path $TestProfile.RegistryPath `
                    -Name FluentThemeEnabled) -ne 0) {
                throw 'Disabling Fluent theme was not persisted.'
            }

            $more = Find-Element `
                $mainRoot `
                ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
                'MoreButton'
            Invoke-Element $more
            $fluentTheme = Find-ToggleElementByName `
                ([Windows.Automation.AutomationElement]::RootElement) `
                $fluentThemeLabel
            if ($null -eq $fluentTheme) {
                throw 'Fluent theme menu item disappeared after it was disabled.'
            }
            $toggle = $fluentTheme.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
            if ($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off) {
                throw 'Fluent theme did not switch off in the UI.'
            }
            $toggle.Toggle()
            Start-Sleep -Milliseconds 150
            if ((Get-ItemPropertyValue `
                    -Path $TestProfile.RegistryPath `
                    -Name FluentThemeEnabled) -ne 1) {
                throw 'Re-enabling Fluent theme was not persisted.'
            }
            $more = Find-Element `
                $mainRoot `
                ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
                'MoreButton'
            Invoke-Element $more
        } elseif ($null -ne $fluentTheme) {
            throw 'Windows 10 exposed the Fluent theme menu item instead of hiding it.'
        }

        $notificationMenu = Find-ToggleElementByAutomationId `
            ([Windows.Automation.AutomationElement]::RootElement) `
            'NotificationMenuItem'
        if ($null -eq $notificationMenu) {
            throw 'Notification quick-toggle menu item was not found.'
        }
        $notificationMenuToggle = $notificationMenu.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern)
        if ($notificationMenuToggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off) {
            throw 'Notification quick toggle did not reflect the persisted disabled default.'
        }
        $notificationMenuToggle.Toggle()
        Start-Sleep -Milliseconds 150
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 1) {
            throw 'Notification quick toggle did not immediately persist enablement.'
        }

        Invoke-Element $more
        $notificationMenu = Find-ToggleElementByAutomationId `
            ([Windows.Automation.AutomationElement]::RootElement) `
            'NotificationMenuItem'
        $notificationMenuToggle = $notificationMenu.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern)
        if ($notificationMenuToggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::On) {
            throw 'Notification quick toggle did not synchronize after persistence.'
        }
        $notificationMenuToggle.Toggle()
        Start-Sleep -Milliseconds 150
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 0) {
            throw 'Notification quick toggle did not immediately persist disablement.'
        }
        Invoke-Element $more

        $settingsMenu = Find-InvokableElementByName `
            ([Windows.Automation.AutomationElement]::RootElement) `
            $settingsMenuLabel
        if ($null -eq $settingsMenu) {
            throw 'Settings menu item was not found.'
        }

        Invoke-Element $settingsMenu
        $settingsWindow = Find-WindowByName $settingsTitle
        if ($null -eq $settingsWindow) {
            throw 'Owned settings window was not found.'
        }
        $settingsHandle = [IntPtr]$settingsWindow.Current.NativeWindowHandle
        $settingsRectangle = $settingsWindow.Current.BoundingRectangle
        $settingsPhysicalRectangle = Get-WindowRectangle $settingsHandle
        $settingsWorkArea = Get-MonitorWorkArea $settingsHandle
        $settingsWindowDpi = [LiangWenPeakBalanceUiTests.NativeMethods]::GetDpiForWindow(
            $settingsHandle)
        $settingsHeight = $settingsPhysicalRectangle.Bottom - $settingsPhysicalRectangle.Top
        $workAreaHeight = $settingsWorkArea.Bottom - $settingsWorkArea.Top
        $maximumSettingsHeight = [int][Math]::Floor($workAreaHeight * 2.0 / 3.0)
        $settingsAutomationHeight = $settingsRectangle.Bottom - $settingsRectangle.Top
        if ($settingsAutomationHeight -le 0) {
            throw 'Settings window has an invalid UI Automation bounding rectangle.'
        }
        $settingsCoordinateScale = $settingsHeight / $settingsAutomationHeight
        if ($settingsWindowDpi -ne $ExpectedDpi -or
            $settingsHeight -gt $maximumSettingsHeight) {
            throw "Settings window violates its DPI/work-area height policy. DPI=$settingsWindowDpi; ExpectedDPI=$ExpectedDpi; Height=$settingsHeight; WorkAreaHeight=$workAreaHeight; Maximum=$maximumSettingsHeight."
        }
        $owner = [LiangWenPeakBalanceUiTests.NativeMethods]::GetWindowLongPtr($settingsHandle, -8)
        $extendedStyle = [LiangWenPeakBalanceUiTests.NativeMethods]::GetWindowLongPtr($settingsHandle, -20).ToInt64()
        $mainExtendedStyle = [LiangWenPeakBalanceUiTests.NativeMethods]::GetWindowLongPtr($mainHandle, -20).ToInt64()
        if ($owner -ne $mainHandle -or
            ($extendedStyle -band 0x80) -eq 0 -or
            ($extendedStyle -band 0x40000) -ne 0) {
            throw 'Settings window is not an owned, taskbar-hidden tool window.'
        }
        if (($extendedStyle -band 0x8) -eq 0 -or ($mainExtendedStyle -band 0x8) -eq 0) {
            throw 'Settings and main windows are not both in the topmost Z-order band.'
        }

        $foregroundDeadline = [DateTime]::UtcNow.AddSeconds(3)
        do {
            Start-Sleep -Milliseconds 50
        } while ([LiangWenPeakBalanceUiTests.NativeMethods]::GetForegroundWindow() -ne $settingsHandle -and
            [DateTime]::UtcNow -lt $foregroundDeadline)
        if ([LiangWenPeakBalanceUiTests.NativeMethods]::GetForegroundWindow() -ne $settingsHandle) {
            throw 'Settings window did not receive foreground activation.'
        }
        if ($mainRoot.Current.IsEnabled) {
            throw 'Modal-like settings editing did not disable the owner window.'
        }
        if (-not (Test-SameRectangle $before (Get-WindowRectangle $mainHandle))) {
            $during = Get-WindowRectangle $mainHandle
            throw "Opening settings changed the main window rectangle. Before=$($before.Left),$($before.Top),$($before.Right),$($before.Bottom); During=$($during.Left),$($during.Top),$($during.Right),$($during.Bottom)."
        }

        $scrollRegion = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::NameProperty) `
            $settingsScrollRegionLabel
        if ($null -eq $scrollRegion) {
            throw 'Settings scroll region was not found.'
        }
        $settingsScrollPattern = $null
        if (-not $scrollRegion.TryGetCurrentPattern(
                [Windows.Automation.ScrollPattern]::Pattern,
                [ref]$settingsScrollPattern) -or
            -not $settingsScrollPattern.Current.VerticallyScrollable) {
            throw 'Settings content does not expose the required native vertical scrolling.'
        }

        $apiSectionHeading = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'ApiBalanceSectionHeading'
        $apiKeySubheading = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'ApiKeySubheading'
        $apiFeatureControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'ApiFeatureToggle'
        $apiKeyControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'ApiKeyBox'
        if ($null -eq $apiSectionHeading -or
            $null -eq $apiKeySubheading -or
            $null -eq $apiFeatureControl -or
            $null -eq $apiKeyControl) {
            throw 'API settings hierarchy controls were not found.'
        }
        $apiSectionScrollItem = $null
        if ($apiSectionHeading.TryGetCurrentPattern(
                [Windows.Automation.ScrollItemPattern]::Pattern,
                [ref]$apiSectionScrollItem)) {
            $apiSectionScrollItem.ScrollIntoView()
            Start-Sleep -Milliseconds 100
        }
        $apiSectionBounds = $apiSectionHeading.Current.BoundingRectangle
        $apiKeySubheadingBounds = $apiKeySubheading.Current.BoundingRectangle
        $apiFeatureBounds = $apiFeatureControl.Current.BoundingRectangle
        $apiKeyBounds = $apiKeyControl.Current.BoundingRectangle
        if ($apiSectionBounds.Bottom -gt $apiKeySubheadingBounds.Top -or
            $apiKeySubheadingBounds.Bottom -gt $apiFeatureBounds.Top -or
            $apiFeatureBounds.Bottom -gt $apiKeyBounds.Top) {
            throw 'API settings visual order is not API 与余额 -> API Key -> enable toggle -> API Key input.'
        }

        $scrollContentIds = @(
            'ApiFeatureToggle',
            'ApiKeyBox',
            'ClearApiKeyButton',
            'CurrencyBox',
            'WarningBalanceBox',
            'RefreshIntervalBox',
            'RateWindowBox',
            'ForecastEnabledBox',
            'AlgorithmBox',
            'NotificationEnabledToggle',
            'AdvanceReminderToggle',
            'AdvanceMinutesBox',
            'TestNotificationButton',
            'CleanupButton')
        foreach ($automationId in $scrollContentIds) {
            $control = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::AutomationIdProperty) $automationId
            if ($null -eq $control) {
                throw "Settings control '$automationId' was not found."
            }
            if ($control.Current.IsOffscreen) {
                $scrollItem = $null
                if ($control.TryGetCurrentPattern(
                        [Windows.Automation.ScrollItemPattern]::Pattern,
                        [ref]$scrollItem)) {
                    $scrollItem.ScrollIntoView()
                    Start-Sleep -Milliseconds 100
                }
            }
            Assert-ElementInsideWindow $control $settingsRectangle $automationId
            Assert-ElementRightGutter `
                $control `
                $scrollRegion `
                $ExpectedDpi `
                $settingsCoordinateScale `
                $automationId
        }

        foreach ($toggleId in @(
            'ApiFeatureToggle',
            'ForecastEnabledBox',
            'NotificationEnabledToggle',
            'AdvanceReminderToggle')) {
            $toggleControl = Find-Element `
                $settingsWindow `
                ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
                $toggleId
            $togglePattern = $null
            if ($null -eq $toggleControl -or
                -not $toggleControl.TryGetCurrentPattern(
                    [Windows.Automation.TogglePattern]::Pattern,
                    [ref]$togglePattern)) {
                throw "Settings control '$toggleId' is not an accessible native ToggleSwitch."
            }
        }

        foreach ($automationId in @(
            'ResetStatisticsButton',
            'CleanupButton',
            'SaveButton',
            'CancelButton')) {
            $control = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::AutomationIdProperty) $automationId
            if ($null -eq $control) {
                throw "Settings control '$automationId' was not found."
            }
            Assert-ElementInsideWindow $control $settingsRectangle $automationId
        }

        $notificationToggleControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'NotificationEnabledToggle'
        $advanceToggleControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'AdvanceReminderToggle'
        $advanceMinutesControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'AdvanceMinutesBox'
        $testNotification = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'TestNotificationButton'
        $notificationDraftToggle = $notificationToggleControl.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern)
        if ($notificationDraftToggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off -or
            $advanceToggleControl.Current.IsEnabled -or
            $advanceMinutesControl.Current.IsEnabled -or
            -not $testNotification.Current.IsEnabled) {
            throw 'Default notification Draft control enablement is incorrect.'
        }
        $advanceRange = $advanceMinutesControl.GetCurrentPattern(
            [Windows.Automation.RangeValuePattern]::Pattern)
        if ($advanceMinutesControl.Current.ControlType -ne [Windows.Automation.ControlType]::Spinner -or
            $advanceRange.Current.Minimum -ne 1 -or
            $advanceRange.Current.Maximum -ne 30 -or
            $advanceRange.Current.SmallChange -ne 1 -or
            $advanceRange.Current.Value -ne 10) {
            throw 'Notification NumberBox does not expose the required 1-30 integer defaults.'
        }

        Invoke-Element $testNotification
        Start-Sleep -Milliseconds 150
        $notificationStatus = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'SettingsStatusText'
        if ($null -eq $notificationStatus -or [string]::IsNullOrWhiteSpace($notificationStatus.Current.Name)) {
            throw 'Test notification did not execute as an immediate action while notifications were disabled.'
        }

        $notificationDraftToggle.Toggle()
        Start-Sleep -Milliseconds 100
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 0) {
            throw 'Editing notificationEnabled in the settings Draft changed persisted state before Save.'
        }
        if (-not $advanceToggleControl.Current.IsEnabled -or
            -not $advanceMinutesControl.Current.IsEnabled -or
            -not $testNotification.Current.IsEnabled) {
            throw 'Enabling the notification Draft did not enable advance controls or preserve the test action.'
        }
        $advanceDraftToggle = $advanceToggleControl.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern)
        $advanceDraftToggle.Toggle()
        Start-Sleep -Milliseconds 100
        if ($advanceMinutesControl.Current.IsEnabled) {
            throw 'Disabling advance reminders did not disable the NumberBox.'
        }
        $advanceDraftToggle.Toggle()
        Start-Sleep -Milliseconds 100
        if (-not $advanceMinutesControl.Current.IsEnabled) {
            throw 'Re-enabling advance reminders did not restore the NumberBox.'
        }

        $advanceScrollItem = $null
        if ($advanceMinutesControl.TryGetCurrentPattern(
                [Windows.Automation.ScrollItemPattern]::Pattern,
                [ref]$advanceScrollItem)) {
            $advanceScrollItem.ScrollIntoView()
            Start-Sleep -Milliseconds 100
        }
        $upSpinButton = Find-Element `
            $advanceMinutesControl `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'UpSpinButton'
        $downSpinButton = Find-Element `
            $advanceMinutesControl `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'DownSpinButton'
        if ($null -eq $upSpinButton -or
            $null -eq $downSpinButton -or
            $upSpinButton.Current.ControlType -ne [Windows.Automation.ControlType]::Button -or
            $downSpinButton.Current.ControlType -ne [Windows.Automation.ControlType]::Button -or
            $upSpinButton.Current.IsOffscreen -or
            $downSpinButton.Current.IsOffscreen) {
            throw 'NumberBox does not expose its native inline Up/Down spin buttons.'
        }

        $advanceRange.SetValue(10)
        Invoke-Element $upSpinButton
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 11) {
            throw 'NumberBox native Up spin button did not increment by SmallChange=1.'
        }
        Invoke-Element $downSpinButton
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 10) {
            throw 'NumberBox native Down spin button did not decrement by SmallChange=1.'
        }

        $advanceRange.SetValue(29)
        Invoke-Element $upSpinButton
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 30 -or $upSpinButton.Current.IsEnabled) {
            throw 'NumberBox native upper boundary is not enforced at 30.'
        }
        $advanceRange.SetValue(2)
        Invoke-Element $downSpinButton
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 1 -or $downSpinButton.Current.IsEnabled) {
            throw 'NumberBox native lower boundary is not enforced at 1.'
        }

        $advanceInput = Find-Element `
            $advanceMinutesControl `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'InputBox'
        $advanceInputValue = $null
        if ($null -eq $advanceInput -or
            -not $advanceInput.TryGetCurrentPattern(
                [Windows.Automation.ValuePattern]::Pattern,
                [ref]$advanceInputValue)) {
            throw 'NumberBox does not expose its native editable input field.'
        }
        $advanceInputValue.SetValue('17')
        $notificationToggleControl.SetFocus()
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 17) {
            throw 'NumberBox did not accept integer keyboard-style input.'
        }

        $advanceInput.SetFocus()
        if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::PostMessage(
                $settingsHandle,
                0x0100,
                [IntPtr]0x26,
                [IntPtr]0x01480001) -or
            -not [LiangWenPeakBalanceUiTests.NativeMethods]::PostMessage(
                $settingsHandle,
                0x0101,
                [IntPtr]0x26,
                [IntPtr]([long]0xC1480001))) {
            throw 'Unable to deliver the isolated NumberBox Up key validation.'
        }
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 18) {
            throw 'NumberBox did not handle the Up key.'
        }
        if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::PostMessage(
                $settingsHandle,
                0x0100,
                [IntPtr]0x28,
                [IntPtr]0x01500001) -or
            -not [LiangWenPeakBalanceUiTests.NativeMethods]::PostMessage(
                $settingsHandle,
                0x0101,
                [IntPtr]0x28,
                [IntPtr]([long]0xC1500001))) {
            throw 'Unable to deliver the isolated NumberBox Down key validation.'
        }
        Start-Sleep -Milliseconds 100
        if ($advanceRange.Current.Value -ne 17) {
            throw 'NumberBox did not handle the Down key.'
        }
        $advanceRange.SetValue(10)

        $clear = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'ClearApiKeyButton'
        Invoke-Element $clear
        Start-Sleep -Milliseconds 100
        if ($clear.Current.Name -ne $undoLabel) {
            throw 'Clear API Key did not enter the reversible draft state.'
        }
        Invoke-Element $clear
        Start-Sleep -Milliseconds 100
        if ($clear.Current.Name -ne $clearLabel) {
            throw 'Undo did not restore the API Key draft state.'
        }

        foreach ($comboId in @('CurrencyBox', 'RefreshIntervalBox', 'RateWindowBox')) {
            $combo = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::AutomationIdProperty) $comboId
            $expand = $combo.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
            $expand.Expand()
            Start-Sleep -Milliseconds 100
            $expand.Collapse()
        }

        $reset = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'ResetStatisticsButton'
        Invoke-Element $reset
        $confirm = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::NameProperty) $resetConfirmationLabel
        if ($null -eq $confirm) {
            throw 'Reset statistics confirmation did not appear.'
        }
        $cancelButtons = $settingsWindow.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new(
                [Windows.Automation.AutomationElement]::NameProperty,
                $cancelLabel))
        $confirmationCancelled = $false
        $offscreenCancel = $null
        for ($index = 0; $index -lt $cancelButtons.Count; ++$index) {
            $candidate = $cancelButtons.Item($index)
            $invokePattern = $null
            if ($candidate.Current.AutomationId -ne 'CancelButton' -and
                $candidate.Current.IsEnabled -and
                $candidate.TryGetCurrentPattern(
                    [Windows.Automation.InvokePattern]::Pattern,
                    [ref]$invokePattern)) {
                if (-not $candidate.Current.IsOffscreen) {
                    $invokePattern.Invoke()
                    $confirmationCancelled = $true
                    break
                }
                if ($null -eq $offscreenCancel) {
                    $offscreenCancel = $invokePattern
                }
            }
        }
        if (-not $confirmationCancelled -and $null -ne $offscreenCancel) {
            $offscreenCancel.Invoke()
            $confirmationCancelled = $true
        }
        if (-not $confirmationCancelled) {
            throw 'Reset statistics confirmation Cancel button was not found.'
        }
        Start-Sleep -Milliseconds 300

        $cancel = Find-Element $settingsWindow ([Windows.Automation.AutomationElement]::NameProperty) $settingsCancelLabel
        Invoke-Element $cancel
        Start-Sleep -Milliseconds 1000
        $after = Get-WindowRectangle $mainHandle
        $ownerEnabled = [LiangWenPeakBalanceUiTests.NativeMethods]::IsWindowEnabled($mainHandle)
        $settingsStillExists = [LiangWenPeakBalanceUiTests.NativeMethods]::IsWindow($settingsHandle)
        $mainStyleAfter = [LiangWenPeakBalanceUiTests.NativeMethods]::GetWindowLongPtr($mainHandle, -20).ToInt64()
        if (-not (Test-SameRectangle $before $after) -or
            -not $ownerEnabled -or
            $settingsStillExists -or
            ($mainStyleAfter -band 0x8) -eq 0) {
            throw "Closing settings changed the main window or left the owner disabled. Before=$($before.Left),$($before.Top),$($before.Right),$($before.Bottom); After=$($after.Left),$($after.Top),$($after.Right),$($after.Bottom); Enabled=$ownerEnabled; SettingsExists=$settingsStillExists."
        }
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 0) {
            throw 'Cancel committed notificationEnabled instead of discarding the Draft.'
        }

        $more = Find-Element $mainRoot ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'MoreButton'
        Invoke-Element $more
        $notificationMenu = Find-ToggleElementByAutomationId `
            ([Windows.Automation.AutomationElement]::RootElement) `
            'NotificationMenuItem'
        if ($null -eq $notificationMenu -or
            $notificationMenu.GetCurrentPattern(
                [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne
                [Windows.Automation.ToggleState]::Off) {
            throw 'Notification menu did not remain synchronized after settings Cancel.'
        }
        $notificationMenu = $null
        $more = $null
        $mainRoot = $null
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
        $settingsMenu = Find-InvokableElementByName `
            ([Windows.Automation.AutomationElement]::RootElement) `
            $settingsMenuLabel
        Invoke-Element $settingsMenu
        $settingsWindow = Find-WindowByName $settingsTitle
        if ($null -eq $settingsWindow) {
            throw 'Settings window did not reopen for the Save check.'
        }
        $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($mainHandle)

        $notificationToggleControl = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'NotificationEnabledToggle'
        $notificationToggleControl.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern).Toggle()
        $save = Find-Element `
            $settingsWindow `
            ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            'SaveButton'
        Invoke-Element $save
        Start-Sleep -Milliseconds 500
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 1) {
            throw 'Settings Save did not commit notificationEnabled.'
        }

        $more = Find-Element $mainRoot ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'MoreButton'
        Invoke-Element $more
        $notificationMenu = Find-ToggleElementByAutomationId `
            ([Windows.Automation.AutomationElement]::RootElement) `
            'NotificationMenuItem'
        $notificationMenuToggle = $notificationMenu.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern)
        if ($notificationMenuToggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::On) {
            throw 'Notification menu did not synchronize after settings Save.'
        }
        $notificationMenuToggle.Toggle()
        Start-Sleep -Milliseconds 150
        if ((Get-ItemPropertyValue `
                -Path $TestProfile.RegistryPath `
                -Name NotificationEnabled) -ne 0) {
            throw 'Notification quick toggle did not disable the setting after Save.'
        }

        Invoke-Element $more
        $notificationMenuToggle = $null
        $notificationMenu = $null
        $more = $null
        $mainRoot = $null
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
        $settingsMenu = Find-InvokableElementByName `
            ([Windows.Automation.AutomationElement]::RootElement) `
            $settingsMenuLabel
        Invoke-Element $settingsMenu
        $settingsWindow = Find-WindowByName $settingsTitle
        if ($null -eq $settingsWindow) {
            throw 'Settings window did not reopen for the owner-close check.'
        }
        $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($mainHandle)
        $settingsHandle = [IntPtr]$settingsWindow.Current.NativeWindowHandle
        if (-not [LiangWenPeakBalanceUiTests.NativeMethods]::PostMessage(
                $mainHandle,
                0x0010,
                [IntPtr]::Zero,
                [IntPtr]::Zero)) {
            throw 'Unable to post WM_CLOSE to MainWindow.'
        }
        if (-not $application.WaitForExit(5000) -or
            [LiangWenPeakBalanceUiTests.NativeMethods]::IsWindow($settingsHandle)) {
            throw 'Closing MainWindow did not synchronously close its settings window.'
        }
    } finally {
        Stop-TestApplication $application
    }

    Write-Host "PASS: balance extension UI at $([int]($ExpectedDpi / 96 * 100))% DPI"
    Write-Host 'Main window client heights: 173; 200/213; 246/259 DIP (update hidden/visible)'
} finally {
    Remove-IsolatedTestProfile -Profile $TestProfile -RepositoryRoot $repositoryRoot
    $global:LASTEXITCODE = 0
}
