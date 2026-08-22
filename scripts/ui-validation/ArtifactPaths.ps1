Set-StrictMode -Version Latest

function Get-UiValidationArtifactPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [ValidateSet('Screenshot', 'Recording')]
        [string]$Kind,

        [Parameter(Mandatory)]
        [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._-]*$')]
        [string]$Name,

        [Parameter(Mandatory)]
        [ValidateSet('.png', '.mkv')]
        [string]$Extension
    )

    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $leaf = if ($Kind -eq 'Screenshot') { 'screenshots' } else { 'recordings' }
    $directory = Join-Path $repositoryRoot "artifacts\ui-validation\$leaf"
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    return Join-Path $directory ($Name + $Extension)
}
