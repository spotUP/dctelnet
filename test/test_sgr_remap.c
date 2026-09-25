/* test/test_sgr_remap.c -- WB SGR pen remap (pure logic). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sgr_remap.h"

/* Stock WB palette head (XRGB): blue, white, black, gray, rest black. */
static void wb_palette(ULONG pal[8]) {
    pal[0] = 0x0055AA00UL;
    pal[1] = 0xFFFFFF00UL;
    pal[2] = 0x00000000UL;
    pal[3] = 0x99999900UL;
    pal[4] = pal[5] = pal[6] = pal[7] = 0x00000000UL;
}

static void test_nearest(void) {
    ULONG pal[8];
    int i;

    wb_palette(pal);
    assert(SgrNearestPen(0xFFFFFF00UL, pal) == 1);
    assert(SgrNearestPen(0x00000000UL, pal) == 2);
    assert(SgrNearestPen(0x0055AA00UL, pal) == 0);
    assert(SgrNearestPen(0x99999900UL, pal) == 3);
    assert(SgrNearestPen(0, NULL) == 0);

    /* Map stays in device range 0-7 for every ANSI slot. */
    {
        UBYTE map[16];
        ULONG ansi[16];
        for (i = 0; i < 16; i++)
            ansi[i] = (ULONG)(i * 0x11111100UL);
        ansi[1] = 0xFFFFFF00UL;  /* white must stay white */
        ansi[0] = 0x00000000UL;  /* black stays black */
        SgrBuildMap(ansi, pal, map);
        for (i = 0; i < 16; i++)
            assert(map[i] <= 7);
        assert(map[1] == 1);
        assert(map[0] == 2);
    }

    SgrBuildMap(NULL, pal, NULL);  /* must not crash */
}

static void remap_str(const char *in, const UBYTE map[16], char *out, size_t outlen,
                      size_t *n) {
    *n = SgrRemap((const UBYTE *)in, strlen(in), map, (UBYTE *)out, outlen);
    if (*n)
        out[*n] = '\0';
}

static void identity_map(UBYTE map[16]) {
    int i;
    for (i = 0; i < 16; i++)
        map[i] = (UBYTE)i;
}

static void test_passthrough(void) {
    UBYTE map[16];
    char out[256];
    size_t n;

    identity_map(map);

    /* Non-SGR and text pass through byte-identically. */
    remap_str("plain text 123!", map, out, sizeof(out), &n);
    assert(n == 15 && strcmp(out, "plain text 123!") == 0);

    remap_str("\x1B[2K\x1B[?7l\x1B[1;20H", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[2K\x1B[?7l\x1B[1;20H") == 0);

    /* Unterminated sequence: verbatim. */
    remap_str("ab\x1B[3", map, out, sizeof(out), &n);
    assert(strcmp(out, "ab\x1B[3") == 0);

    /* 8-bit CSI non-SGR: verbatim. */
    remap_str("\x9B" "2K", map, out, sizeof(out), &n);
    assert(n == 3 && memcmp(out, "\x9B" "2K", 3) == 0);

    assert(SgrRemap(NULL, 3, map, (UBYTE *)out, sizeof(out)) == 0);
    assert(SgrRemap((const UBYTE *)"x", 1, NULL, (UBYTE *)out, sizeof(out)) == 0);
}

static void test_rewrite_identity(void) {
    UBYTE map[16];
    char out[256];
    size_t n;

    identity_map(map);

    /* 3-digit params: the device parses the full 0-49 range. */
    remap_str("\x1B[0m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[037;040m") == 0);

    remap_str("\x1B[31m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[031m") == 0);

    remap_str("\x1B[1;36m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[1;036m") == 0);

    remap_str("\x1B[m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[037;040m") == 0);

    remap_str("\x1B[39;49m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[037;040m") == 0);

    /* 8-bit introducer preserved. */
    remap_str("\x9B" "0m", map, out, sizeof(out), &n);
    assert(n == 9 && memcmp(out, "\x9B" "037;040m", 9) == 0);

    /* Too small: 0 (caller writes input verbatim). */
    assert(SgrRemap((const UBYTE *)"\x1B[0m", 4, map, (UBYTE *)out, 5) == 0);
}

static void test_rewrite_mapped(void) {
    UBYTE map[16];
    char out[256];
    size_t n;
    int i;

    for (i = 0; i < 16; i++)
        map[i] = 7;  /* everything -> pen 7 */
    map[0] = 2;
    map[7] = 1;

    remap_str("\x1B[0m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[031;042m") == 0);

    remap_str("\x1B[42m", map, out, sizeof(out), &n);
    assert(strcmp(out, "\x1B[047m") == 0);

    remap_str("A\x1B[33mB\x1B[0mC", map, out, sizeof(out), &n);
    assert(strcmp(out, "A\x1B[037mB\x1B[031;042mC") == 0);
}

int main(void) {
    test_nearest();
    test_passthrough();
    test_rewrite_identity();
    test_rewrite_mapped();
    printf("sgr_remap: all assertions passed\n");
    return 0;
}
