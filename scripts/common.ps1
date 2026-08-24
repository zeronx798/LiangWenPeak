Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:LiangWenPeakRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Get-RepoRoot {
    return $script:LiangWenPeakRepositoryRoot
}

function Test-ReleaseVersion([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }

    return $Value -match '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$'
}

function Get-SourceVersion {
    [CmdletBinding()]
    param([string]$RepositoryRoot = (Get-RepoRoot))

    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    $canonicalPath = Join-Path $root 'Version.props'
    if (-not (Test-Path -LiteralPath $canonicalPath -PathType Leaf)) {
        throw "ERROR: canonical version file was not found: $canonicalPath"
    }

    try {
        [xml]$canonicalDocument = [IO.File]::ReadAllText($canonicalPath)
    } catch {
        throw "ERROR: Version.props is not valid XML: $($_.Exception.Message)"
    }

    $canonicalNodes = @($canonicalDocument.SelectNodes("/*[local-name()='Project']/*[local-name()='PropertyGroup']/*[local-name()='LiangWenPeakVersion']"))
    if ($canonicalNodes.Count -ne 1) {
        throw "ERROR: Version.props must contain exactly one LiangWenPeakVersion definition; found $($canonicalNodes.Count)."
    }

    $version = $canonicalNodes[0].InnerText.Trim()
    if (-not (Test-ReleaseVersion $version)) {
        throw "ERROR: Version.props contains an invalid LiangWenPeakVersion: '$version'. Expected SemVer such as 1.0.0."
    }

    $definitionFiles = @()
    $definitionFiles += Get-ChildItem -LiteralPath $root -File -Filter *.props
    foreach ($sourceDirectoryName in @('src', 'tests')) {
        $sourceDirectory = Join-Path $root $sourceDirectoryName
        if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
            $definitionFiles += Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File |
                Where-Object { $_.Extension -in '.props', '.vcxproj' }
        }
    }

    $conflicts = @()
    foreach ($file in $definitionFiles | Sort-Object FullName -Unique) {
        try {
            [xml]$document = [IO.File]::ReadAllText($file.FullName)
        } catch {
            throw "ERROR: version-bearing project file is not valid XML: $($file.FullName)"
        }

        $nodes = @($document.SelectNodes("//*[local-name()='LiangWenPeakVersion' or local-name()='Version' or local-name()='VersionPrefix']"))
        foreach ($node in $nodes) {
            $candidate = $node.InnerText.Trim()
            if ([string]::IsNullOrWhiteSpace($candidate) -or $candidate.Contains('$(')) {
                continue
            }

            if (-not (Test-ReleaseVersion $candidate)) {
                $conflicts += "$($file.FullName) <$($node.LocalName)>='$candidate' (invalid release version)"
            } elseif ($candidate -ne $version) {
                $conflicts += "$($file.FullName) <$($node.LocalName)>='$candidate'"
            }
        }
    }

    $versionHeaders = @()
    foreach ($sourceDirectoryName in @('src', 'tests')) {
        $sourceDirectory = Join-Path $root $sourceDirectoryName
        if (Test-Path -LiteralPath $sourceDirectory -PathType Container) {
            $versionHeaders += Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File -Include Version.h,AppVersion.h
        }
    }

    foreach ($header in $versionHeaders | Sort-Object FullName -Unique) {
        $matches = [regex]::Matches(
            [IO.File]::ReadAllText($header.FullName),
            '(?m)^\s*#define\s+(?:LIANGWENPEAK|APP)_VERSION(?:_STRING)?\s+"([^"]+)"')
        foreach ($match in $matches) {
            $candidate = $match.Groups[1].Value
            if (-not (Test-ReleaseVersion $candidate) -or $candidate -ne $version) {
                $conflicts += "$($header.FullName) #define='$candidate'"
            }
        }
    }

    if ($conflicts.Count -gt 0) {
        $details = $conflicts | ForEach-Object { "- $_" }
        throw "ERROR: conflicting release version definitions were found.`nCanonical: $canonicalPath = $version`n$($details -join "`n")"
    }

    return $version
}

function Get-RequiredWindowsSdkVersion {
    [CmdletBinding()]
    param([string]$RepositoryRoot = (Get-RepoRoot))

    $propsPath = Join-Path ([IO.Path]::GetFullPath($RepositoryRoot)) 'Directory.Build.props'
    if (-not (Test-Path -LiteralPath $propsPath -PathType Leaf)) {
        throw "ERROR: Directory.Build.props was not found: $propsPath"
    }

    [xml]$document = [IO.File]::ReadAllText($propsPath)
    $nodes = @($document.SelectNodes("/*[local-name()='Project']/*[local-name()='PropertyGroup']/*[local-name()='WindowsTargetPlatformVersion']"))
    if ($nodes.Count -ne 1) {
        throw "ERROR: Directory.Build.props must define exactly one WindowsTargetPlatformVersion."
    }

    $version = $nodes[0].InnerText.Trim()
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "ERROR: invalid WindowsTargetPlatformVersion: '$version'."
    }
    return $version
}

function Find-VSWhere {
    [CmdletBinding()]
    param([string]$Path)

    if (-not [string]::IsNullOrWhiteSpace($Path)) {
        $candidate = [IO.Path]::GetFullPath($Path)
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
        throw "ERROR: vswhere.exe was not found at the requested path: $candidate"
    }

    $standardPath = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $standardPath -PathType Leaf) {
        return $standardPath
    }

    $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw @'
ERROR: vswhere.exe was not found.

Install Visual Studio 2022 or Build Tools 2022 with:
- Desktop development with C++
- MSVC v143
- Windows 10/11 SDK
'@
}

function Find-VisualStudio {
    [CmdletBinding()]
    param([string]$VSWherePath = (Find-VSWhere))

    if (-not (Test-Path -LiteralPath $VSWherePath -PathType Leaf)) {
        throw "ERROR: vswhere executable was not found: $VSWherePath"
    }

    $arguments = @(
        '-latest',
        '-products', '*',
        '-version', '[17.0,18.0)',
        '-requires', 'Microsoft.Component.MSBuild', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-format', 'json',
        '-utf8'
    )
    $output = @(& $VSWherePath @arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "ERROR: vswhere failed with exit code $exitCode.`n$($output -join "`n")"
    }

    try {
        $parsedInstances = ($output -join "`n") | ConvertFrom-Json
        $instances = @($parsedInstances)
    } catch {
        throw "ERROR: vswhere returned invalid JSON: $($_.Exception.Message)"
    }

    if ($null -eq $parsedInstances -or $instances.Count -eq 0) {
        throw @'
ERROR: Visual Studio 2022 or Build Tools 2022 with Desktop development with C++ was not found.

Required:
- MSVC v143 (x64/x86 build tools)
- MSBuild
- Windows 10/11 SDK
'@
    }

    return $instances[0]
}

