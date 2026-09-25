---
date: 2026-09-25
topic: "AGA direct-render path (own ViewPort/copper console)"
tags: [dctelnet, aga, chipset, copper, blitter, performance, plan]
status: final
---

# AGA direct-render plan

Research: `thoughts/shared/research/2026-09-25_aga-direct-render.md`
(3 tracks: cost map, hardware+reuse, integration checklist).

## Decisions already made (owner, 2026-09-25)

- Architecture: **own ViewPort/copper** (not Intuition-screen bitmap).
- Parity: **full ibmcon ANSI subset**, no fallback toggle.
- **Probe build first**: SpeedTest floods on the A1200 gate the big build.
- **ANSI first**, PETSCII reuses the path later.

## Invariant

Every byte that renders through ibmcon today renders identically (glyphs,
attributes, cursor, scroll, regions) through the direct path, at a measured
scroll-bound throughput at least 2x the console.device baseline.
Everything else (menus, dialogs, scrollback, XEM, per-entry, network)
behaves byte-identically.

## Design

Two screens, flipped by hotkey (demo-scene style, multitasking-safe):
Intuition screen keeps menus/dialogs/requesters/XEM untouched; the AGA
View (graphics.library `View`+`ViewPort`, own copper, chip bitmaps) owns
the session. No console.device anywhere near it. (Single-screen bitmap
variant considered 2026-09-25 and held as fallback; owner: try two
screens first.)

- **Selector**: new `flags` bit (16-31 free) + Options item, classified
  screen-reopen in `SITE_PREFS_SCREEN_FLAGS`; mutually exclusive with XEM
  (refuse with notice, same pattern as the depth gate); per-entry
  settings carry it automatically via `DisplayDiffers`.
- **Interpreter** (`src/aga_text.c`, host-testable): cell grid
  (glyph+fg/bg/attrs per cell) + the inventoried subset (SGR
  0/1/3/4/7/23/24/30-37/39/40-47/49, cursor A-H/f/E/F, erase J/K, IL/DL
  @/P/L/M/S/T, regions r/t, modes 20/>1/?7, save/restore s/u, controls
  BEL/BS/HT/LF/VT/FF/CR/SO/SI, 24-param cap, depth ladder). Dirty-cell
  tracking; scroll = grid rotate (free), never a blit.
- **Glyphs**: ripped at init from the loaded ANSI `TextFont`
  (`CharData`/`CharLoc`/`Modulo`) into chip-RAM blitter cells -- no new
  font files, no redistribution issue. PETSCII later reuses its strikes.
- **Backend** (Amiga-only): chip bitmaps (640x256x8 max, ~160 KB),
  copper palette load from `ansi32[]` (+UI pens), cookie-cut glyph blits
  (`$CA`, `hardware/blit.h`), hardware scroll via `bplpt`/BPLCON1 where
  legal, sprite block cursor. FMODE 32/64-bit values come from the AGA
  HRM (NDK gap, documented in research).
- **Routing**: `ConWrite` branches to the renderer when the AGA View is
  loaded (same place `drivertype` branches today); `AddBuf` still fed at
  the 3 `Receive` sites (scrollback/save/print untouched); NAWS/`40,25`
  geometry unchanged; `isAppIconified` drop kept.
- **Input**: `input.device IND_ADDHANDLER` filter (`IECLASS_RAWKEY`,
  up/repeat/qualifiers) replicating the `MapRawKey` path; explicit
  focus arbitration (no windows to activate).
- **Re-homed**: `LEDs()`/activity rects (no titlebar chrome on a raw
  View -- status row or dropped, owner-visible choice in Phase 3);
  `PETSCII_CONSOLE_SETUP` becomes native no-wrap; cursor hide/show
  sequences honored natively.
- Out of scope: RTG (definitionally), >8-bit, XEM+AGA-direct combo,
  PETSCII rendering (separate follow-up reusing the backend).

## Verification

- Automated (host): interpreter suite -- SGR/cursor/erase/IL-DL/regions/
  wrap/scroll/tab/SO-SI, 24-param cap, depth ladder, dirty tracking,
  idempotence cases; mutation-checked. vbcc warning-clean.
- Automated (Amiga): full build links; View loads without taking down
  Workbench (FS-UAE first, then iron).
- Manual (owner, A1200): Phase-0 flood numbers (printables / CRLF /
  SGR / PETSCII, B/s + lines/s) vs post-build same floods -- **GO gate:
  scroll-bound throughput >= 2x baseline, else STOP**; board sessions
  (SyncTerm-reference ANSI art compared cell-for-cell); flip key both
  ways mid-session; disconnect/restart/iconify paths; per-entry mode
  switch.

## Phases

### Phase 0 -- probe build (gates everything)
`SpeedTest` flood variants (4 streams, B/s + lines/s, `timer.device`
microsecond clocks). Owner runs on the A1200, posts numbers. GO/NO-GO
against the 2x gate. Success: numbers on the table, decision recorded.

### Phase 1 -- interpreter + glyph cells (no hardware)
`aga_text.c` + host suite green + mutation-checked; runtime strike rip
from the loaded ANSI font (FS-UAE verified via a dump-and-compare test
against `Text()` output). Success: interpreter proven without touching
the display.

### Phase 2 -- View/copper/bitmap/blitter bring-up
Chip bitmaps, copper palette (`ansi32[]`), cookie-cut text, hardware
scroll, sprite cursor, on a toggle key beside the Intuition screen.
Success: floods beat baseline (the gate, re-measured), no OS damage.

### Phase 3 -- routing + input + re-homing
`ConWrite` branch, `input.device` handler, `AddBuf` feed, LEDs/status
decision, XEM exclusion, flip key. Success: full session loop on iron.

### Phase 4 -- config + classification + polish
Flag bit, Options item (+ commit), `DisplayDiffers` screen class,
per-entry carry, depth/mode validation. Success: manual matrix green.

### Landing
Branch `feature/aga-direct` from `main` (shared files: `ConWrite`,
`OpenDisplay`, `site_prefs` -- rebase over whatever landed first); one
upstream PR. No PR without explicit owner permission.
