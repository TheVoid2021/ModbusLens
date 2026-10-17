=== V4-R1 PRE ENVIRONMENT (Agent measured, 2026-10-17 16:43:01) ===
--- VSPD installs ---
Virtual Serial Port Driver 11 by Electronic Team, Inc. (Build 11.0.1096) | D:\Virtual Serial Port Driver 11\
--- vspdpro ---
running pid=7552 path=D:\Virtual Serial Port Driver 11\vspdpro.exe
vspdpro.exe ver=11.0.1096 sig=Valid
--- drivers ---
evserial9.sys ver=10.6.960.0 sig=Valid
evsbc9.sys ver=10.6.983.0 sig=Valid
sershare.sys ver=10.0.191.0 sig=Valid
--- services ---
evserial9: State=Running ExitCode=0
VSBC9: State=Running ExitCode=0
--- PnP ports ---
OK | 蓝牙链接上的标准串行 (COM3) | BTHENUM\{00001101-0000-1000-8000-00805F9B34FB}_VID&000105D6_PID&000A\8&21FF7238&0&B251007024BA_C00000000 | CM_PROB_NONE
OK | Electronic Team Virtual Serial Port (COM7->COM8) | VSBC9\DEVICES\0000 | CM_PROB_NONE
OK | Electronic Team Virtual Serial Port (COM8->COM7) | VSBC9\DEVICES\0001 | CM_PROB_NONE
OK | 蓝牙链接上的标准串行 (COM4) | BTHENUM\{00001101-0000-1000-8000-00805F9B34FB}_LOCALMFG&0000\8&21FF7238&0&000000000000_00000000 | CM_PROB_NONE
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
--- exclusive open/release test (no slave, no ModbusLens) ---
COM7 exclusive open+release OK
COM8 exclusive open+release OK
--- problem devices ---
(none)