function Find-MSBuild {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$VisualStudio)

    $installationPath = [IO.Path]::GetFullPath([string]$VisualStudio.installationPath)
    $candidates = @(
        (Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $installationPath 'MSBuild\17.0\Bin\MSBuild.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "ERROR: MSBuild.exe was not found in Visual Studio installation: $installationPath"
}

function Find-MsvcToolchain {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$VisualStudio,
        [ValidateSet('x64')][string]$Architecture = 'x64'
    )

    $toolsRoot = Join-Path ([string]$VisualStudio.installationPath) 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $toolsRoot -PathType Container)) {
        throw "ERROR: MSVC v143 tools directory was not found: $toolsRoot"
    }

    $toolsets = @(Get-ChildItem -LiteralPath $toolsRoot -Directory | Sort-Object Name -Descending)
    foreach ($toolset in $toolsets) {
        $compiler = Join-Path $toolset.FullName "bin\Hostx64\$Architecture\cl.exe"
        if (Test-Path -LiteralPath $compiler -PathType Leaf) {
            return [PSCustomObject]@{
                Version = $toolset.Name
                CompilerPath = $compiler
            }
        }
    }

    throw "ERROR: MSVC v143 x64 compiler was not found under: $toolsRoot"
}

function Find-WindowsSdk {
    [CmdletBinding()]
    param(
        [string]$RepositoryRoot = (Get-RepoRoot),
        [string]$KitsRootOverride
    )

    $requiredVersion = Get-RequiredWindowsSdkVersion $RepositoryRoot
    $roots = @()
    if (-not [string]::IsNullOrWhiteSpace($KitsRootOverride)) {
        $roots += $KitsRootOverride
    } else {
        try {
            $registryRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction Stop).KitsRoot10
            if (-not [string]::IsNullOrWhiteSpace($registryRoot)) {
                $roots += $registryRoot
            }
        } catch {
            # Fall back to the standard Windows Kits location below.
        }
        $roots += Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10'
    }

    foreach ($root in $roots | Select-Object -Unique) {
        if ([string]::IsNullOrWhiteSpace($root)) {
            continue
        }
        $fullRoot = [IO.Path]::GetFullPath($root)
        $header = Join-Path $fullRoot "Include\$requiredVersion\um\Windows.h"
        $library = Join-Path $fullRoot "Lib\$requiredVersion\um\x64\kernel32.lib"
        if ((Test-Path -LiteralPath $header -PathType Leaf) -and
            (Test-Path -LiteralPath $library -PathType Leaf)) {
            return [PSCustomObject]@{
                Version = $requiredVersion
                Root = $fullRoot
            }
        }
    }

    throw @"
