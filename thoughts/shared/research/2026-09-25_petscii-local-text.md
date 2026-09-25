---
date: 2026-09-25
topic: PETSCII Mode garbles DCTelnet's own local status texts
tags: [research, petscii, fonts, console, ibmcon, localprint]
status: final
---

# PETSCII local-text research

Owner report 2026-09-25 (+screenshot): with PETSCII Mode on, the startup
info block and other local messages print through `Petscii.font`, so
lowercase ASCII lands on C64 graphics glyphs ("P[graphics]: 68020").
Wanted: BBS output PETSCII, app texts readable. Documents what IS on
`feature/per-entry-settings`.

## 1. One console, one font, shared

- `ConWrite()` (`src/DCTelnet.c:274`) is the single choke point (XEM or
  `DoIO(CMD_WRITE)`; dropped while iconified). `LocalPrint()` (`:298`) and
  `LocalFmt()` (`:305`, via `buf`+`RawDoFmt`) wrap it with no translation.
  `TextFmt()` (`:319`) draws to Xfer gauges only -- unaffected.
- `Receive()` (`:903`): PETSCII branch (`:980-1012`) translates to raw glyphs
  or ANSI, then the **same `ConWrite()`** (`:1009`, `:1016`). BBS bytes and
  local bytes share console + font; no per-write charset tag.
- Translator state is global `g_petsciiState`; the C64 shift flag toggles on
  bytes 14/142 and the font swaps inline in `Receive()` (`:1000-1002`).

## 2. Local-text inventory (all via LocalPrint/LocalFmt/ConWrite)

- **Startup banner** (`OpenDisplay`, `if(!isConnected)`, `:3395-3437`): the
  reported bug -- `:3415` Processor/Kickstart/Display-engine/TCP-Stack block,
  `:3433` SocketBase string, `:3437` "Not active".
- **Connect-time** (`EstablishTCPConnection` `:2775-2874`): "Looking
  up ...", "Host lookup aborted.", "Unknown host...", "Found ...",
  "Connecting to ...", "Cannot Open Socket.", "Connected."; `CheckError()`
  (`:2702-2710`); the PETSCII-missing-font warning itself (`:2896-2898`).
- **Disconnect**: "Connection closed ... spent online."
  (`DisConnect`, `:535-539`) -- still PETSCII font (no reopen on
  disconnect).
- **Misc**: Xfer library errors (`src/Xfer.c:879,915`), `SpeedTest()`
  (`:1146-1194`), STRIP_ANSI reset (`:2434`), `SendMisc()` (`:1049-1056`,
  direct `ConWrite` when offline).
- Keyboard echo (`OutKey`, `:2007-2009`) writes the *translated PETSCII
  byte* -- correctly uses the PETSCII font; shares the path by design.

## 3. Font mechanics and constraints

- Open (`OpenAppScreen`, `:2932`): `ansiFont` (+topaz fallback `:2944`);
  `petsciiFont`/`petsciiFontLower` when the mode is on (`:2955-2956`).
  Window opens directly in the PETSCII font (`:3146`).
- **Cell size is fixed at `OpenDevice()`** from the RastPort font (verified
  in `ibmcon.device.bugfixed.asm` ConInit `:919-930`; code comment
  `:2496-2499`): toggling PETSCII needs close+screen-reopen (`:2500-2501`).
  Mid-screen `SetFont` changes glyphs but not `con_Cols` (40 PETSCII cols on
  an 80-col grid -- hence `PETSCII_CONSOLE_SETUP "\x1b[?7l"` + the
  dispatcher's own 40-col wrap).
- XEM path pins `xem_font = ansiFont` (`src/Xem_wrapper.c:127`):
  PETSCII+XEM unhandled (pre-existing).
- Coverage comes from the **font pair + runtime switching** (upper/lower
  256-slot strikes from `tools/glyphs/c64_{upper,lower}_8x8.bin`); raw ASCII
  lowercase lands on graphics in whichever half is selected.

## 4. ibmcon charset-switch capability: none usable

The only in-stream mechanism is SO/SI (`$0E`/`$0F`) setting `con_CharMask`
(low<->high half of the *same* font). **No escape/CSI swaps the TextFont
mid-screen.** Any fix is host-side: translate bytes or swap font around the
write.

## 5. Candidate levels (facts, not design)

1. Translate local texts through the PETSCII encoder when the PETSCII font
   is active (e.g. via `petscii_stream_to_rawglyphs`): no flicker/reopen;
   touches shared `g_petsciiState` cursor/shift accounting; SGR colours in
   local strings need mapping.
2. `SetFont(win->RPort, ansiFont)` around local writes: one-place scope;
   per-write cost + races with `Receive()`'s shift-swap; 40-col grid
   mismatch for 80-col ASCII text.
3. Policy: defer/suppress pre-connect banner, ASCII-only subset for status:
   smallest change; future local texts stay broken; hides info in PETSCII
   mode.
