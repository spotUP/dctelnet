---
date: 2026-09-25
topic: "Select-all on entry for prefilled fields (tab fix)"
tags: [dctelnet, gadtools, guis, requesters]
status: final
---

# Tab select-all plan

Research: `thoughts/shared/research/2026-09-25_tab-prefill.md`.
NDK protocol: `intuition/sghooks.h` (SGWork, EditOp, SGA_*, SGH_KEY/CLICK).

## Decisions already made (owner, 2026-09-25)

- Behaviour: **select-all on entry** (first keystroke replaces), modern UI.
- Scope: all GadTools sites (edit window, Function Keys, `GetStringRequester`).

## Invariant

Saved values are already correct (read-back-on-OK everywhere); this changes
only editing UX. After the change: entering a prefilled field by ANY path
(TAB, mouse click... see edge below, shortcut, initial activation) arms
replace; the first printable keystroke replaces the content; Enter/OK still
saves exactly what is shown.

## Design: one shared EditHook

GadTools string/integer gadgets support `GTST_EditHook`/`GTIN_EditHook`
(same tag, `libraries/gadtools.h:334-335`); the hook gets `SGH_KEY` per
keystroke and `SGH_CLICK` on activation/click, operating on `SGWork`
(`WorkBuffer/NumChars/BufferPos/LongInt`, honour `SGA_USE`).

- New shared hook `StringSelectAllHook` (hook + `struct Hook` boilerplate) in
  `src/utils.c` (`utils.h`), wired via `GTST_EditHook`/`GTIN_EditHook` tags
  in `editProfileGTags`, `fKeysGTags`, and the requester's gadget tags
  (`src/requesters.c:543-546`). GadTools supplies `WorkBuffer` for tag
  hooks; no manual `StringExtend` needed.
- Armed state: one static `struct Gadget *armedGadget` (single task, all
  dialogs modal -- safe).
  - `SGH_CLICK`: arm when the activation did NOT come from a real mouse
    click (`IEvent == NULL` for `ActivateGadget`, or `ie_Class !=
    IECLFC... RAWMOUSE`); a genuine click disarms (cursor positioning keeps
    working). Always leave `SGA_REDISPLAY`. Return nonzero (command
    implemented).
  - `SGH_KEY` with `EO_INSERTCHAR`/`EO_REPLACECHAR` while armed for this
    gadget: reset `WorkBuffer[0] = Code; NumChars = 1; BufferPos = 1`
    (the keystroke is pre-applied in `WorkBuffer` -- emptying alone would
    eat the typed char); for `SGM_LONGINT` (Port field) also set
    `LongInt` to the digit value. Leave `SGA_USE|SGA_REDISPLAY`. Disarm.
  - Any other op (ENTER, cursor moves, CLEAR, ...): disarm, leave `SGWork`
    untouched.
- TAB cycling itself needs no code: whatever moves activation (Intuition
  TAB, shortcuts, mouse) funnels through `SGH_CLICK`.
- Open verification first: whether `CreateGadgetA` sets `GFLG_TABCYCLE` by
  default (`MakeGadgets` never sets `ng_Flags`) -- the FS-UAE matrix covers
  both outcomes; the hook works either way.

## Verification

No host test: the hook speaks NDK structs (`SGWork`, `StringInfo`,
`InputEvent`) -- shimming all of `intuition/intuition.h` for a test would
duplicate the NDK and drift. Stated deviation; verification is:

- Automated: full vbcc build with the Makefile warning flags (new code must
  be warning-clean, incl. the `Hook`/`HookEntry` boilerplate).
- Manual (owner, FS-UAE): matrix -- TAB into prefilled field then type
  (replaces); click mid-text then type (inserts at cursor, does NOT nuke);
  shortcut-focus then type (replaces); Enter in field then OK (saves shown
  value); Port integer field; Function Keys window; connect prompt;
  cancel leaves everything unchanged.

## Phases

### Phase 1 -- hook + edit window
Hook in `utils.c`, tag in `editProfileGTags`, matrix rows 1-3 on the edit
window. Success: TAB-replace works, click-insert preserved.

### Phase 2 -- Function Keys + requester
Same tags in `fKeysGTags` + requester gadget; full matrix. Success: all
dialogs behave identically.

### Landing
Branch `feature/tab-select-all` from `main` (independent of PETSCII and
per-entry work); one upstream PR. No PR without explicit owner permission.
