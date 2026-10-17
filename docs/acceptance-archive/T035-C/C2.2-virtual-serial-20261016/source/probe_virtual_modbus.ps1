# probe_virtual_modbus.ps1 - INDEPENDENT COM7 test client (C2.2-V3)
# Sends a real FC03 request through COM7 -> VSPD -> COM8 (external slave),
# reads the real response bytes from COM7, asserts per mode. No ModbusLens,
# no shared memory with the slave, no direct function calls: every byte
# travels through the real virtual serial pair.
param(
    [ValidateSet("NORMAL","CRC_ERROR","TIMEOUT","WRONG_SLAVE","WRONG_FUNCTION","EXCEPTION","FRAGMENTED")]
    [string]$Mode = "NORMAL",
    [string]$PortName = "COM7",
    [int]$ReadTimeoutMs = 5000,
    [int]$TimeoutExpectMs = 3000,
    [string]$EvidenceDir = $PSScriptRoot
)
$ErrorActionPreference = "Stop"

function Get-ModbusCrc([byte[]]$data) {
    $crc = 0xFFFF
    foreach ($b in $data) {
        $crc = $crc -bxor $b
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xA001 } else { $crc = $crc -shr 1 }
        }
    }
    return [uint16]$crc
}
function Test-ModbusCrc([byte[]]$frame) {
    if ($frame.Length -lt 3) { return $false }
    $payload = $frame[0..($frame.Length - 3)]
    $expect = Get-ModbusCrc $payload
    return ($frame[$frame.Length - 2] -eq ([byte]($expect -band 0xFF)) -and $frame[$frame.Length - 1] -eq ([byte](($expect -shr 8) -band 0xFF)))
}
function Hex([byte[]]$b) { if ($null -eq $b -or $b.Length -eq 0) { return "(none)" }; return (($b | ForEach-Object { $_.ToString("X2") }) -join " ") }

$logFile = Join-Path $EvidenceDir ("probe_log_{0}.log" -f $Mode)
"=== PROBE mode=$Mode port=$PortName $(Get-Date -Format HH:mm:ss.fff) ===" | Out-File $logFile -Encoding utf8

# ---- build the request with the INDEPENDENT CRC implementation ----
$payload = [byte[]](0x01,0x03,0x00,0x31,0x00,0x01)
$crc = Get-ModbusCrc $payload
$request = @($payload + [byte]($crc -band 0xFF) + [byte](($crc -shr 8) -band 0xFF))
$expectReq = "01 03 00 31 00 01 D5 C5"
$gotReq = Hex $request
"COM7 PROBE TX: $gotReq" | Out-File $logFile -Append -Encoding utf8
$fail = 0
if ($gotReq -ne $expectReq) { "FAIL probe CRC: request is '$gotReq' expected '$expectReq'" | Out-File $logFile -Append -Encoding utf8; $fail++ }

