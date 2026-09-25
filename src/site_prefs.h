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

/* -- Sidecar storage: PROGDIR:Sites/<settingsId>.prefs --
 *
 * A 4-byte magic/version ('DCS1') followed by a PrefsStruct dump in the same
 * byte layout as DCTelnet.Prefs. A missing file, bad magic or short read
 * decodes as "no settings" (FALSE): entries from books written before this
 * feature, or by old DCTelnet versions, simply use the global settings.
 * Longer files are accepted and the trailing bytes ignored.
 *
 * NOTE: the struct dump is 68k-native (376 bytes). The host tests below only
 * prove the framing logic; the on-disk layout is exercised on Amiga. */

/* Room for "PROGDIR:Sites/" + 20 digits + ".prefs" + NUL. */
#define SITE_PREFS_PATH_LEN 64

size_t SitePrefs_EncodedSize(void);
size_t SitePrefs_Encode(const struct PrefsStruct *entry, UBYTE *out, size_t outLen);
BOOL SitePrefs_Decode(const UBYTE *in, size_t inLen, struct PrefsStruct *entry);

/* sizeof(PrefsStruct) in 1.9.1 (68k): a DCS1 blob carries this many struct
 * bytes. Anything before ansi32[] keeps its offset (append-only rule). */
#define SITE_PREFS_V1_PREFIX 376

/* RGB4 (0xRGB) to XRGB 0xRRGGBB00 (nibble * 17). Pure; host-tested. */
ULONG SitePrefs_RGB4to32(UWORD rgb4);

/* Fill entry->ansi32[] from the legacy entry->color[] shadow. */
void SitePrefs_DeriveAnsi32(struct PrefsStruct *entry);

/* "PROGDIR:Sites/<id>.prefs" into out; NULL when outLen is too small. */
char *SitePrefs_FileName(ULONG id, char *out, size_t outLen);

#endif /* SITE_PREFS_H */
