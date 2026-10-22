[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$directory = Join-Path $root 'resources\icons'
& (Join-Path $root 'scripts\generate-icons.ps1')
$before = @(Get-ChildItem $directory -File | Get-FileHash -Algorithm SHA256)
& (Join-Path $root 'scripts\generate-icons.ps1')
$after = @(Get-ChildItem $directory -File | Get-FileHash -Algorithm SHA256)
if (Compare-Object $before $after -Property Path,Hash) { throw 'Icon generation is not deterministic.' }
Add-Type -AssemblyName System.Drawing
$sizes = @(16,20,24,32,40,48,64,128,256)
$reader = [IO.BinaryReader]::new([IO.File]::OpenRead((Join-Path $directory 'app.ico')))
try {
    if ($reader.ReadUInt16() -ne 0 -or $reader.ReadUInt16() -ne 1 -or
        $reader.ReadUInt16() -ne $sizes.Count) { throw 'Invalid ICO directory.' }
    foreach ($size in $sizes) {
        $width=$reader.ReadByte(); $height=$reader.ReadByte()
        $expected=if($size -eq 256){0}else{$size}
        if($width -ne $expected -or $height -ne $expected){throw 'ICO frame dimensions differ.'}
        [void]$reader.ReadUInt16()
        if($reader.ReadUInt16() -ne 1 -or $reader.ReadUInt16() -ne 32){throw 'ICO frame depth differs.'}
        $length=$reader.ReadUInt32(); $offset=$reader.ReadUInt32()
        $next=$reader.BaseStream.Position
        $reader.BaseStream.Position=$offset
        $embedded=$reader.ReadBytes($length)
        $png=[IO.File]::ReadAllBytes((Join-Path $directory "app-$size.png"))
        if([Convert]::ToBase64String($png) -ne [Convert]::ToBase64String($embedded)) {
            throw 'ICO frame does not match its generated PNG.'
        }
        $reader.BaseStream.Position=$next
        $bitmap=[Drawing.Bitmap]::new((Join-Path $directory "app-$size.png"))
        try {
            if($bitmap.Width -ne $size -or $bitmap.Height -ne $size){throw 'PNG dimensions differ.'}
            $visible=0; $solidBlue=0
            for($y=0;$y -lt $size;++$y){for($x=0;$x -lt $size;++$x){
                $pixel=$bitmap.GetPixel($x,$y)
                if(($x -eq 0 -or $x -eq ($size-1)) -and ($y -eq 0 -or $y -eq ($size-1)) -and
                    $pixel.A -gt 0){throw "Icon touches canvas edge at size $size."}
                if($pixel.A -gt 0){++$visible}
                if($pixel.A -eq 255 -and $pixel.R -eq 82 -and $pixel.G -eq 161 -and $pixel.B -eq 243){++$solidBlue}
            }}
            if($visible -eq 0 -or $solidBlue -eq 0){throw 'Empty icon or wrong source color.'}
        } finally {$bitmap.Dispose()}
    }
} finally {$reader.Dispose()}
Write-Host 'PASS: reproducible static icons, PNG/ICO identity, dimensions, blue paint and transparent margins'
