[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName Accessibility
Add-Type -ReferencedAssemblies ([Accessibility.IAccessible].Assembly.Location) -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TrayMenuAccessibility {
    [DllImport("oleacc.dll")]
    static extern int AccessibleObjectFromWindow(IntPtr window,uint objectId,
        ref Guid iid,[MarshalAs(UnmanagedType.Interface)] out Accessibility.IAccessible accessible);
    public static bool Invoke(IntPtr window,string name) {
        foreach(uint objectId in new uint[]{0xFFFFFFFC,0xFFFFFFFD}) {
            Guid iid=typeof(Accessibility.IAccessible).GUID;
            Accessibility.IAccessible menu;
            if(AccessibleObjectFromWindow(window,objectId,ref iid,out menu)!=0) continue;
            try {
                for(int child=1;child<=menu.accChildCount;++child) {
                    if(menu.get_accName(child)==name) {menu.accDoDefaultAction(child);return true;}
                }
            } finally {Marshal.ReleaseComObject(menu);}
        }
        return false;
    }
}
'@
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TrayStartupNative {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr window,int index);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [StructLayout(LayoutKind.Sequential)] public struct Point {public int X,Y;}
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
    public static bool RightClick(int x,int y,int processId) {
        uint actual;GetWindowThreadProcessId(WindowFromPoint(new Point {X=x,Y=y}),out actual);
        if(actual!=processId || !SetCursorPos(x,y)) return false;
        mouse_event(0x0008,0,0,0,UIntPtr.Zero);
        mouse_event(0x0010,0,0,0,UIntPtr.Zero);
        return true;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
    [StructLayout(LayoutKind.Sequential)] public struct IconId {public uint Size;public IntPtr Window;public uint Id;public Guid Guid;}
    [DllImport("shell32.dll")] static extern int Shell_NotifyIconGetRect(ref IconId id,out Rect rect);
    public static bool HasTrayIcon(IntPtr window) {
        IconId id=new IconId {Size=(uint)Marshal.SizeOf(typeof(IconId)),Window=window,Id=1};
        Rect rect; return Shell_NotifyIconGetRect(ref id,out rect)==0;
    }
    public static IntPtr FindMainWindow(int processId) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window,IntPtr data) {
            uint process; GetWindowThreadProcessId(window,out process);
            var text=new System.Text.StringBuilder(256);GetWindowText(window,text,256);
            if(process==processId && text.ToString()=="梁文峰时钟") {result=window;return false;}
            return true;
        },IntPtr.Zero);
        return result;
    }
    public static IntPtr FindPopupMenu(int processId) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window,IntPtr data) {
            uint process;GetWindowThreadProcessId(window,out process);
            var text=new System.Text.StringBuilder(256);GetClassName(window,text,256);
            if(process==processId && text.ToString()=="#32768") {result=window;return false;}
            return true;
        },IntPtr.Zero);
        return result;
    }
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,System.Text.StringBuilder text,int count);
}
'@

