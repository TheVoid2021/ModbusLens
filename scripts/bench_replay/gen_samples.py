"""Generate deterministic mixed .mlog v1 samples for Replay-pipeline benchmarking.

Sizes: 10k / 100k / 1M transaction rows, written to out/ (git-ignored).
Composition per 100 records (documented; see docs/10_REPLAY_PERFORMANCE_BENCHMARK.md):
  - 60 FC03 success        (elapsed 17)
  - 10 FC03 exception 0x02 (elapsed 18)
  -  5 response CRC error  (elapsed 19, valid request, corrupted response CRC)
  -  5 timeout             (elapsed 1000, NO_RESPONSE)
  -  8 FC06 echo success   (elapsed 15)
  -  7 Function 0x10 success (elapsed 20)
  -  3 broadcast FC06      (addr 0, NO_RESPONSE -> ExpectedNoResponse, elapsed 16)
  -  2 broadcast 0x10      (addr 0, NO_RESPONSE, elapsed 16)
The cycle repeats exactly every 100 rows, so any multiple of 100 keeps the mix.
"""

import os


def crc16(payload: bytes) -> int:
    crc = 0xFFFF
    for b in payload:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(payload: bytes) -> str:
    c = crc16(payload)
    full = payload + bytes([c & 0xFF, c >> 8])
    return " ".join(f"{x:02X}" for x in full)


# sanity checks against real captured frames from samples/demo_v1.mlog
assert frame(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x02])) == "01 03 00 00 00 02 C4 0B"
assert frame(bytes([0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8])) == "01 03 04 00 64 00 C8 BA 7A"
assert frame(bytes([0x01, 0x83, 0x02])) == "01 83 02 C0 F1"

fc03_req = frame(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x02]))
fc03_ok = frame(bytes([0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8]))
fc03_exc = frame(bytes([0x01, 0x83, 0x02]))
_badcrc = frame(bytes([0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8])).split()
_badcrc[-1] = "%02X" % (int(_badcrc[-1], 16) ^ 0x01)  # same 9 tokens, last CRC byte flipped
fc03_badcrc = " ".join(_badcrc)

fc06_req = frame(bytes([0x01, 0x06, 0x00, 0x64, 0x00, 0xC8]))
bcast06 = frame(bytes([0x00, 0x06, 0x00, 0x64, 0x00, 0xC8]))

fc10_req = frame(bytes([0x01, 0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x64]))
fc10_resp = frame(bytes([0x01, 0x10, 0x00, 0x00, 0x00, 0x01]))
bcast10 = frame(bytes([0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x64]))

# (elapsed, request, response)   -- response None => NO_RESPONSE
pat = [
    (17, fc03_req, fc03_ok),    # x60
    (18, fc03_req, fc03_exc),   # x10
    (19, fc03_req, fc03_badcrc),  # x5
    (1000, fc03_req, None),     # x5
    (15, fc06_req, fc06_req),   # x8
    (20, fc10_req, fc10_resp),  # x7
    (16, bcast06, None),        # x3
    (16, bcast10, None),        # x2
]
counts = [60, 10, 5, 5, 8, 7, 3, 2]

HEADER = "MODBUSLENS_MLOG|1|timeout_ms=1000\n"


def gen(path: str, rows: int) -> None:
    total = 0
    with open(path, "w") as f:
        f.write(HEADER)
        while total < rows:
            for (el, req, resp), n in zip(pat, counts):
                r = resp if resp is not None else "NO_RESPONSE"
                f.write(f"TXN|{el}|{req}|{r}\n" * n)
                total += n
    assert total == rows


if __name__ == "__main__":
    os.makedirs("out", exist_ok=True)
    for label, rows in (("10k", 10_000), ("100k", 100_000), ("1m", 1_000_000)):
        gen(f"out/sample_{label}.mlog", rows)
        print(f"out/sample_{label}.mlog : {rows} rows")