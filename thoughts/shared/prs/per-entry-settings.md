---
date: 2026-09-25
topic: "PR to bruno-frederic/dctelnet: per-entry settings (issue #10)"
tags: [dctelnet, address-book, prefs, pr, issue-10]
status: draft
---

Branch `feature/per-entry-settings` (base: `feature/petscii-mode`, i.e. stacked
on PR #14; rebase onto main once #14 merges). Pushed to `fork`.
PR creation needs the owner's explicit permission -- never open one unasked.

## Summary

Fixes #10: an Address Book entry can now carry **all** settings (the whole
`PrefsStruct`), so e.g. a C64 board gets PETSCII Mode + the Petscii font while
everything else keeps the global setup.

- Entry settings live in `PROGDIR:Sites/<id>.prefs` (`'DCS1'` + struct dump);
  `BookStruct.res[0..3]` becomes `ULONG settingsId` (record stays 256 bytes,
  old binaries load the book fine and use global settings).
- `prefs` is now the effective session state; new `globalPrefs` is what
  `DCTelnet.Prefs` holds. Menu/requester edits commit to both unless entry
  settings are active (then session-only). Server-negotiated Local Echo no
  longer leaks into the saved prefs.
- Connecting from the Address Book installs the entry's settings and reopens
  the display first when they need it; disconnect restores the globals (and
  the display). Window geometry always stays global.
- Edit window: "Settings: Global/Own" + Use Current / Use Global buttons
  (staged until OK). Options menu: "Save Settings to Address Book Entry"
  while connected to an entry (tweak-and-save flow). Deleting an entry
  deletes its sidecar.

## Commits

1. `701325f` global/effective split + host-testable `site_prefs` core.
2. `41a7d92` sidecar storage (struct field, codec, delete cleanup).
3. `62c80b5` apply on connect, restore on disconnect.
4. `d78e781` UI (edit window, Options menu item).

## Testing

- Host: `cd test && make` (translator, tables, font check, U+FFFD guard, new
  `test_site_prefs`: apply/restore/display-diff/codec round-trip/bad
  magic/short-file/trailing-bytes/filename; mutation-checked).
- vbcc/68k: `DCTelnet.c`, `guis.c`, `site_prefs.c`, `connect.c` compile clean
  with the Makefile warning flags. (`Xfer.c` has pre-existing warning-65
  failures with this vbcc version, identical on unmodified HEAD.)
- FS-UAE (owner): global ANSI/IBM + entry with PETSCII/Petscii font;
  disconnect restores; quit/restart keeps global prefs; 1.9.1 binary loads
  the new book. [PENDING at PR-draft time]

## Notes

- Out of scope (per plan): prefilled-values-on-tab issue, delete-row-0
  `lastcode` underflow -- separate PRs (D8).
- `src/smakefile` (Amiga-native) predates PETSCII and does not list the new
  modules either; `src/Makefile` (vbcc) is updated.
