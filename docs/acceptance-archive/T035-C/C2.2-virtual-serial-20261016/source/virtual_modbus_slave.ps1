# virtual_modbus_slave.ps1 - EXTERNAL Modbus RTU slave simulator (C2.2-V3)
# Opens the REAL COM8 (VSPD pair with COM7). Reads real bytes, parses real
# requests, validates CRC, builds responses DYNAMICALLY, writes real bytes.
# No replay file, no ModbusLens, no fake transport, no polling of its own.
# Usage: virtual_modbus_slave.ps1 -Mode NORMAL [-MaxRequests 1] [-IdleExitSeconds 6]
#        virtual_modbus_slave.ps1 -SelfTest   (CRC-16 vector tests only)
param(
    [ValidateSet("NORMAL","CRC_ERROR","TIMEOUT","WRONG_SLAVE","WRONG_FUNCTION","EXCEPTION","FRAGMENTED")]
    [string]$Mode = "NORMAL",
    [int]$MaxRequests = 1,
    [int]$IdleExitSeconds = 8,
    [switch]$SelfTest,
    [string]$PortName = "COM8",
    [string]$EvidenceDir = $PSScriptRoot
)

$BAUD = 9600
$MY_SLAVE = 1
$SUPPORTED_FC = 0x03
$SUPPORTED_ADDR = 49
$SUPPORTED_QTY = 1
$REGISTER_VALUE = 1234          # 0x04D2 - raw register only (no scale/offset here)
$MAX_BUFFER = 256               # bounded receive buffer
$FRAME_MS = 120                 # inter-segment delay for FRAGMENTED mode

function Get-ModbusCrc([byte[]]$data) {
    # Standard Modbus RTU CRC-16: init 0xFFFF, poly 0xA001, LSB first.
    $crc = 0xFFFF
    foreach ($b in $data) {
        $crc = $crc -bxor $b
        for ($i = 0; $i -lt 8; $i++) {
            if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 0xA001 } else { $crc = $crc -shr 1 }
        }
    }
    return [uint16]$crc
}
function Add-ModbusCrc([byte[]]$data) {
    $crc = Get-ModbusCrc $data
    return @($data + [byte]($crc -band 0xFF) + [byte](($crc -shr 8) -band 0xFF))
}
function Test-ModbusCrc([byte[]]$frame) {
    # frame = payload + crcLo + crcHi ; valid when recomputed CRC matches
    if ($frame.Length -lt 3) { return $false }
    $payload = $frame[0..($frame.Length - 3)]
    $expect = Get-ModbusCrc $payload
    $gotLo = $frame[$frame.Length - 2]
    $gotHi = $frame[$frame.Length - 1]
    return ($gotLo -eq ([byte]($expect -band 0xFF)) -and $gotHi -eq ([byte](($expect -shr 8) -band 0xFF)))
}
function Hex([byte[]]$b) { if ($null -eq $b -or $b.Length -eq 0) { return "(none)" }; return (($b | ForEach-Object { $_.ToString("X2") }) -join " ") }

if ($SelfTest) {
    # ---- CRC-16 independent vector tests ----
    $fail = 0
    $req = [byte[]](0x01,0x03,0x00,0x31,0x00,0x01)
    $crc = Get-ModbusCrc $req
    $lo = [byte]($crc -band 0xFF); $hi = [byte](($crc -shr 8) -band 0xFF)
    if ($lo -ne 0xD5 -or $hi -ne 0xC5) { Write-Output "SELFTEST FAIL request CRC: got $($lo.ToString('X2')) $($hi.ToString('X2')) want D5 C5"; $fail++ } else { Write-Output "SELFTEST PASS request CRC vector = D5 C5" }
    $rsp = [byte[]](0x01,0x03,0x02,0x04,0xD2)
    $crc2 = Get-ModbusCrc $rsp
    $lo2 = [byte]($crc2 -band 0xFF); $hi2 = [byte](($crc2 -shr 8) -band 0xFF)
    if ($lo2 -ne 0x3A -or $hi2 -ne 0xD9) { Write-Output "SELFTEST FAIL response CRC: got $($lo2.ToString('X2')) $($hi2.ToString('X2')) want 3A D9"; $fail++ } else { Write-Output "SELFTEST PASS response CRC vector = 3A D9 (low/high order correct)" }
    $good = [byte[]](0x01,0x03,0x00,0x31,0x00,0x01,0xD5,0xC5)
    if (-not (Test-ModbusCrc $good)) { Write-Output "SELFTEST FAIL valid frame rejected"; $fail++ } else { Write-Output "SELFTEST PASS valid request frame accepted" }
    $bad = [byte[]](0x01,0x03,0x00,0x31,0x00,0x01,0xD5,0xC6)   # single-bit damage in CRC
    if (Test-ModbusCrc $bad) { Write-Output "SELFTEST FAIL single-bit damaged frame accepted"; $fail++ } else { Write-Output "SELFTEST PASS single-bit damaged frame rejected" }
    Write-Output "SELFTEST SUMMARY: failures=$fail"
    if ($fail -eq 0) { exit 0 } else { exit 1 }
}

