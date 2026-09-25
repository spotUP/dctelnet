#ifndef UTILS_H
#define UTILS_H

/**
 * @file utils.h
 * @brief General-purpose utility functions for AmigaOS 2.0+.
 *
 * @author Bruno FREDERIC
 * @date 2026
 */

#include <exec/types.h>
#include <string.h>
#include <utility/hooks.h>   /* struct Hook (selectAllHook below) */

// Types


// Global variables exported


// Functions exported
ULONG mytime(void);
void myctime(ULONG secs, char *outbuf, size_t maxLen);
size_t strlcpy(char *dst, const char *src, size_t dstSize);
size_t strlcat(char *dst, const char *src, size_t dstSize);
void mysprintf(char *Buffer, char *ctl, ...);

/* 68k OS-callback register convention (a0=hook, a2=object, a1=message).
 * vbcc __reg() parameters; plain prototype elsewhere. */
#ifdef __VBCC__
#define HOOK_A0 __reg("a0")
#define HOOK_A1 __reg("a1")
#define HOOK_A2 __reg("a2")
#else
#define HOOK_A0
#define HOOK_A1
#define HOOK_A2
#endif

/* Shared GadTools string/integer EditHook emulating select-all-on-entry:
 * the first printable keystroke after activation replaces the prefilled
 * content (tab fix). Wired via GTST_EditHook/GTIN_EditHook tags. */
extern struct Hook selectAllHook;

#ifdef __VBCC__
int stricmp(const char *a, const char *b);
#endif

#endif /* UTILS_H */
