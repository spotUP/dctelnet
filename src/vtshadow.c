/* src/vtshadow.c -- the screen of the console renderers, kept over a display
 * reopen (vtshadow.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vtshadow.h"
#include "ansiscan.h"

static vt_term *shadow;         /* NULL unless a console renderer is up */
static BOOL shadowCp437;        /* its dialect: pcansi (ibmcon) or amiga (console.device) */
static BOOL replaying;          /* Shadow_Feed ignores the replay's own bytes */

/* The engine's replies (DSR, DA...) never go to the BBS: the console renderer
 * answers for itself. Only Shadow_Serialize's DECRQM probe reads one. */
static char replyBuf[32];
static long replyLen;

static void ShadowReply(void *user, const vt_u8 *buf, long len)
{
    while (len-- > 0 && replyLen < (long)sizeof(replyBuf) - 1)
        replyBuf[replyLen++] = (char)*buf++;
    replyBuf[replyLen] = 0;
}

vt_term *Shadow_NewTerm(int cols, int rows, BOOL cp437)
{
    static vt_callbacks cb;         /* zero: no damage, scroll, bell... */
    vt_term *t;

    cb.reply = ShadowReply;
    t = vt_new(cols, rows, 0, &cb, NULL);

    if (t)
    {
        vt_set_personality(t, cp437 ? VT_PCANSI : VT_AMIGA);
        vt_set_music(t, 0);         /* AnsiMusic_Filter took it out already */
    }
    return t;
}

void Shadow_Feed(const UBYTE *data, long len)
{
    if (shadow && !replaying && len > 0)
        vt_write(shadow, (const vt_u8 *)data, len);
}

static UBYTE PaletteIndex(const vt_term *t, vt_color c, UBYTE def)
{
    if (c & VT_COLOR_RGB)
        c = (vt_color)vt_rgb_to_256(VT_RGB_OF(c));
    return c < 16 ? (UBYTE)c : def;     /* default, and 256-colour beyond 16: the default */
}

void Shadow_Cell(const vt_term *t, const vt_cell *c, BOOL cp437, UBYTE out[4])
{
    vt_u16 ch = c->ch;
    UBYTE b = '?';

    if ((c->pad & VT_CELL_IMAGE) || c->width == 0)
        b = ' ';
    else if (VT_CELL_IS_CLUSTER(c))
        b = '?';
    else if (ch >= 0x20 && ch < 0x7F)
        b = (UBYTE)ch;
    else if (cp437 && ch >= 0x80)
    {
        const vt_u16 *hi = vt_cp437_table();
        int i;

        for (i = 0; i < 128; i++)
            if (hi[i] == ch) { b = (UBYTE)(0x80 + i); break; }
    }
    else if (!cp437 && ch >= 0xA0 && ch <= 0xFF)
        b = (UBYTE)ch;
    out[0] = b;
    out[1] = PaletteIndex(t, c->fg, 7);
    out[2] = PaletteIndex(t, c->bg, 0);
    out[3] = (UBYTE)(((c->attr & VT_ATTR_UNDERLINE) ? 0x01 : 0)
                     | ((c->attr & VT_ATTR_BOLD) ? 0x10 : 0)
                     | ((c->attr & VT_ATTR_INVERSE) ? 0x20 : 0)
                     | ((c->attr & VT_ATTR_BLINK) ? 0x40 : 0));
}

/* DECRQM ?7 on t itself: is autowrap on? (A query, no state changes.) */
static BOOL AutowrapOn(vt_term *t)
{
    static const vt_u8 q[] = "\033[?7$p";

    replyLen = 0;
    replyBuf[0] = 0;
    vt_write(t, q, (long)sizeof(q) - 1);
    return strstr(replyBuf, "?7;2$y") == NULL;     /* no answer: on, the default */
}

/* The engine's current pen (DECRQSS "m", the engine's own answer) as the SGR
 * the rows use, so the console's pen equals the one the BBS last set. Empty
 * when the pen is the default (the tail's reset already is that). */
