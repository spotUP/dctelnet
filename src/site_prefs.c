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

    /* Windows-only restart: palette (LoadRGB32 in OpenAppWindow), XEM library
     * (OpenDisplay windows section), packet/toolbar/jump-scroll windows.
     * Both palette generations are compared: the editor writes color[]
     * until phase 3 keeps ansi32[] in sync with it. */
    if (memcmp(a->ansi32, b->ansi32, sizeof(a->ansi32)) != 0 ||
        memcmp(a->color, b->color, sizeof(a->color)) != 0 ||
        strcmp(a->displaydriver, b->displaydriver) != 0 ||
        (changedFlags & SITE_PREFS_RESTART_FLAGS) != 0)
        return TRUE;

    return FALSE;
}

size_t SitePrefs_EncodedSize(void)
{
    return 4 + sizeof(struct PrefsStruct);
}

ULONG SitePrefs_RGB4to32(UWORD rgb4)
{
    ULONG r = (ULONG)((rgb4 >> 8) & 0xF) * 17;
    ULONG g = (ULONG)((rgb4 >> 4) & 0xF) * 17;
    ULONG b = (ULONG)(rgb4 & 0xF) * 17;

    return (r << 24) | (g << 16) | (b << 8);
}

void SitePrefs_DeriveAnsi32(struct PrefsStruct *entry)
{
    int i;

    if (entry == NULL)
        return;
    for (i = 0; i < 16; i++)
        entry->ansi32[i] = SitePrefs_RGB4to32(entry->color[i]);
}

ULONG SitePrefs_TripletToXRGB(ULONG r, ULONG g, ULONG b)
{
    return ((r >> 24) << 24) | ((g >> 24) << 16) | ((b >> 24) << 8);
}

UWORD SitePrefs_XRGBtoRGB4(ULONG xrgb)
{
    return (UWORD)(((xrgb >> 28) << 8) | (((xrgb >> 20) & 0xF) << 4) |
                   ((xrgb >> 12) & 0xF));
}

size_t SitePrefs_BuildRGB32Table(const ULONG ansi32[16], const ULONG ui32[16],
                                 ULONG *table, size_t tableLen)
{
    size_t pos = 0;
    size_t i;
    ULONG v;

    if (ansi32 == NULL || ui32 == NULL || table == NULL ||
        tableLen < SITE_PREFS_RGB32_TABLE)
        return 0;

    table[pos++] = ((ULONG)32 << 16) | 0;
    for (i = 0; i < 16; i++)
    {
        v = ansi32[i];
        table[pos++] = ((v >> 24) & 0xFF) * 0x01010101UL;
        table[pos++] = ((v >> 16) & 0xFF) * 0x01010101UL;
        table[pos++] = ((v >> 8) & 0xFF) * 0x01010101UL;
    }

    for (i = 0; i < 16; i++)
    {
        v = ui32[i];
        table[pos++] = ((v >> 24) & 0xFF) * 0x01010101UL;
        table[pos++] = ((v >> 16) & 0xFF) * 0x01010101UL;
        table[pos++] = ((v >> 8) & 0xFF) * 0x01010101UL;
    }

    table[pos++] = 0;
    return pos;
}

static void rgb32_triplet(ULONG xrgb, ULONG *table, size_t *pos)
{
    table[(*pos)++] = ((xrgb >> 24) & 0xFF) * 0x01010101UL;
    table[(*pos)++] = ((xrgb >> 16) & 0xFF) * 0x01010101UL;
    table[(*pos)++] = ((xrgb >> 8) & 0xFF) * 0x01010101UL;
}

size_t SitePrefs_BuildRGB32Full(const ULONG ansi32[16], const ULONG ui32[16],
                                const ULONG rest[224], ULONG *table,
                                size_t tableLen)
{
    size_t pos = 0;
    size_t i;

    if (ansi32 == NULL || ui32 == NULL || rest == NULL || table == NULL ||
        tableLen < SITE_PREFS_RGB32_FULL)
        return 0;

    table[pos++] = ((ULONG)256 << 16) | 0;
    for (i = 0; i < 16; i++)
        rgb32_triplet(ansi32[i], table, &pos);
    for (i = 0; i < 16; i++)
        rgb32_triplet(ui32[i], table, &pos);
    for (i = 0; i < 224; i++)
        rgb32_triplet(rest[i], table, &pos);
    table[pos++] = 0;
    return pos;
}

/* dri_Pens slot order (intuition/screens.h) for the UI block. */
#define UIP_TEXT 2
#define UIP_SHINE 3
#define UIP_SHADOW 4
#define UIP_FILL 5
#define UIP_FILLTEXT 6
#define UIP_BACKGROUND 7

