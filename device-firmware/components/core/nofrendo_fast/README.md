# Nofrendo fast NES core

The `vendor` directory comes from Espressif's `esp32-nesemu` repository at
commit `693e378643dd4810665e52c0ec02bd9f64559c50` and contains the Nofrendo core.
Nofrendo is GPL-2.0 licensed; the original per-file notices are preserved.

This component is kept separate from the existing MIT-licensed Agnes adapter
while the ESP32-C3 scanline renderer, zero-copy flash ROM mapping, three-button
input, and partition-backed SRAM adapter are integrated and benchmarked.

The embedded adapter can report each completed 20-line PPU chunk to the platform
while a frame is still running. The ESP32 runtime uses this hook to overlap LCD
DMA with the remaining CPU/PPU work instead of blocking on a full-frame blit.
