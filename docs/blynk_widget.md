# blynk_widget.h

## Purpose
Defines Blynk virtual-pin aliases used by the firmware so the source stays readable and avoids repeated literal pin IDs.

## Defined pins
- GAUGE_HOUSE -> V2
- TEMPV4 -> V4
- TEMPV6 -> V6
- TEMPV5 -> V5
- R_DIV -> V3
- BLINK_TST -> V9
- VRECOV -> V19
- VFAIL -> V20
- VRETRY -> V34
- BOOT -> V10

## Usage
Include this header wherever the firmware writes Blynk virtual pins, especially in the main runtime and the terminal-command handlers.
