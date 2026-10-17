=== C2.2-V3-R3 VSPD 11 BASELINE (Agent measured, 2026-10-17 16:11:50) ===
--- VSPD installs (registry) ---
Virtual Serial Port Driver 11 by Electronic Team, Inc. (Build 11.0.1096) | D:\Virtual Serial Port Driver 11\
--- vspdpro exe (path+version+signature) ---
running pid=7552 start=16:02:51
D:\Virtual Serial Port Driver 11\vspdpro.exe ver=11.0.1096 sig=Valid
--- drivers ---
evserial9.sys ver=10.6.960.0 sig=Valid signer=CN=Microsoft Windows Hardware Compatibility Publis
evsbc9.sys ver=10.6.983.0 sig=Valid signer=CN=Microsoft Windows Hardware Compatibility Publis
sershare.sys ver=10.0.191.0 sig=Valid signer=CN=Microsoft Windows Hardware Compatibility Publis
--- driver services ---
evserial9: State=Running ExitCode=0
VSBC9: State=Running ExitCode=0
sershare: State=Stopped ExitCode=1077
--- PnP ports ---
OK | 蓝牙链接上的标准串行 (COM3) | BTHENUM\{00001101-0000-1000-8000-00805F9B34FB}_VID&000105D6_PID&000A\8&21FF7238&0&B251007024BA_C00000000 | Problem=CM_PROB_NONE
OK | Electronic Team Virtual Serial Port (COM7->COM8) | VSBC9\DEVICES\0000 | Problem=CM_PROB_NONE
OK | Electronic Team Virtual Serial Port (COM8->COM7) | VSBC9\DEVICES\0001 | Problem=CM_PROB_NONE
OK | 蓝牙链接上的标准串行 (COM4) | BTHENUM\{00001101-0000-1000-8000-00805F9B34FB}_LOCALMFG&0000\8&21FF7238&0&000000000000_00000000 | Problem=CM_PROB_NONE
--- problem devices ---
(none)
--- SERIALCOMM ---
\Device\BthModem1 = COM4
\Device\BthModem0 = COM3
\Device\VSerial9_0 = COM7
\Device\VSerial9_1 = COM8
--- GetPortNames ---
COM3
COM4
COM7
COM8
--- open test ---
COM7 OPEN OK
COM8 OPEN OK
