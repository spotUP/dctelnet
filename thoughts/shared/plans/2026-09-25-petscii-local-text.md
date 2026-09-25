---
date: 2026-09-25
topic: "Readable local texts in PETSCII Mode (translate to PETSCII)"
tags: [dctelnet, petscii, fonts, console, localprint]
status: final
---

# PETSCII local-text plan

Research: `thoughts/shared/research/2026-09-25_petscii-local-text.md`.

## Decisions already made (owner, 2026-09-25)

- Level: **translate local texts to PETSCII** (no font-swap races, no
  info suppression).

## Invariant

Every byte the BBS sends renders exactly as today. Every local status text
is readable under the PETSCII font in both C64 charsets. ANSI mode output
is byte-identical.

## Design: case-fold at the local-text choke point

Core fact: a C64 charset shows ASCII uppercase bytes ($41-5A) readably in
BOTH halves (upper/graphic 'A', lower 'a'), while ASCII lowercase ($61-7A)
lands on graphics. So folding `a-z` -> `A-Z` makes any status text readable
in either charset; digits, punctuation and ESC/SGR bytes pass through
untouched (SGR colours in the banner keep working; no translator state
involved).

- New tiny module `src/petscii_local.c/.h`:
  `void Petscii_MapLocalText(char *s)` -- in-place fold of `a-z` to `A-Z`.
  ECMA-48 sequences (`ESC [ params intermediates final`, 8-bit CSI ditto)
  are skipped byte-identically (folding an SGR final `m` or `?7l` would
  corrupt them). Everything else passes through. Idempotent.
- Wiring (`src/DCTelnet.c`, same file as `drivertype`):
  - `LocalFmt`: fold `buf` in place after formatting, before `ConWrite`,
    when `PETSCII && !XEM`.
  - `LocalPrint(char *data)`: literals can't be folded in place -- copy
    through a static scratch (`static char localScratch[2048]`, truncate)
    when active, else today's direct path.
  - Active condition: `(prefs.flags & FLAG_PETSCII_MODE) &&
    drivertype == DRIVER_NORMAL`. XEM excluded (`xem_font = ansiFont` is
    already readable); WB console covered (same font logic).
  - Double-mapping is idempotent (A-Z stable), so shared-`buf` call chains
    are safe.
- Explicitly untouched: BBS path (`Receive` translators), keyboard echo
  (`OutKey` writes translated PETSCII -- correct already), `TextFmt`
  (Xfer gauges, RastPort font), `SendMisc` login sends (user data; a
  PETSCII login translation would be a separate follow-up, noted not done).
- Limitation noted: `[\]^` and a few puncts are graphics in one half;
  status texts don't use them. Long lines may wrap at the 40-col grid --
  same as BBS output, acceptable.

## Verification

- Automated (host, `cd test && make`): `test_petscii_local.c` -- all 256
  bytes (a-z folded, rest identical), empty string, SGR sequence preserved,
  idempotence; mutation-checked. Follows the `test_site_prefs.c` pattern.
- Automated (vbcc): full build warning-clean.
- Manual (owner, FS-UAE, PETSCII entry): startup banner readable
  (screenshot vs SyncTerm-style expectation), connect/disconnect texts,
  missing-font warning (temporarily rename `Fonts/`), ANSI-mode session
  byte-identical, XEM session unchanged.

## Phases

### Phase 1 -- module + host tests
`petscii_local.c/.h`, `test_petscii_local.c`, Makefile wiring, green +
mutation-checked. Success: mapping proven without touching the app.

### Phase 2 -- wiring in LocalPrint/LocalFmt
Gated fold as above. Success: manual matrix green; ANSI/XEM untouched.

### Landing
Branch `feature/petscii-local-text` from `feature/petscii-mode`
(independent of per-entry work; rebase onto `main` once #14 merges); one
upstream PR. No PR without explicit owner permission.
