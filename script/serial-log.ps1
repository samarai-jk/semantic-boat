<#
.SYNOPSIS
Shows the firmware log from the ST-Link virtual COM port, appends it to a
file, and sends console commands to the firmware.

.DESCRIPTION
Only one program can open a COM port. This script owns the port, prints the
log to the console, and appends it to build/logs/<Name>.log so other tools
(an editor tab, `Get-Content -Wait`, or an agent) can read the same stream.

Commands reach the firmware console in two ways:
- typed into this terminal (sent on Enter), when the script runs interactively;
- written as lines to build/logs/<Name>-command.txt by any other tool; the
  script sends each line and deletes the file.

It reconnects automatically when the port disappears, for example while the
probe is unplugged. Stop with Ctrl+C.

.EXAMPLE
script\serial-log.ps1
script\serial-log.ps1 -Port COM4 -Name lab-test-01
Set-Content build\logs\serial-command.txt "cal 0x48 1 11.972"
#>
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$Name = "serial"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $repoRoot "build\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logFile = Join-Path $logDir "$Name.log"
$commandFile = Join-Path $logDir "$Name-command.txt"

$interactive = $false
try { $interactive = -not [Console]::IsInputRedirected } catch { $interactive = $false }

function Find-StLinkPort {
    $device = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match "STLink Virtual COM Port \((COM\d+)\)" } |
        Select-Object -First 1
    if ($device -and $device.Name -match "\((COM\d+)\)") { return $Matches[1] }
    return $null
}

function Send-Command($serial, $writer, [string]$command) {
    $command = $command.Trim()
    if (-not $command) { return }
    $serial.Write("$command`r`n")
    $writer.WriteLine("--- serial-log: sent '$command' ---")
}

Write-Host "[serial-log] Writing to $logFile"
Write-Host "[serial-log] Commands: type here (Enter sends) or write lines to $commandFile"
$waitingReported = $false
$typed = ""
while ($true) {
    $portName = if ($Port) { $Port } else { Find-StLinkPort }
    if (-not $portName) {
        if (-not $waitingReported) { Write-Host "[serial-log] Waiting for the ST-Link VCP..." }
        $waitingReported = $true
        Start-Sleep -Seconds 1
        continue
    }

    $serial = New-Object System.IO.Ports.SerialPort $portName, $Baud, "None", 8, "One"
    $writer = $null
    try {
        $serial.Open()
        $waitingReported = $false
        $writer = [System.IO.StreamWriter]::new($logFile, $true)
        $writer.AutoFlush = $true
        $marker = "--- serial-log: $portName $Baud opened $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ---"
        $writer.WriteLine($marker)
        Write-Host "[serial-log] $marker"
        while ($true) {
            if (Test-Path $commandFile) {
                $lines = @(Get-Content $commandFile)
                Remove-Item $commandFile -Force
                foreach ($line in $lines) { Send-Command $serial $writer $line }
            }
            if ($interactive) {
                while ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    if ($key.Key -eq "Enter") {
                        Write-Host ""
                        Send-Command $serial $writer $typed
                        $typed = ""
                    } elseif ($key.Key -eq "Backspace") {
                        if ($typed.Length -gt 0) { $typed = $typed.Substring(0, $typed.Length - 1) }
                    } elseif ($key.KeyChar -ne [char]0) {
                        $typed += $key.KeyChar
                        Write-Host -NoNewline $key.KeyChar
                    }
                }
            }
            $data = $serial.ReadExisting()
            if ($data) {
                $writer.Write($data)
                Write-Host -NoNewline $data
            } else {
                Start-Sleep -Milliseconds 50
            }
        }
    } catch {
        Write-Host "[serial-log] $portName unavailable: $($_.Exception.Message)"
        Start-Sleep -Seconds 1
    } finally {
        if ($serial.IsOpen) { $serial.Close() }
        if ($writer) { $writer.Close() }
    }
}
