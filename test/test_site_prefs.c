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

    b = a; b.color[0] = 0x0FFF;
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

int main(void) {
    test_apply_keeps_global_geometry();
    test_restore_global();
    test_identical_needs_no_restart();
    test_screen_fields_reopen_screen();
    test_window_flags_restart_without_screen();
    test_live_settings_need_no_restart();
    test_null_reopen_pointer_is_safe();
    printf("site_prefs: all assertions passed\n");
    return 0;
}