ERROR: Windows SDK $requiredVersion was not found.

Install the Windows 10/11 SDK component for Visual Studio 2022.
Required headers and x64 libraries were not found.
"@
}

function Get-BuildEnvironment {
    [CmdletBinding()]
    param(
        [ValidateSet('x64')][string]$Architecture = 'x64',
        [string]$RepositoryRoot = (Get-RepoRoot)
    )

    $vswhere = Find-VSWhere
    $visualStudio = Find-VisualStudio $vswhere
    $msbuild = Find-MSBuild $visualStudio
    $msvc = Find-MsvcToolchain $visualStudio $Architecture
    $sdk = Find-WindowsSdk $RepositoryRoot

    return [PSCustomObject]@{
        VSWherePath = $vswhere
        VisualStudio = $visualStudio
        MSBuildPath = $msbuild
        Msvc = $msvc
        WindowsSdk = $sdk
    }
}

function Get-BuildContext {
    [CmdletBinding()]
    param(
        [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
        [ValidateSet('x64')][string]$Architecture = 'x64',
        [string]$RepositoryRoot = (Get-RepoRoot)
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    $version = Get-SourceVersion $root
    $target = "windows-$($Architecture.ToLowerInvariant())"
    $packageName = "LiangWenPeak-$version-$target"
    return [PSCustomObject]@{
        RepositoryRoot = $root
        SolutionPath = Join-Path $root 'LiangWenPeak.sln'
        Version = $version
        Configuration = $Configuration
        Architecture = $Architecture
        Target = $target
        BuildOutput = Join-Path $root "build\$Architecture\$Configuration"
        PackageName = $packageName
        PackageStagingDirectory = Join-Path $root "build\package-staging\$packageName"
        PackageSmokeDirectory = Join-Path $root "build\package-smoke\$packageName"
        PackageDirectory = Join-Path $root "dist\$packageName"
        PackageArchive = Join-Path $root "dist\$packageName.zip"
    }
}

function Write-BuildEnvironmentSummary {
    param(
        [Parameter(Mandatory)]$Context,
        [Parameter(Mandatory)]$Environment
    )

    $visualStudio = $Environment.VisualStudio
    $displayVersion = if ($null -ne $visualStudio.catalog) {
        $visualStudio.catalog.productDisplayVersion
    } else {
        $visualStudio.installationVersion
    }

    Write-Host ''
    Write-Host ("LiangWenPeak {0} Build" -f $Context.Configuration)
    Write-Host ''
    Write-Host ("Version       : {0}" -f $Context.Version)
    Write-Host ("Architecture  : {0}" -f $Context.Architecture)
    Write-Host ("Configuration : {0}" -f $Context.Configuration)
    Write-Host ''
    Write-Host ("Visual Studio : {0} {1}" -f $visualStudio.displayName, $displayVersion)
    Write-Host ("MSBuild       : {0}" -f $Environment.MSBuildPath)
    Write-Host ("MSVC          : {0}" -f $Environment.Msvc.Version)
    Write-Host ("Windows SDK   : {0} ({1})" -f $Environment.WindowsSdk.Version, $Environment.WindowsSdk.Root)
    Write-Host ("Repository    : {0}" -f $Context.RepositoryRoot)
    Write-Host ''
}

function Assert-GeneratedPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $fullParent = [IO.Path]::GetFullPath($AllowedParent).TrimEnd('\', '/')
    $prefix = $fullParent + [IO.Path]::DirectorySeparatorChar
    if ($fullPath.Equals($fullParent, [StringComparison]::OrdinalIgnoreCase) -or
        -not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ERROR: refusing to modify generated path outside '$fullParent': $fullPath"
    }
    return $fullPath
}

function Remove-GeneratedDirectory {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $safePath = Assert-GeneratedPath $Path $AllowedParent
    if (Test-Path -LiteralPath $safePath) {
        Remove-Item -LiteralPath $safePath -Recurse -Force
    }
}

function Remove-GeneratedFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $safePath = Assert-GeneratedPath $Path $AllowedParent
    if (Test-Path -LiteralPath $safePath) {
        Remove-Item -LiteralPath $safePath -Force
    }
}

function Clear-GeneratedDirectoryContents {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $safePath = Assert-GeneratedPath $Path $AllowedParent
    if (-not (Test-Path -LiteralPath $safePath -PathType Container)) {
        return
    }

    foreach ($item in Get-ChildItem -LiteralPath $safePath -Force) {
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

function Clear-PackageDirectoryPreservingData {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $safePath = Assert-GeneratedPath $Path $AllowedParent
    if (-not (Test-Path -LiteralPath $safePath -PathType Container)) {
        return
    }

    foreach ($item in Get-ChildItem -LiteralPath $safePath -Force) {
        if ($item.PSIsContainer -and
            $item.Name.Equals('data', [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
    }
}

function Sync-PortablePackage {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][string]$AllowedParent
    )

    $sourcePath = [IO.Path]::GetFullPath($Source)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
        throw "ERROR: portable staging directory was not found: $sourcePath"
    }
    if (Test-Path -LiteralPath (Join-Path $sourcePath 'data')) {
        throw "ERROR: portable staging must not contain a data directory: $sourcePath"
    }

    $destinationPath = Assert-GeneratedPath $Destination $AllowedParent
    Clear-PackageDirectoryPreservingData -Path $destinationPath -AllowedParent $AllowedParent
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $sourcePath -Force) {
        Copy-Item -LiteralPath $item.FullName -Destination $destinationPath -Recurse -Force
    }
}

function Clear-BuildConfiguration {
    param([Parameter(Mandatory)]$Context)

    $buildRoot = Join-Path $Context.RepositoryRoot 'build'
    Clear-GeneratedDirectoryContents $Context.BuildOutput $buildRoot

    $objectRoot = Join-Path $buildRoot 'obj'
    if (Test-Path -LiteralPath $objectRoot -PathType Container) {
        foreach ($projectDirectory in Get-ChildItem -LiteralPath $objectRoot -Directory) {
            $configurationDirectory = Join-Path $projectDirectory.FullName "$($Context.Architecture)\$($Context.Configuration)"
            Clear-GeneratedDirectoryContents $configurationDirectory $objectRoot
        }
    }
}

function Clear-PackageArtifacts {
    param([Parameter(Mandatory)]$Context)

    $distRoot = Join-Path $Context.RepositoryRoot 'dist'
    $buildRoot = Join-Path $Context.RepositoryRoot 'build'
    New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
    Clear-PackageDirectoryPreservingData $Context.PackageDirectory $distRoot
    Remove-GeneratedFile $Context.PackageArchive $distRoot
    Remove-GeneratedDirectory $Context.PackageStagingDirectory $buildRoot
    Remove-GeneratedDirectory $Context.PackageSmokeDirectory $buildRoot
}

function Invoke-CheckedProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf) -and
        $null -eq (Get-Command $FilePath -ErrorAction SilentlyContinue)) {
        throw "ERROR: $Label executable was not found: $FilePath"
    }

    & $FilePath @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "ERROR: $Label failed with exit code $exitCode."
    }
}