static size_t PenSgr(vt_term *t, char *out)
{
    static const char q[] = "\033P$qm\033\\";
    const char *p;
    UBYTE fg = 7, bg = 0, flags = 0;
    int v[8], n = 0, i;

    replyLen = 0;
    replyBuf[0] = 0;
    vt_write(t, (const vt_u8 *)q, (long)sizeof(q) - 1);
    p = strstr(replyBuf, "$r");
    if (!p)
        return 0;
    p += 2;
    while (*p && *p != 'm')
    {
        int val = 0;

        while (*p >= '0' && *p <= '9')
            val = val * 10 + (*p++ - '0');
        if (n < 8)
            v[n++] = val;
        if (*p == ';' || *p == ':')
            p++;
        else if (*p != 'm')
            break;
    }
    for (i = 0; i < n; i++)
    {
        int c = v[i];

        if (c == 1) flags |= 0x10;
        else if (c == 4) flags |= 0x01;
        else if (c == 5 || c == 6) flags |= 0x40;
        else if (c == 7) flags |= 0x20;
        else if (c >= 30 && c <= 37) fg = (UBYTE)(c - 30);
        else if (c >= 40 && c <= 47) bg = (UBYTE)(c - 40);
        else if (c >= 90 && c <= 97) fg = (UBYTE)(c - 90 + 8);
        else if (c >= 100 && c <= 107) bg = (UBYTE)(c - 100 + 8);
        else if ((c == 38 || c == 48) && i + 2 < n && v[i + 1] == 5)
        {
            UBYTE idx = v[i + 2] < 16 ? (UBYTE)v[i + 2] : (UBYTE)(c == 38 ? 7 : 0);

            if (c == 38) fg = idx; else bg = idx;
            i += 2;
        }
    }
    if (fg == 7 && bg == 0 && flags == 0)
        return 0;
    return Ansi_PutSgr(((ULONG)fg << 16) | ((ULONG)bg << 8) | flags, FALSE, out);
}

long Shadow_Serialize(vt_term *t, BOOL cp437, ShadowWriteFn write, void *user)
{
    static const char head[] = "\033[?7l\033[0m\033[H\033[J";
    int cols = vt_cols(t), rows = vt_rows(t), x, y, cx, cy;
    BOOL wrap = AutowrapOn(t), started = FALSE;
    ULONG attr = ANSI_ATTR_RESET;
    UBYTE *cells = (UBYTE *)malloc((size_t)cols * 4);
    char *buf = (char *)malloc((size_t)cols * 24 + 32);
    char tail[64];
    long total = 0;

    if (!cells || !buf)
    {
        free(cells);
        free(buf);
        return 0;
    }
    vt_cursor(t, &cx, &cy);
    for (y = 0; y < rows; y++)
    {
        int n = 0, pos;
        const vt_cell *row = vt_row(t, y, &n);
        size_t len;

        for (x = 0; x < cols; x++)
        {
            if (x < n)
                Shadow_Cell(t, &row[x], cp437, cells + x * 4);
            else
            {
                cells[x * 4] = ' '; cells[x * 4 + 1] = 7; cells[x * 4 + 2] = 0; cells[x * 4 + 3] = 0;
            }
        }
        pos = sprintf(buf, "\033[%d;1H", y + 1);
        len = Ansi_ScreenRow(cells, (UWORD)cols, FALSE, &attr, buf + pos) - 2;  /* no CR LF */
        if (len == 0)
            continue;
        if (!started)
        {
            write(user, head, (long)sizeof(head) - 1);
            total += (long)sizeof(head) - 1;
            started = TRUE;
        }
        write(user, buf, (long)(pos + len));
        total += (long)(pos + len);
    }
    if (started || cx != 0 || cy != 0)
    {
        int m;

        if (!started)
        {
            write(user, head, (long)sizeof(head) - 1);
            total += (long)sizeof(head) - 1;
        }
        m = sprintf(tail, "\033[0m\033[%d;%dH%s", cy + 1, cx + 1, wrap ? "\033[?7h" : "");
        m += (int)PenSgr(t, tail + m);      /* last: the BBS's pen holds again */
        write(user, tail, m);
        total += m;
    }
    free(cells);
    free(buf);
    return total;
}

static void FeedInto(void *user, const char *data, long len)
{
    vt_write((vt_term *)user, (const vt_u8 *)data, len);
}

BOOL Shadow_DisplayOpened(enum RendererRoute route, int cols, int rows,
                          ShadowWriteFn write, void *user)
{
    BOOL cp437 = route == ROUTE_IBMCON, done;

    if (route == ROUTE_NONE)
        return FALSE;               /* iconified: nothing is shown, the shadow keeps going */
    if (route != ROUTE_CONSOLE && route != ROUTE_IBMCON)
    {
        Shadow_Close();             /* the built-in renderer keeps its own engine */
        return FALSE;
    }
    if (!shadow)
    {
        shadow = Shadow_NewTerm(cols, rows, cp437);
        shadowCp437 = cp437;
        return FALSE;               /* a new screen: nothing to show yet */
    }
    if (cp437 != shadowCp437)       /* ibmcon <-> console.device: the screen moves over */
    {
        vt_term *next = Shadow_NewTerm(vt_cols(shadow), vt_rows(shadow), cp437);

        if (next)
            Shadow_Serialize(shadow, shadowCp437, FeedInto, next);
        vt_free(shadow);
        shadow = next;
        shadowCp437 = cp437;
        if (!shadow)
            return FALSE;
    }
    vt_resize(shadow, cols, rows);
    replaying = TRUE;
    done = Shadow_Serialize(shadow, cp437, write, user) > 0;
    replaying = FALSE;
    return done;
}

void Shadow_Close(void)
{
    if (shadow)
        vt_free(shadow);
    shadow = NULL;
}

vt_term *Shadow_Term(void)
{
    return shadow;
}
