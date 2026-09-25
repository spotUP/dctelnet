/* test/test_petscii_local.c -- local-text case folding for the PETSCII font. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "petscii_local.h"

/* a-z folds, everything else (all 256 byte values) is untouched. */
static void test_full_byte_sweep(void) {
    char buf[257];
    int i;

    for (i = 0; i < 256; i++)
        buf[i] = (char)i;
    buf[256] = '\0';

    /* NUL at 0 ends the string: check the two halves separately. */
    Petscii_MapLocalText(buf + 1);
    for (i = 1; i < 256; i++) {
        if (i >= 'a' && i <= 'z')
            assert(buf[i] == (char)(i - ('a' - 'A')));
        else
            assert(buf[i] == (char)i);
    }

    buf[0] = 'x';
    Petscii_MapLocalText(buf);
    assert(buf[0] == 'X');
}

static void test_banner_line(void) {
    char line[] = "Processor: 68020, Kickstart 3.1.";

    Petscii_MapLocalText(line);
    assert(strcmp(line, "PROCESSOR: 68020, KICKSTART 3.1.") == 0);
}

static void test_sgr_sequences_preserved(void) {
    /* ESC [ 0 ; 1 ; 3 6 m must survive byte-identically (colours): the
     * final 'm' is NOT folded (would become delete-line 'M'). */
    char sgr[] = { 27, '[', '0', ';', '1', ';', '3', '6', 'm', 0 };
    char csi[] = { (char)0x9B, '0', ';', '3', '6', 'm', 0 };
    char wrap[] = { 27, '[', '?', '7', 'l', 0 };
    char mixed[] = { 27, '[', '0', ';', '3', '6', 'm', 'a', 'b', 'c', 0 };

    Petscii_MapLocalText(sgr);
    assert(sgr[0] == 27 && strcmp(sgr + 1, "[0;1;36m") == 0);

    /* 8-bit CSI (used in live DCTelnet.c strings) likewise. */
    Petscii_MapLocalText(csi);
    assert(csi[0] == (char)0x9B && strcmp(csi + 1, "0;36m") == 0);

    /* The console wrap-disable sequence. */
    Petscii_MapLocalText(wrap);
    assert(strcmp(wrap + 1, "[?7l") == 0);

    /* Text after a sequence still folds. */
    Petscii_MapLocalText(mixed);
    assert(strcmp(mixed, "\x1B[0;36mABC") == 0);

    /* Unterminated sequence: text before still folds, tail untouched. */
    {
        char cut[] = { 'x', 27, '[', '0', 0 };
        Petscii_MapLocalText(cut);
        assert(cut[0] == 'X' && cut[1] == 27 && cut[2] == '[' && cut[3] == '0');
    }
}

static void test_idempotent_and_safe(void) {
    char line[] = "ALREADY UPPER 123!";

    Petscii_MapLocalText(line);
    assert(strcmp(line, "ALREADY UPPER 123!") == 0);
    Petscii_MapLocalText(line);
    assert(strcmp(line, "ALREADY UPPER 123!") == 0);

    Petscii_MapLocalText("");
    Petscii_MapLocalText(NULL);  /* must not crash */
}

int main(void) {
    test_full_byte_sweep();
    test_banner_line();
    test_sgr_sequences_preserved();
    test_idempotent_and_safe();
    printf("petscii_local: all assertions passed\n");
    return 0;
}