function Invoke-TestExecutable {
    param([Parameter(Mandatory)][string]$TestExecutable)

    if (-not (Test-Path -LiteralPath $TestExecutable -PathType Leaf)) {
        throw "ERROR: test executable was not found: $TestExecutable"
    }
    Invoke-CheckedProcess -FilePath $TestExecutable -Label 'Tests'
}

function Assert-PackageBuildOutputs {
    param([Parameter(Mandatory)]$Context)

    $requiredFiles = @(
        (Join-Path $Context.BuildOutput 'LiangWenPeak.exe'),
        (Join-Path $Context.BuildOutput 'LiangWenPeak.App.exe'),
        (Join-Path $Context.BuildOutput 'App.xbf'),
        (Join-Path $Context.BuildOutput 'ApiSettingsWindow.xbf'),
        (Join-Path $Context.BuildOutput 'MainWindow.xbf'),
        (Join-Path $Context.BuildOutput 'LiangWenPeak.pri')
    )
    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "ERROR: required build output is missing; package was not created: $requiredFile"
        }
    }
}

function Test-ValidationArtifactName([string]$Name) {
    return $Name -match '^(?i:compact-|final-|main-|numeral-|startup-|ui-|settings-|baseline)' -or
        $Name -match '(?i)(^|[-_])(probe|sheet)([-_.]|$)' -or
        $Name -match '(?i)ui-validation'
}

