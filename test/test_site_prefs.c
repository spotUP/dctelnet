/* test/test_site_prefs.c -- per-entry settings: apply/restore/diff logic. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "site_prefs.h"

static void make_global(struct PrefsStruct *p) {
    memset(p, 0, sizeof(*p));
    p->DisplayID = 0x1234;
    p->DisplayWidth = 640;
    p->DisplayHeight = 256;
    p->DisplayDepth = 4;
    p->fontsize = 8;
    strcpy(p->fontname, "topaz.font");
    p->flags = FLAG_TOOL_BAR;
    p->win_left = 10; p->win_top = 11; p->win_width = 640; p->win_height = 200;
    p->sb_left = 1; p->sb_top = 12; p->sb_width = 640; p->sb_height = 124;
    p->toolBarWin_left = 5; p->toolBarWin_top = 6;
    p->sb_lines = 300;
    strcpy(p->displayidstr, "VT102");
    strcpy(p->uploadpath, "RAM:");
}

static void make_entry(struct PrefsStruct *p) {
    memset(p, 0, sizeof(*p));
    p->DisplayID = 0x5678;
    p->DisplayWidth = 800;
    p->DisplayHeight = 600;
    p->DisplayDepth = 2;
    p->fontsize = 16;
    strcpy(p->fontname, "Petscii.font");
    p->flags = FLAG_TOOL_BAR | FLAG_PETSCII_MODE | FLAG_LOCAL_ECHO;
    p->win_left = 99; p->win_top = 99; p->win_width = 99; p->win_height = 99;
    p->sb_left = 99; p->sb_top = 99; p->sb_width = 99; p->sb_height = 99;
    p->toolBarWin_left = 99; p->toolBarWin_top = 99;
    p->sb_lines = 1000;
    strcpy(p->displayidstr, "PETSCII");
    strcpy(p->uploadpath, "DH0:");
}

/* ApplyEntry takes every field from the entry except window geometry (D5). */
static void test_apply_keeps_global_geometry(void) {
    struct PrefsStruct global, entry, eff;

    make_global(&global);
    make_entry(&entry);
    SitePrefs_ApplyEntry(&eff, &global, &entry);

    assert(eff.win_left == 10 && eff.win_top == 11);
    assert(eff.win_width == 640 && eff.win_height == 200);
    assert(eff.sb_left == 1 && eff.sb_top == 12);
    assert(eff.sb_width == 640 && eff.sb_height == 124);
    assert(eff.toolBarWin_left == 5 && eff.toolBarWin_top == 6);

    assert(eff.DisplayID == 0x5678);
    assert(eff.fontsize == 16);
    assert(strcmp(eff.fontname, "Petscii.font") == 0);
    assert(eff.flags == (FLAG_TOOL_BAR | FLAG_PETSCII_MODE | FLAG_LOCAL_ECHO));
    assert(eff.sb_lines == 1000);
    assert(strcmp(eff.displayidstr, "PETSCII") == 0);
    assert(strcmp(eff.uploadpath, "DH0:") == 0);

    /* The sources are untouched. */
    assert(global.win_left == 10 && entry.win_left == 99);
}

/* Disconnect restores the globals byte for byte. */
static void test_restore_global(void) {
    struct PrefsStruct global, eff;

    make_global(&global);
    memset(&eff, 0xAA, sizeof(eff));
    SitePrefs_RestoreGlobal(&eff, &global);
    assert(memcmp(&eff, &global, sizeof(eff)) == 0);
}

static void test_identical_needs_no_restart(void) {
    struct PrefsStruct a, b;
    BOOL reopen = TRUE;

    make_global(&a);
    make_global(&b);
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));
    assert(reopen == FALSE);
}

