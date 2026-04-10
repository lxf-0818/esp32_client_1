# misc.cpp

## Purpose
Utility helpers for runtime metadata and diagnostics.

## APIs

### getBootTime(lastBoot, strReason)
- syncs time using NTP (`pool.ntp.org`)
- formats boot timestamp
- reads reset reason code and maps to readable text
- retries time read up to 3 times

Output examples:
- `lastBoot`: `MM/DD/YYYY HH:MM 0xRR`
- `strReason`: reset reason string

### get_reset_reason(reason, strReason)
Maps ESP32 reset-reason numeric codes to symbolic names such as:
- POWERON_RESET
- SW_RESET
- RTCWDT_BROWN_OUT_RESET
- etc.