function Test-ForbiddenPackageFile {
    param(
        [Parameter(Mandatory)][IO.FileInfo]$File,
        [Parameter(Mandatory)][string]$PackageRoot
    )

    $forbiddenExtensions = @(
        '.pdb', '.lib', '.exp', '.ilk', '.iobj', '.ipdb', '.obj', '.recipe', '.tlog',
        '.mkv', '.mp4', '.avi', '.webm'
    )
    if ($forbiddenExtensions -contains $File.Extension.ToLowerInvariant()) {
        return $true
    }
    if ($File.Name -match '(?i)Tests' -or (Test-ValidationArtifactName $File.Name)) {
        return $true
    }

    $rootPrefix = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $relativePath = $File.FullName.Substring($rootPrefix.Length)
    return $relativePath -match '(?i)(^|[\\/])ui-validation([\\/]|$)'
}

function Assert-PortablePackage {
    param(
        [Parameter(Mandatory)]$Context,
        [string]$PackageDirectory = $Context.PackageDirectory
    )

    $applicationDirectory = Join-Path $PackageDirectory "app-$($Context.Version)"
    $requiredFiles = @(
        (Join-Path $PackageDirectory 'LiangWenPeak.exe'),
        (Join-Path $PackageDirectory 'current.txt'),
        (Join-Path $applicationDirectory 'LiangWenPeak.App.exe'),
        (Join-Path $applicationDirectory 'App.xbf'),
        (Join-Path $applicationDirectory 'ApiSettingsWindow.xbf'),
        (Join-Path $applicationDirectory 'MainWindow.xbf'),
        (Join-Path $applicationDirectory 'LiangWenPeak.pri'),
        (Join-Path $applicationDirectory 'Microsoft.WindowsAppRuntime.dll'),
        (Join-Path $applicationDirectory 'Microsoft.ui.xaml.dll')
    )
    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "ERROR: package validation failed; required file is missing: $requiredFile"
        }
    }

    $xamlDirectory = Join-Path $applicationDirectory 'Microsoft.UI.Xaml'
    if (-not (Test-Path -LiteralPath $xamlDirectory -PathType Container)) {
        throw "ERROR: package validation failed; runtime directory is missing: $xamlDirectory"
    }

    $dataDirectory = Join-Path $PackageDirectory 'data'
    if (Test-Path -LiteralPath $dataDirectory) {
        throw "ERROR: package validation found a runtime data directory: $dataDirectory"
    }

    $currentVersion = [IO.File]::ReadAllText((Join-Path $PackageDirectory 'current.txt')).Trim()
    if ($currentVersion -ne $Context.Version) {
        throw "ERROR: package validation failed; current.txt='$currentVersion', source version='$($Context.Version)'."
    }

    $forbidden = @(Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File |
        Where-Object { Test-ForbiddenPackageFile $_ $PackageDirectory })
    if ($forbidden.Count -gt 0) {
        $details = $forbidden | Select-Object -First 20 | ForEach-Object { "- $($_.FullName)" }
        throw "ERROR: package validation found forbidden artifacts:`n$($details -join "`n")"
    }
}

function Get-DirectorySize([string]$Path) {
    $measurement = Get-ChildItem -LiteralPath $Path -Recurse -File | Measure-Object -Property Length -Sum
    if ($null -eq $measurement.Sum) {
        return [int64]0
    }
    return [int64]$measurement.Sum
}

function Format-ByteSize([int64]$Bytes) {
    if ($Bytes -ge 1GB) {
        return '{0:N2} GB' -f ($Bytes / 1GB)
    }
    if ($Bytes -ge 1MB) {
        return '{0:N2} MB' -f ($Bytes / 1MB)
    }
    if ($Bytes -ge 1KB) {
        return '{0:N2} KB' -f ($Bytes / 1KB)
    }
    return "$Bytes bytes"
}
