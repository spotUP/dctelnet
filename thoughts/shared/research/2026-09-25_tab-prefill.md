---
date: 2026-09-25
topic: Tab into prefilled field appends instead of replacing (GadTools string gadgets)
tags: [research, gadtools, guis, address-book, requesters]
status: final
---

# Tab-prefill research

Owner report 2026-09-25: tabbing into a prefilled text field leaves the old
text; typing appends. Documents what IS on `feature/per-entry-settings`.

## 1. Focus and activation today

Edit window (`EditProfile`, `src/guis.c:840-988`, gadgets `src/guis.c:686-698`,
IDs `src/edit.h`):

- IDCMP is `STRINGIDCMP|TEXTIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY`
  (`src/guis.c:762`). No `IDCMP_RAWKEY`: raw TAB never reaches the app.
- Initial focus `ActivateGadget(GD_SITE)` (`src/guis.c:875`). Shortcut keys
  S/A/P/U/W (+T/G, O/C) only call `ActivateGadget` (`src/guis.c:921`) -- no
  cursor/selection handling of any kind.
- No `'\t'` case in the VANILLAKEY switch; while a string gadget has focus,
  keystrokes go to the gadget, so shortcuts only fire when no text field is
  active. GADGETUP from a string field (Enter) is silently ignored
  (`src/guis.c:924-944` handles only OK/Cancel/UseCurrent/UseGlobal).
- Gadgets are built by `MakeGadgets` (`src/guis.c:110-137`) with tags from
  `editProfileGTags` (`src/guis.c:714-726`): only `GTST_String`/`GTIN_Number`
  + MaxChars + `GT_Underscore`. Verified: **no `GA_Immediate`, no
  `GTST_EditHook` anywhere in app code** (zero hits outside `third_party/`).

Connect prompt: `GetStringRequester` (`src/requesters.c:376-681`), single
STRING gadget (`:543-546`), `ActivateGadget` at `:603`; loop handles only
CLOSE / VANILLAKEY(ESC/RETURN) / GADGETUP(OK/Cancel) (`:623-660`). Same TAB
gap. Callers prefill from last server (`src/DCTelnet.c:1798`), Finger default
(`:1224`), XPR options (`:2560`), display ID (`:2593`).

History: `2a50c53` (issue #4 fix) rewrote EditProfile from
copy-on-GADGETUP-per-field (with an Enter-to-next-field ActivateGadget chain)
to read-back-all-on-OK -- deleting the only keyboard field-advance logic.
Field movement now relies entirely on Intuition-side TAB, which the app
cannot observe. `6196853` did the same for Function Keys.

## 2. Scope (all prefilled string/integer gadgets)

| Window | Fields |
|---|---|
| Edit Address Book Profile (`src/guis.c:840`) | 4x STRING + 1x INTEGER Port from `BookStruct`; "new" prefills `"*new site*"`, `"*ip/host here*"`, 23. **Primary site** |
| Function Keys (`src/guis.c:1328`) | 10x STRING from `fKeys`; **no ActivateGadget at all** |
| `GetStringRequester` (`src/requesters.c:376`) | 1x STRING per caller (connect, Finger, XPR options, display ID, `xem_tgets`) |
| XPR Options (`src/Xfer.c:1380-1435`) | INTEGER + STRINGs, but `MENU_PROTOCOL_OPTIONS` TODO (`DCTelnet.c:2564`) suggests `XferOptions()` is unwired -- marginal |
| Out of scope | Connecting... (text-only), Address Book main (listview/buttons/cycle), ASL/file/font/screenmode requesters (system-owned) |

Saved values are NOT affected (read-back-on-OK everywhere): purely entry UX.

## 3. What GadTools offers

- **No select-all-on-activate tag.** `GA_Immediate` only controls GADGETUP
  timing. The customization point is `GTST_EditHook` (string kind; INTEGER
  coverage needs a docs check).
- Plain `StringInfo` exposes `Buffer/UndoBuffer/BufferPos/DispPos/LongInt`
  but **no selection range**: true highlight/replace-on-type cannot be done
  by poking `StringInfo`; cursor can only be repositioned (cf. packet-window
  send path resetting `BufferPos/DispPos`, `src/DCTelnet.c:2043-2044`).

## 4. Candidate levels (facts, not design)

1. Shared `GTST_EditHook` emulating select-all: only level covering
   Intuition-driven TAB/mouse entry the loop never sees; new mechanism, no
   in-repo precedent, care with `UndoBuffer` + INTEGER field.
2. Dialog-loop focus events + own TAB cycling (`IDCMP_RAWKEY`,
   `ActivateGadget`): small/local per dialog; cursor placement only, misses
   entries without messages.
3. Stopgap: reset `BufferPos`/`DispPos` after programmatic `ActivateGadget`:
   swaps append-at-end for insert-at-start, shortcut-focus only; does not fix
   the report.

Open owner question (from todos): select-all-on-activate (modern) vs clear
on entry.
