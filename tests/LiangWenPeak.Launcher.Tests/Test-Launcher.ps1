[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [switch]$SkipStage,

    [switch]$SmokeOnly
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

function Get-ApplicationProcesses([string]$expectedPath) {
    $fullExpectedPath = [IO.Path]::GetFullPath($expectedPath)
    $matches = @()
    foreach ($process in Get-Process -Name 'LiangWenPeak.App' -ErrorAction SilentlyContinue) {
        try {
            if ([IO.Path]::GetFullPath($process.Path).Equals($fullExpectedPath, [StringComparison]::OrdinalIgnoreCase)) {
                $matches += $process
            }
        } catch {
            # A process can exit between enumeration and reading its executable path.
        }
    }
    return $matches
}

function Stop-ApplicationProcess([Diagnostics.Process]$process) {
    if ($process.HasExited) {
        return
    }

    $process.Refresh()
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
        [void]$process.CloseMainWindow()
    }

    if (-not $process.WaitForExit(5000)) {
        $process.Kill()
        $process.WaitForExit()
    }
}

function Invoke-SuccessScenario(
    [string]$name,
    [string]$launcherPath,
    [string]$applicationPath,
    [string]$workingDirectory) {

    $beforeIds = @(Get-ApplicationProcesses $applicationPath | ForEach-Object Id)
    $launcher = Start-Process -FilePath $launcherPath -WorkingDirectory $workingDirectory -PassThru
    if (-not $launcher.WaitForExit(10000)) {
        $launcher.Kill()
        throw "$name failed: the launcher did not exit immediately."
    }
    if ($launcher.ExitCode -ne 0) {
        throw "$name failed: launcher exit code $($launcher.ExitCode)."
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $application = $null
    while ([DateTime]::UtcNow -lt $deadline -and $null -eq $application) {
        $application = Get-ApplicationProcesses $applicationPath |
            Where-Object { $beforeIds -notcontains $_.Id } |
            Select-Object -First 1
        if ($null -eq $application) {
            Start-Sleep -Milliseconds 100
        }
    }

    if ($null -eq $application) {
        throw "$name failed: LiangWenPeak.App.exe did not start from the expected directory."
    }

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
    [string]$name,
    [string]$fixtureRoot,
    [string]$launcherSource,
    [scriptblock]$arrange,
    [string]$expectedMessage) {

    New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
    Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $fixtureRoot 'LiangWenPeak.exe') -Force
    & $arrange $fixtureRoot

    $process = Start-Process -FilePath (Join-Path $fixtureRoot 'LiangWenPeak.exe') -WorkingDirectory 'C:\' -PassThru
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
        if (-not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) {
                $process.Kill()
                $process.WaitForExit()
            }
        }
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

$distributionRoot = $context.PackageDirectory
$launcherSource = Join-Path $distributionRoot 'LiangWenPeak.exe'
$currentSource = Join-Path $distributionRoot 'current.txt'
if (-not (Test-Path -LiteralPath $launcherSource -PathType Leaf) -or
    -not (Test-Path -LiteralPath $currentSource -PathType Leaf)) {
    throw 'The staged launcher layout was not found. Run scripts\package.ps1 first.'
}

$version = [IO.File]::ReadAllText($currentSource).Trim()
if ($version -ne $context.Version) {
    throw "The staged current.txt version '$version' does not match source version '$($context.Version)'."
}
$applicationSource = Join-Path $distributionRoot "app-$version\LiangWenPeak.App.exe"

$application = $null
try {
    $application = Invoke-SuccessScenario `
        'normal staged launch' `
        $launcherSource `
        $applicationSource `
        $distributionRoot
} finally {
    if ($null -ne $application) {
        Stop-ApplicationProcess $application
    }
}

if ($SmokeOnly) {
    Write-Host 'Launcher staging smoke test passed.'
    return
}

$testRoot = Join-Path $repositoryRoot "build\launcher-tests\$Configuration"
Assert-SafeTestDirectory $testRoot $repositoryRoot
if (Test-Path -LiteralPath $testRoot) {
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

$spaceRoot = Join-Path $testRoot 'Test Apps\LiangWenPeak'
Copy-DirectoryContents $distributionRoot $spaceRoot
$spaceCurrent = Join-Path $spaceRoot 'current.txt'
[IO.File]::WriteAllText($spaceCurrent, " `t$version`r`n", [Text.UTF8Encoding]::new($true))
$spaceApplication = Join-Path $spaceRoot "app-$version\LiangWenPeak.App.exe"
$application = $null
try {
    $application = Invoke-SuccessScenario `
        'path with spaces and arbitrary working directory' `
        (Join-Path $spaceRoot 'LiangWenPeak.exe') `
        $spaceApplication `
        'C:\'
} finally {
    if ($null -ne $application) {
        Stop-ApplicationProcess $application
    }
}

$errorRoot = Join-Path $testRoot 'errors'
Invoke-ErrorScenario `
    'missing current.txt' `
    (Join-Path $errorRoot 'missing-current') `
    $launcherSource `
    { param($root) } `
    'current.txt was not found.'

Invoke-ErrorScenario `
    'invalid version traversal' `
    (Join-Path $errorRoot 'invalid-version') `
    $launcherSource `
    { param($root) Write-Utf8WithoutBom (Join-Path $root 'current.txt') "..\other`r`n" } `
    'current.txt contains an invalid version.'

Invoke-ErrorScenario `
    'missing application directory' `
    (Join-Path $errorRoot 'missing-directory') `
    $launcherSource `
    { param($root) Write-Utf8WithoutBom (Join-Path $root 'current.txt') "9.9.9`r`n" } `
    'Application version directory not found:'

Invoke-ErrorScenario `
    'missing application executable' `
    (Join-Path $errorRoot 'missing-executable') `
    $launcherSource `
    {
        param($root)
        Write-Utf8WithoutBom (Join-Path $root 'current.txt') "1.0.0`r`n"
        New-Item -ItemType Directory -Path (Join-Path $root 'app-1.0.0') -Force | Out-Null
    } `
    'Application executable not found:'

Invoke-ErrorScenario `
    'CreateProcessW failure' `
    (Join-Path $errorRoot 'invalid-executable') `
    $launcherSource `
    {
        param($root)
        Write-Utf8WithoutBom (Join-Path $root 'current.txt') "1.0.0`r`n"
        $appDirectory = Join-Path $root 'app-1.0.0'
        New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null
        Write-Utf8WithoutBom (Join-Path $appDirectory 'LiangWenPeak.App.exe') 'not an executable'
    } `
    'Could not start LiangWenPeak.App.exe.'

Remove-Item -LiteralPath $testRoot -Recurse -Force
$testContainer = Split-Path -Parent $testRoot
if ((Test-Path -LiteralPath $testContainer) -and
    @(Get-ChildItem -LiteralPath $testContainer -Force).Count -eq 0) {
    Remove-Item -LiteralPath $testContainer -Force
}
Write-Host 'All launcher integration tests passed.'
