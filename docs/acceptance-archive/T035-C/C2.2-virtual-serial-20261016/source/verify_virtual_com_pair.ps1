# verify_virtual_com_pair.ps1 - C2.2-V2 bidirectional raw-byte verification
# Uses ONLY Windows .NET System.IO.Ports.SerialPort. No ModbusLens, no
# RecordingSerialTransport, no HarnessWriteTransport, no replay, no pyserial.
# Exit 0 only if ALL tests pass. Open() success alone is never a PASS.
$ErrorActionPreference = "Stop"
$log = Join-Path $PSScriptRoot "VSPD_PAIR_TEST.log"
function Log([string]$msg) {
    $stamp = (Get-Date).ToString("HH:mm:ss.fff")
    "$stamp $msg" | Tee-Object -FilePath $log -Append
}
"" | Out-File $log -Encoding utf8
Log "=== VSPD PAIR BIDIRECTIONAL RAW BYTE TEST (COM7 <-> COM8) ==="

$READ_TOTAL_MS = 4000   # total budget to accumulate the expected byte count
$BAUD = 9600

function ReadExact([System.IO.Ports.SerialPort]$port, [int]$count, [string]$label) {
    $got = New-Object System.Collections.Generic.List[byte]
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($got.Count -lt $count -and $sw.ElapsedMilliseconds -lt $READ_TOTAL_MS) {
        $toRead = $port.BytesToRead
        if ($toRead -gt 0) {
            $buf = New-Object byte[] $toRead
            $n = $port.Read($buf, 0, $toRead)
            for ($i = 0; $i -lt $n; $i++) { $got.Add($buf[$i]) }
        } else {
            Start-Sleep -Milliseconds 10
        }
    }
    if ($got.Count -ne $count) {
        Log ("FAIL " + $label + ": timeout, got " + $got.Count + " bytes, expected " + $count)
        return $null
    }
    return $got.ToArray()
}

function Hex([byte[]]$b) { return (($b | ForEach-Object { $_.ToString("X2") }) -join " ") }

function RunTransferTest($writer, $reader, [byte[]]$payload, [string]$label) {
    # Optional fragmentation: $segments array of byte[] written with delays
    $reader.DiscardInBuffer(); $writer.DiscardOutBuffer()
    Log ("TX " + $label + " via " + $writer.PortName + ": " + (Hex $payload))
    $writer.Write($payload, 0, $payload.Length)
    $got = ReadExact $reader $payload.Length $label
    if ($null -eq $got) { return $false }
    $ok = $true
    if ($got.Length -ne $payload.Length) { $ok = $false }
    for ($i = 0; $i -lt [Math]::Min($got.Length, $payload.Length); $i++) {
        if ($got[$i] -ne $payload[$i]) { $ok = $false }
    }
    if (-not $ok) {
        Log ("FAIL " + $label + ": byte mismatch. RX=" + (Hex $got))
        return $false
    }
    Log ("RX " + $label + " via " + $reader.PortName + ": " + (Hex $got) + " (" + $got.Length + "/" + $payload.Length + " bytes, EXACT binary match)")
    return $true
}

function RunFragmentedTest($writer, $reader, [byte[][]]$segments, [byte[]]$expected, [string]$label) {
    $reader.DiscardInBuffer(); $writer.DiscardOutBuffer()
    $hexParts = ($segments | ForEach-Object { (($_ | ForEach-Object { $_.ToString("X2") }) -join " ") }) -join " | "
    Log ("TX " + $label + " via " + $writer.PortName + " in " + $segments.Count + " chunks: " + $hexParts)
    foreach ($seg in $segments) {
        $writer.Write($seg, 0, $seg.Length)
        Start-Sleep -Milliseconds 120   # create a real chunk boundary
    }
    $got = ReadExact $reader $expected.Length $label
    if ($null -eq $got) { return $false }
    $ok = $got.Length -eq $expected.Length
    for ($i = 0; $i -lt [Math]::Min($got.Length, $expected.Length); $i++) {
        if ($got[$i] -ne $expected[$i]) { $ok = $false }
    }
    if (-not $ok) {
        Log ("FAIL " + $label + ": accumulated mismatch. RX=" + (Hex $got))
        return $false
    }
    Log ("RX " + $label + " via " + $reader.PortName + ": " + (Hex $got) + " (accumulated " + $got.Length + "/" + $expected.Length + ", EXACT binary match)")
    return $true
}

$failures = 0
$c7 = $null; $c8 = $null
try {
    $c7 = New-Object System.IO.Ports.SerialPort("COM7", $BAUD, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $c8 = New-Object System.IO.Ports.SerialPort("COM8", $BAUD, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    foreach ($p in @($c7, $c8)) {
        $p.Handshake = [System.IO.Ports.Handshake]::None
        $p.ReadTimeout = 500
        $p.WriteTimeout = 500
    }
    $c7.Open(); $c8.Open()
    Log ("OPEN: COM7 + COM8 open OK at " + $BAUD + "/8N1, no flow control")

    # --- Test A: COM7 -> COM8, 8 bytes (valid FC03 request frame) ---
    $req = [byte[]](0x01,0x03,0x00,0x31,0x00,0x01,0xD5,0xC5)
    if (-not (RunTransferTest $c7 $c8 $req "TEST-A COM7->COM8")) { $failures++ }

    # --- Test B: COM8 -> COM7, 7 bytes (valid FC03 response frame) ---
    $rsp = [byte[]](0x01,0x03,0x02,0x04,0xD2,0x3A,0xD9)
    if (-not (RunTransferTest $c8 $c7 $rsp "TEST-B COM8->COM7")) { $failures++ }

    # --- Test C: fragmented COM8 -> COM7 (3 chunks) ---
    $segs = @(
        [byte[]](0x01,0x03),
        [byte[]](0x02,0x04,0xD2),
        [byte[]](0x3A,0xD9)
    )
    if (-not (RunFragmentedTest $c8 $c7 $segs $rsp "TEST-C fragmented COM8->COM7")) { $failures++ }
    Log "NOTE: Test C proves accumulated bytes only; real RTU frame boundary handling is out of scope."

    # --- Test D: close, reopen, close again (no leaked handles) ---
    $c7.Close(); $c8.Close()
    Log "CLOSE: both ports closed"
    Start-Sleep -Milliseconds 300
    $c7.Open(); $c8.Open()
    Log "REOPEN: COM7 + COM8 reopened OK"
    $probe = [byte[]](0x55,0xAA)
    $c8.Write($probe, 0, $probe.Length)
    $got = ReadExact $c7 $probe.Length "TEST-D reopen roundtrip"
    if ($null -eq $got) { $failures++ }
    else {
        $dok = ($got[0] -eq 0x55 -and $got[1] -eq 0xAA)
        if (-not $dok) { Log ("FAIL TEST-D: RX=" + (Hex $got)); $failures++ }
        else { Log ("TEST-D PASS: reopen roundtrip " + (Hex $got)) }
    }
    $c7.Close(); $c8.Close()
    Log "CLOSE: both ports closed after reopen test"
} finally {
    foreach ($p in @($c7, $c8)) {
        if ($p -ne $null -and $p.IsOpen) { try { $p.Close() } catch {} }
    }
}

Log ("=== SUMMARY: failures=" + $failures + " ===")
if ($failures -eq 0) {
    Log "RESULT: ALL TESTS PASS (bidirectional raw byte transfer verified)"
    exit 0
} else {
    Log "RESULT: FAILED (" + $failures + " test(s))"
    exit 1
}
