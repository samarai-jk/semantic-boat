<#
.SYNOPSIS
Live event monitor for can-mcu firmware: follows the serial and SWD logs,
shows events in colour, and sends typed commands to the firmware console.

.DESCRIPTION
Reads build/logs/serial.log (written by script/serial-log.ps1, which must be
running) and build/logs/swd.log (written by script/swd-probe.ps1). Periodic
lines such as the per-second module values are hidden unless -All is given;
events are highlighted:

  RESTART      reset cause after a restart (red)
  FATAL/POWER  fatal errors, supply drops below 2.9 V, crash or hang of the previous run (red)
  health       restart statistics and the previous run (white)
  ALARM        measurement alarms becoming active (red: alarm/emergency, yellow: warning)
  alarm/sensor alarms clearing or acknowledged, sensor console replies (white)
  values       measurement values every 5 s, sensors found or lost (green)
  button       button presses (cyan)
  supply       supply dips below the firmware threshold (yellow)
  can          CAN controller state changes (magenta)
  module/i2c   input module and I2C devices appearing, digital input changes (green)
  console      console commands and replies (white)
  swd          SWD probe results (dark cyan)
  warnings and errors not covered above (yellow / red)
  heartbeat    every 10 s, as a sign of life (dark gray)

If the serial log stays silent for -SilenceSeconds, the monitor reports it
once (board unpowered, hung, or the serial logger not running).

Type a command and press Enter to send it to the firmware console (for
example "help", "ain", "supply 3200"). Commands are passed to the serial
logger through build/logs/serial-command.txt. Stop with Ctrl+C.

