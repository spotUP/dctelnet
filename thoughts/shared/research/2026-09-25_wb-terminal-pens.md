---
date: 2026-09-25
topic: WB mode terminal background and ANSI colours (SGR remap)
tags: [research, workbench, pens, sgr, ibmcon, colormap]
status: final
---

# WB terminal research

Owner report 2026-09-25: on Workbench the terminal has no black background.
Documents what IS on `feature/deeper-screens`.

## 1. Mechanism

- ibmcon defaults at `ConInit`: `con_FgPen = con_DefaultPen` (=1),
  `con_BgPen = 0` (`ibmcon.device.bugfixed.asm:940-941`, `:1405-1406`).
  SGR 0 resets to the same pair.
- On the custom screen pen 0 = ANSI black: correct terminal. On Workbench
  pen 0 = WB blue: blue terminal. The window base fill is irrelevant --
  the console fills everything with its bg pen.
- SGR parsing asymmetry (verified `:1441-1510`): foreground does
  `pen = param-30` with NO range validation; background validates
  `pen < (1<<depth)` and falls back to 0. So remapped fg pens of any value
  work; remapped bg pens must exist on the screen.
- SGR 39/49 (default fg/bg) are honoured (`:1455-1461`, `:1502-1504`).
- Palette can't be reprogrammed (shared WB screen) and ibmcon's pens can't
  be redirected (ships stock 1.4 binary; rebuilding the device is out of
  scope). Depth ladder already adapts (`:802-840`).

## 2. What the OS offers (verified in NDK/autodocs)

- `ObtainBestPen(cm, r, g, b, tags)` (V39): shared best-match pen, safe on
  public screens; `ReleasePen` frees (refcounted). Precision tag
  `OBP_Precision` available.
- Stock 4-colour WB physically cannot show 8 ANSI colours: best-match
  collapses to white/black/gray. Readable and approximate is the ceiling
  there; deep WB gets near-perfect ANSI.

## 3. Design (facts, not plan)

- ibmcon dispatches SGR params 0-49 only (`Csi_m_SetGraphics` jump table;
  >49 ignored). Colour params address pens 0-7 exclusively (30-37/40-47,
  plus 39/49 defaults). Consequence: remapped pens MUST be 0-7 -- an
  `ObtainBestPen` over the whole colormap would return inexpressible pens.
- So: nearest-match each ANSI target against WB pens 0-7 (read via
  `GetRGB32`, V39+), no obtain/release needed (shared pens only
  referenced). Stock 4c WB collapses readably; mono collapses furthest;
  deep WB uses its first 8 (the core WB set). Pre-V39: legacy behaviour.
- WB-only, V39+-gated SGR rewrite in `ConWrite`: parse `ESC[...m`/`CSI...m`,
  remap colour params through the 8-entry map
  (fg 30-37, bg 40-47, 0 -> mapped white-on-black, 39/49 -> mapped
  defaults); every other sequence and byte passes through. BBS and local
  output share `ConWrite`, so one point covers both; XEM excluded.
- Map (re)built at console open (WB colormap valid while our window is
  open); dropped in `CloseDisplay`.
- The rewrite itself is pure `(bytes, map) -> bytes`: host-testable.
- Out of scope: window BackFill base (console fills everything anyway),
  XEM-on-WB rendering, rebuilding ibmcon.device.