/* Screen fields (mode, font, PETSCII/HIDE_TITLEBAR/WORKBENCH) reopen it. */
static void test_screen_fields_reopen_screen(void) {
    struct PrefsStruct a, b;
    BOOL reopen = FALSE;

    make_global(&a);

    b = a; b.DisplayDepth = 2;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);

    b = a; b.DisplayID = 0x9999;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);

    b = a; strcpy(b.fontname, "Petscii.font");
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);

    b = a; b.fontsize = 16;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);

    b = a; b.flags |= FLAG_PETSCII_MODE;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);

    b = a; b.flags |= FLAG_HIDE_TITLEBAR;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == TRUE);
}

/* Windows-only settings restart without reopening the screen. */
static void test_window_flags_restart_without_screen(void) {
    struct PrefsStruct a, b;
    BOOL reopen = TRUE;

    make_global(&a);

    b = a; b.flags |= FLAG_PACKET_WINDOW;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; b.flags |= FLAG_USE_XEM_LIBRARY;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; b.flags &= ~FLAG_TOOL_BAR;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; b.flags |= FLAG_JUMP_SCROLL;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; b.color[0] = 0x0FFF;  /* legacy shadow still restarts */
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; b.ansi32[0] = 0xFFFFFFFF;
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);

    b = a; strcpy(b.displaydriver, "foo.xem");
    assert(SitePrefs_DisplayDiffers(&a, &b, &reopen) && reopen == FALSE);
}

/* Live and dead settings never restart: echo, raw, keys, scrollback,
 * transfer paths, TTYPE string, geometry, dead persisted bits. */
static void test_live_settings_need_no_restart(void) {
    struct PrefsStruct a, b;
    BOOL reopen = TRUE;

    make_global(&a);

    b = a; b.flags |= FLAG_LOCAL_ECHO;
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.flags |= FLAG_RAW_CONNECTION;
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.flags |= FLAG_BS_DEL_SWAP;
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.flags |= FLAG_CRLF_CORRECTION; /* dead bit: no reader */
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.flags |= FLAG_STRIP_COLOUR; /* dead bit: no reader */
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.sb_lines = 9999;
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; strcpy(b.uploadpath, "DH1:");
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; strcpy(b.displayidstr, "PETSCII");
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));

    b = a; b.win_width = 100;
    assert(!SitePrefs_DisplayDiffers(&a, &b, &reopen));
}

static void test_null_reopen_pointer_is_safe(void) {
    struct PrefsStruct a, b;

    make_global(&a);
    b = a; b.flags |= FLAG_PETSCII_MODE;
    assert(SitePrefs_DisplayDiffers(&a, &b, NULL));

    b = a;
    assert(!SitePrefs_DisplayDiffers(&a, &b, NULL));
}

/* Encode -> decode round-trips the whole struct. (On the host the struct
 * layout differs from the 68k one; this proves the framing, not the bytes.) */
static void test_sidecar_round_trip(void) {
    struct PrefsStruct entry, back;
    UBYTE buf[4 + sizeof(struct PrefsStruct) + 16];
    size_t n;

    make_entry(&entry);
    assert(SitePrefs_EncodedSize() == 4 + sizeof(struct PrefsStruct));
    n = SitePrefs_Encode(&entry, buf, sizeof(buf));
    assert(n == SitePrefs_EncodedSize());
    assert(buf[0] == 'D' && buf[1] == 'C' && buf[2] == 'S' && buf[3] == '2');

    memset(&back, 0xAA, sizeof(back));
    assert(SitePrefs_Decode(buf, n, &back));
    assert(memcmp(&back, &entry, sizeof(back)) == 0);
}

