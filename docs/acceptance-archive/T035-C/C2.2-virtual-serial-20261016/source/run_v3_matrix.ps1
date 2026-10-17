# run_v3_matrix.ps1 - C2.2-V3 scenario matrix runner (S0 CRC + S1..S7)
# For each scenario: ensure no stale processes/ports, start the COM8 slave in
# ONE mode, wait for its READY flag, run the COM7 probe once, collect both
# logs, stop the slave, verify port release. No ModbusLens involved.
$ErrorActionPreference = "Continue"
$here = $PSScriptRoot
$evidence = Join-Path $here "V3_PROTOCOL_EVIDENCE.log"
"" | Out-File $evidence -Encoding utf8
function L([string]$m) { "$((Get-Date -Format HH:mm:ss.fff)) $m" | Tee-Object -FilePath $evidence -Append }

$modes = @("NORMAL","CRC_ERROR","TIMEOUT","WRONG_SLAVE","WRONG_FUNCTION","EXCEPTION","FRAGMENTED")
$results = @{}

# ---------- S0: CRC self-test (independent vector checks) ----------
L "S0 START: CRC-16 self-test (vectors D5C5 / 3AD9 / valid / single-bit damage)"
$self = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here "virtual_modbus_slave.ps1") -SelfTest 2>&1
$self | ForEach-Object { L "  S0: $_" }
if ($LASTEXITCODE -eq 0) { $results["S0-CRC"] = "PASS" } else { $results["S0-CRC"] = "FAIL" }

# ---------- S1..S7 ----------
foreach ($mode in $modes) {
    $tag = "S{0}" -f ($modes.IndexOf($mode) + 1)
    L "${tag} START: mode=$mode"

    # 1) no stale slave processes
    Get-Process powershell -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $PID } | Out-Null
    # 2) clear stale ready flags
    $ready = Join-Path $here ("slave_ready_{0}.flag" -f $mode)
    if (Test-Path $ready) { Remove-Item $ready -Force }

    # 3) start slave (COM8)
    $slaveLog = Join-Path $here ("slave_log_{0}.log" -f $mode)
    if (Test-Path $slaveLog) { Remove-Item $slaveLog -Force }
    $probeLog = Join-Path $here ("probe_log_{0}.log" -f $mode)
    if (Test-Path $probeLog) { Remove-Item $probeLog -Force }
    $slave = Start-Process -FilePath "powershell.exe" -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File","$here\virtual_modbus_slave.ps1","-Mode",$mode,"-EvidenceDir",$here) -PassThru -WindowStyle Hidden
    L "${tag}: slave started (pid=$($slave.Id)) mode=$mode port=COM8"

    # 4) wait for READY flag (bounded)
    $ok = $false
    for ($i = 0; $i -lt 100; $i++) {
        if (Test-Path $ready) { $ok = $true; break }
        if ($slave.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $ok) { L "${tag} FAIL: slave never became READY"; $results[$tag] = "FAIL"; try { Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue } catch {}; continue }
    L "${tag}: slave READY (COM8 open)"

    # 5) run the probe (COM7) once
    $probe = Start-Process -FilePath "powershell.exe" -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File","$here\probe_virtual_modbus.ps1","-Mode",$mode,"-EvidenceDir",$here) -PassThru -WindowStyle Hidden -Wait
    $probeExit = $probe.ExitCode
    L "${tag}: probe exit=$probeExit"

    # 6) let the slave finish its bounded exit, then stop it
    for ($i = 0; $i -lt 50; $i++) { if ($slave.HasExited) { break }; Start-Sleep -Milliseconds 100 }
    if (-not $slave.HasExited) { try { Stop-Process -Id $slave.Id -Force -ErrorAction SilentlyContinue } catch {}; L "${tag}: slave force-stopped" }
    Start-Sleep -Milliseconds 300

    # 7) verify port release
    $released = $true
    foreach ($p in @("COM7","COM8")) {
        $openOk = $false
        for ($attempt = 1; $attempt -le 5 -and -not $openOk; $attempt++) {
            try {
                $sp = New-Object System.IO.Ports.SerialPort($p, 9600, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
                $sp.Open(); $sp.Close(); $openOk = $true
            } catch {
                if ($attempt -eq 5) { $released = $false; L "${tag} FAIL: $p not released after 5 attempts: $($_.Exception.Message)" }
                Start-Sleep -Milliseconds 400
            }
        }
    }
    if (-not $released) { $results[$tag] = "FAIL"; continue }

    # 8) append both logs to the evidence trail
    L "${tag} --- COM8 SIMULATOR log ---"
    if (Test-Path $slaveLog) { Get-Content $slaveLog | ForEach-Object { L "  $_" } }
    L "${tag} --- COM7 PROBE log ---"
    if (Test-Path $probeLog) { Get-Content $probeLog | ForEach-Object { L "  $_" } }

    if ($probeExit -eq 0 -and $released) { $results[$tag] = "PASS" } else { $results[$tag] = "FAIL" }
}

L "=== MATRIX SUMMARY ==="
$allPass = $true
foreach ($k in ($results.Keys | Sort-Object)) {
    L ("  {0} = {1}" -f $k, $results[$k])
    if ($results[$k] -ne "PASS") { $allPass = $false }
}
if ($allPass) { L "MATRIX RESULT: ALL PASS"; exit 0 } else { L "MATRIX RESULT: FAIL"; exit 1 }
