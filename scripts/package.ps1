[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Architecture = 'x64',

    [switch]$SkipBuild,

    [switch]$FullLauncherTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

function Copy-ApplicationPayload {
    param(
        [Parameter(Mandatory)]$Context,
        [Parameter(Mandatory)][string]$Destination
    )

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Context.BuildOutput -Force) {
        if ($item.PSIsContainer) {
            if ($item.Name -match '(?i)^(?:data|ui-validation)$') {
                continue
            }
            Copy-Item -LiteralPath $item.FullName -Destination $Destination -Recurse -Force
            continue
        }

        if ($item.Name -eq 'LiangWenPeak.exe' -or
            (Test-ForbiddenPackageFile -File $item -PackageRoot $Context.BuildOutput)) {
            continue
        }
        Copy-Item -LiteralPath $item.FullName -Destination $Destination -Force
    }

    $forbiddenPayloadFiles = @(Get-ChildItem -LiteralPath $Destination -Recurse -File |
        Where-Object { Test-ForbiddenPackageFile -File $_ -PackageRoot $Destination })
    foreach ($file in $forbiddenPayloadFiles) {
        Remove-Item -LiteralPath $file.FullName -Force
    }

    $validationDirectories = @(Get-ChildItem -LiteralPath $Destination -Recurse -Directory |
        Where-Object { $_.Name -match '(?i)^ui-validation$' } |
        Sort-Object FullName -Descending)
    foreach ($directory in $validationDirectories) {
        Remove-Item -LiteralPath $directory.FullName -Recurse -Force
    }
}

function Assert-PortableArchive {
    param(
        [Parameter(Mandatory)]$Context,
        [Parameter(Mandatory)][string]$ArchivePath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        $requiredEntries = @(
            'LiangWenPeak.exe',
            'current.txt',
            "app-$($Context.Version)/LiangWenPeak.App.exe"
        )
        foreach ($requiredEntry in $requiredEntries) {
            if ($entryNames -notcontains $requiredEntry) {
                throw "ERROR: ZIP validation failed; required entry is missing: $requiredEntry"
            }
        }

        $unexpectedWrapper = @($entryNames | Where-Object { $_ -like "$($Context.PackageName)/*" })
        if ($unexpectedWrapper.Count -gt 0) {
            throw 'ERROR: ZIP validation failed; archive contains an extra package-name directory layer.'
        }

        $dataEntries = @($entryNames | Where-Object { $_ -match '(?i)^data(/|$)' })
        if ($dataEntries.Count -gt 0) {
            throw 'ERROR: ZIP validation failed; archive contains runtime user data.'
        }
    } finally {
        $archive.Dispose()
    }
}

try {
    $context = Get-BuildContext -Configuration $Configuration -Architecture $Architecture
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -Architecture $Architecture
        if (-not $?) {
            throw 'ERROR: build.ps1 failed.'
        }
    }

    Clear-PackageArtifacts -Context $context
    Assert-PackageBuildOutputs -Context $context

    $stagingDirectory = $context.PackageStagingDirectory
    $applicationDirectory = Join-Path $stagingDirectory "app-$($context.Version)"
    Write-Host "[STAGE] Creating $($context.PackageName)..."
    Copy-ApplicationPayload -Context $context -Destination $applicationDirectory

    Copy-Item `
        -LiteralPath (Join-Path $context.BuildOutput 'LiangWenPeak.exe') `
        -Destination (Join-Path $stagingDirectory 'LiangWenPeak.exe') `
        -Force

    $utf8WithoutBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText(
        (Join-Path $stagingDirectory 'current.txt'),
        "$($context.Version)`r`n",
        $utf8WithoutBom)

    Write-Host '[VALIDATE] Checking portable package contents...'
    Assert-PortablePackage -Context $context -PackageDirectory $stagingDirectory

    $launcherTest = Join-Path $context.RepositoryRoot 'tests\LiangWenPeak.Launcher.Tests\Test-Launcher.ps1'
    if (-not (Test-Path -LiteralPath $launcherTest -PathType Leaf)) {
        throw "ERROR: launcher smoke test was not found: $launcherTest"
    }

    Write-Host '[SMOKE] Launching the staged portable application...'
    $launcherTestArguments = @{
        Configuration = $Configuration
        Platform = $Architecture
        SkipStage = $true
        DistributionRoot = $context.PackageSmokeDirectory
    }
    if (-not $FullLauncherTests) {
        $launcherTestArguments.SmokeOnly = $true
    }
    New-Item -ItemType Directory -Path $context.PackageSmokeDirectory -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $stagingDirectory -Force) {
        Copy-Item `
            -LiteralPath $item.FullName `
            -Destination $context.PackageSmokeDirectory `
            -Recurse `
            -Force
    }
    & $launcherTest @launcherTestArguments
    if (-not $?) {
        throw 'ERROR: staged launcher smoke test failed.'
    }
    Write-Host '[SMOKE] PASS'
    Assert-PortablePackage -Context $context -PackageDirectory $stagingDirectory

    Write-Host '[PACKAGE] Creating portable ZIP...'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory(
        $stagingDirectory,
        $context.PackageArchive,
        [IO.Compression.CompressionLevel]::Optimal,
        $false)

    Assert-PortableArchive -Context $context -ArchivePath $context.PackageArchive
    Sync-PortablePackage `
        -Source $stagingDirectory `
        -Destination $context.PackageDirectory `
        -AllowedParent (Join-Path $context.RepositoryRoot 'dist')
    Write-Host '[PACKAGE] PASS'

    [PSCustomObject]@{
        Context = $context
        UncompressedSize = Get-DirectorySize $stagingDirectory
        ZipSize = (Get-Item -LiteralPath $context.PackageArchive).Length
        Sha256 = (Get-FileHash -LiteralPath $context.PackageArchive -Algorithm SHA256).Hash
    }
} catch {
    if ($null -ne (Get-Variable context -ErrorAction SilentlyContinue)) {
        if ($null -ne $context -and (Test-Path -LiteralPath $context.PackageArchive -PathType Leaf)) {
            Remove-GeneratedFile $context.PackageArchive (Join-Path $context.RepositoryRoot 'dist')
        }
    }
    throw
} finally {
    if ($null -ne (Get-Variable context -ErrorAction SilentlyContinue) -and $null -ne $context) {
        $buildRoot = Join-Path $context.RepositoryRoot 'build'
        Remove-GeneratedDirectory $context.PackageStagingDirectory $buildRoot
        Remove-GeneratedDirectory $context.PackageSmokeDirectory $buildRoot
    }
}
