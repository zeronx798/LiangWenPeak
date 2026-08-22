[CmdletBinding()]
param(
    [string]$Name = ('startup-{0:yyyyMMdd-HHmmss}' -f (Get-Date)),

    [ValidateRange(1, 120)]
    [int]$DurationSeconds = 5,

    [ValidateRange(1, 240)]
    [int]$FrameRate = 60,

    [string]$FfmpegPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ArtifactPaths.ps1')

if ([string]::IsNullOrWhiteSpace($FfmpegPath)) {
    $ffmpeg = Get-Command ffmpeg.exe -ErrorAction Stop
    $FfmpegPath = $ffmpeg.Source
}

$outputPath = Get-UiValidationArtifactPath -Kind Recording -Name $Name -Extension '.mkv'
& $FfmpegPath -hide_banner -loglevel error -f gdigrab -framerate $FrameRate -i desktop -t $DurationSeconds -c:v ffv1 -y $outputPath
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed with exit code $LASTEXITCODE."
}

Write-Output $outputPath
