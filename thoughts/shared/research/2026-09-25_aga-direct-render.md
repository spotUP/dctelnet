---
date: 2026-09-25
topic: Chipset AGA direct-render path (own copper/blitter console)
tags: [research, aga, chipset, copper, blitter, performance, ibmcon]
status: final
---

# AGA direct-render research

Owner direction 2026-09-25: plan a hardware-banging AGA screen path for
maximum speed, demo-scene style. Three-track synthesis; documents what IS
on `feature/wb-terminal-pens`.

## 1. Where time goes (track 1)

- One `recv()` per `WaitSelect` wakeup (`DCTelnet.c:988`); ANSI mode emits
  exactly 1 `ConWrite` per recv (`:1094`); PETSCII up to 18 (`:1087`,
  240-byte chunks); `ConWrite` = 1 synchronous `DoIO(CMD_WRITE)` (`:326`).
- In ibmcon: every `ConWrite` pays cursor XOR-out + XOR-back (2 `RectFill`);
  printables batch into one `graphics Text()` per run (`FlushText`,
  `ibmcon.device.bugfixed.asm:2406`); SGR-heavy output fragments runs;
  **scroll = full-bitmap `ScrollRaster`** (`CursorDownN :2549`,
  `Csi_S :1623`) once per line past the bottom margin.
- Translator (`petscii_dispatch.c:206-242`) is fast-RAM table work, tens of
  68000 insns/byte, no OS calls. LEDs/scrollback/sigbit workaround are
  small constants or one-time. **Verdict: the console device dominates by
  1-2 orders of magnitude; translator is noise.**
- Measurement plan (probe build, real A1200): P1 socket (`recv :988`), P2
  parse loop (`:1026-1054`, RAW vs telnet), P3 translator (`:1072/1083`),
  P4 device (`ConWrite :289` split remap `:304` vs `DoIO :326`), P5
  scrollback (`AddBuf :889` via its flag), P6 scroll-vs-glyph differential
  (tall window vs pinned-at-bottom, cursor hidden `CSI 0p` vs shown, unit
  CHARMAP vs jump-scroll unit 2), P7 LEDs. Clocks: `timer.device`
  UNIT_MICROHZ or `ReadEClock`; rastertime bands; extend the existing
  `SpeedTest` harness (`:1209-1276`, 200×`DoIO`, lines/sec rating) with
  flood variants (printables / CRLF / SGR / PETSCII).

## 2. Hardware + reuse (track 2)

- NDK has all control registers (`hardware/custom.h`: `bplpt[8]`,
  `bplcon0-4`, `bpl1mod/2mod`, `cop1lc`, `bltcon0/1`, `bltapt-dpt`,
  `bltsize`, `sprpt[8]`, `dmacon`), blitter minterms incl. cookie-cut
  (`hardware/blit.h`: `$CA` = A+fill over dest), copper pseudo-ops
  (`graphics/copper.h`), BPLCON bits (`graphics/display.h`).
  **Gap: FMODE 32/64-bit fetch values are NOT in the NDK** -- need the AGA
  HRM. Geometry: 80x25 = HIRES 640x256; 40x25 PETSCII (16px cells) = same
  fetch; 8 planes = ~160 KB chip RAM per buffer.
- Reuse: font strikes are 1-plane row-major stripes (glyph g at bit offset
  g*16/row, `tools/gen_petscii_font.py:26-38`), not directly blittable
  cells -- regenerate per-glyph 8-byte cells from `tools/glyphs/*.bin`
  (ideal cookie-cut A-source) or slice words per row. `petscii_dispatch_
  byte()` (`petscii_dispatch.c:73-137`) already yields glyph+attrs+cursor
  state -- the reusable seam (bypass both ANSI emitters). `ansi32[16]` is
  the copper palette source (repeat to 256 or map 0-15 + UI 16+).
- ibmcon ANSI subset to reimplement (full list in track report):
  SGR 0/1/3/4/7/23/24/30-37/39/40-47/49 (NOT 2/5/6/8/22/27/28/29/38/48;
  attrs rebuild from zero; unit>=1 swaps pens 1<->7; fg==bg resets);
  cursor A-H/f (0->1, clamped), E/F, erase J0/1/2 + K0/1/2 (fills use SGR
  bg pen), insert/delete @/P/L/M/S/T (whole-window even in regions),
  regions r/t, modes 20/>1/?7 (+init LNM-off/margin+wrap-on), save/restore
  s/u, controls BEL/BS/HT(stops 9,17..)/LF/VT/FF/CR/SO/SI, max 24 params,
  DEPTH ladder (mono/fixed/8-col/16-col+bold), cursor = COMPLEMENT RectFill.
- Input without windows: `input.device IND_ADDHANDLER` + `Interrupt`
  chain, filter `IECLASS_RAWKEY`, replicate `MapRawKey` + up/repeat +
  qualifier tracking; focus arbitration becomes ours.
- In-tree direct-hardware use: effectively none (only chip-RAM mouse
  sprite). All rendering is Intuition windows + graphics.library.

## 3. Integration surface (track 3)

- Lifecycle to mirror: `OpenDisplay` 8 steps (screen+fonts, Visual/DrawInfo,
  `OpenAppWindow`, menus, XEM, console bind, `?7l`, LEDs, banner) and
  `CloseDisplay` reverse incl. signal-31 repair; restart triggers per menu
  item (screen vs windows class); per-entry apply/restore; iconify path.
- Must stay Intuition: menu strip, ALL requesters/dialogs (address book,
  edit, toolbar, scrollback, packet, Xfer, connecting windows), XEM
  (`xem_window = win` renders via graphics `Text` into our window!).
- Replaceable: console `OpenDevice`/`CloseDevice` bind, `SetFont(win)+
  PETSCII swap, `?7l` setup (native no-wrap instead), `BuildWbPenMap`
  (native palette load reusing the `LoadRGB32` path).
- Separable: scrollback (`AddBuf` feed at 3 `Receive` sites keeps
  list/save/print working). Re-home: `LEDs()` + activity rects (target
  `scr->RPort`, invalid without Intuition chrome).
- Config precedent: `FLAG_USE_XEM_LIBRARY` + `displaydriver` (derive
  `drivertype`, branch `ConWrite`, caps, menu validation). New selector =
  new flag bit (16-31 free) classified in `SITE_PREFS_*_FLAGS`, or
  driver-style string (auto windows-restart via existing strcmp arm).
- `ConWrite` caller inventory: BBS bytes (the replaced hot path), local UI
  texts (banner/disconnect/progress/errors, PETSCII-folded), echoes
  (`SendMisc` offline sends, `OutKey` local echo), cursor discipline
  (`SpeedTest` hide/show, STRIP reset). NAWS geometry derives from
  win/font metrics; `petscii_dispatch_init(40,25)` per connection.

## 4. Candidate levels

1. **Coexisting custom screen** (own ViewPort/copper via graphics.library,
   LoadView-based, multitasking-safe; menus/dialogs/XEM stay Intuition):
   real headroom (kills per-DoIO + XOR + ScrollRaster overhead), keeps the
   app intact. Recommended.
2. **Translator/renderer split only** (glyph-index feed, still console
   device): removes ~nothing measurable (translator is noise). Rejected.
3. **Full machine takeover** (Disable(), custom copper, no OS): maximum
   speed, breaks TCP/menus/multitasking -- contradicts a comms app.
   Rejected unless demo-mode-only is requested.
