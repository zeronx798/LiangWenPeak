[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $repositoryRoot 'scripts\common.ps1')
Add-Type -AssemblyName UIAutomationClient

if (-not ('LiangWenPeakCleanupUiTests.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace LiangWenPeakCleanupUiTests
{
    public static class NativeMethods
    {
        [DllImport("user32.dll")]
        public static extern IntPtr GetWindow(IntPtr window, uint command);
    }
}
'@
}

function Find-TestElement {
    param(
        [Parameter(Mandatory)][Windows.Automation.AutomationElement]$Root,
        [Parameter(Mandatory)][Windows.Automation.AutomationProperty]$Property,
        [Parameter(Mandatory)][object]$Value,
        [int]$ProcessId = 0,
        [switch]$AllowDisabled,
        [int]$TimeoutMilliseconds = 5000
    )

    $condition = [Windows.Automation.PropertyCondition]::new($Property, $Value)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        $elements = $Root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
        for ($index = 0; $index -lt $elements.Count; ++$index) {
            $candidate = $elements.Item($index)
            if (($ProcessId -eq 0 -or $candidate.Current.ProcessId -eq $ProcessId) -and
                ($AllowDisabled -or $candidate.Current.IsEnabled)) {
                return $candidate
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Find-OwnedSettingsWindow {
    param([int]$ProcessId, [IntPtr]$OwnerHandle)

    $desktop = [Windows.Automation.AutomationElement]::RootElement
    $nameCondition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::NameProperty,
        '设置')
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $windows = $desktop.FindAll([Windows.Automation.TreeScope]::Descendants, $nameCondition)
        for ($index = 0; $index -lt $windows.Count; ++$index) {
            $candidate = $windows.Item($index)
            $handle = [IntPtr]$candidate.Current.NativeWindowHandle
            if ($candidate.Current.ProcessId -eq $ProcessId -and
                [LiangWenPeakCleanupUiTests.NativeMethods]::GetWindow($handle, 4) -eq $OwnerHandle) {
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

function Invoke-DialogButton {
    param(
        [Windows.Automation.AutomationElement]$SettingsWindow,
        [string]$AutomationId
    )

    $button = Find-TestElement `
        -Root $SettingsWindow `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value $AutomationId
    if ($null -eq $button) {
        throw "ContentDialog button '$AutomationId' was not found."
    }
    Invoke-TestElement $button
}

function Assert-FileHashes([hashtable]$ExpectedHashes) {
    foreach ($path in $ExpectedHashes.Keys) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Cleanup deleted test data: $path"
        }
        $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actual -ne $ExpectedHashes[$path]) {
            throw "Cleanup modified test data: $path"
        }
    }
}

function Get-SelectedComboBoxText(
    [Windows.Automation.AutomationElement]$ComboBox) {
    $selectionPattern = $null
    if (-not $ComboBox.TryGetCurrentPattern(
            [Windows.Automation.SelectionPattern]::Pattern,
            [ref]$selectionPattern)) {
        throw "ComboBox '$($ComboBox.Current.AutomationId)' does not expose SelectionPattern."
    }
    $selection = @($selectionPattern.Current.GetSelection())
    if ($selection.Count -ne 1) {
        throw "ComboBox '$($ComboBox.Current.AutomationId)' does not have exactly one selected item."
    }
    return $selection[0].Current.Name
}

function New-UnrelatedShortcut([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        throw "Refusing to overwrite an existing unrelated shortcut: $Path"
    }
    $wsh = New-Object -ComObject WScript.Shell
    try {
        $link = $wsh.CreateShortcut($Path)
        $link.TargetPath = Join-Path $env:SystemRoot 'System32\notepad.exe'
        $link.Description = 'Unrelated LiangWenPeak cleanup isolation sentinel'
        $link.Save()
    } finally {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($wsh)
    }
}

$context = Get-BuildContext -Configuration $Configuration -RepositoryRoot $repositoryRoot
$profile = New-IsolatedTestProfile `
    -RepositoryRoot $repositoryRoot `
    -TestArea 'cleanup-ui'
$unrelatedShortcut = Join-Path $env:APPDATA `
    "Microsoft\Windows\Start Menu\Programs\Unrelated Test Shortcut $($profile.RunId).lnk"
$application = $null

try {
    $applicationDirectory = Join-Path $profile.PortableRoot "app-$($context.Version)"
    New-Item -ItemType Directory -Path $applicationDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $context.BuildOutput -Force | Copy-Item `
        -Destination $applicationDirectory `
        -Recurse `
        -Force
    Copy-Item `
        -LiteralPath (Join-Path $context.BuildOutput 'LiangWenPeak.exe') `
        -Destination $profile.ExpectedLauncherPath `
        -Force
    [IO.File]::WriteAllText(
        (Join-Path $profile.PortableRoot 'current.txt'),
        "$($context.Version)`r`n",
        [Text.UTF8Encoding]::new($false))

    New-Item -Path $profile.RegistryPath -Force | Out-Null
    $seededRegistryValues = @(
        @{ Name = 'ApiFeatureEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'BalanceForecastEnabled'; Type = 'DWord'; Value = 1 },
        @{ Name = 'SelectedCurrency'; Type = 'String'; Value = 'USD' },
        @{ Name = 'BalanceRefreshIntervalMinutes'; Type = 'DWord'; Value = 30 },
        @{ Name = 'BalanceRateWindowSeconds'; Type = 'DWord'; Value = 3600 },
        @{ Name = 'PreferredPredictionAlgorithm'; Type = 'DWord'; Value = 2 },
        @{ Name = 'WarningBalances'; Type = 'String'; Value = 'USD=12300000000' },
        @{ Name = 'KnownCurrencies'; Type = 'String'; Value = 'CNY;USD' },
        @{ Name = 'NotificationEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'NotificationAdvanceEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'NotificationAdvanceMinutes'; Type = 'DWord'; Value = 27 },
        @{ Name = 'FluentThemeEnabled'; Type = 'DWord'; Value = 0 },
        @{ Name = 'LastAdvanceNotificationTransitionUnixSeconds'; Type = 'QWord'; Value = 12345 },
        @{ Name = 'LastArrivedNotificationTransitionUnixSeconds'; Type = 'QWord'; Value = 67890 }
    )
    foreach ($value in $seededRegistryValues) {
        New-ItemProperty `
            -Path $profile.RegistryPath `
            -Name $value.Name `
            -PropertyType $value.Type `
            -Value $value.Value `
            -Force | Out-Null
    }

    $fakeApiKey = "TEST_ONLY_DO_NOT_USE_$($profile.RunId)"
    $fakeIdentity = "TEST_ONLY_HISTORY_IDENTITY_$($profile.RunId)"
    Set-IsolatedCredential $profile.ApiCredentialResource $profile.ApiCredentialUserName $fakeApiKey
    Set-IsolatedCredential `
        $profile.HistoryCredentialResource `
        $profile.HistoryCredentialUserName `
        $fakeIdentity

    $dataFiles = @(
        (Join-Path $profile.PortableRoot 'data\balance-history.csv'),
        (Join-Path $profile.PortableRoot 'data\history\test.csv'),
        (Join-Path $profile.PortableRoot 'data\do-not-delete.txt'))
    foreach ($path in $dataFiles) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        $contents = if ((Split-Path -Leaf $path) -eq 'balance-history.csv') {
            "series_id,timestamp,currency,balance`r`n"
        } else {
            "TEST_DATA_SENTINEL_$($profile.RunId)_$(Split-Path -Leaf $path)"
        }
        [IO.File]::WriteAllText(
            $path,
            $contents,
            [Text.UTF8Encoding]::new($false))
    }
    $dataHashes = @{}
    foreach ($path in $dataFiles) {
        $dataHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }

    New-UnrelatedShortcut $unrelatedShortcut
    Write-Host "ISOLATED_TEST_RUN_ID=$($profile.RunId)"

    $applicationPath = Join-Path $applicationDirectory 'LiangWenPeak.App.exe'
    $application = Start-IsolatedTestProcess `
        -Profile $profile `
        -FilePath $applicationPath `
        -WorkingDirectory $applicationDirectory
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $application.Refresh()
    } while (-not $application.HasExited -and
        $application.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $deadline)
    if ($application.HasExited -or $application.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'Cleanup test main window did not become visible.'
    }

    $mainHandle = $application.MainWindowHandle
    $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($mainHandle)
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
    $settings = Find-OwnedSettingsWindow $application.Id $mainHandle
    if ($null -eq $settings) {
        throw 'Cleanup test could not find the isolated owned settings window.'
    }

    $testNotification = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'TestNotificationButton'
    Invoke-TestElement $testNotification
    $shortcutDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
    } while (-not (Test-Path -LiteralPath $profile.ShortcutPath -PathType Leaf) -and
        [DateTime]::UtcNow -lt $shortcutDeadline)
    if (-not (Test-Path -LiteralPath $profile.ShortcutPath -PathType Leaf)) {
        throw 'The isolated test notification did not create its test-only shortcut.'
    }
    $historyDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $historyBeforeCleanup = Get-IsolatedToastHistoryCount $profile.AppUserModelId
        if ($null -eq $historyBeforeCleanup -or $historyBeforeCleanup -gt 0) {
            break
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $historyDeadline)
    if ($null -ne $historyBeforeCleanup -and $historyBeforeCleanup -le 0) {
        throw 'Supported isolated notification history did not receive the cleanup test Toast.'
    }
    $shortcutIdentity = Get-ShortcutIdentity $profile.ShortcutPath
    if ($shortcutIdentity.AppUserModelId -ne $profile.AppUserModelId -or
        -not $shortcutIdentity.TargetPath.Equals(
            [IO.Path]::GetFullPath($profile.ExpectedLauncherPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The isolated Toast shortcut identity or Launcher target is incorrect.'
    }

    $notificationToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'NotificationEnabledToggle'
    $notificationToggle.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    $advanceToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'AdvanceReminderToggle'
    $advanceToggle.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
    $advanceBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'AdvanceMinutesBox'
    $advanceBox.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).SetValue(30)
    $apiKeyBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'ApiKeyBox'
    $apiKeyBox.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue(
        "TEST_ONLY_DRAFT_DO_NOT_USE_$($profile.RunId)")

    $cleanupButton = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'CleanupButton'
    Invoke-TestElement $cleanupButton
    $confirmationTitle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::NameProperty) `
        -Value '彻底清理 LiangWenPeak？'
    if ($null -eq $confirmationTitle) {
        throw 'Cleanup confirmation dialog did not appear.'
    }
    Invoke-DialogButton $settings 'CloseButton'
    Start-Sleep -Milliseconds 250

    if (-not (Test-Path -LiteralPath $profile.RegistryPath) -or
        -not (Test-IsolatedCredentialExists $profile.ApiCredentialResource $profile.ApiCredentialUserName) -or
        -not (Test-IsolatedCredentialExists $profile.HistoryCredentialResource $profile.HistoryCredentialUserName) -or
        -not (Test-Path -LiteralPath $profile.ShortcutPath -PathType Leaf)) {
        throw 'Cancelling cleanup changed isolated persisted state.'
    }
    foreach ($value in $seededRegistryValues) {
        if ((Get-ItemPropertyValue `
                -LiteralPath $profile.RegistryPath `
                -Name $value.Name) -ne $value.Value) {
            throw "Cancelling cleanup changed isolated Registry value '$($value.Name)'."
        }
    }
    if ($null -ne $historyBeforeCleanup -and
        (Get-IsolatedToastHistoryCount $profile.AppUserModelId) -ne $historyBeforeCleanup) {
        throw 'Cancelling cleanup changed isolated notification history.'
    }
    $shortcutAfterCancel = Get-ShortcutIdentity $profile.ShortcutPath
    if ($shortcutAfterCancel.AppUserModelId -ne $profile.AppUserModelId -or
        -not $shortcutAfterCancel.TargetPath.Equals(
            [IO.Path]::GetFullPath($profile.ExpectedLauncherPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Cancelling cleanup changed the isolated Toast shortcut.'
    }
    Assert-FileHashes $dataHashes

    Invoke-TestElement $cleanupButton
    $confirmationTitle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::NameProperty) `
        -Value '彻底清理 LiangWenPeak？'
    if ($null -eq $confirmationTitle) {
        throw 'Cleanup confirmation dialog did not reappear.'
    }
    Invoke-DialogButton $settings 'PrimaryButton'

    $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
    } while ((Test-Path -LiteralPath $profile.RegistryPath) -and
        [DateTime]::UtcNow -lt $cleanupDeadline)
    if (Test-Path -LiteralPath $profile.RegistryPath) {
        throw 'Cleanup did not delete the isolated Registry subtree.'
    }
    if (Test-IsolatedCredentialExists $profile.ApiCredentialResource $profile.ApiCredentialUserName) {
        throw 'Cleanup did not delete the isolated API Key credential.'
    }
    if (Test-IsolatedCredentialExists $profile.HistoryCredentialResource $profile.HistoryCredentialUserName) {
        throw 'Cleanup did not delete the isolated History Identity credential.'
    }
    if (Test-Path -LiteralPath $profile.ShortcutPath) {
        throw 'Cleanup did not delete the isolated Toast shortcut.'
    }
    if ($null -ne $historyBeforeCleanup) {
        $historyAfterCleanup = Get-IsolatedToastHistoryCount $profile.AppUserModelId
        if ($null -eq $historyAfterCleanup -or $historyAfterCleanup -ne 0) {
            throw 'Cleanup did not clear supported isolated notification history.'
        }
    }
    if (-not (Test-Path -LiteralPath $unrelatedShortcut -PathType Leaf)) {
        throw 'Cleanup deleted an unrelated adjacent shortcut.'
    }
    Assert-FileHashes $dataHashes

    $notificationToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'NotificationEnabledToggle'
    $advanceToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'AdvanceReminderToggle' `
        -AllowDisabled
    $advanceBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'AdvanceMinutesBox' `
        -AllowDisabled
    $notificationState = $notificationToggle.GetCurrentPattern(
        [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
    $advanceState = $advanceToggle.GetCurrentPattern(
        [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
    $advanceValue = $advanceBox.GetCurrentPattern(
        [Windows.Automation.RangeValuePattern]::Pattern).Current.Value
    if ($notificationState -ne [Windows.Automation.ToggleState]::Off -or
        $advanceState -ne [Windows.Automation.ToggleState]::On -or
        $advanceValue -ne 10 -or
        $advanceToggle.Current.IsEnabled -or
        $advanceBox.Current.IsEnabled) {
        throw 'Cleanup did not destroy the old Draft and rebuild notification defaults.'
    }

    $apiFeatureToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'ApiFeatureToggle'
    $forecastToggle = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'ForecastEnabledBox'
    $currencyBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'CurrencyBox'
    $refreshBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'RefreshIntervalBox'
    $rateWindowBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'RateWindowBox'
    $algorithmBox = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'AlgorithmBox'
    if ($apiFeatureToggle.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne
            [Windows.Automation.ToggleState]::On -or
        $forecastToggle.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne
            [Windows.Automation.ToggleState]::Off -or
        (Get-SelectedComboBoxText $currencyBox) -ne 'CNY' -or
        (Get-SelectedComboBoxText $refreshBox) -ne '1 分钟' -or
        (Get-SelectedComboBoxText $rateWindowBox) -ne '30 天' -or
        (Get-SelectedComboBoxText $algorithmBox) -ne '滑动平均（推荐）') {
        throw 'Cleanup did not rebuild the complete settings UI from central defaults.'
    }

    $save = Find-TestElement `
        -Root $settings `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'SaveButton'
    Invoke-TestElement $save
    Start-Sleep -Milliseconds 500
    if (-not (Test-Path -LiteralPath $profile.RegistryPath) -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name ApiFeatureEnabled) -ne 1 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name BalanceForecastEnabled) -ne 0 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name SelectedCurrency) -ne 'CNY' -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name BalanceRefreshIntervalMinutes) -ne 1 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name BalanceRateWindowSeconds) -ne 2592000 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name PreferredPredictionAlgorithm) -ne 0 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name KnownCurrencies) -ne 'CNY' -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name WarningBalances) -match 'USD' -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name NotificationEnabled) -ne 0 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name NotificationAdvanceEnabled) -ne 1 -or
        (Get-ItemPropertyValue -Path $profile.RegistryPath -Name NotificationAdvanceMinutes) -ne 10) {
        throw 'Save after cleanup did not persist only the new default Draft.'
    }
    $persistedPropertyNames = @(
        (Get-ItemProperty -LiteralPath $profile.RegistryPath).PSObject.Properties.Name)
    if ($persistedPropertyNames -contains 'LastAdvanceNotificationTransitionUnixSeconds' -or
        $persistedPropertyNames -contains 'LastArrivedNotificationTransitionUnixSeconds' -or
        $persistedPropertyNames -contains 'FluentThemeEnabled') {
        throw 'Save after cleanup resurrected pre-cleanup dedup or Fluent state.'
    }
    if ((Test-IsolatedCredentialExists $profile.ApiCredentialResource $profile.ApiCredentialUserName) -or
        (Test-IsolatedCredentialExists $profile.HistoryCredentialResource $profile.HistoryCredentialUserName) -or
        (Test-Path -LiteralPath $profile.ShortcutPath)) {
        throw 'Save after cleanup resurrected pre-cleanup identity or notification state.'
    }
    Assert-FileHashes $dataHashes

    $mainRoot = [Windows.Automation.AutomationElement]::FromHandle($mainHandle)
    $more = Find-TestElement `
        -Root $mainRoot `
        -Property ([Windows.Automation.AutomationElement]::AutomationIdProperty) `
        -Value 'MoreButton' `
        -ProcessId $application.Id
    Invoke-TestElement $more
    $notificationMenu = Find-TestElement `
        -Root ([Windows.Automation.AutomationElement]::RootElement) `
        -Property ([Windows.Automation.AutomationElement]::NameProperty) `
        -Value '启用通知' `
        -ProcessId $application.Id
    if ($notificationMenu.GetCurrentPattern(
            [Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne
        [Windows.Automation.ToggleState]::Off) {
        throw 'Cleanup did not stop notifications and refresh the main-menu toggle.'
    }

    Write-Host 'PASS: isolated danger cleanup, Draft reset, Save non-resurrection, and data preservation'
} finally {
    if ($null -ne $application) {
        Stop-IsolatedTestProcess -Profile $profile -Process $application
    }
    if (Test-Path -LiteralPath $unrelatedShortcut -PathType Leaf) {
        $unrelatedIdentity = Get-ShortcutIdentity $unrelatedShortcut
        $expectedUnrelatedTarget = [IO.Path]::GetFullPath(
            (Join-Path $env:SystemRoot 'System32\notepad.exe'))
        if (-not $unrelatedIdentity.TargetPath.Equals(
                $expectedUnrelatedTarget,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to remove an unrelated shortcut whose target changed during the test.'
        }
        Remove-Item -LiteralPath $unrelatedShortcut -Force
    }
    Remove-IsolatedTestProfile -Profile $profile -RepositoryRoot $repositoryRoot
    if ((Test-Path -LiteralPath $profile.RegistryPath) -or
        (Test-IsolatedCredentialExists $profile.ApiCredentialResource $profile.ApiCredentialUserName) -or
        (Test-IsolatedCredentialExists $profile.HistoryCredentialResource $profile.HistoryCredentialUserName) -or
        (Test-Path -LiteralPath $profile.ShortcutPath) -or
        (Test-Path -LiteralPath $unrelatedShortcut)) {
        throw 'Isolated cleanup teardown left test-only system state behind.'
    }
    Write-Host 'PASS: isolated cleanup Registry/Credential/shortcut teardown'
    $global:LASTEXITCODE = 0
}
