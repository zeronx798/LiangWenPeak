[CmdletBinding()]
param(
    [string]$ProcessName = 'LiangWenPeak.App',

    [long]$WindowHandle,

    [string]$Name = ('ui-{0:yyyyMMdd-HHmmss}' -f (Get-Date)),

    [string]$OutputPath,

    [switch]$PassThru
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

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetWindowAttribute(
            IntPtr window,
            int attribute,
            out Rect value,
            int valueSize);

        [DllImport("user32.dll")]
        public static extern uint GetDpiForWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

        public static bool GetPhysicalExtendedFrameRect(IntPtr window, out Rect rect)
        {
            IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4));
            try
            {
                const int DwmwaExtendedFrameBounds = 9;
                return DwmGetWindowAttribute(
                    window,
                    DwmwaExtendedFrameBounds,
                    out rect,
                    Marshal.SizeOf<Rect>()) == 0;
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

if ($WindowHandle -ne 0) {
    $handle = [IntPtr]$WindowHandle
} else {
    $process = Get-Process -Name $ProcessName -ErrorAction Stop | Select-Object -First 1
    $process.Refresh()
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) {
        throw "Process '$ProcessName' does not have a visible main window."
    }
}

$rect = [UiValidation.NativeMethods+Rect]::new()
if (-not [UiValidation.NativeMethods]::GetPhysicalExtendedFrameRect($handle, [ref]$rect)) {
    throw "DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS) failed for HWND $handle."
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

$result = [pscustomobject]@{
    OutputPath = $OutputPath
    WindowHandle = $handle.ToInt64()
    Left = $rect.Left
    Top = $rect.Top
    Width = $width
    Height = $height
    Dpi = [UiValidation.NativeMethods]::GetDpiForWindow($handle)
}
if ($PassThru) {
    Write-Output $result
} else {
    Write-Output $OutputPath
}
