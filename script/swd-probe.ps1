<#
.SYNOPSIS
Reads diagnostic registers of a running can-mcu target over SWD, without
halting it, and appends the results to build/logs/swd.log.

.DESCRIPTION
Only one program can use the ST-Link debug interface at a time: while a VS
Code debug session (or another OpenOCD) is attached, this script cannot
connect, and the other way round. The serial log is not affected.

Probes:
  pins    Levels of PA4..PA7 (module digital inputs) and PA11 (CAN RX) over
          400 samples. PA11 also toggles with the node's own transmissions.
  inputs  Briefly switches PA4..PA7 from pull-up to pull-down: a pin that
          stays high is driven by the connected module output.
  can     bxCAN error and status registers (TEC, REC, last error, state).
  all     All of the above (default).

Modes:
  -Follow       Shows swd.log live, like tail -f, including probes run by
                anyone else (an agent, another terminal). Does not use the
                ST-Link.
  -Every <s>    Repeats the probe every <s> seconds until Ctrl+C. The ST-Link
                is held only during each probe, so flashing and other probes
                can run in between.

.EXAMPLE
script\swd-probe.ps1
script\swd-probe.ps1 can
script\swd-probe.ps1 -Follow
script\swd-probe.ps1 can -Every 2
#>
param(
    [ValidateSet("all", "pins", "inputs", "can")]
    [string]$Probe = "all",
    [switch]$Follow,
    [int]$Every = 0
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $repoRoot "build\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$logFile = Join-Path $logDir "swd.log"

$openocd = if ($env:OPENOCD) { $env:OPENOCD } else { "E:\Tools\xpack-openocd-0.12.0-7\bin\openocd.exe" }
$scripts = if ($env:OPENOCD_SCRIPTS) { $env:OPENOCD_SCRIPTS } else { "E:\Tools\xpack-openocd-0.12.0-7\openocd\scripts" }

$pins = @'
set low {0 0 0 0 0}
set n 400
for {set i 0} {$i < $n} {incr i} {
  set idr [read_memory 0x48000010 32 1]
  set index 0
  foreach bit {4 5 6 7 11} {
    if {(($idr >> $bit) & 1) == 0} { lset low $index [expr {[lindex $low $index] + 1}] }
    incr index
  }
}
echo [format "pins: low in %d samples: PA4 %d, PA5 %d, PA6 %d, PA7 %d, PA11 (CAN RX) %d" $n {*}$low]
'@

$inputs = @'
set pupdr [read_memory 0x4800000C 32 1]
proc highs {} {
  set ones {0 0 0 0}
  for {set i 0} {$i < 50} {incr i} {
    set idr [read_memory 0x48000010 32 1]
    for {set p 0} {$p < 4} {incr p} { if {(($idr >> (4 + $p)) & 1) == 1} { lset ones $p [expr {[lindex $ones $p] + 1}] } }
  }
  return $ones
}
set up [highs]
write_memory 0x4800000C 32 [expr {($pupdr & ~0xFF00) | 0xAA00}]
sleep 5
set down [highs]
write_memory 0x4800000C 32 $pupdr
echo [format "inputs: high of 50 with pull-up:   PA4 %d, PA5 %d, PA6 %d, PA7 %d" {*}$up]
echo [format "inputs: high of 50 with pull-down: PA4 %d, PA5 %d, PA6 %d, PA7 %d (50 = driven by the module)" {*}$down]
'@

$can = @'
set esr [read_memory 0x40006418 32 1]
set msr [read_memory 0x40006404 32 1]
set lec [lindex {none stuff form ack bit-recessive bit-dominant crc software} [expr {($esr >> 4) & 7}]]
set state [expr {($esr & 4) ? "bus-off" : (($esr & 2) ? "error-passive" : (($esr & 1) ? "error-warning" : "error-active"))}]
echo [format "can: %s, TEC %d, REC %d, last error %s, RX level %d (ESR %08x MSR %08x)" $state [expr {($esr >> 16) & 0xFF}] [expr {($esr >> 24) & 0xFF}] $lec [expr {($msr >> 11) & 1}] $esr $msr]
'@

$body = switch ($Probe) {
    "pins" { $pins }
    "inputs" { $inputs }
    "can" { $can }
    default { "$pins`n$inputs`n$can" }
}
if ($Follow) {
    if (-not (Test-Path $logFile)) { New-Item -ItemType File -Path $logFile | Out-Null }
    Write-Host "[swd-probe] Following $logFile (Ctrl+C to stop)"
    Get-Content -Path $logFile -Tail 20 -Wait
    return
}

$tcl = Join-Path $logDir "swd-probe.tcl"
Set-Content -Path $tcl -Value "init`n$body`nexit" -Encoding ascii

function Invoke-Probe {
    # OpenOCD reports on stderr; collect it as text instead of treating it as an error.
    $ErrorActionPreference = "Continue"
    $output = & $openocd -s $scripts -c "gdb port disabled; tcl port disabled; telnet port disabled" `
        -f interface/stlink.cfg -f target/stm32l4x.cfg -f $tcl 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = "Stop"
    $results = @($output | Where-Object { $_ -match '^(pins|inputs|can):' })
    if ($results.Count -eq 0) {
        $results = @("error: no connection to the target") + @($output | Where-Object { $_ -match 'Error' })
    }
    $stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $lines = @("--- swd-probe $Probe $stamp ---") + $results
    Add-Content -Path $logFile -Value $lines
    $lines | ForEach-Object { Write-Host $_ }
}

if ($Every -gt 0) {
    while ($true) {
        Invoke-Probe
        Start-Sleep -Seconds $Every
    }
} else {
    Invoke-Probe
}
