# misc.cpp

## Purpose
Utility helpers for runtime metadata and diagnostics.

## APIs

### getBootTime(lastBoot, strReason)
- syncs time using NTP (pool.ntp.org)
- formats the boot timestamp into a human-readable string
- reads the ESP32 reset reason code and maps it to a readable text label
- retries the time read up to three times before giving up

Output examples:
- lastBoot: MM/DD/YYYY HH:MM 0xRR
- strReason: reset reason string

### get_reset_reason(reason, strReason)
Maps ESP32 reset-reason numeric codes to symbolic names such as:
- POWERON_RESET
- SW_RESET
- RTCWDT_BROWN_OUT_RESET
- and other ESP32 reset-reason values
