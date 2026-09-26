[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Firmware,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$ConfigFile
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$configPath = (Resolve-Path $ConfigFile).Path

if ($Firmware -ne 'remote-a') {
    throw "No SWD configuration staging address is defined for firmware '$Firmware'."
}

$stagingAddress = '0x0803C000'
$stagingBytes = 16KB
$headerBytes = 16
$maximumSourceBytes = $stagingBytes - $headerBytes
$source = [System.IO.File]::ReadAllBytes($configPath)

# Strip an optional UTF-8 BOM. The embedded JSON reader intentionally accepts
# UTF-8 JSON, not transport-specific byte-order markers.
if ($source.Length -ge 3 -and $source[0] -eq 0xEF -and
    $source[1] -eq 0xBB -and $source[2] -eq 0xBF) {
    $trimmed = New-Object byte[] ($source.Length - 3)
    [Array]::Copy($source, 3, $trimmed, 0, $trimmed.Length)
    $source = $trimmed
}

if ($source.Length -eq 0 -or $source.Length -gt $maximumSourceBytes) {
    throw "Configuration is $($source.Length) bytes; the SWD staging limit is $maximumSourceBytes bytes."
}

# Give immediate editor-friendly feedback. The device remains the authoritative
# schema compiler after reset.
$null = [System.Text.Encoding]::UTF8.GetString($source) | ConvertFrom-Json

function Get-Crc32([byte[]]$Bytes) {
    [uint64]$crc = 0xFFFFFFFF
    foreach ($value in $Bytes) {
        $crc = ($crc -bxor [uint64]$value) -band 0xFFFFFFFF
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) {
                $crc = (($crc -shr 1) -bxor 0xEDB88320) -band 0xFFFFFFFF
            } else {
                $crc = ($crc -shr 1) -band 0xFFFFFFFF
            }
        }
    }
    return [uint32](($crc -bxor 0xFFFFFFFF) -band 0xFFFFFFFF)
}

$image = New-Object byte[] ($headerBytes + $source.Length)
[Array]::Copy([BitConverter]::GetBytes([uint32]0x4A434253), 0, $image, 0, 4) # SBCJ
[Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $image, 4, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]$source.Length), 0, $image, 8, 4)
[Array]::Copy([BitConverter]::GetBytes((Get-Crc32 $source)), 0, $image, 12, 4)
[Array]::Copy($source, 0, $image, $headerBytes, $source.Length)

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) "semantic-boat-config-$PID.bin"
$openOcd = if ($env:OPENOCD) { $env:OPENOCD } else {
    'E:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe'
}
$openOcdScripts = if ($env:OPENOCD_SCRIPTS) { $env:OPENOCD_SCRIPTS } else {
    'E:\Tools\xpack-openocd-0.12.0-7\openocd\scripts'
}

try {
    [System.IO.File]::WriteAllBytes($temporary, $image)
    Write-Host "[config] Uploading $($source.Length) JSON bytes to $Firmware staging flash..."
    & $openOcd -s $openOcdScripts -f interface/stlink.cfg -f target/stm32l4x.cfg `
        -c "init; reset halt; flash write_image erase {$temporary} $stagingAddress bin; verify_image {$temporary} $stagingAddress bin; reset run; shutdown"
    if ($LASTEXITCODE -ne 0) { throw "OpenOCD configuration upload failed." }
    Write-Host '[config] Upload complete. The device will compile and commit it to EEPROM.'
} finally {
    if (Test-Path $temporary) { Remove-Item $temporary -Force }
}