$port = $null
$rx = $null
try {
    $port = New-Object System.IO.Ports.SerialPort($PortName, 9600, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $port.Handshake = [System.IO.Ports.Handshake]::None
    $port.ReadTimeout = 200
    $port.WriteTimeout = 500
    $port.Open()
    $port.DiscardInBuffer(); $port.DiscardOutBuffer()
    $port.Write($request, 0, $request.Length)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $got = New-Object System.Collections.Generic.List[byte]
    $budget = if ($Mode -eq "TIMEOUT") { $TimeoutExpectMs } else { $ReadTimeoutMs }
    while ($sw.ElapsedMilliseconds -lt $budget) {
        $toRead = $port.BytesToRead
        if ($toRead -gt 0) {
            $buf = New-Object byte[] $toRead
            $n = $port.Read($buf, 0, $toRead)
            for ($i = 0; $i -lt $n; $i++) { $got.Add($buf[$i]) }
        } else {
            Start-Sleep -Milliseconds 10
        }
    }
    $rx = $got.ToArray()
    "COM7 PROBE RX: $(Hex $rx) (len=$($rx.Length), waited $($sw.ElapsedMilliseconds)ms)" | Out-File $logFile -Append -Encoding utf8
} catch {
    "PROBE ERROR: $($_.Exception.Message)" | Out-File $logFile -Append -Encoding utf8
    $fail++
} finally {
    if ($port -ne $null -and $port.IsOpen) { try { $port.Close() } catch {} }
    "COM7 PROBE CLOSED" | Out-File $logFile -Append -Encoding utf8
}

# ---- per-mode assertions on the REAL received bytes ----
switch ($Mode) {
    "NORMAL" {
        if ($rx.Length -ne 7) { "FAIL NORMAL: expected 7 bytes got $($rx.Length)" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if ($rx[0] -ne 1 -or $rx[1] -ne 3 -or $rx[2] -ne 2) { "FAIL NORMAL: header fields wrong ($(Hex $rx))" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        $raw = [int]$rx[3] * 256 + [int]$rx[4]
        if ($raw -ne 1234) { "FAIL NORMAL: raw register is $raw expected 1234" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (-not (Test-ModbusCrc $rx)) { "FAIL NORMAL: response CRC invalid ($(Hex $rx))" | Out-File $logFile -Append -Encoding utf8; $fail++ }
    }
    "CRC_ERROR" {
        if ($rx.Length -ne 7) { "FAIL CRC_ERROR: expected 7 bytes got $($rx.Length)" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if ($rx[0] -ne 1 -or $rx[1] -ne 3 -or $rx[2] -ne 2 -or (([int]$rx[3]*256+[int]$rx[4]) -ne 1234)) { "FAIL CRC_ERROR: non-CRC fields were also damaged" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (Test-ModbusCrc $rx) { "FAIL CRC_ERROR: response CRC is VALID but mode requires INVALID" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        else { "PASS CRC_ERROR: received frame has INVALID CRC (only CRC damaged)" | Out-File $logFile -Append -Encoding utf8 }
    }
    "TIMEOUT" {
        if ($rx.Length -ne 0) { "FAIL TIMEOUT: received $($rx.Length) unexpected bytes ($(Hex $rx))" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        else { "PASS TIMEOUT: zero response bytes within ${TimeoutExpectMs}ms" | Out-File $logFile -Append -Encoding utf8 }
    }
    "WRONG_SLAVE" {
        if ($rx.Length -ne 7) { "FAIL WRONG_SLAVE: expected 7 bytes got $($rx.Length)" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if ($rx[0] -ne 2) { "FAIL WRONG_SLAVE: slave is $($rx[0]) expected 2" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if ($rx[1] -ne 3 -or (([int]$rx[3]*256+[int]$rx[4]) -ne 1234)) { "FAIL WRONG_SLAVE: register content changed" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (-not (Test-ModbusCrc $rx)) { "FAIL WRONG_SLAVE: CRC invalid" | Out-File $logFile -Append -Encoding utf8; $fail++ }
    }
    "WRONG_FUNCTION" {
        if ($rx.Length -ne 7) { "FAIL WRONG_FUNCTION: expected 7 bytes got $($rx.Length)" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if ($rx[0] -ne 1 -or $rx[1] -ne 4 -or $rx[2] -ne 2) { "FAIL WRONG_FUNCTION: header wrong ($(Hex $rx))" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (([int]$rx[3]*256+[int]$rx[4]) -ne 1234) { "FAIL WRONG_FUNCTION: register content changed" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (-not (Test-ModbusCrc $rx)) { "FAIL WRONG_FUNCTION: CRC invalid" | Out-File $logFile -Append -Encoding utf8; $fail++ }
    }
    "EXCEPTION" {
        if ($rx.Length -ne 5) { "FAIL EXCEPTION: expected 5 bytes got $($rx.Length)" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if ($rx[0] -ne 1 -or $rx[1] -ne 0x83 -or $rx[2] -ne 0x02) { "FAIL EXCEPTION: exception frame wrong ($(Hex $rx))" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        if (-not (Test-ModbusCrc $rx)) { "FAIL EXCEPTION: CRC invalid (must be a LEGAL exception frame)" | Out-File $logFile -Append -Encoding utf8; $fail++ }
    }
    "FRAGMENTED" {
        if ($rx.Length -ne 7) { "FAIL FRAGMENTED: accumulated $($rx.Length) bytes expected 7" | Out-File $logFile -Append -Encoding utf8; $fail++; break }
        if (-not (Test-ModbusCrc $rx)) { "FAIL FRAGMENTED: accumulated frame CRC invalid" | Out-File $logFile -Append -Encoding utf8; $fail++ }
        $raw = [int]$rx[3] * 256 + [int]$rx[4]
        if ($raw -ne 1234) { "FAIL FRAGMENTED: raw is $raw expected 1234" | Out-File $logFile -Append -Encoding utf8; $fail++ }
    }
}
"=== PROBE mode=$Mode SUMMARY: failures=$fail ===" | Out-File $logFile -Append -Encoding utf8
if ($fail -eq 0) { "PROBE RESULT: PASS" | Out-File $logFile -Append -Encoding utf8; exit 0 } else { "PROBE RESULT: FAIL" | Out-File $logFile -Append -Encoding utf8; exit 1 }
