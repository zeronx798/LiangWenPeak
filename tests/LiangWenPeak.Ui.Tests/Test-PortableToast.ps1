[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$DistributionArchive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')
Add-Type -AssemblyName UIAutomationClient

if (-not ('LiangWenPeak.UiTests.NotificationStateNativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;

namespace LiangWenPeak.UiTests
{
    public static class NotificationStateNativeMethods
    {
        [DllImport("shell32.dll")]
        public static extern int SHQueryUserNotificationState(out int state);
    }
}
'@
}

function Get-DesktopNotificationState {
    [int]$state = 0
    try {
        $result = [LiangWenPeak.UiTests.NotificationStateNativeMethods]::SHQueryUserNotificationState(
            [ref]$state)
        if ($result -ne 0) {
            return [PSCustomObject]@{
                Name = "Unavailable (HRESULT=$result)"
                ShouldInspectBanner = $true
            }
        }

        $name = switch ($state) {
            1 { 'QUNS_NOT_PRESENT' }
            2 { 'QUNS_BUSY' }
            3 { 'QUNS_RUNNING_D3D_FULL_SCREEN' }
            4 { 'QUNS_PRESENTATION_MODE' }
            5 { 'QUNS_ACCEPTS_NOTIFICATIONS' }
            6 { 'QUNS_QUIET_TIME' }
            7 { 'QUNS_APP' }
            default { "UNKNOWN_$state" }
        }
        return [PSCustomObject]@{
            Name = $name
            ShouldInspectBanner = ($state -eq 5)
        }
    } catch {
        return [PSCustomObject]@{
            Name = "Unavailable ($($_.Exception.GetType().Name))"
            ShouldInspectBanner = $true
        }
    }
}

function Wait-MainWindow([Diagnostics.Process]$Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
    } while (-not $Process.HasExited -and
        $Process.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $deadline)
    if ($Process.HasExited -or $Process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'The isolated portable main window did not become visible.'
    }
}

function Find-TestElement {
    param(
        [Windows.Automation.AutomationElement]$Root,
        [Windows.Automation.AutomationProperty]$Property,
        [object]$Value,
        [int]$ProcessId = 0,
        [int]$TimeoutMilliseconds = 5000
    )

    $condition = [Windows.Automation.PropertyCondition]::new($Property, $Value)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        for ($index = 0; $index -lt $elements.Count; ++$index) {
            $candidate = $elements.Item($index)
            if (($ProcessId -eq 0 -or $candidate.Current.ProcessId -eq $ProcessId) -and
                $candidate.Current.IsEnabled) {
                return $candidate
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Invoke-TestElement([Windows.Automation.AutomationElement]$Element) {
    $scroll = $null
    if ($Element.TryGetCurrentPattern(
            [Windows.Automation.ScrollItemPattern]::Pattern,
            [ref]$scroll)) {
        $scroll.ScrollIntoView()
        Start-Sleep -Milliseconds 100
    }
    $Element.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
}

function Find-TestSettingsWindow([int]$ProcessId) {
    $condition = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::NameProperty,
            '设置'),
        [Windows.Automation.PropertyCondition]::new(
            [Windows.Automation.AutomationElement]::ControlTypeProperty,
            [Windows.Automation.ControlType]::Window))
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $windows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            $condition)
        for ($index = 0; $index -lt $windows.Count; ++$index) {
            if ($windows.Item($index).Current.ProcessId -eq $ProcessId) {
                return $windows.Item($index)
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Get-TestToastText([string]$AppUserModelId) {
    try {
        $history = [Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime]::History
        $notifications = @($history.GetHistory($AppUserModelId))
        if ($notifications.Count -eq 0) {
            return $null
        }
        $nodes = $notifications[0].Content.GetElementsByTagName('text')
        if ($nodes.Length -lt 2) {
            return $null
        }
        return [PSCustomObject]@{
            Title = [string]$nodes.Item(0).InnerText
            Body = [string]$nodes.Item(1).InnerText
        }
    } catch {
        return $null
    }
}

$context = Get-BuildContext -Configuration $Configuration -RepositoryRoot $repositoryRoot
$sourceArchive = if ([string]::IsNullOrWhiteSpace($DistributionArchive)) {
    $context.PackageArchive
} else {
    [IO.Path]::GetFullPath($DistributionArchive)
}
if (-not (Test-Path -LiteralPath $sourceArchive -PathType Leaf)) {
    throw "The portable ZIP was not found: $sourceArchive"
}

$profile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'portable-toast'
$application = $null

try {
    Expand-Archive `
        -LiteralPath $sourceArchive `
        -DestinationPath $profile.PortableRoot `
        -Force
    Assert-PortablePackage -Context $context -PackageDirectory $profile.PortableRoot
    $dataSentinel = Join-Path $profile.PortableRoot 'data\do-not-delete.txt'
    New-Item -ItemType Directory -Path (Split-Path -Parent $dataSentinel) -Force | Out-Null
    [IO.File]::WriteAllText(
        $dataSentinel,
        "PORTABLE_TOAST_DATA_SENTINEL_$($profile.RunId)",
        [Text.UTF8Encoding]::new($false))
    $dataHash = (Get-FileHash -LiteralPath $dataSentinel -Algorithm SHA256).Hash

    New-Item -Path $profile.RegistryPath -Force | Out-Null
    foreach ($entry in @(
        @{ Name = 'ApiFeatureEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'NotificationEnabled'; Type = 'DWord'; Value = 1 },
        @{ Name = 'NotificationAdvanceEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'NotificationAdvanceMinutes'; Type = 'DWord'; Value = 10 },
        @{ Name = 'LastAdvanceNotificationTransitionUnixSeconds'; Type = 'QWord'; Value = 4102444800 },
        @{ Name = 'LastArrivedNotificationTransitionUnixSeconds'; Type = 'QWord'; Value = 4102444800 }
    )) {
        New-ItemProperty `
            -Path $profile.RegistryPath `
            -Name $entry.Name `
            -PropertyType $entry.Type `
            -Value $entry.Value `
            -Force | Out-Null
    }

    Write-Host "ISOLATED_TEST_RUN_ID=$($profile.RunId)"
    $version = [IO.File]::ReadAllText(
        (Join-Path $profile.PortableRoot 'current.txt')).Trim()
    $applicationPath = Join-Path $profile.PortableRoot "app-$version\LiangWenPeak.App.exe"
    $application = Start-IsolatedPortableApplication `
        -Profile $profile `
        -LauncherPath $profile.ExpectedLauncherPath `
        -ExpectedApplicationPath $applicationPath `
        -WorkingDirectory $profile.PortableRoot
    Wait-MainWindow $application

    $shortcutDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
    } while (-not (Test-Path -LiteralPath $profile.ShortcutPath -PathType Leaf) -and
        [DateTime]::UtcNow -lt $shortcutDeadline)
    if (-not (Test-Path -LiteralPath $profile.ShortcutPath -PathType Leaf)) {
        throw 'Persisted notification enablement did not create the isolated Toast shortcut.'
    }
    $identity = Get-ShortcutIdentity $profile.ShortcutPath
    if ($identity.AppUserModelId -ne $profile.AppUserModelId -or
        -not $identity.TargetPath.Equals(
            [IO.Path]::GetFullPath($profile.ExpectedLauncherPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Initial isolated Toast shortcut identity is incorrect.'
    }

    Stop-IsolatedTestProcess -Profile $profile -Process $application
    $application = $null
    $originalRoot = [IO.Path]::GetFullPath($profile.PortableRoot)
    $movedRoot = [IO.Path]::GetFullPath((Join-Path $profile.RunRoot "Moved Portable $($profile.RunId)"))
    $runBoundary = [IO.Path]::GetFullPath($profile.RunRoot).TrimEnd('\', '/') +
        [IO.Path]::DirectorySeparatorChar
    if (-not $originalRoot.StartsWith($runBoundary, [StringComparison]::OrdinalIgnoreCase) -or
        -not $movedRoot.StartsWith($runBoundary, [StringComparison]::OrdinalIgnoreCase) -or
        $movedRoot.IndexOf($profile.RunId, [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw 'Refusing to move an isolated portable root outside its test-run directory.'
    }
    Move-Item -LiteralPath $originalRoot -Destination $movedRoot
    $profile.PortableRoot = $movedRoot
    $profile.ExpectedLauncherPath = Join-Path $movedRoot 'LiangWenPeak.exe'
    $dataSentinel = Join-Path $movedRoot 'data\do-not-delete.txt'
    $applicationPath = Join-Path $movedRoot "app-$version\LiangWenPeak.App.exe"

    $application = Start-IsolatedPortableApplication `
        -Profile $profile `
        -LauncherPath $profile.ExpectedLauncherPath `
        -ExpectedApplicationPath $applicationPath `
        -WorkingDirectory $movedRoot
    Wait-MainWindow $application
    $repairDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $identity = Get-ShortcutIdentity $profile.ShortcutPath
    } while (-not $identity.TargetPath.Equals(
            [IO.Path]::GetFullPath($profile.ExpectedLauncherPath),
            [StringComparison]::OrdinalIgnoreCase) -and
        [DateTime]::UtcNow -lt $repairDeadline)
    if ($identity.AppUserModelId -ne $profile.AppUserModelId -or
        -not $identity.TargetPath.Equals(
            [IO.Path]::GetFullPath($profile.ExpectedLauncherPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Moving the portable directory did not repair the isolated shortcut target.'
    }
    Write-Host 'PASS: isolated portable shortcut target repaired after directory move'

    $startAppDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $indexedStartApp = $null
    do {
        $indexedStartApp = @(Get-StartApps | Where-Object {
                $_.AppID -eq $profile.AppUserModelId
            } | Select-Object -First 1)
        if ($indexedStartApp.Count -eq 0) {
            Start-Sleep -Milliseconds 200
        }
    } while ($indexedStartApp.Count -eq 0 -and
        [DateTime]::UtcNow -lt $startAppDeadline)
    if ($indexedStartApp.Count -eq 0) {
        throw 'Windows Start did not index the isolated shortcut AUMID.'
    }

    $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($application.MainWindowHandle)
    $more = Find-TestElement `
        -Root $mainRoot `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'MoreButton' `
        -ProcessId $application.Id
    Invoke-TestElement $more
    $settingsMenu = Find-TestElement `
        -Root ([Windows.Automation.AutomationElement]::RootElement) `
        -Property ([Windows.Automation.AutomationElement]::NameProperty) `
        -Value '设置...' `
        -ProcessId $application.Id
    Invoke-TestElement $settingsMenu
    $settings = Find-TestSettingsWindow $application.Id
    if ($null -eq $settings) {
        throw 'The isolated portable settings window did not open.'
    }
    $testNotification = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'TestNotificationButton'
    $desktopNotificationState = Get-DesktopNotificationState
    $inspectToastBanner = $desktopNotificationState.ShouldInspectBanner
    Invoke-TestElement $testNotification

    $statusDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $status = Find-TestElement `
            -Root $settings `
            -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
            -Value 'SettingsStatusText' `
            -TimeoutMilliseconds 0
    } while (($null -eq $status -or $status.Current.Name -ne '测试通知已发送') -and
        [DateTime]::UtcNow -lt $statusDeadline)
    if ($null -eq $status -or $status.Current.Name -ne '测试通知已发送') {
        $statusText = if ($null -eq $status) { '<missing>' } else { $status.Current.Name }
        throw "Classic WinRT rejected the isolated portable test notification. Status=$statusText"
    }

    $toastDeadline = [DateTime]::UtcNow.AddSeconds(10)
    $toastText = $null
    $popupTitle = $null
    $popupBody = $null
    do {
        $toastText = Get-TestToastText $profile.AppUserModelId
        if ($inspectToastBanner) {
            $popupTitle = Find-TestElement `
                -Root ([Windows.Automation.AutomationElement]::RootElement) `
                -Property ([Windows.Automation.AutomationElement]::NameProperty) `
                -Value 'LiangWenPeak 通知测试' `
                -TimeoutMilliseconds 0
            $popupBody = Find-TestElement `
                -Root ([Windows.Automation.AutomationElement]::RootElement) `
                -Property ([Windows.Automation.AutomationElement]::NameProperty) `
                -Value '通知功能工作正常。' `
                -TimeoutMilliseconds 0
        }
        if (($null -ne $toastText -and
                $toastText.Title -eq 'LiangWenPeak 通知测试' -and
                $toastText.Body -eq '通知功能工作正常。') -and
            (-not $inspectToastBanner -or
                ($null -ne $popupTitle -and $null -ne $popupBody))) {
            break
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $toastDeadline)
    if ($null -eq $toastText -or
        $toastText.Title -ne 'LiangWenPeak 通知测试' -or
        $toastText.Body -ne '通知功能工作正常。') {
        throw 'The isolated Toast history did not contain the exact test title and body.'
    }
    Write-Host 'PASS: classic WinRT delivered the exact isolated test title/body to notification history'
    $toastSetting = try {
        [string]([Windows.UI.Notifications.ToastNotificationManager,Windows.UI.Notifications,ContentType=WindowsRuntime]::CreateToastNotifier(
                $profile.AppUserModelId).Setting)
    } catch {
        "Unavailable ($($_.Exception.HResult.ToString('X8')))"
    }
    if (-not $inspectToastBanner -or $null -eq $popupTitle -or $null -eq $popupBody) {
        Write-Warning '当前桌面会话没有向 UI Automation 暴露 Toast 横幅标题/正文，进行 UI Automation 请先关闭专注/勿扰模式。'
        Write-Host "SKIP: Toast banner UI Automation; ShellNotificationState=$($desktopNotificationState.Name); NotificationSetting=$toastSetting"
    } else {
        Write-Host 'PASS: isolated Toast banner exposed the exact title/body to UI Automation'
    }
    if ((Get-FileHash -LiteralPath $dataSentinel -Algorithm SHA256).Hash -ne $dataHash) {
        throw 'Portable move or Toast validation modified the isolated data sentinel.'
    }

    Write-Host 'PASS: Release-equivalent isolated portable classic Toast delivery/history and shortcut target repair'
} finally {
    if ($null -ne $application) {
        Stop-IsolatedTestProcess -Profile $profile -Process $application
    }
    Remove-IsolatedTestProfile -Profile $profile -RepositoryRoot $repositoryRoot
    if ((Test-Path -LiteralPath $profile.RegistryPath) -or
        (Test-IsolatedCredentialExists $profile.ApiCredentialResource $profile.ApiCredentialUserName) -or
        (Test-IsolatedCredentialExists $profile.HistoryCredentialResource $profile.HistoryCredentialUserName) -or
        (Test-Path -LiteralPath $profile.ShortcutPath)) {
        throw 'Isolated portable Toast teardown left test-only system state behind.'
    }
    Write-Host 'PASS: isolated portable Toast Registry/Credential/shortcut teardown'
    $global:LASTEXITCODE = 0
}