#define UIP_CLASH_DELTA 250

static ULONG ui_luminance(ULONG xrgb)
{
    ULONG r = (xrgb >> 24) & 0xFF;
    ULONG g = (xrgb >> 16) & 0xFF;
    ULONG b = (xrgb >> 8) & 0xFF;

    return (r * 30 + g * 59 + b * 11) / 100;
}

static int ui_clash(ULONG a, ULONG b)
{
    ULONG la = ui_luminance(a), lb = ui_luminance(b);

    return (la > lb ? la - lb : lb - la) < UIP_CLASH_DELTA;
}

/* Black or white, whichever contrasts base most. */
static ULONG ui_pick(ULONG base)
{
    ULONG l = ui_luminance(base);

    return (255 - l) >= l ? 0xFFFFFF00UL : 0x00000000UL;
}

void SitePrefs_FixUiContrast(ULONG ui32[16])
{
    ULONG base;

    if (ui32 == NULL)
        return;

    base = ui32[UIP_BACKGROUND];

    /* Text readable on the base. */
    if (ui_clash(ui32[UIP_TEXT], base))
        ui32[UIP_TEXT] = ui_pick(base);

    /* Bevels distinct from the base... */
    if (ui_clash(ui32[UIP_SHINE], base))
        ui32[UIP_SHINE] = ui_pick(base);
    if (ui_clash(ui32[UIP_SHADOW], base))
        ui32[UIP_SHADOW] = ui_pick(base);

    /* ...and from each other (a flat bevel reads as no bevel). */
    if (ui_clash(ui32[UIP_SHINE], ui32[UIP_SHADOW]))
    {
        if (ui_luminance(ui32[UIP_SHINE]) >= 128)
            ui32[UIP_SHADOW] = 0x00000000UL;
        else
            ui32[UIP_SHINE] = 0xFFFFFF00UL;
    }

    /* Button faces off the base, text off the face. */
    /* Button faces may equal the base (authentic WB look -- faces read via
     * bevel + text), but face text must contrast the face. */
    if (ui_clash(ui32[UIP_FILLTEXT], ui32[UIP_FILL]))
        ui32[UIP_FILLTEXT] = ui_pick(ui32[UIP_FILL]);
}

size_t SitePrefs_Encode(const struct PrefsStruct *entry, UBYTE *out, size_t outLen)
{
    size_t need = SitePrefs_EncodedSize();

    if (entry == NULL || out == NULL || outLen < need)
        return 0;

    /* Explicit bytes, not a ULONG store: identical on 68k and host. */
    out[0] = 'D';
    out[1] = 'C';
    out[2] = 'S';
    out[3] = '2';
    memcpy(out + 4, entry, sizeof(*entry));
    return need;
}

BOOL SitePrefs_Decode(const UBYTE *in, size_t inLen, struct PrefsStruct *entry)
{
    if (in == NULL || entry == NULL)
        return FALSE;
    if (inLen < 4)
        return FALSE;
    if (in[0] != 'D' || in[1] != 'C' || in[2] != 'S')
        return FALSE;

    if (in[3] == '2')
    {
        if (inLen < SitePrefs_EncodedSize())
            return FALSE;
        /* Trailing bytes (newer fields) are ignored. */
        memcpy(entry, in + 4, sizeof(*entry));
        return TRUE;
    }

    if (in[3] == '1')
    {
        /* 1.9.1 sidecar: legacy prefix, no ansi32[] -- derive it. */
        if (inLen < 4 + SITE_PREFS_V1_PREFIX)
            return FALSE;
        memcpy(entry, in + 4, SITE_PREFS_V1_PREFIX);
        SitePrefs_DeriveAnsi32(entry);
        return TRUE;
    }

    return FALSE;
}

char *SitePrefs_FileName(ULONG id, char *out, size_t outLen)
{
    static const char prefix[] = "PROGDIR:Sites/";
    static const char suffix[] = ".prefs";
    /* 20 digits cover a 64-bit ULONG on the host; 10 suffice on the Amiga. */
    char digits[20];
    int n = 0;
    ULONG v = id;
    size_t need, pos, i;

    if (out == NULL || outLen == 0)
        return NULL;

    do {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v > 0);

    need = (sizeof(prefix) - 1) + (size_t)n + (sizeof(suffix) - 1) + 1;
    if (need > outLen)
        return NULL;

    memcpy(out, prefix, sizeof(prefix) - 1);
    pos = sizeof(prefix) - 1;
    for (i = 0; i < (size_t)n; i++)
        out[pos + i] = digits[n - 1 - (int)i];
    pos += (size_t)n;
    memcpy(out + pos, suffix, sizeof(suffix));  /* copies the NUL too */
    return out;
}
