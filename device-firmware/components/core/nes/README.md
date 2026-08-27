# NES core

This component vendors [`kgabis/agnes`](https://github.com/kgabis/agnes) at
commit `0e4220b084c467e39c04805d955e78c463feadd0` under the MIT license.  The
upstream license is preserved in `vendor/LICENSE.agnes`.

Local changes replace the upstream 256×240 palette-index framebuffer (61,440
bytes) with a single 256-byte scanline and add a scanline callback.  ROM data
is retained by pointer rather than copied, allowing the platform adapter to
memory-map a flash partition without consuming internal RAM.

The initial upstream core supports iNES Mapper 0, 1, 2, and 4 and does not yet
emulate the APU.  The wrapper recognizes iNES battery-backed RAM and provides
an exact-size import/export API for the 8 KiB Mapper 1 save used by Final
Fantasy. Platform display DMA, ROM/save partition mapping, and the three-button
input adapter remain separate layers; no commercial ROM is included in this
repository.

The first hardware milestone is deliberately silent because Agnes has no APU.
Video, Mapper 1 banking, three-button input, and battery saves are kept
independent so audio emulation can be added after on-device FPS is measured.
