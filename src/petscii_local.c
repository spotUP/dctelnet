/* src/petscii_local.c -- readable local texts under the PETSCII font. */
#include "petscii_local.h"

/* Byte classes of the ECMA-48 sequences DCTelnet's own strings use. */
#define ESC_BYTE 0x1B
#define CSI_BYTE 0x9B

void Petscii_MapLocalText(char *s)
{
    unsigned char c;

    if (s == NULL)
        return;

    while (*s)
    {
        c = (unsigned char)*s;

        /* Skip escape sequences untouched: folding an SGR final 'm' into
         * 'M' (delete line) or '?7l' into '?7L' would corrupt them. */
        if (c == ESC_BYTE)
        {
            s++;                        /* the ESC itself */
            if (*s == '[')
            {
                s++;
                /* parameters (0x30-0x3F) and intermediates (0x20-0x2F) */
                while (*s && (unsigned char)*s < 0x40)
                    s++;
            }
            if (*s)
                s++;                    /* final byte (or lone ESC-letter) */
        }
        else if (c == CSI_BYTE)
        {
            s++;                        /* the CSI itself */
            while (*s && (unsigned char)*s < 0x40)
                s++;
            if (*s)
                s++;                    /* final byte */
        }
        else
        {
            if (c >= 'a' && c <= 'z')
                *s = (char)(c - ('a' - 'A'));
            s++;
        }
    }
}
