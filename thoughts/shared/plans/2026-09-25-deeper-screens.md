---
date: 2026-09-25
topic: "256-colour AGA/RTG with private ANSI pens (deeper screens)"
tags: [dctelnet, aga, rtg, palette, pens, prefs, ibmcon]
status: final
---

# Deeper-screens plan

Research: `thoughts/shared/research/2026-09-25_deeper-screens-aga-rtg.md`.

## Decisions already made (owner, 2026-09-25)

- **Full private-pens redesign** (not the caps-only stopgap).
- Scope: **<=8-bit paletted** AGA/RTG. True >8-bit chunky RTG is excluded
  (fits neither ibmcon's planar pen model nor `LoadRGB*`).

## Invariant

ANSI output (ibmcon pens 0-15) keeps working byte-identically, while the
AmigaOS UI (title bar, menus, gadgets, requesters) renders in its own
colours on any depth. No existing book/prefs may become unloadable.

## Design: ANSI stays low, UI moves high

Key insight: ibmcon cannot emit pens >=16 (would fork the device, and
DCTelnet ships the stock binary) -- so ANSI keeps pens 0-15 and the **UI
moves to obtained high pens**. No ibmcon change, no XEM change.

- Prefs migration (append-only, first 376 bytes untouched so old builds
  keep reading):
  - Keep `UWORD color[16]` in place (legacy RGB4, old-file compat).
  - Append `ULONG ansi32[16]` (XRGB `0xRRGGBB00` for `LoadRGB32`).
  - `LoadPrefs`: short/old file -> `ansi32[i] = RGB4to32(color[i])`
    (`nibble * 17`); `SavePrefs` writes the new size.
  - Sidecar: bump magic `DCS1`->`DCS2` for the new size;
    `SitePrefs_Decode` also accepts `DCS1` at the legacy size (copies the
    prefix incl. `color[16]`, derives `ansi32`). New sidecars stay opaque
    to old builds (trailing bytes ignored -- already true).
- Screen open (`OpenAppScreen`): on depth>=5 (V39+ only, see below)
  `ObtainPen` 16 UI pens; `SA_Pens` UI slots (`colorPens` positions) point
  at them; program the full 256 table with one `LoadRGB32`: 0-15 from
  `ansi32`, 16-31 UI colours sampled from the Workbench screen colormap
  at startup (`LockPubScreen("Workbench")` + `GetRGB32`; hardcoded classic
  fallback), 32-255 black. `SA_Colors32` alternative only if Load-after-open
  flickers on RTG (verify on HW).
- Depth gating: requester caps (`ASLSM_MaxDepth`, `RTSC_MaxDepth`) 4->8;
  depth>4 requires `GfxBase>=39`, else cap stays 4. Screen-open failure
  keeps the existing `DEFAULT_MONITOR_ID` path.
- Hardcoded-pen users move to UI pens: `SetAPen(...,1/10/11/15)` (LEDs,
  `:1124,1661,2405,2793,3126`), IntuiText `FrontPen = 15` (`:3216`),
  `DetailPen/BlockPen = 255` (`:3021-3022`) -- via a small `UiPen(n)`
  mapping macro so the mapping lives in one place.
- Palette editor (`ChoosePalette`): still edits the 16 ANSI slots, but
  round-trips `GetRGB32`/`SetRGB32` into `ansi32` (UI derived, not edited).
- XEM: while `FLAG_USE_XEM_LIBRARY`, cap the requester at 4 with a notice
  (`xemvt340.library` depth>4 behaviour unknown, binary-only).
- WB mode: excluded (Workbench owns that palette; items already disabled).
- `site_prefs.c`: the `color` memcmp becomes `ansi32` (sizeof-driven);
  palette stays per-entry as today. `sb_lines`-style geometry rule
  unaffected.

## Verification

- Automated (host, `cd test && make`): pure `RGB4to32` conversion table
  test + `DCS1` legacy-sidecar migration test (old blob decodes, `ansi32`
  derived) in `test_site_prefs.c`; mutation-checked.
- Automated (vbcc): full build warning-clean.
- Manual (owner, FS-UAE A1200 = AGA): 256-colour screen -- UI in WB colours,
  ANSI 16 correct incl. bold 8-15, palette editor round-trip, per-entry
  palette apply/restore, quit/reload, old 1.9.1 binary loads the new book,
  XEM NOTICE + cap at 4. RTG/Picasso96: owner-dependent, optional row.

## Phases

### Phase 1 -- struct + migration + host tests
`PrefsStruct` append, `LoadPrefs`/`SavePrefs` migration, `DCS2` +
`DCS1`-accept in `site_prefs.c`, conversion + migration tests green.
Success: old prefs/book/sidecar files load; new files round-trip.

### Phase 2 -- screen open + palette + UiPen mapping
`ObtainPen` UI block, `LoadRGB32` table, `SA_Pens` retarget, `UiPen`
macro at all hardcoded sites. Success: AGA 256-colour screen, UI correct,
ANSI correct, depth<=4 byte-identical behaviour.

### Phase 3 -- requesters + editor + XEM gate
Caps 4->8 (both requesters, V39 gate), RGB32 editor round-trip, XEM cap
notice. Success: full manual matrix above.

### Landing
Branch `feature/deeper-screens` from `main` (touches `site_prefs` shared
with per-entry work -- rebase if that lands first); one upstream PR. No PR
without explicit owner permission.