function Find-Control($Root, [string]$Id) {
    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        $control = $Root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        if ($null -ne $control) { return $control }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Missing test control: $Id"
}
function Invoke-Control($Control) {
    $scroll=$null
    if($Control.TryGetCurrentPattern([Windows.Automation.ScrollItemPattern]::Pattern,[ref]$scroll)) {
        $scroll.ScrollIntoView(); Start-Sleep -Milliseconds 100
    }
    $Control.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 200
}
function Get-Toggle($Control) {
    $Control.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
}
function Find-ByName($Root, [string]$Name, [int]$ProcessId) {
    $condition=[Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$ProcessId),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$Name))
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        $popup=[TrayStartupNative]::FindPopupMenu($ProcessId)
        if($popup -ne [IntPtr]::Zero -and $Name -in @('打开主窗口','退出')) {
            $Root=[Windows.Automation.AutomationElement]::FromHandle($popup)
        }
        $control=$Root.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        if($null -ne $control){return $control}
        Start-Sleep -Milliseconds 50
    } while([DateTime]::UtcNow -lt $deadline)
    throw "Missing named test control: $Name"
}
function Invoke-OneShot($App, [string]$Name) {
    $root=[Windows.Automation.AutomationElement]::FromHandle($App.Handle)
    [void][TrayStartupNative]::SetForegroundWindow($App.Handle)
    (Find-Control $root 'MinimizeButton').SetFocus()
    [Windows.Forms.SendKeys]::SendWait('+{F10}')
    Start-Sleep -Milliseconds 200
    if([TrayStartupNative]::IsIconic($App.Handle) -or -not [TrayStartupNative]::IsWindowVisible($App.Handle)) {
        throw 'Context menu also executed the default minimize action.'
    }
    Invoke-Control (Find-ByName ([Windows.Automation.AutomationElement]::RootElement) $Name $App.Process.Id)
}
function Invoke-TrayCommand($App, [string]$Name) {
    [void][TrayStartupNative]::PostMessage($App.Handle,0x802A,[IntPtr]::Zero,[IntPtr]0x1007B)
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    do {
        $popup=[TrayStartupNative]::FindPopupMenu($App.Process.Id)
        if($popup -ne [IntPtr]::Zero -and [TrayMenuAccessibility]::Invoke($popup,$Name)) {
            Start-Sleep -Milliseconds 300
            return
        }
        Start-Sleep -Milliseconds 50
    } while([DateTime]::UtcNow -lt $deadline)
    throw "Test-owned native tray menu action missing: $Name"
}
function Open-Settings($Process, [IntPtr]$Handle) {
    $root=[Windows.Automation.AutomationElement]::FromHandle($Handle)
    Invoke-Control (Find-Control $root 'MoreButton')
    $condition=[Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$Process.Id),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'SettingsMenuItem'))
    $menu=[Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
    Invoke-Control $menu
    $condition=[Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$Process.Id),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'设置'))
    $deadline=[DateTime]::UtcNow.AddSeconds(6)
    do {
        $settings=[Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        if($null -ne $settings){return $settings}
        Start-Sleep -Milliseconds 50
    } while([DateTime]::UtcNow -lt $deadline)
    throw 'Test settings window missing.'
}
function Start-TestApp([string]$Arguments = '') {
    $process=Start-IsolatedPortableApplication -Profile $profile -LauncherPath $profile.ExpectedLauncherPath `
        -ExpectedApplicationPath $applicationPath -WorkingDirectory $profile.PortableRoot -Arguments $Arguments
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do {
        $handle=[TrayStartupNative]::FindMainWindow($process.Id)
        if($handle -ne [IntPtr]::Zero){break}
        Start-Sleep -Milliseconds 100
    } while([DateTime]::UtcNow -lt $deadline)
    if($handle -eq [IntPtr]::Zero){throw 'Test App HWND missing.'}
    Start-Sleep -Seconds 2
    return [PSCustomObject]@{Process=$process;Handle=$handle}
}
function Restore-TestApp([IntPtr]$Handle) {
    # Deliver the version-4 Shell callback only to the explicitly owned HWND.
    [void][TrayStartupNative]::PostMessage($Handle,0x802A,[IntPtr]::Zero,[IntPtr]0x10400)
    Start-Sleep -Milliseconds 300
    if(-not [TrayStartupNative]::IsWindowVisible($Handle) -or [TrayStartupNative]::IsIconic($Handle) -or
        [TrayStartupNative]::HasTrayIcon($Handle)){throw 'Tray restore failed.'}
}

$context=Get-BuildContext -Configuration $Configuration -RepositoryRoot $repositoryRoot
$profile=New-IsolatedTestProfile -RepositoryRoot $repositoryRoot -TestArea 'tray-startup-ui'
try {
    $directory=Join-Path $profile.PortableRoot "app-$($context.Version)"
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Get-ChildItem -LiteralPath $context.BuildOutput -Force | Copy-Item -Destination $directory -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $context.BuildOutput 'LiangWenPeak.exe') -Destination $profile.ExpectedLauncherPath
    [IO.File]::WriteAllText((Join-Path $profile.PortableRoot 'current.txt'),$context.Version)
    $applicationPath=Join-Path $directory 'LiangWenPeak.App.exe'
    New-Item -Path $profile.RegistryPath -Force | Out-Null
    New-ItemProperty -Path $profile.RegistryPath -Name ApiFeatureEnabled -PropertyType DWord -Value 0 -Force | Out-Null
    Write-Host "ISOLATED_TEST_RUN_ID=$($profile.RunId)"
    $runPath=Join-Path $profile.RegistryPath 'StartupRun'
    $runName="LiangWenPeak.Test.$($profile.RunId)"

    $app=Start-TestApp
    try {
        $settings=Open-Settings $app.Process $app.Handle
        $startup=Find-Control $settings 'StartupEnabledBox'
        $hidden=Find-Control $settings 'StartupHiddenBox'
        if((Get-Toggle $startup).Current.ToggleState -ne 'Off' -or
            (Get-Toggle $hidden).Current.ToggleState -ne 'On' -or $hidden.Current.IsEnabled) {
            throw 'Startup defaults/cascade incorrect.'
        }
        (Get-Toggle $startup).Toggle(); Start-Sleep -Milliseconds 100
        if(-not $hidden.Current.IsEnabled){throw 'Child startup option remains disabled.'}
        (Get-Toggle $hidden).Toggle()
        (Get-Toggle $startup).Toggle(); Start-Sleep -Milliseconds 100
        if($hidden.Current.IsEnabled -or (Get-Toggle $hidden).Current.ToggleState -ne 'Off') {
            throw 'Parent reset child draft value.'
        }
        if(Test-Path $runPath){throw 'Draft wrote startup registration.'}
        Invoke-Control (Find-Control $settings 'CancelButton')
        if(Test-Path $runPath){throw 'Cancel wrote startup registration.'}

        $settings=Open-Settings $app.Process $app.Handle
        (Get-Toggle (Find-Control $settings 'StartupEnabledBox')).Toggle()
        Invoke-Control (Find-Control $settings 'SaveButton')
        $command=Get-ItemPropertyValue -Path $runPath -Name $runName
        if($command -ne ('"'+$profile.ExpectedLauncherPath+'" --startup')){throw 'Startup target is not stable Launcher.'}
        if((Get-ItemPropertyValue $profile.RegistryPath StartupEnabled) -ne 1){throw 'Startup Save did not persist.'}

        $root=[Windows.Automation.AutomationElement]::FromHandle($app.Handle)
        Invoke-Control (Find-Control $root 'MinimizeButton')
        if(-not [TrayStartupNative]::IsIconic($app.Handle)){throw 'Default minimize did not enter taskbar.'}
        [void][TrayStartupNative]::ShowWindow($app.Handle,9)
        [void][TrayStartupNative]::SetForegroundWindow($app.Handle)
        Start-Sleep -Milliseconds 200
        $button=Find-Control $root 'MinimizeButton'
        $physical=[TrayStartupNative+Rect]::new()
        [void][TrayStartupNative]::GetWindowRect($app.Handle,[ref]$physical)
        $rootBounds=$root.Current.BoundingRectangle
        $bounds=$button.Current.BoundingRectangle
        $scale=($physical.Right-$physical.Left)/$rootBounds.Width
        $x=[int]($physical.Left+($bounds.Left-$rootBounds.Left+$bounds.Width/2)*$scale)
        $y=[int]($physical.Top+($bounds.Top-$rootBounds.Top+$bounds.Height/2)*$scale)
        if(-not [TrayStartupNative]::RightClick($x,$y,$app.Process.Id)) {
            throw 'Refusing mouse input outside the test-owned window.'
        }
        Start-Sleep -Milliseconds 300
        if([TrayStartupNative]::IsIconic($app.Handle) -or -not [TrayStartupNative]::IsWindowVisible($app.Handle)) {
            throw 'Right click also triggered left-click default behavior.'
        }
        $condition=[Windows.Automation.AndCondition]::new(
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$app.Process.Id),
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'最小化到托盘'))
        $once=[Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        Invoke-Control $once
        if([TrayStartupNative]::IsWindowVisible($app.Handle) -or -not [TrayStartupNative]::HasTrayIcon($app.Handle)) {
            throw 'One-shot tray operation did not hide/register icon.'
        }
        if((Get-ItemPropertyValue $profile.RegistryPath MinimizeBehavior) -ne 0){throw 'One-shot operation changed default.'}
        Restore-TestApp $app.Handle
        $settings=Open-Settings $app.Process $app.Handle
        $radio=Find-Control $settings 'MinimizeBehaviorBox'
        $trayOption=Find-ByName $radio '最小化到托盘' $app.Process.Id
        $trayOption.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        Invoke-Control (Find-Control $settings 'SaveButton')
        Invoke-Control (Find-Control $root 'MinimizeButton')
        if([TrayStartupNative]::IsWindowVisible($app.Handle) -or -not [TrayStartupNative]::HasTrayIcon($app.Handle)) {
            throw 'Saved default tray minimize failed.'
        }
        $secondary=Start-IsolatedTestProcess -Profile $profile -FilePath $applicationPath -WorkingDirectory $directory
        if(-not $secondary.WaitForExit(10000) -or $secondary.ExitCode -ne 0){throw 'Duplicate launch failed.'}
        [void]$profile.OwnedProcessIds.Remove($secondary.Id)
        Start-Sleep -Milliseconds 300
        if(-not [TrayStartupNative]::IsWindowVisible($app.Handle) -or [TrayStartupNative]::HasTrayIcon($app.Handle)) {
            throw 'Duplicate manual launch did not restore the tray-hidden primary.'
        }
        Invoke-OneShot $app '最小化窗口'
        if(-not [TrayStartupNative]::IsIconic($app.Handle) -or
            (Get-ItemPropertyValue $profile.RegistryPath MinimizeBehavior) -ne 1) {
            throw 'One-shot taskbar minimize changed tray default.'
        }
        [void][TrayStartupNative]::ShowWindow($app.Handle,9)
        $settings=Open-Settings $app.Process $app.Handle
        $settingsHandle=[IntPtr]$settings.Current.NativeWindowHandle
        Restore-TestApp $app.Handle
        if([TrayStartupNative]::GetForegroundWindow() -ne $settingsHandle){throw 'Restore covered owned settings.'}
        (Get-Toggle (Find-Control $settings 'StartupEnabledBox')).Toggle()
        Invoke-Control (Find-Control $settings 'SaveButton')
        if((Get-ItemPropertyValue $profile.RegistryPath StartupEnabled) -ne 0 -or
            $null -ne (Get-ItemProperty $runPath -Name $runName -ErrorAction SilentlyContinue)) {
            throw 'Disabling startup did not remove only the test registration.'
        }
        $settings=Open-Settings $app.Process $app.Handle
        (Get-Toggle (Find-Control $settings 'StartupEnabledBox')).Toggle()
        $disabledLauncher=$profile.ExpectedLauncherPath+'.disabled'
        Move-Item -LiteralPath $profile.ExpectedLauncherPath -Destination $disabledLauncher
        try {
            Invoke-Control (Find-Control $settings 'SaveButton')
            $failureStatus=Find-Control $settings 'SettingsStatusText'
            if((Get-ItemPropertyValue $profile.RegistryPath StartupEnabled) -ne 0 -or
                [string]::IsNullOrWhiteSpace($failureStatus.Current.Name) -or $failureStatus.Current.IsOffscreen) {
                throw 'Startup registration failure falsely committed or lacks feedback.'
            }
        } finally {Move-Item -LiteralPath $disabledLauncher -Destination $profile.ExpectedLauncherPath}
        Invoke-Control (Find-Control $settings 'CancelButton')

        Invoke-Control (Find-Control $root 'MinimizeButton')
        Invoke-TrayCommand $app '打开主窗口'
        if(-not [TrayStartupNative]::IsWindowVisible($app.Handle)){throw 'Tray Open command failed.'}
        Write-Host 'PASS: defaults, cascade, Draft/Cancel/Save, stable startup registration, native minimize, one-shot tray and restore'
    } finally { Stop-IsolatedTestProcess -Profile $profile -Process $app.Process }

    foreach($enabled in @(0,1)) {
        foreach($hidden in @(0,1)) {
            New-ItemProperty $profile.RegistryPath StartupEnabled -PropertyType DWord -Value $enabled -Force | Out-Null
            New-ItemProperty $profile.RegistryPath StartupHidden -PropertyType DWord -Value $hidden -Force | Out-Null
            New-ItemProperty $profile.RegistryPath AlwaysOnTop -PropertyType DWord -Value $hidden -Force | Out-Null
            foreach($arguments in @('', '--startup')) {
                $app=Start-TestApp $arguments
                try {
                    $expectedHidden=$arguments -eq '--startup' -and $enabled -eq 1 -and $hidden -eq 1
                    if([TrayStartupNative]::IsWindowVisible($app.Handle) -eq $expectedHidden -or
                        [TrayStartupNative]::HasTrayIcon($app.Handle) -ne $expectedHidden) {
                        throw "Startup combination failed: enabled=$enabled hidden=$hidden args=$arguments"
                    }
                    if($expectedHidden){Restore-TestApp $app.Handle}
                    $beforeStyle=[TrayStartupNative]::GetWindowLongPtr($app.Handle,-20).ToInt64()
                    Restore-TestApp $app.Handle
                    $afterStyle=[TrayStartupNative]::GetWindowLongPtr($app.Handle,-20).ToInt64()
                    if(($beforeStyle -band 8) -ne ($afterStyle -band 8) -or
                        (($afterStyle -band 8) -ne 0) -ne ($hidden -eq 1)) {
                        throw 'Restore changed persisted always-on-top behavior.'
                    }
                } finally {Stop-IsolatedTestProcess -Profile $profile -Process $app.Process}
            }
        }
    }
    Write-Host 'PASS: all login/manual startup combinations from staged Launcher'
    New-ItemProperty $profile.RegistryPath MinimizeBehavior -PropertyType DWord -Value 1 -Force | Out-Null
    $app=Start-TestApp
    $root=[Windows.Automation.AutomationElement]::FromHandle($app.Handle)
    Invoke-Control (Find-Control $root 'MinimizeButton')
    Invoke-TrayCommand $app '退出'
    if(-not $app.Process.WaitForExit(5000) -or [TrayStartupNative]::HasTrayIcon($app.Handle)) {
        throw 'Tray Exit left process or icon behind.'
    }
    [void]$profile.OwnedProcessIds.Remove($app.Process.Id)
    Write-Host 'PASS: saved tray default, one-shot taskbar, single-instance restore, owned settings foreground, registration failure, tray Open/Exit'
} finally {
    Remove-IsolatedTestProfile -Profile $profile -RepositoryRoot $repositoryRoot
}