$logFile = Join-Path $EvidenceDir ("slave_log_{0}.log" -f $Mode)
$readyFile = Join-Path $EvidenceDir ("slave_ready_{0}.flag" -f $Mode)
if (Test-Path $readyFile) { Remove-Item $readyFile -Force }
"=== SLAVE SIM mode=$Mode port=$PortName started $(Get-Date -Format HH:mm:ss.fff) ===" | Out-File $logFile -Encoding utf8

$port = $null
try {
    $port = New-Object System.IO.Ports.SerialPort($PortName, $BAUD, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $port.Handshake = [System.IO.Ports.Handshake]::None
    $port.ReadTimeout = 200
    $port.WriteTimeout = 500
    $port.Open()
    "SLAVE OPEN: $PortName opened at $BAUD/8N1 no-flow-control" | Out-File $logFile -Append -Encoding utf8
    "READY" | Out-File $readyFile -Encoding utf8

    $buffer = New-Object System.Collections.Generic.List[byte]
    $requestsServed = 0
    $badFrames = 0
    $lastByteTime = Get-Date

    while ($true) {
        $toRead = $port.BytesToRead
        if ($toRead -gt 0) {
            $chunk = New-Object byte[] $toRead
            $n = $port.Read($chunk, 0, $toRead)
            for ($i = 0; $i -lt $n; $i++) { $buffer.Add($chunk[$i]) }
            $lastByteTime = Get-Date
            "COM8 SIMULATOR RX: $(Hex ($buffer.ToArray())) (accumulated $($buffer.Count))" | Out-File $logFile -Append -Encoding utf8
            if ($buffer.Count -gt $MAX_BUFFER) { $buffer.RemoveRange(0, $buffer.Count - $MAX_BUFFER) }
        } else {
            Start-Sleep -Milliseconds 8
        }

        # ---- try to assemble a candidate request from the buffer front ----
        while ($buffer.Count -ge 8) {
            $frame = $buffer.GetRange(0, 8).ToArray()
            $payload = $frame[0..5]
            $slave = $payload[0]; $fc = $payload[1]
            $addr = [int]$payload[2] * 256 + [int]$payload[3]
            $qty = [int]$payload[4] * 256 + [int]$payload[5]
            if (-not (Test-ModbusCrc $frame)) {
                # not a valid frame at the front: slide one byte (bounded)
                $badFrames++
                "COM8 SIMULATOR: candidate window not a valid frame (badFrames=$badFrames), slide 1 byte: $(Hex $frame)" | Out-File $logFile -Append -Encoding utf8
                $buffer.RemoveAt(0)
                continue
            }
            "COM8 SIMULATOR REQUEST: slave=$slave fc=$fc addr=$addr qty=$qty bytes=$(Hex $frame) CRC=VALID" | Out-File $logFile -Append -Encoding utf8
            # ---- VALIDATE_SLAVE_FUNCTION_ADDRESS_QUANTITY ----
            if ($slave -ne $MY_SLAVE -or $fc -ne $SUPPORTED_FC -or $addr -ne $SUPPORTED_ADDR -or $qty -ne $SUPPORTED_QTY) {
                "COM8 SIMULATOR REJECT: request does not match slave=$MY_SLAVE fc=$SUPPORTED_FC addr=$SUPPORTED_ADDR qty=$SUPPORTED_QTY (bounded ignore, no response)" | Out-File $logFile -Append -Encoding utf8
                $buffer.RemoveRange(0, 8)
                continue
            }
            # ---- BUILD_RESPONSE (dynamic per mode) ----
            $resp = $null
            switch ($Mode) {
                "NORMAL"       { $resp = Add-ModbusCrc ([byte[]](0x01,0x03,0x02,0x04,0xD2)) }
                "CRC_ERROR"    { $r = [byte[]](Add-ModbusCrc ([byte[]](0x01,0x03,0x02,0x04,0xD2))); $r[6] = [byte]($r[6] -bxor 0xFF); $resp = $r }
                "TIMEOUT"      { $resp = $null }
                "WRONG_SLAVE"  { $resp = Add-ModbusCrc ([byte[]](0x02,0x03,0x02,0x04,0xD2)) }
                "WRONG_FUNCTION" { $resp = Add-ModbusCrc ([byte[]](0x01,0x04,0x02,0x04,0xD2)) }
                "EXCEPTION"    { $resp = Add-ModbusCrc ([byte[]](0x01,0x83,0x02)) }
                "FRAGMENTED"   { $resp = Add-ModbusCrc ([byte[]](0x01,0x03,0x02,0x04,0xD2)) }
            }
            $requestsServed++
            if ($null -eq $resp) {
                "COM8 SIMULATOR: mode=$Mode -> NO RESPONSE SENT (request received, bounded silence)" | Out-File $logFile -Append -Encoding utf8
            } elseif ($Mode -eq "FRAGMENTED") {
                $seg1 = $resp[0..1]; $seg2 = $resp[2..4]; $seg3 = $resp[5..6]
                $port.Write($seg1, 0, $seg1.Length); Start-Sleep -Milliseconds $FRAME_MS
                $port.Write($seg2, 0, $seg2.Length); Start-Sleep -Milliseconds $FRAME_MS
                $port.Write($seg3, 0, $seg3.Length)
                "COM8 SIMULATOR TX (FRAGMENTED 3 segments, ${FRAME_MS}ms gaps): $(Hex $seg1) | $(Hex $seg2) | $(Hex $seg3)" | Out-File $logFile -Append -Encoding utf8
            } else {
                $port.Write($resp, 0, $resp.Length)
                "COM8 SIMULATOR TX: $(Hex $resp) (len=$($resp.Length), CRC recomputed dynamically)" | Out-File $logFile -Append -Encoding utf8
            }
            "COM8 SIMULATOR FINISH_TRANSACTION #$requestsServed" | Out-File $logFile -Append -Encoding utf8
            $buffer.RemoveRange(0, 8)
            if ($requestsServed -ge $MaxRequests) {
                "COM8 SIMULATOR: max requests ($MaxRequests) reached -> bounded exit" | Out-File $logFile -Append -Encoding utf8
                break
            }
        }
        if ($requestsServed -ge $MaxRequests) { break }
        # bounded idle exit (no complete request within IdleExitSeconds)
        if (((Get-Date) - $lastByteTime).TotalSeconds -gt $IdleExitSeconds) {
            "COM8 SIMULATOR: idle timeout (${IdleExitSeconds}s) -> bounded exit" | Out-File $logFile -Append -Encoding utf8
            break
        }
    }
} catch {
    "COM8 SIMULATOR ERROR: $($_.Exception.Message)" | Out-File $logFile -Append -Encoding utf8
    exit 2
} finally {
    if ($port -ne $null -and $port.IsOpen) { try { $port.Close() } catch {} }
    "COM8 SIMULATOR CLOSED: port released $(Get-Date -Format HH:mm:ss.fff)" | Out-File $logFile -Append -Encoding utf8
}
"=== SLAVE SIM mode=$Mode done requestsServed=$requestsServed badFrames=$badFrames ===" | Out-File $logFile -Append -Encoding utf8
exit 0
