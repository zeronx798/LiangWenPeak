[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testScripts = @(Get-ChildItem `
    -LiteralPath (Join-Path $repositoryRoot 'tests') `
    -Recurse `
    -File `
    -Filter '*.ps1' |
    Where-Object Name -ne 'Test-TestIsolation.ps1')

$forbidden = @(
    @{ Name = 'production Registry provider path'; Pattern = "HKCU:\\Software\\LiangWenPeak(?!\.Tests)" },
    @{ Name = 'production Registry native path'; Pattern = "HKCU\\Software\\LiangWenPeak(?!\.Tests)" },
    @{ Name = 'Registry backup/restore'; Pattern = "(?i)reg(?:\.exe)?\s+(?:export|import)\b" },
    @{ Name = 'image-name process termination'; Pattern = "(?i)(?:taskkill|Stop-Process)[^\r\n]*LiangWenPeak" },
    @{ Name = 'production credential resource'; Pattern = "LiangWenPeak\.DeepSeekApi" },
    @{ Name = 'production Toast AUMID'; Pattern = "zeronx798\.LiangWenPeak(?!\.Test\.)" },
    @{ Name = 'production Start Menu shortcut'; Pattern = "(?<!Test )LiangWenPeak\.lnk" }
)

$failures = @()
foreach ($script in $testScripts) {
    $text = [IO.File]::ReadAllText($script.FullName)
    foreach ($rule in $forbidden) {
        if ([Text.RegularExpressions.Regex]::IsMatch($text, $rule.Pattern)) {
            $failures += "$($script.FullName): $($rule.Name)"
        }
    }

    if ($text -match '(?i)(?:LiangWenPeak\.App\.exe|LiangWenPeak\.exe)' -and
        $script.Name -notin @('Test-BuildPipeline.ps1', 'Test-TestIsolation.ps1') -and
        $text -notmatch 'Start-Isolated(?:TestProcess|PortableApplication)') {
        $failures += "$($script.FullName): executable test lacks Start-IsolatedTestProcess"
    }
}

foreach ($uiScript in @(
    @{ Name = 'Test-BalanceExtension.ps1'; Functions = @(
        'Find-Element', 'Find-InvokableElementByName', 'Find-ToggleElementByName',
        'Find-WindowByName', 'Find-ToggleElementByAutomationId') },
    @{ Name = 'Test-AboutDialog.ps1'; Functions = @(
        'Find-ElementByProperty', 'Find-ElementByNameAndControlType') })) {
    $uiPath = Join-Path $repositoryRoot "tests\LiangWenPeak.Ui.Tests\$($uiScript.Name)"
    $tokens = $null
    $parseErrors = $null
    $uiAst = [Management.Automation.Language.Parser]::ParseFile(
        $uiPath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        $failures += "$uiPath`: UI Automation script contains parser errors"
    }
    foreach ($functionName in $uiScript.Functions) {
        $functionAst = $uiAst.Find({
            param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq $functionName
        }, $true)
        if ($null -eq $functionAst -or
            $functionAst.Body.Extent.Text -notmatch 'Get-TestAutomationCondition') {
            $failures += "$uiPath`: $functionName lacks isolated-process filtering"
        }
    }
}

if ($failures.Count -ne 0) {
    throw "Test isolation audit failed:`n$($failures -join "`n")"
}

Write-Host 'PASS: test scripts contain no production Registry, credential, AUMID, shortcut, or image-name termination paths'
Write-Host 'PASS: balance/About UI desktop lookups require isolated-process filtering'