/* Bad magic, short reads and NULLs decode as "no settings". */
static void test_sidecar_rejects_bad_input(void) {
    struct PrefsStruct entry, back;
    UBYTE buf[4 + sizeof(struct PrefsStruct) + 16];
    size_t n;
    UBYTE canary[sizeof(struct PrefsStruct)];

    make_entry(&entry);
    n = SitePrefs_Encode(&entry, buf, sizeof(buf));

    memset(canary, 0xAA, sizeof(canary));

    buf[0] = 'X';
    memcpy(&back, canary, sizeof(back));
    assert(!SitePrefs_Decode(buf, n, &back));
    assert(memcmp(&back, canary, sizeof(back)) == 0);
    buf[0] = 'D';

    buf[3] = '9';  /* unknown version */
    memcpy(&back, canary, sizeof(back));
    assert(!SitePrefs_Decode(buf, n, &back));
    assert(memcmp(&back, canary, sizeof(back)) == 0);
    buf[3] = '2';

    memcpy(&back, canary, sizeof(back));
    assert(!SitePrefs_Decode(buf, n - 1, &back));  /* truncated */
    assert(!SitePrefs_Decode(buf, 3, &back));      /* shorter than magic */
    assert(!SitePrefs_Decode(buf, 0, &back));
    assert(memcmp(&back, canary, sizeof(back)) == 0);

    assert(!SitePrefs_Decode(NULL, n, &back));
    assert(!SitePrefs_Decode(buf, n, NULL));

    assert(SitePrefs_Encode(&entry, buf, n - 1) == 0);  /* too small */
    assert(SitePrefs_Encode(NULL, buf, sizeof(buf)) == 0);
    assert(SitePrefs_Encode(&entry, NULL, sizeof(buf)) == 0);
}

/* Longer files (newer fields) are accepted; the known prefix still matches. */
static void test_sidecar_ignores_trailing_bytes(void) {
    struct PrefsStruct entry, back;
    UBYTE buf[4 + sizeof(struct PrefsStruct) + 16];
    size_t n, i;

    make_entry(&entry);
    n = SitePrefs_Encode(&entry, buf, sizeof(buf));
    for (i = n; i < sizeof(buf); i++)
        buf[i] = (UBYTE)i;

    assert(SitePrefs_Decode(buf, sizeof(buf), &back));
    assert(memcmp(&back, &entry, sizeof(back)) == 0);
}

static void test_rgb4to32(void) {
    assert(SitePrefs_RGB4to32(0x000) == 0x00000000UL);
    assert(SitePrefs_RGB4to32(0xFFF) == 0xFFFFFF00UL);
    assert(SitePrefs_RGB4to32(0xD00) == 0xDD000000UL);
    assert(SitePrefs_RGB4to32(0x0D0) == 0x00DD0000UL);
    assert(SitePrefs_RGB4to32(0x00D) == 0x0000DD00UL);
    assert(SitePrefs_RGB4to32(0x123) == 0x11223300UL);
}

static void test_derive_ansi32(void) {
    struct PrefsStruct p;
    int i;

    memset(&p, 0, sizeof(p));
    for (i = 0; i < 16; i++)
        p.color[i] = (UWORD)(i * 0x111);
    SitePrefs_DeriveAnsi32(&p);
    for (i = 0; i < 16; i++)
        assert(p.ansi32[i] == SitePrefs_RGB4to32((UWORD)(i * 0x111)));

    SitePrefs_DeriveAnsi32(NULL);  /* must not crash */
}

/* A 1.9.1 (DCS1) sidecar decodes: legacy prefix copied, ansi32[] derived
 * from the embedded color[] shadow. */
static void test_triplet_and_back(void) {
    /* GetRGB32 triplets are full 32-bit fractions (byte replicated). */
    assert(SitePrefs_TripletToXRGB(0xDDDDDDDDUL, 0xDDDDDDDDUL, 0xDDDDDDDDUL)
           == 0xDDDDDD00UL);
    assert(SitePrefs_TripletToXRGB(0xFF000000UL, 0x00FF0000UL, 0x0000FF00UL)
           == 0xFF000000UL);  /* top bytes only */
    assert(SitePrefs_TripletToXRGB(0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL)
           == 0xFFFFFF00UL);
    assert(SitePrefs_TripletToXRGB(0, 0, 0) == 0);

    assert(SitePrefs_XRGBtoRGB4(0x11223300UL) == 0x123);
    assert(SitePrefs_XRGBtoRGB4(0xFFFFFF00UL) == 0xFFF);
    assert(SitePrefs_XRGBtoRGB4(0x00000000UL) == 0x000);
    assert(SitePrefs_XRGBtoRGB4(0xDD000000UL) == 0xD00);
}