.EXAMPLE
script\monitor.ps1
script\monitor.ps1 -All
#>
param(
    [switch]$All,
    [int]$SilenceSeconds = 15,
    [int]$History = 300
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $repoRoot "build\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$serialLog = Join-Path $logDir "serial.log"
$swdLog = Join-Path $logDir "swd.log"
$commandFile = Join-Path $logDir "serial-command.txt"

$interactive = $false
try { $interactive = -not [Console]::IsInputRedirected } catch { $interactive = $false }

# Each rule: pattern, colour, label. The first matching rule wins; a null
# colour hides the line unless -All is given.
$serialRules = @(
    @{ Pattern = 'reset cause: (.+)$'; Color = 'Red'; Label = 'RESTART' },
    @{ Pattern = '\*\*\* FATAL'; Color = 'Red'; Label = 'FATAL' },
    @{ Pattern = 'power: VDD fell'; Color = 'Red'; Label = 'POWER' },
    @{ Pattern = 'health: (previous run (ended|hung)|.*hung)'; Color = 'Red'; Label = 'health' },
    @{ Pattern = 'health:|supervision:'; Color = 'White'; Label = 'health' },
    @{ Pattern = 'alarm: .*(ALARM|EMERGENCY) ACTIVE'; Color = 'Red'; Label = 'ALARM' },
    @{ Pattern = 'alarm: .*WARNING ACTIVE'; Color = 'Yellow'; Label = 'ALARM' },
    @{ Pattern = '\] [IWE] alarm: '; Color = 'White'; Label = 'alarm' },
    @{ Pattern = '\] [IWE] (sensors?:|ack:|[a-z][a-z0-9_-]*: ((temperature|humidity|voltage) instance|offset|stored|applied|defaults|statistics|no valid|cal2|gain))'; Color = 'White'; Label = 'sensor' },
    @{ Pattern = '\] [IWE] values: '; Color = 'Green'; Label = 'values' },
    @{ Pattern = '\] [IWE] (dht20|sht31|ds18b20|ct1780|veml7700)[ :]'; Color = 'Green'; Label = 'sensor' },
    @{ Pattern = 'lab-test-01 \(.*built'; Color = 'DarkRed'; Label = 'boot' },
    @{ Pattern = 'BF\d pressed'; Color = 'Cyan'; Label = 'button' },
    @{ Pattern = 'supply dip'; Color = 'Yellow'; Label = 'supply' },
    @{ Pattern = 'can state:'; Color = 'Magenta'; Label = 'can' },
    @{ Pattern = 'input module: (DI\d|ADC)'; Color = 'Green'; Label = 'module' },
    @{ Pattern = 'i2c 0x[0-9A-F]{2} (appeared|disappeared)|i2c: \d+ device'; Color = 'Green'; Label = 'i2c' },
    @{ Pattern = 'n2k: (claimed address \d+$|address|.*lower NAME|.*NAME wins|no free address)'; Color = 'DarkGreen'; Label = 'n2k' },
    @{ Pattern = '\] I > '; Color = 'White'; Label = 'console' },
    @{ Pattern = '\] [IWE] (cal |ain |supply:|commands:|  |console:|usage:)'; Color = 'White'; Label = 'console' },
    @{ Pattern = 'heartbeat:'; Color = 'DarkGray'; Label = 'heartbeat' },
    @{ Pattern = '--- serial-log: sent'; Color = 'DarkGray'; Label = 'sent' },
    @{ Pattern = '\] E '; Color = 'Red'; Label = 'error' },
    @{ Pattern = '\] W '; Color = 'Yellow'; Label = 'warning' },
    @{ Pattern = '--- serial-log: .* opened'; Color = 'DarkGray'; Label = 'logger' }
)

function Show-Line([string]$line, [string]$source) {
    $line = $line.TrimEnd("`r")
    if (-not $line) { return }
    if ($source -eq 'swd') {
        if ($line -match '^--- swd-probe') { Write-Host $line -ForegroundColor DarkGray }
        else { Write-Host ("{0,-9} {1}" -f 'swd', $line) -ForegroundColor DarkCyan }
        return
    }
    foreach ($rule in $serialRules) {
        if ($line -match $rule.Pattern) {
            Write-Host ("{0,-9} {1}" -f $rule.Label, $line) -ForegroundColor $rule.Color
            return
        }
    }
    if ($All) { Write-Host ("{0,-9} {1}" -f '', $line) -ForegroundColor Gray }
}

# Follows a growing text file; starts at its end and survives truncation.
class Follower {
    [string]$Path
    [long]$Position = 0
    [string]$Pending = ""

    Follower([string]$path, [int]$history) {
        $this.Path = $path
        if (Test-Path $path) { $this.Position = (Get-Item $path).Length }
    }

    [string[]] Read() {
        if (-not (Test-Path $this.Path)) { return @() }
        $length = (Get-Item $this.Path).Length
        if ($length -lt $this.Position) { $this.Position = 0; $this.Pending = "" }
        if ($length -eq $this.Position) { return @() }
        [string]$text = ""
        $stream = [System.IO.File]::Open($this.Path, 'Open', 'Read', 'ReadWrite, Delete')
        try {
            $stream.Seek($this.Position, 'Begin') | Out-Null
            $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
            $text = $this.Pending + $reader.ReadToEnd()
            $this.Position = $stream.Position
        } finally { $stream.Dispose() }
        $parts = $text -split "`n"
        $this.Pending = $parts[-1]
        if ($parts.Count -le 1) { return @() }
        return $parts[0..($parts.Count - 2)]
    }
}

Write-Host "[monitor] Following $serialLog and $swdLog$(if ($All) { ' (all lines)' } else { ' (events only, -All shows everything)' })" -ForegroundColor DarkGray
if (Test-Path $serialLog) {
    Write-Host "[monitor] Recent events:" -ForegroundColor DarkGray
    Get-Content $serialLog -Tail $History | ForEach-Object { Show-Line $_ 'serial' }
}
Write-Host "[monitor] Live. Type a console command and press Enter (try 'help')." -ForegroundColor DarkGray

$serial = [Follower]::new($serialLog, $History)
$swd = [Follower]::new($swdLog, $History)
$lastSerialAt = Get-Date
$silent = $false
$typed = ""

while ($true) {
    $lines = $serial.Read()
    if ($lines.Count -gt 0) {
        $lastSerialAt = Get-Date
        if ($silent) {
            Write-Host ("{0,-9} board output resumed" -f 'monitor') -ForegroundColor Green
            $silent = $false
        }
        foreach ($line in $lines) { Show-Line $line 'serial' }
    } elseif (-not $silent -and ((Get-Date) - $lastSerialAt).TotalSeconds -ge $SilenceSeconds) {
        Write-Host ("{0,-9} no serial output for {1} s: board unpowered or hung, or serial-log.ps1 not running" -f 'SILENT', $SilenceSeconds) -ForegroundColor Red
        $silent = $true
    }
    foreach ($line in $swd.Read()) { Show-Line $line 'swd' }

    if ($interactive) {
        while ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq "Enter") {
                Write-Host ""
                if ($typed.Trim()) { Add-Content -Path $commandFile -Value $typed.Trim() }
                $typed = ""
            } elseif ($key.Key -eq "Backspace") {
                if ($typed.Length -gt 0) { $typed = $typed.Substring(0, $typed.Length - 1) }
            } elseif ($key.KeyChar -ne [char]0) {
                $typed += $key.KeyChar
                Write-Host -NoNewline $key.KeyChar -ForegroundColor White
            }
        }
    }
    Start-Sleep -Milliseconds 100
}
