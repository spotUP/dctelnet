---
date: 2026-09-25
topic: 256-colour AGA and RTG screens with private ANSI pens
tags: [research, aga, rtg, palette, pens, ibmcon, prefs]
status: final
---

# Deeper-screens research

Owner report 2026-09-25: on its own screen DCTelnet opens at most 16 colours
and the ANSI palette takes over the SAME pens AmigaOS UI uses. Wanted:
256-colour AGA (depth 8) and RTG (P96/CGX, 8-bit paletted) with ANSI on
private pens. Documents what IS on `feature/per-entry-settings`.

Scope note: true RTG >8-bit (15/16/24-bit chunky) fits neither ibmcon's
planar pen model nor `LoadRGB*`; this covers <=8-bit paletted only.

## 1. Current screen/palette path

- Storage: `PrefsStruct` (`src/DCTelnet.h:8-35`) holds
  `DisplayID/Width/Height/Depth`, `UWORD color[16]`; whole-struct
  save/load (`SavePrefs` `src/DCTelnet.c:626-635`, `LoadPrefs`
  `:1266-1345`). Per-entry: `DisplayDepth` diff reopens the screen,
  `color[]` diff restarts windows only (`src/site_prefs.c:50-70`).
- **Both mode requesters hard-cap depth 4**: ASL `ASLSM_MaxDepth, 4`
  (`src/requesters.c:1051`), ReqTools `RTSC_MaxDepth, 4`
  (`src/DCTelnet.c:732`); first-run default PAL HIRES depth 4 (`:697-700`).
- Screen open (`OpenAppScreen`, `src/DCTelnet.c:2932-3006`): `colorPens`
  (`:259`, `~0`-terminated `SA_Pens` array, only positions 0-11 set) passed
  via `SA_Pens` (`:2984`, `:2998`); **no `ObtainPen`/`ObtainBestPen`
  anywhere (verified)**; pens are hardcoded 0-15. Depth<3 uses
  `&colorPens[12]` (`:2971`).
- Palette load (`OpenAppWindow`, `:3067`): `LoadRGB4(..., prefs.color, 16)`
  fullscreen-only; editor `ChoosePalette()` (`:751-775`) reads back
  `GetRGB4` for `i<16`. WB mode disables both items (`:3026-3029`).
- Other hardcoded-pen users: `SetAPen(...,15/10/11/1)` (`:1124,1661,2405,
  2793,3126`), IntuiText `FrontPen = 15` (`:3216`), `DetailPen/BlockPen =
  255` (`:3021-3022`).

## 2. ibmcon.device pen model (`/Users/spot/Code/ibmcon/ibmcon.device.bugfixed.asm`)

- SGR 30-37/40-47 map straight onto pens (`con_FgPen` `:1437-1450`,
  `con_BgPen` `:1467-1474`); unit>=1 swaps 1<->7 (IBM order); DCTelnet opens
  unit>=1, so the swap is active.
- **Depth ladder is already depth-agnostic >=4** (`:802-840`): depth>=4 gives
  SGR + bold=pen+8. Depths 5-8 render today with pens 0-15; **nothing maps
  SGR to pens >=16**.
- Cell size (`con_Cols`) derives from `tf_XSize` once at `ConInit`
  (verified `:919-930`): font swaps still need close/reopen.

## 3. XEM path

Only coupling is `xemIO->xem_screendepth = scr->BitMap.Depth`
(`src/Xem_wrapper.c:130`); `xemvt340.library` is binary-only -- whether it
handles depth>4 is unknown. Keep XEM on the depth-4 path unless HW proves
otherwise.

## 4. NDK availability (bundled `.ndk/`, nominally 3.2)

Verified present: `ObtainPen`/`ObtainBestPen(A)`/`ReleasePen` (V39),
`LoadRGB32`/`SetRGB32`, `SA_Pens`/`SA_Depth`/`SA_DisplayID`/`SA_Colors32`
(V39)/`SA_SharePens`, `DisplayInfo.MaxDepth`, `DIPF_IS_AA` (AGA),
`DIPF_IS_FOREIGN` (RTG-as-foreign), `BestModeID`+`BIDTAG_Depth` (V39).
**No vendor SDK**: no P96/CGX headers or FD files (verified) -- standard
intuition/graphics calls suffice (P96/CGX emulate them); detect via
`DIPF_IS_FOREIGN`/`BestModeID` if needed. GfxBase version guards needed
(codebase only checks Asl>=38, Gfx>=36).

## 5. Fonts

`tools/gen_petscii_font.py` emits classic 1-bpp `TextFont` strikes; rendering
uses Fg/Bg pens, so they work unchanged at any depth. **No deeper font
versions needed.**

## 6. Candidate levels (facts, not design)

1. Widen `color` to 256 + `SA_Colors32`/`LoadRGB32` + private-pen
   reservation: only level separating ANSI from UI pens and persisting
   256-colour palettes; breaks prefs-blob compat (`DCS1` size, `<252` old
   check) -- needs version/migrate.
2. Remap SGR in ibmcon to a 16-of-256 private window: forks device ABI,
   covers neither UI separation nor RTG>8-bit.
3. Lift caps only (`MaxDepth` -> 8, allow 5-8): trivial, zero format change;
   pens still collide, entries 16-255 uninit, editor still 16-wide. Stopgap
   spike at best.
