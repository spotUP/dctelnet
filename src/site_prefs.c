/* src/site_prefs.c -- per-Address-Book-entry settings (upstream issue #10).
 *
 * Plain C, no AmigaOS calls: covered by test/test_site_prefs.c on the host.
 */
#include "site_prefs.h"

#include <string.h>

/* Flags that need CloseDisplay()/OpenDisplay() when they change. */
#define SITE_PREFS_RESTART_FLAGS \
    (FLAG_USE_WORKBENCH | FLAG_HIDE_TITLEBAR | FLAG_PACKET_WINDOW | \
     FLAG_USE_XEM_LIBRARY | FLAG_TOOL_BAR | FLAG_JUMP_SCROLL | FLAG_PETSCII_MODE)

/* ... of those, the ones that need the screen itself reopened
 * (shouldReopenScreen), not just the windows. */
#define SITE_PREFS_SCREEN_FLAGS \
    (FLAG_USE_WORKBENCH | FLAG_HIDE_TITLEBAR | FLAG_PETSCII_MODE)

void SitePrefs_ApplyEntry(struct PrefsStruct *effective,
                          const struct PrefsStruct *global,
                          const struct PrefsStruct *entry)
{
    *effective = *entry;

    /* D5: window geometry always stays global. */
    effective->win_left = global->win_left;
    effective->win_top = global->win_top;
    effective->win_width = global->win_width;
    effective->win_height = global->win_height;
    effective->sb_left = global->sb_left;
    effective->sb_top = global->sb_top;
    effective->sb_width = global->sb_width;
    effective->sb_height = global->sb_height;
    effective->toolBarWin_left = global->toolBarWin_left;
    effective->toolBarWin_top = global->toolBarWin_top;
}

void SitePrefs_RestoreGlobal(struct PrefsStruct *effective,
                             const struct PrefsStruct *global)
{
    *effective = *global;
}

BOOL SitePrefs_DisplayDiffers(const struct PrefsStruct *a,
                              const struct PrefsStruct *b,
                              BOOL *reopenScreen)
{
    ULONG changedFlags;
    BOOL screen;

    screen = (BOOL)(a->DisplayID != b->DisplayID ||
                    a->DisplayWidth != b->DisplayWidth ||
                    a->DisplayHeight != b->DisplayHeight ||
                    a->DisplayDepth != b->DisplayDepth ||
                    a->fontsize != b->fontsize ||
                    strcmp(a->fontname, b->fontname) != 0);

    changedFlags = a->flags ^ b->flags;
    if (changedFlags & SITE_PREFS_SCREEN_FLAGS)
        screen = TRUE;

    if (reopenScreen != NULL)
        *reopenScreen = screen;

    if (screen)
        return TRUE;

    /* Windows-only restart: palette (LoadRGB4 in OpenAppWindow), XEM library
     * (OpenDisplay windows section), packet/toolbar/jump-scroll windows. */
    if (memcmp(a->color, b->color, sizeof(a->color)) != 0 ||
        strcmp(a->displaydriver, b->displaydriver) != 0 ||
        (changedFlags & SITE_PREFS_RESTART_FLAGS) != 0)
        return TRUE;

    return FALSE;
}