static void test_rgb32_table(void) {
    ULONG ansi[16], ui[16], table[SITE_PREFS_RGB32_TABLE];
    ULONG short_table[SITE_PREFS_RGB32_TABLE - 1];
    size_t n;
    int i;

    for (i = 0; i < 16; i++) {
        ansi[i] = 0x11223300UL;
        ui[i] = 0x0055AA00UL;
    }

    n = SitePrefs_BuildRGB32Table(ansi, ui, table, SITE_PREFS_RGB32_TABLE);
    assert(n == SITE_PREFS_RGB32_TABLE);

    /* ANSI record: count 16 from 0, then R/G/B triplets. */
    assert(table[0] == (((ULONG)16 << 16) | 0));
    assert(table[1] == 0x11111111UL);
    assert(table[2] == 0x22222222UL);
    assert(table[3] == 0x33333333UL);

    /* UI record starts at 1 + 16*3 = 49: count 16 from 16. */
    assert(table[49] == (((ULONG)16 << 16) | 16));
    assert(table[50] == 0x00000000UL);
    assert(table[51] == 0x55555555UL);
    assert(table[52] == 0xAAAAAAAAUL);

    /* Zero terminator. */
    assert(table[98] == 0);

    assert(SitePrefs_BuildRGB32Table(ansi, ui, short_table,
                                     SITE_PREFS_RGB32_TABLE - 1) == 0);
    assert(SitePrefs_BuildRGB32Table(NULL, ui, table,
                                     SITE_PREFS_RGB32_TABLE) == 0);
    assert(SitePrefs_BuildRGB32Table(ansi, ui, NULL,
                                     SITE_PREFS_RGB32_TABLE) == 0);
}

static ULONG t_lum(ULONG x) {
    return (((x >> 24) & 0xFF) * 30 + (((x >> 16) & 0xFF) * 59) +
            (((x >> 8) & 0xFF) * 11)) / 100;
}

static int t_clash(ULONG a, ULONG b) {
    ULONG la = t_lum(a), lb = t_lum(b);
    return (la > lb ? la - lb : lb - la) < 48;
}

/* Flat gray theme (the reported failure): everything readable after. */
static void test_contrast_flat_theme(void) {
    ULONG ui[16];
    ULONG snapshot[16];
    int i;

    for (i = 0; i < 16; i++)
        ui[i] = 0x99999900UL;
    ui[7] = 0x99999900UL;  /* gray base */

    SitePrefs_FixUiContrast(ui);

    assert(!t_clash(ui[2], ui[7]));   /* text vs base */
    assert(!t_clash(ui[6], ui[5]));   /* button text vs face */
    assert(!t_clash(ui[3], ui[7]));   /* shine vs base */
    assert(!t_clash(ui[4], ui[7]));   /* shadow vs base */
    assert(!t_clash(ui[3], ui[4]));   /* bevel has two sides */
    /* face may equal base (authentic WB): readability comes from bevel+text */

    /* Idempotent: a second pass changes nothing. */
    memcpy(snapshot, ui, sizeof(ui));
    SitePrefs_FixUiContrast(ui);
    assert(memcmp(snapshot, ui, sizeof(ui)) == 0);
}

