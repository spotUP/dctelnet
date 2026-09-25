/* src/site_prefs.h -- per-Address-Book-entry settings (upstream issue #10).
 *
 * `prefs` (DCTelnet.h) is the effective session state every reader uses.
 * `globalPrefs` is what LoadPrefs() fills and SavePrefs() writes. While
 * connected to an entry with settings of its own, `prefs` holds the entry's
 * snapshot and `globalPrefs` is left alone; on disconnect the globals return.
 *
 * Window geometry (win_*, sb_*, toolBarWin_*) always stays global (D5).
 */
#ifndef SITE_PREFS_H
#define SITE_PREFS_H

#include "DCTelnet.h"

/* effective = entry, except the window-geometry fields which stay global. */
void SitePrefs_ApplyEntry(struct PrefsStruct *effective,
                          const struct PrefsStruct *global,
                          const struct PrefsStruct *entry);

/* effective = global (disconnect path). */
void SitePrefs_RestoreGlobal(struct PrefsStruct *effective,
                             const struct PrefsStruct *global);

/* TRUE when switching between a and b needs CloseDisplay()/OpenDisplay().
 * *reopenScreen (may be NULL) is set TRUE when the screen itself must be
 * reopened, FALSE when reopening the windows is enough. Live settings
 * (echo, scrollback, transfer paths, ...) never need a restart. */
BOOL SitePrefs_DisplayDiffers(const struct PrefsStruct *a,
                              const struct PrefsStruct *b,
                              BOOL *reopenScreen);

#endif /* SITE_PREFS_H */
