/*
 * @file prefs_file.h
 * @brief The DCTelnet.Prefs file as bytes: which format it is and what it holds.
 *
 * Pure (no AmigaOS calls), unit-tested on the host. prefs.c reads the file
 * and hands the bytes here.
 */
#ifndef PREFS_FILE_H
#define PREFS_FILE_H

#include <exec/types.h>
#include <string.h>
#include "prefs.h"

enum
{
    PREFS_FILE_BAD,     // too short, or a version this build does not know
    PREFS_FILE_V2,      // "DCTP" version 2, of any data size
    PREFS_FILE_LEGACY   // a DCTelnet 1.x file: the raw old struct, no header
};

/* The longest Prefs file there can be: the header and its 16-bit dataSize
 * (a 1.x file is 508 bytes at most). prefs.c reads the whole file, never a
 * fixed sizeof(PrefsStruct): an older file is converted, a newer one longer. */
#define PREFS_FILE_MAX  (sizeof(struct DCTFileHeader) + 65535UL)

/*
 * Decodes a whole Prefs file into out (cleared first).
 *
 * A v2 file whose data is shorter than this build's PrefsStruct (written
 * by an older 2.x) fills the fields it has and leaves the rest 0 -- for
 * ValidateAndInitPrefs() to default; a longer one (a newer 2.x) gives its
 * first sizeof(PrefsStruct) bytes. Fields are only ever appended, so
 * neither is an error.
 *
 * A legacy file keeps every setting it has a place for (Prefs_FromLegacy).
 */
int Prefs_Decode(const UBYTE *file, size_t len, struct PrefsStruct *out);

/*
 * The settings of a DCTelnet 1.x PrefsStruct (big-endian bytes, len long)
 * in the current struct: screen mode, font, palette, window geometry,
 * paths, libraries, terminal type, scrollback size, and the option flags
 * with their current meaning. FALSE if it is too short to hold the screen
 * mode, font and palette.
 */
BOOL Prefs_FromLegacy(const UBYTE *old, size_t len, struct PrefsStruct *out);

/*
 * TRUE when p's full-screen mode suits p's renderer: the built-in renderer
 * (retro32-term) needs 80 columns and 25 rows of 8x8 cells, at least
 * 640x200 (it draws 16 colours straight into the bitplanes of a 4-plane
 * screen, through the RastPort on any other); ibmcon.device at most
 * 1920x1080; the others any size. Checked on load and whenever the
 * renderer changes.
 */
/*
 * PETSCII Mode used to be State bit 15 (APP_OLD_PETSCII_MODE); it is the Terminal
 * Type PETSCII now. A p with the old flag gets TermType PETSCII and the flag
 * cleared; the one place that reads the flag. Prefs_Decode and the Address Book
 * entry decoder call it, so what they hand out never carries the flag.
 */
void Prefs_MigratePetscii(struct PrefsStruct *p);

BOOL Prefs_ScreenFits(const struct PrefsStruct *p);


/*
 * The palette settings p's renderer shows: AnsiColors (ANSI order) for the
 * built-in renderer and XEM, DeviceColors (the console's pen order) for
 * console.device and ibmcon.device.
 */
UWORD *Prefs_Palette(struct PrefsStruct *p);

/*
 * The State of a first run (no prefs file, or an invalid State in it):
 * full screen, scrollback history on (as DCTelnet 1.x had it, so
 * Shift+Page Up scrolls back from the start), and rendererBits, the
 * renderer for this CPU (Renderer_DefaultForCpu()).
 */
ULONG Prefs_FirstRunState(ULONG rendererBits);

/*
 * The defaults of a first run (no prefs file, or an invalid State in it):
 * the State from Prefs_FirstRunState(rendererBits) and Terminal > ANSI
 * Music on. Fields whose default is 0 are left as they are.
 */
void Prefs_ApplyFirstRun(struct PrefsStruct *p, ULONG rendererBits);

/*
 * The first-run screen mode. p already holds the default mode (what the
 * requester shows preselected). ask() is the screen mode requester writing
 * into p: TRUE when the user chose a mode. When it returns FALSE (Cancel,
 * the window closed, ASL failed) p keeps the default and startup carries on;
 * the mode is changed later in Settings. Returns FALSE only when p holds no
 * mode the start can use.
 */
BOOL Prefs_FirstRunScreen(struct PrefsStruct *p, BOOL (*ask)(void));

/*
 * Default Scrollback window height for a display of displayHeight pixels
 * (half the display less a margin), never below WIN_MIN_HEIGHT. Safe to call
 * before the display size is known (0): returns WIN_MIN_HEIGHT.
 */
WORD Prefs_DefaultScrollbackHeight(WORD displayHeight);

#endif /* PREFS_FILE_H */
