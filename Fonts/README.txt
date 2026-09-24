Petscii.font / PetsciiLower.font
================================

Real C64 PETSCII glyph shapes, packaged as Amiga strike fonts (16x8,
width-doubled from the source 8x8 to compensate Amiga hires' 2x
horizontal pixel density vs the C64's roughly-square pixels).

Source: glyph bitmaps extracted from SyncTerm/Synchronet's
src/conio/allfonts.c ("eight_by_eight" Commodore 64 UPPER/Lower tables).
The glyphs are indexed by raw PETSCII byte value.

LICENSE STATUS: UNRESOLVED. allfonts.c itself carries no copyright/license
header (unlike petscii.c/cterm_petscii.c in the same codebase, which are
explicitly LGPLv2+ under Rob Swindell). SyncTerm's docs claim these fonts
were "imported from FreeBSD syscons," but the current FreeBSD source tree
has no Commodore/PETSCII font file, so that provenance claim could not be
verified. Please treat these files as unlicensed until that is settled;
without them DCTelnet's PETSCII Mode falls back to CP437 lookalike glyphs.

Generator: tools/gen_petscii_font.py rebuilds Petscii/8 and PetsciiLower/8
from tools/glyphs/*.bin (repacks glyph-major 8x8 into Amiga's row-major
strike layout, doubles width, assembles with vasm and links with vlink).
Check: python3 tools/check_amiga_font.py Fonts/Petscii.font (also run by
`make` in test/).
