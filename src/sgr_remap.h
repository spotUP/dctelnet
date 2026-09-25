/* src/sgr_remap.h -- WB terminal SGR pen remap (pure logic). */
#ifndef SGR_REMAP_H
#define SGR_REMAP_H

#include <exec/types.h>
#include <stddef.h>

/* Nearest WB pen (0-7) for an XRGB 0xRRGGBB00 target, given the screen's
 * first 8 palette entries in the same format. Weighted RGB distance. */
UBYTE SgrNearestPen(ULONG target, const ULONG palette[8]);

/* Build the 16-entry SGR map (ANSI colour N -> WB pen) from ANSI targets
 * and the screen palette. Entry i maps SGR fg 30+i / bg 40+i. */
void SgrBuildMap(const ULONG ansi32[16], const ULONG palette[8],
                 UBYTE penmap[16]);

/* Rewrite ECMA-48 SGR sequences (ESC[...m, 8-bit CSI...m) mapping colour
 * params through penmap (values must be 0-7: all ibmcon can address):
 * fg 30-37 -> 30+pen, bg 40-47 -> 40+pen, 0 -> mapped white-on-black
 * pair, 39/49 -> mapped defaults. Every other sequence and byte passes
 * through byte-identically (introducer style preserved).
 * Returns output bytes, or 0 when outLen is too small (caller: write the
 * input verbatim). */
size_t SgrRemap(const UBYTE *in, size_t inLen, const UBYTE penmap[16],
                UBYTE *out, size_t outLen);

#endif /* SGR_REMAP_H */
