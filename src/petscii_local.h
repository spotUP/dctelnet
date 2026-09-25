/* src/petscii_local.h -- readable local texts under the PETSCII font. */
#ifndef PETSCII_LOCAL_H
#define PETSCII_LOCAL_H

#include <stddef.h>

/* In-place fold of ASCII lowercase to uppercase. A C64 charset shows the
 * uppercase bytes ($41-5A) readably in BOTH halves, while ASCII lowercase
 * ($61-7A) lands on graphics glyphs. ECMA-48 escape sequences (ESC...final,
 * 8-bit CSI...final) pass through byte-identically -- folding an SGR final
 * 'm' or '?7l' would corrupt them. Everything else (digits, punctuation,
 * CR/LF) is untouched. Idempotent. */
void Petscii_MapLocalText(char *s);

#endif /* PETSCII_LOCAL_H */
