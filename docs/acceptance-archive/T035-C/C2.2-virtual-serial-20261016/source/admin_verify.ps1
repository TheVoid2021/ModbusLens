# admin_verify.ps1 - C2.2-V2 ADMIN READ-ONLY VERIFY (SecureBoot + BCD)
# No modifications. Output: ADMIN_VERIFY_RESULT.txt (same dir).
$ErrorActionPreference = "Continue"
$out = Join-Path $PSScriptRoot "ADMIN_VERIFY_RESULT.txt"
"=== C2.2-V2 ADMIN VERIFY ($((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))) ===" | Out-File -FilePath $out -Encoding utf8
try {
    $sb = Confirm-SecureBootUEFI -ErrorAction Stop
    "SecureBoot = $sb" | Out-File -FilePath $out -Append -Encoding utf8
} catch {
    "SecureBoot query FAILED: $($_.Exception.Message)" | Out-File -FilePath $out -Append -Encoding utf8
}
"--- bcdedit /enum {current} ---" | Out-File -FilePath $out -Append -Encoding utf8
$bcd = (& bcdedit.exe /enum "{current}" 2>&1) | Out-String
$bcd | Out-File -FilePath $out -Append -Encoding utf8
if ($bcd -match "testsigning\s+(Yes|No)") {
    "testsigning MATCH: $($Matches[0])" | Out-File -FilePath $out -Append -Encoding utf8
} else {
    "testsigning: NOT PRESENT IN BCD (= default No/unset)" | Out-File -FilePath $out -Append -Encoding utf8
}
if ($bcd -match "nointegritychecks\s+(Yes|No)") {
    "nointegritychecks MATCH: $($Matches[0])" | Out-File -FilePath $out -Append -Encoding utf8
} else {
    "nointegritychecks: NOT PRESENT IN BCD (= default No/unset)" | Out-File -FilePath $out -Append -Encoding utf8
}
"=== ADMIN VERIFY DONE ===" | Out-File -FilePath $out -Append -Encoding utf8
