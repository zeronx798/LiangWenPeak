[CmdletBinding()]
param(
    [string]$ProcessName = 'LiangWenPeak.App',

    [string]$Name = ('ui-{0:yyyyMMdd-HHmmss}' -f (Get-Date)),

    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ArtifactPaths.ps1')

if (-not ('UiValidation.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace UiValidation
{
    public static class NativeMethods
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct Rect
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr window, out Rect rect);

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
    }
}
'@
}

Add-Type -AssemblyName System.Drawing

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Get-UiValidationArtifactPath -Kind Screenshot -Name $Name -Extension '.png'
} else {
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($OutputPath)) -Force | Out-Null
}

$process = Get-Process -Name $ProcessName -ErrorAction Stop | Select-Object -First 1
$process.Refresh()
if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
    throw "Process '$ProcessName' does not have a visible main window."
}

$rect = [UiValidation.NativeMethods+Rect]::new()
if (-not [UiValidation.NativeMethods]::GetPhysicalWindowRect($process.MainWindowHandle, [ref]$rect)) {
    throw "GetWindowRect failed for process '$ProcessName'."
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    throw "Process '$ProcessName' reported an invalid window rectangle."
}

$bitmap = [Drawing.Bitmap]::new($width, $height)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

Write-Output $OutputPath