/* Healthy classic theme passes through untouched. */
static void test_contrast_healthy_untouched(void) {
    ULONG ui[16] = {
        0x0055AA00UL, 0xFFFFFF00UL, 0x00000000UL, 0xFFFFFF00UL,
        0x00000000UL, 0x99999900UL, 0x00000000UL, 0x99999900UL,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    ULONG snapshot[16];

    memcpy(snapshot, ui, sizeof(ui));
    SitePrefs_FixUiContrast(ui);
    assert(memcmp(snapshot, ui, sizeof(ui)) == 0);

    SitePrefs_FixUiContrast(NULL);  /* must not crash */
}

/* Black base (dark theme): light text chosen. */
static void test_contrast_dark_base(void) {
    ULONG ui[16];
    int i;

    for (i = 0; i < 16; i++)
        ui[i] = 0x00000000UL;

    SitePrefs_FixUiContrast(ui);

    assert(!t_clash(ui[2], ui[7]));
    assert(t_lum(ui[2]) > 200);  /* text went light */
}

static void test_sidecar_v1_legacy(void) {    struct PrefsStruct entry, back;
    UBYTE blob[4 + SITE_PREFS_V1_PREFIX + 8];
    size_t colorOff;
    UWORD legacyColor[16];
    int i;

    make_entry(&entry);
    for (i = 0; i < 16; i++)
        legacyColor[i] = (UWORD)(0x100 + i);

    colorOff = (size_t)((char *)entry.color - (char *)&entry);
    assert(colorOff + sizeof(legacyColor) <= SITE_PREFS_V1_PREFIX);

    blob[0] = 'D'; blob[1] = 'C'; blob[2] = 'S'; blob[3] = '1';
    memcpy(blob + 4, &entry, SITE_PREFS_V1_PREFIX);
    memcpy(blob + 4 + colorOff, legacyColor, sizeof(legacyColor));
    for (i = 0; i < 8; i++)
        blob[4 + SITE_PREFS_V1_PREFIX + i] = (UBYTE)i;

    memset(&back, 0xAA, sizeof(back));
    assert(SitePrefs_Decode(blob, sizeof(blob), &back));
    assert(memcmp(&back, blob + 4, SITE_PREFS_V1_PREFIX) == 0);
    for (i = 0; i < 16; i++)
        assert(back.ansi32[i] == SitePrefs_RGB4to32(legacyColor[i]));

    /* Short legacy blob: no settings. */
    assert(!SitePrefs_Decode(blob, 4 + SITE_PREFS_V1_PREFIX - 1, &back));
}

static void test_sidecar_file_name(void) {    char path[SITE_PREFS_PATH_LEN];
    char tiny[8];

    assert(SitePrefs_FileName(1, path, sizeof(path)) == path);
    assert(strcmp(path, "PROGDIR:Sites/1.prefs") == 0);

    assert(SitePrefs_FileName(12345, path, sizeof(path)) == path);
    assert(strcmp(path, "PROGDIR:Sites/12345.prefs") == 0);

    assert(SitePrefs_FileName(0, path, sizeof(path)) == path);
    assert(strcmp(path, "PROGDIR:Sites/0.prefs") == 0);

    assert(SitePrefs_FileName(1, tiny, sizeof(tiny)) == NULL);
    assert(SitePrefs_FileName(1, NULL, sizeof(path)) == NULL);
    assert(SitePrefs_FileName(1, path, 0) == NULL);
}

int main(void) {
    test_apply_keeps_global_geometry();
    test_restore_global();
    test_identical_needs_no_restart();
    test_screen_fields_reopen_screen();
    test_window_flags_restart_without_screen();
    test_live_settings_need_no_restart();
    test_null_reopen_pointer_is_safe();
    test_sidecar_round_trip();
    test_sidecar_rejects_bad_input();
    test_sidecar_ignores_trailing_bytes();
    test_sidecar_file_name();
    test_rgb4to32();
    test_derive_ansi32();
    test_sidecar_v1_legacy();
    test_triplet_and_back();
    test_rgb32_table();
    test_contrast_flat_theme();
    test_contrast_healthy_untouched();
    test_contrast_dark_base();
    printf("site_prefs: all assertions passed\n");
    return 0;
}
