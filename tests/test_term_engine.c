/* tests/test_term_engine.c -- retro32-term (src/third_party/retro32-term/term-engine.c)
 * on the host, with the graphics.library calls it makes recorded. While
 * something covers the terminal (term_covered) and on a bitmap that is not
 * planar (RTG), it must draw only through the window's RastPort -- never
 * into the bitplanes, which then hold another window's pixels -- and
 * directly into its own area of the bitmap otherwise. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <exec/types.h>
#include "prefs.h"
#include "charset.h"

typedef char *STRPTR;
struct Library { UWORD lib_Version; };
struct GfxBase { struct Library LibNode; };
struct BitMap { UWORD BytesPerRow, Rows; UBYTE Flags, Depth; UWORD pad; UBYTE *Planes[8]; };
struct TextFont { UWORD tf_YSize, tf_XSize, tf_Baseline; UBYTE tf_LoChar, tf_HiChar; void *tf_CharData; UWORD tf_Modulo; void *tf_CharLoc; };
struct RastPort { struct BitMap *BitMap; UBYTE Mask, FgPen, BgPen, DrawMode; WORD cx, cy; struct TextFont *Font; struct Layer *Layer; };
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
struct Layer { struct Layer *back; struct Rectangle bounds; };
struct Layer_Info { struct Layer *top_layer; };
struct ColorMap { int unused; };
struct ViewPort { struct ColorMap *ColorMap; };
struct Screen { WORD Width, Height; struct ViewPort ViewPort; struct RastPort RastPort; struct Layer_Info LayerInfo; };
struct Window { WORD LeftEdge, TopEdge, Width, Height; BYTE BorderLeft, BorderTop, BorderRight, BorderBottom; struct Layer *WLayer; ULONG IDCMPFlags; ULONG Flags; };
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define BMA_DEPTH 4
#define BMA_FLAGS 12
#define BMF_STANDARD 8

#define W 640
#define H 256
#define BPR (W / 8)
static UBYTE planes[4][BPR * H];
static struct BitMap bm;
static struct GfxBase gfx = { { 40 } };
static struct GfxBase *GfxBase = &gfx;
static ULONG bitmapFlags = BMF_STANDARD;

/* The RastPort calls, one line each. */
static char calls[8192];
static void call(const char *fmt, long a, long b, long c, long d, long e)
{
    char line[160];
    snprintf(line, sizeof(line), fmt, a, b, c, d, e);
    strcat(calls, line);
}

/* The screen's LayerInfo, as Intuition sees it: the drawing that went into
 * the planes while it was not held, and the planes as they were given back. */
static int layerLocks, bltUnlocked, blitBusy, unlockedBusy;
static UBYTE atUnlock[4][BPR * H];
static void WaitBlit(void) { blitBusy = 0; }
static void LockLayerInfo(struct Layer_Info *li) { (void)li; layerLocks++; }
static void UnlockLayerInfo(struct Layer_Info *li)
{
    (void)li;
    assert(layerLocks > 0);
    layerLocks--;
    unlockedBusy += blitBusy;
    memcpy(atUnlock, planes, sizeof(atUnlock));
}
static ULONG GetBitMapAttr(struct BitMap *b, ULONG attr)
{
    return attr == BMA_FLAGS ? bitmapFlags : attr == BMA_DEPTH ? b->Depth : 0;
}
/* MA2-9: pens from the colour map, recorded. obtainFail makes the next
 * ones fail (-1); the pen handed out is 50 + the count so far, mod 200. */
#define TAG_END 0
#define OBP_Precision 1
#define PRECISION_EXACT 0
#define OBP_FailIfBad 2
static int obtainCalls, releaseCalls, obtainFail;
static ULONG obtainR, obtainG, obtainB;
static LONG obtainTags[4];
static LONG ObtainBestPen(struct ColorMap *cm, ULONG r, ULONG g, ULONG b, ...)
{
    (void)cm;
    obtainR = r; obtainG = g; obtainB = b;
    obtainTags[0] = OBP_Precision; obtainTags[1] = PRECISION_EXACT; obtainTags[2] = OBP_FailIfBad; obtainTags[3] = 1;
    obtainCalls++;
    return obtainFail ? -1 : 50 + (obtainCalls - 1) % 200;
}
static void ReleasePen(struct ColorMap *cm, ULONG pen) { (void)cm; (void)pen; releaseCalls++; }
static void SetFont(struct RastPort *rp, struct TextFont *tf) { rp->Font = tf; }
static void SetAPen(struct RastPort *rp, ULONG pen) { rp->FgPen = (UBYTE)pen; }
static void SetBPen(struct RastPort *rp, ULONG pen) { rp->BgPen = (UBYTE)pen; }
static void SetDrMd(struct RastPort *rp, ULONG mode) { rp->DrawMode = (UBYTE)mode; }
static void Move(struct RastPort *rp, LONG x, LONG y) { rp->cx = (WORD)x; rp->cy = (WORD)y; }
static void Text(struct RastPort *rp, STRPTR s, ULONG n)
{
    char line[160];
    snprintf(line, sizeof(line), "Text(%d,%d fg%d bg%d \"%.*s\")\n", rp->cx, rp->cy, rp->FgPen, rp->BgPen, (int)n, s);
    strcat(calls, line);
}
static void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    if (rp->DrawMode == COMPLEMENT)
        call("Complement(%ld,%ld,%ld,%ld mask%ld)\n", (long)x0, (long)y0, (long)x1, (long)y1, (long)rp->Mask);
    else
        call("RectFill(%ld,%ld,%ld,%ld pen%ld)\n", (long)x0, (long)y0, (long)x1, (long)y1, (long)rp->FgPen);
}
/* Pictures (kitty graphics): the chunky writes, one line each with the
 * first pixel's pen. */
static int chunkyCalls;
#define FB_W 700
#define FB_H 420
static UBYTE chunkyFb[FB_W * FB_H];     /* the last pen written at each pixel, 0xFF where none */
static void WriteChunkyPixels(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, UBYTE *a, LONG mod)
{
    LONG x, y, stride = mod ? mod : x1 - x0 + 1;
    (void)rp;
    chunkyCalls++;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (x >= 0 && x < FB_W && y >= 0 && y < FB_H)
                chunkyFb[y * FB_W + x] = a[(y - y0) * stride + (x - x0)];
    call("Chunky(%ld,%ld,%ld,%ld pen%ld)\n", (long)x0, (long)y0, (long)x1, (long)y1, (long)a[0]);
}
static LONG WritePixelArray8(struct RastPort *rp, ULONG x0, ULONG y0, ULONG x1, ULONG y1, UBYTE *a, struct RastPort *tmp)
{
    (void)tmp;
    WriteChunkyPixels(rp, (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1, a, 0);
    return 0;
}
static struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG d, ULONG f, struct BitMap *friendBm)
{
    (void)w; (void)h; (void)d; (void)f;
    return friendBm;
}
static void FreeBitMap(struct BitMap *b) { (void)b; }
static void ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1)
{
    char line[160];
    snprintf(line, sizeof(line), "ScrollRaster(%ld,%ld %ld,%ld,%ld,%ld bg%d)\n", (long)dx, (long)dy, (long)x0, (long)y0, (long)x1, (long)y1, rp->BgPen);
    strcat(calls, line);
}

/* The engine's blits: minterm 0xC0 copies, 0xFF sets, 0x00 clears, in the
 * planes of mask; x and w are multiples of 8. Overlaps go through a copy. */
static LONG BltBitMap(struct BitMap *s, WORD sx, WORD sy, struct BitMap *d, WORD dx, WORD dy,
                      WORD w, WORD h, UBYTE minterm, UBYTE mask, UBYTE *tmp)
{
    static UBYTE buf[BPR * H];
    WORD p, y;

    (void)tmp;
    blitBusy = 1;
    if (!layerLocks)
        bltUnlocked++;
    for (p = 0; p < 4; p++) {
        if (!(mask & (1 << p)))
            continue;
        if (minterm == 0xC0) {
            for (y = 0; y < h; y++)
                memcpy(buf + y * BPR, s->Planes[p] + (sy + y) * BPR + sx / 8, w / 8);
            for (y = 0; y < h; y++)
                memcpy(d->Planes[p] + (dy + y) * BPR + dx / 8, buf + y * BPR, w / 8);
        } else {
            for (y = 0; y < h; y++)
                memset(d->Planes[p] + (dy + y) * BPR + dx / 8, minterm == 0xFF ? 0xFF : 0x00, w / 8);
        }
    }
    return 4;
}

/* An answer to the BBS. A menu opens meanwhile when menuOnSend is set. */
static struct Layer winLayer, menuLayer;
static struct Screen screen;
static int sentLocked, menuOnSend;
static UBYTE sentBuf[256];
static int sentLen, sentCalls;
static void send_data_(const UBYTE *d, LONG n)
{
    if (sentLen + n <= (int)sizeof(sentBuf))
        memcpy(sentBuf + sentLen, d, (size_t)n);
    sentLen += (int)n;
    sentCalls++;
    sentLocked += layerLocks;
    if (menuOnSend)
        screen.LayerInfo.top_layer = &menuLayer;
}
#define send_data(d, n) send_data_((d), (n))
#include "../src/third_party/retro32-term/term-engine.c"

/* DCTelnet's side of vtbuiltin.c: its screen, the terminal window and
 * layers.library. */
static struct Library layers = { 40 };
static struct Library *LayersBase = &layers;
static struct Screen *scr = &screen;
static struct Window window, *win = &window;
/* MA3: the window title and IDCMP calls, recorded. */
#define IDCMP_MOUSEMOVE 0x10UL
#define IDCMP_RAWKEY 0x400UL
static char MainWindowTitle[] = "DCTelnet own title";
#define WFLG_BACKDROP 0x100UL
static char titleSet[256];
static const UBYTE *screenTitleSet;     /* (UBYTE *)-1 when the screen title stays */
static int titleCalls;
static int ledCalls;
static void SetWindowTitles(struct Window *w, UBYTE *t, UBYTE *s)
{
    (void)w;
    strcpy(titleSet, (const char *)t);
    screenTitleSet = s;
    titleCalls++;
}
static void LEDs(void) { ledCalls++; }
static void ModifyIDCMP(struct Window *w, ULONG flags) { w->IDCMPFlags = flags; }
/* DCTelnet.c's selection drag flag, and Intuition's ReportMouse recorded: it
 * sends no IDCMP_MOUSEMOVE while it is off. */
static BOOL selDragging, reportMouse;
static int reportCalls;
static void ReportMouse(BOOL on, struct Window *w) { (void)w; reportMouse = on; reportCalls++; }
/* guis.c's side: TRUE while the Modern tool bar is open (its hover). */
static BOOL toolBarHover;
static BOOL ToolBarWantsHover(void) { return toolBarHover; }
/* The built-in renderer reads the user's preferences. */
struct PrefsStruct prefs;

/* The sentinel for what VtDraw hands the glyph writer: every call is kept
 * (the column, the row, the code, the resolved pens), then drawn for real. */
#define PROBE_MAX 20000
static struct { WORD x, y; UBYTE code, fg, bg; } probe[PROBE_MAX];
static int probes;
static void probe_glyph(WORD x, UBYTE ch)
{
    if (term_cursor_busy) {         /* the cursor block or its removal: not the text */
        term_glyph(x, ch);
        return;
    }
    if (probes < PROBE_MAX) {
        probe[probes].x = x;
        probe[probes].y = cur_y;
        probe[probes].code = ch;
        probe[probes].fg = term_pen_fg;
        probe[probes].bg = term_pen_bg;
    }
    probes++;
    term_glyph(x, ch);
}
#define term_glyph(x, c) probe_glyph((x), (c))
#include "vtengine.h"
/* KG-10: the screen's display mode and its pixel aspect. modeRes is what
 * GetDisplayInfoData reports (0: the mode is unknown); aspectCalls and
 * aspectRx/Ry record what VtSetAspect handed the engine. */
struct DisplayInfo { struct { UWORD x, y; } Resolution; };
#define DTAG_DISP 0x80001000UL
#define INVALID_ID (~0UL)
static int modeRx, modeRy;
static int aspectCalls, aspectRx, aspectRy;
static ULONG GetVPModeID(struct ViewPort *vp) { (void)vp; return 0x29000UL; }
static long GetDisplayInfoData(void *h, UBYTE *buf, ULONG size, ULONG tag, ULONG id)
{
    struct DisplayInfo *di = (struct DisplayInfo *)buf;
    (void)h; (void)size; (void)id;
    assert(tag == DTAG_DISP);
    if (!modeRx)
        return 0;
    di->Resolution.x = (UWORD)modeRx;
    di->Resolution.y = (UWORD)modeRy;
    return 1;
}
static void probe_aspect(vt_term *t, int rx, int ry);
#define vt_set_pixel_aspect(t, rx, ry) probe_aspect((t), (rx), (ry))
/* The sentinel for VtOpen: what the global vtTerm is while vt_new() draws. */
static int newCalls;
static int newSawTerm;
static int newSb;                   /* the scrollback line count VtOpen handed vt_new */
static vt_term *probe_new(int cols, int rows, int sb, const vt_callbacks *cb, void *user);
#define vt_new(c, r, sb, cb, u) probe_new((c), (r), (sb), (cb), (u))
/* H4: DCTelnet.c's reopen hook and default-screen rule, as counting stubs. */
static int ripReopens, ripDefaultAsked;
static ULONG ripReopenDisplayID, ripReopenWas;
static ULONG ripReopenRenderer;
static BOOL VtRipDefaultScreen(ULONG *id, UWORD *w, UWORD *h)
{
    ripDefaultAsked++;
    *id = 0x91000UL; *w = 640; *h = 400;
    return FALSE;
}
static void VtRipScreenChanged(const struct PrefsStruct *before)
{
    ripReopens++;
    ripReopenWas = before->DisplayID;
    ripReopenDisplayID = prefs.DisplayID;
    ripReopenRenderer = prefs.State & APP_RENDERER_ALL;
}
#include "site_prefs.h"
struct SiteHandChanges handChanges;     /* DCTelnet.c's record of temporary changes */
ULONG sessionSettingsId;                /* 0: a quick connect, no entry session */
#include "../src/vtbuiltin.c"
#undef term_glyph
#undef vt_new
#undef vt_set_pixel_aspect
static void probe_aspect(vt_term *t, int rx, int ry)
{
    aspectCalls++;
    aspectRx = rx;
    aspectRy = ry;
    vt_set_pixel_aspect(t, rx, ry);
}
static vt_term *probe_new(int cols, int rows, int sb, const vt_callbacks *cb, void *user)
{
    newCalls++;
    newSb = sb;
    newSawTerm = vtTerm != NULL;    /* vt_new() draws through VtDraw() right here */
    return vt_new(cols, rows, sb, cb, user);
}

/* A font whose glyphs all differ: row r of character c is c ^ (r * 37). */
static UBYTE fontData[8][256];
static ULONG fontLoc[256];
static struct TextFont font = { 8, 8, 6, 0, 255, fontData, 256, fontLoc };
/* The C64's size: 16x8 cells. Row r of character c is c << 8 | (c ^ r).
 * font_extract() reads 3 bytes at each glyph's byte offset, so the last glyph
 * of the last row reads 2 bytes past the 512 of a row: one slack row. */
static UBYTE wideData[9][512];
static ULONG wideLoc[256];
static struct TextFont wide = { 8, 16, 6, 0, 255, wideData, 512, wideLoc };
static struct RastPort winRp;

/* The terminal of a 640x245 window at y = 11, under a title bar: 30 rows. */
#define TOP 11
static void setup(void)
{
    WORD c, r, p;

    for (c = 0; c < 256; c++) {
        fontLoc[c] = (ULONG)(c * 8) << 16 | 8;
        wideLoc[c] = (ULONG)(c * 16) << 16 | 16;
        for (r = 0; r < 8; r++) {
            fontData[r][c] = (UBYTE)(c ^ (r * 37));
            wideData[r][2 * c] = (UBYTE)c;
            wideData[r][2 * c + 1] = (UBYTE)(c ^ r);
        }
    }
    term_cell_source = NULL;        /* the standalone cursor until a test opens VtOpen() */
    bm.BytesPerRow = BPR; bm.Rows = H; bm.Depth = 4;
    for (p = 0; p < 4; p++) bm.Planes[p] = planes[p];
    memset(planes, 0x55, sizeof(planes));           /* "the title bar and other windows" */
    memset(&winRp, 0, sizeof(winRp));
    winRp.Mask = 0xFF;
    screen.Width = W; screen.Height = H;
    screen.RastPort.BitMap = &bm; screen.RastPort.Mask = 0xFF;
    /* the terminal window, the screen's only window: nothing over it */
    window.LeftEdge = 0; window.TopEdge = 0; window.Width = W; window.Height = H;
    window.BorderTop = TOP;
    window.WLayer = &winLayer;
    winLayer.bounds.MaxX = W - 1; winLayer.bounds.MaxY = H - 1;
    screen.LayerInfo.top_layer = &winLayer;
    /* a menu strip's layer over the terminal's top rows */
    menuLayer.back = &winLayer;
    menuLayer.bounds.MinX = 0; menuLayer.bounds.MinY = 0;
    menuLayer.bounds.MaxX = 199; menuLayer.bounds.MaxY = 40;
    layerLocks = bltUnlocked = blitBusy = unlockedBusy = sentLocked = menuOnSend = 0;
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    calls[0] = 0;
}

static void feed(const char *s)
{
    while (*s)
        term_feed((UBYTE)*s++);
    term_flush();
}

static void test_the_terminal_starts_below_the_title_bar_and_matches_the_window_rows(void)
{
    WORD p;

    setup();
    assert(term_rows == 30);                        /* 245 / 8, what DCTelnet tells the BBS */
    for (p = 0; p < 4; p++) {
        assert(planes[p][(TOP - 1) * BPR] == 0x55);  /* the bar is not drawn over */
        assert(planes[p][TOP * BPR] == 0x00);        /* the terminal is cleared from there */
    }
    feed("A");
    assert(planes[0][TOP * BPR] == 'A' && planes[0][(TOP + 1) * BPR] == ('A' ^ 37));
    assert(calls[0] == 0);                          /* direct: no RastPort call */
}

static void test_a_covered_terminal_draws_only_through_the_rastport(void)
{
    static UBYTE before[sizeof(planes)];
    int i;

    setup();
    term_covered(1);
    memcpy(before, planes, sizeof(planes));
    feed("Hello \x1b[31mred\x1b[0m");
    assert(memcmp(before, planes, sizeof(planes)) == 0);   /* (checked here too: later scrolls */
    for (i = 0; i < 30; i++)                                /*  would move a direct draw out)  */
        feed("\r\n");                               /* to the bottom and one line further */
    cursor_show();
    feed("\x1b[5;10H\x1b[2L\x1b[3P");
    assert(memcmp(before, planes, sizeof(planes)) == 0);   /* another window's pixels are untouched */
    assert(strstr(calls, "Text(0,6 fg7 bg0 \"Hello \")"));  /* runs, at the window's coordinates */
    assert(strstr(calls, "Text(48,6 fg1 bg0 \"red\")"));
    assert(strstr(calls, "ScrollRaster(0,8 0,0,639,239 bg0)\nRectFill(0,232,639,239 pen0)"));
                                  /* the line feed at the bottom, and the new row cleared (a
                                     backfill hook, not BgPen, would fill it otherwise) */
    assert(strstr(calls, "Complement(0,232,7,239 mask15)"));        /* the cursor: pen XOR 15 */
    assert(strstr(calls, "ScrollRaster(0,-16 0,32,639,239 bg0)"));   /* insert 2 lines at row 5 */
    assert(strstr(calls, "ScrollRaster(24,0 72,32,639,39 bg0)"));    /* delete 3 characters */
    assert(winRp.Mask == 0xFF);                     /* the mask is given back */
}

static void test_uncovered_again_it_draws_directly(void)
{
    setup();
    term_covered(1);
    feed("x");
    term_covered(0);
    calls[0] = 0;
    feed("\rA");
    assert(calls[0] == 0 && planes[0][TOP * BPR] == 'A');
}

static void test_an_rtg_bitmap_always_takes_the_rastport(void)
{
    static UBYTE before[sizeof(planes)];

    bitmapFlags = 0;                                /* not BMF_STANDARD: chunky */
    setup();
    term_covered(0);
    memcpy(before, planes, sizeof(planes));
    feed("rtg");
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "Text(0,6 fg7 bg0 \"rtg\")"));
    bitmapFlags = BMF_STANDARD;
    assert(term_init_area(&screen, NULL, 0, 0, 0, 0, W, H, &font) == 0);     /* planar, direct only */
    bitmapFlags = 0;
    assert(term_init_area(&screen, NULL, 0, 0, 0, 0, W, H, &font) != 0);     /* RTG needs a RastPort */
    bitmapFlags = BMF_STANDARD;
}

static void test_the_pen_map_colours_the_rastport_path_and_stops_direct_drawing(void)
{
    static const UBYTE pens[16] = { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55 };

    static UBYTE before[sizeof(planes)];

    setup();
    term_set_pens(pens);            /* the Workbench's shared pens: never written as numbers */
    term_covered(0);
    memcpy(before, planes, sizeof(planes));
    feed("\x1b[1;33mY");
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "Text(0,6 fg51 bg40 \"Y\")"));    /* bright yellow = ANSI 11 */
    term_set_pens(NULL);
    term_covered(0);
    calls[0] = 0;
    feed("\rZ");
    assert(calls[0] == 0 && planes[0][TOP * BPR] != before[TOP * BPR]);  /* pens 0-15: direct again */
}

/* 8 bitplanes (AGA): direct drawing writes planes 0-3 only and the upper
 * planes' bits showed through (a grey background), so the RastPort draws. */
static void test_an_8_plane_screen_takes_the_rastport(void)
{
    static UBYTE before[sizeof(planes)];

    setup();
    bm.Depth = 8;
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    calls[0] = 0;
    memcpy(before, planes, sizeof(planes));
    feed("aga");
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "Text(0,6 fg7 bg0 \"aga\")"));
    bm.Depth = 4;
}

/* The C64 fonts (Petscii.font) have 16x8 cells: 40 columns in 640 pixels,
 * two bytes per plane row, the wrap at column 40, a cursor two bytes wide. */
static void test_a_16_pixel_font_gives_40_columns(void)
{
    setup();
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &wide) == 0);
    assert(term_cols == 40);
    feed("\x41");
    assert(planes[0][TOP * BPR] == 0x41 && planes[0][TOP * BPR + 1] == 0x41);    /* row 0: c, c ^ 0 */
    assert(planes[0][(TOP + 3) * BPR + 1] == (0x41 ^ 3));
    assert(planes[1][TOP * BPR] == 0x41);                                         /* pen 7: planes 0-2 */
    assert(planes[3][TOP * BPR] == 0x00);
    {
        int i;
        for (i = 0; i < 39; i++) feed("B");
        feed("C");                                  /* column 41: the next row */
        assert(planes[0][(TOP + 8) * BPR] == 'C');
    }
    cursor_show();
    assert(planes[0][(TOP + 8) * BPR + 2] == 0xFF && planes[0][(TOP + 8) * BPR + 3] == 0xFF);
    cursor_hide();
    term_set_font(&wide);   /* the other C64 charset: new glyphs from here, the screen stays */
    assert(planes[0][TOP * BPR] == 0x41 && term_cols == 40);
    assert(calls[0] == 0);
}

/* vt_new() draws its blank screen through VtDraw() before it returns: on a
 * second VtOpen the global vtTerm must not still be the previous engine (freed
 * in the original bug), so VtDraw finds no terminal and reads no row. */
static void test_vtopen_twice_never_draws_through_the_previous_engine(void)
{
    setup();
    memset(&prefs, 0, sizeof(prefs));
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    assert(VtOpen() == 0);
    assert(vtTerm != NULL);
    VtClose();
    assert(vtTerm == NULL);
    newCalls = 0;
    assert(VtOpen() == 0);                      /* no engine: the one open that makes one */
    assert(newCalls == 1 && newSawTerm == 0);
    assert(vtTerm != NULL);
    VtClose();
    assert(vtTerm == NULL);
}

static void vt_write_str(const char *s)
{
    VtWrite((const UBYTE *)s, (long)strlen(s));
}

/* Upstream todos: a menu opened between the look at the layers and the
 * drawing was drawn over. Intuition makes a menu's layer under the screen's
 * LayerInfo: while the terminal draws straight into the bitmap, the lock is
 * held from the look to the end of the drawing, the blitter's too. */
static void test_a_menu_cannot_open_while_the_terminal_draws_into_the_bitmap(void)
{
    int i;

    setup();
    assert(VtOpen() == 0);
    bltUnlocked = 0;                            /* (term_init_area's clear) */
    for (i = 0; i < 32; i++)
        vt_write_str("line\r\n");             /* scrolls: blits */
    assert(calls[0] == 0);                      /* drawn straight into the bitmap */
    assert(bltUnlocked == 0);                   /* every blit with the layers held */
    assert(unlockedBusy == 0);                  /* and done before they were given back */
    assert(memcmp(atUnlock, planes, sizeof(planes)) == 0);    /* nothing drawn after */
    assert(planes[0][(TOP + 29 * 8) * BPR + 1] == 0);         /* (the new bottom row, */
    assert(planes[0][(TOP + 29 * 8) * BPR] == (UBYTE)~' ');   /*  the cursor in it, drawn as ' ')   */
    assert(planes[0][(TOP + 28 * 8) * BPR] == 'l');
    assert(layerLocks == 0);
    vt_write_str("\x1b[?25l\n");     /* a scroll and no cursor: the write ends on a blit */
    assert(bltUnlocked == 0 && unlockedBusy == 0);
    VtClose();
}

static void test_a_menu_open_over_the_terminal_is_drawn_around(void)
{
    static UBYTE before[sizeof(planes)];

    setup();
    assert(VtOpen() == 0);
    screen.LayerInfo.top_layer = &menuLayer;
    memcpy(before, planes, sizeof(planes));
    vt_write_str("Hello");
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "\"Hello\""));
    assert(layerLocks == 0);                    /* the RastPort's layer clips: not held */
    screen.LayerInfo.top_layer = &winLayer;     /* the menu closes */
    calls[0] = 0;
    vt_write_str("\rA");
    assert(calls[0] == 0 && planes[0][TOP * BPR] == 'A');
    VtClose();
}

/* A send can wait for the network, and every window on the screen with it:
 * the layers are given back for it, and looked at again after. */
static void test_a_menu_opened_while_an_answer_is_sent_is_drawn_around(void)
{
    static UBYTE before[sizeof(planes)];

    setup();
    assert(VtOpen() == 0);
    menuOnSend = 1;
    memcpy(before, planes, sizeof(planes));
    vt_write_str("A\x1b[6nB");                  /* the BBS asks where the cursor is */
    assert(sentLocked == 0);
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "\"AB\""));
    assert(layerLocks == 0);
    VtClose();
}


/* --- MA1: the built-in renderer as a PC ANSI terminal for BBS art --- */

/* A fresh terminal, the probe empty, the preferences at their defaults. */
static void vt_begin(void)
{
    setup();
    memset(&prefs, 0, sizeof(prefs));
    assert(VtOpen() == 0);
    probes = 0;
}

static void vt_put(const char *s)
{
    VtWrite((const UBYTE *)s, (long)strlen(s));
}

static void probe_codes(char *out, size_t max)
{
    int i;
    size_t n = 0;

    for (i = 0; i < probes && i < PROBE_MAX && n + 8 < max; i++)
        n += (size_t)snprintf(out + n, max - n, "%02X ", probe[i].code);
    out[n] = 0;
}

static void test_ma1_2_line_art_lands_in_the_grid_and_reaches_the_glyph_writer(void)
{
    char codes[256];
    int n;
    const vt_cell *row;

    vt_begin();
    vt_put("\xC9\xCD\xBB\r\n\xBA\r\n\xC8\xCD\xBC");
    probe_codes(codes, sizeof(codes));
    assert(strcmp(codes, "C9 CD BB BA C8 CD BC ") == 0);
    assert(probe[0].x == 0 && probe[0].y == 0 && probe[1].x == 1 && probe[2].x == 2);
    assert(probe[3].x == 0 && probe[3].y == 1);
    assert(probe[4].x == 0 && probe[4].y == 2 && probe[6].x == 2);
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0x2554 && row[1].ch == 0x2550 && row[2].ch == 0x2557);
    VtClose();
}

static void test_ma1_3_blocks_and_shades_map_to_themselves_never_to_the_fallback(void)
{
    static const UBYTE want[] = { 0xDB, 0xB0, 0xB1, 0xB2, 0xDC, 0xDF };
    int i;

    vt_begin();
    vt_put("\xDB\xB0\xB1\xB2\xDC\xDF");
    assert(probes == 6);
    for (i = 0; i < 6; i++)
        assert(probe[i].code == want[i] && probe[i].code != '?');
    VtClose();
}

static void test_ma1_1_latin1_byte_reaches_the_glyph_writer_unchanged(void)
{
    vt_begin();
    vt_put("\xE9");
    assert(probes == 1 && probe[0].code == 0xE9);
    VtClose();
}

static void test_ma1_4_sixteen_colours(void)
{
    vt_begin();
    vt_put("\x1b[0;31mA");
    assert(probes == 1 && probe[0].fg == 1);
    probes = 0;
    vt_put("\x1b[1;31mA");
    assert(probes == 1 && probe[0].fg == 9);
    probes = 0;
    vt_put("\x1b[0m\x1b[1;33;44mA");
    assert(probes == 1 && probe[0].fg == 11 && probe[0].bg == 4);   /* bright yellow: ANSI order, red is 1 and 9 */
    VtClose();
}

static void expect_cursor(int x, int y);

static void test_ma1_5_6_7_ice_colours_decide_what_blink_draws_as(void)
{
    const vt_cell *row;
    int n;

    vt_begin();                                  /* default: iCE colours */
    assert(prefs.Blink == BLINK_ICE);
    vt_put("\x1b[5;44mA");
    assert(probes == 1 && probe[0].bg == 12);
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].attr & VT_ATTR_BLINK);        /* the grid keeps what was sent */
    VtClose();

    vt_begin();
    prefs.Blink = BLINK_STEADY;                  /* iCE colours off: drawn as sent */
    vt_put("\x1b[5;44mA");
    assert(probes == 1 && probe[0].bg == 4);
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].attr & VT_ATTR_BLINK);

    /* The menu toggle: the cells already on the screen are drawn again. */
    prefs.Blink = BLINK_ICE;
    probes = 0;
    VtRedraw();
    assert(probes == term_cols * term_rows && probe[0].code == 'A' && probe[0].bg == 12);
    prefs.Blink = BLINK_STEADY;
    probes = 0;
    VtRedraw();
    assert(probe[0].bg == 4);
    /* The redraw leaves the cursor where the engine has it, not on the
     * last row it drew. */
    expect_cursor(1, 0);
    VtClose();
}

static void expect_cursor(int x, int y)
{
    int cx, cy;

    vt_cursor(vtTerm, &cx, &cy);
    assert(cx == x && cy == y);
    assert(cur_x == x && cur_y == y);           /* VtSyncCursor put them there */
}

/* A1200, built-in renderer, own screen: Tool Bar on while connected reopens
 * the display (CloseDisplay/OpenDisplay). CloseConsoleDevice() freed the
 * engine and VtOpen() made a blank one: the BBS's text was gone. The engine
 * outlives the reopen; the new, shorter window (the tool bar above it) gets
 * the same cells drawn again. */
static void test_a_display_reopen_keeps_the_text_and_draws_it_again(void)
{
    const vt_cell *row;
    int n, i, seen = 0;
    char line[8];

    vt_begin();
    assert(!vtKeptText);                    /* a first open: blank, OpenDisplay() may welcome */
    vt_put("HELLO\r\nBBS");
    VtReleasePens();                        /* CloseConsoleDevice() in CloseDisplay() */
    /* the reopened window: 16 pixels lower and shorter, two rows fewer */
    assert(term_init_area(&screen, &winRp, 0, TOP + 16, 0, TOP + 16, W, H - TOP - 16, &font) == 0);
    newCalls = 0;
    probes = 0;
    assert(VtOpen() == 0);
    assert(newCalls == 0);                  /* the same engine, not a blank one */
    assert(vtKeptText);                     /* so OpenDisplay() writes no clearing welcome screen */
    assert(vt_rows(vtTerm) == term_rows && vt_cols(vtTerm) == term_cols);
    row = vt_row(vtTerm, 0, &n);
    for (i = 0; i < 5; i++) line[i] = (char)row[i].ch;
    line[5] = 0;
    assert(strcmp(line, "HELLO") == 0);
    row = vt_row(vtTerm, 1, &n);
    assert(row[0].ch == 'B' && row[2].ch == 'S');
    /* and drawn: the reopen cleared the area, the cells reach the glyph writer */
    for (i = 0; i < probes && i < PROBE_MAX; i++)
        if (probe[i].y == 0 && probe[i].x == 0 && probe[i].code == 'H') seen |= 1;
        else if (probe[i].y == 1 && probe[i].x == 2 && probe[i].code == 'S') seen |= 2;
    assert(seen == 3);
    expect_cursor(3, 1);
    VtClose();
}

/* A reopen in another display mode (PETSCII entry ends, or starts): OpenDisplay()
 * drops the engine with VtClose() first, so VtOpen() makes a blank one and the
 * welcome screen shows. A reopen in the same mode keeps (test above). */
static void test_a_reopen_in_another_display_mode_starts_blank(void)
{
    const vt_cell *row;
    int n;

    vt_begin();
    vt_put("C64 TEXT");
    VtReleasePens();
    VtClose();                              /* OpenDisplay(): the mode changed */
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    newCalls = 0;
    assert(VtOpen() == 0);
    assert(newCalls == 1);                  /* a blank engine */
    assert(!vtKeptText);                    /* so the welcome screen is written */
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0 || row[0].ch == ' ');
    VtClose();
}

static void test_ma1_8_cursor_save_restore_position_and_erase(void)
{
    vt_begin();
    vt_put("\x1b[5;10H");
    expect_cursor(9, 4);
    vt_put("\x1b[s\x1b[1;1H");
    expect_cursor(0, 0);
    vt_put("\x1b[u");
    expect_cursor(9, 4);
    vt_put("XYZ\x1b[K");                         /* erase to the end of the line */
    expect_cursor(12, 4);
    vt_put("\x1b[2J");                           /* ANSI.SYS: clear and home */
    expect_cursor(0, 0);
    VtClose();
}

static void test_ma1_init_strings_draw_nothing_and_leave_the_cursor(void)
{
    vt_begin();
    vt_put("\x1b[20l");
    vt_put("\x1b[0 p");
    assert(probes == 0);
    expect_cursor(0, 0);
    VtClose();
}

/* MA1-9: a real BBS ANSI art file (tests/ansi_art/README names its source),
 * fed the way ConWrite feeds the engine: 1024 bytes at a time. */
static unsigned long crc32_update(unsigned long crc, const UBYTE *p, size_t n)
{
    int k;

    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
    return crc;
}

#define ART_GRID_CRC 0xD5F60475UL

static void test_ma1_9_the_sample_art_file_gives_the_known_grid(void)
{
    static UBYTE art[8192];
    FILE *f = fopen("ansi_art/up_rough_banner.ans", "rb");
    size_t len, off, end;
    unsigned long crc = 0xFFFFFFFFUL;
    int x, y, n;

    assert(f);
    len = fread(art, 1, sizeof(art), f);
    fclose(f);
    for (end = 0; end < len && art[end] != 0x1A; end++)     /* the SAUCE record follows ^Z */
        ;
    assert(end > 3000);
    vt_begin();
    for (off = 0; off < end; off += 1024)
        VtWrite(art + off, (long)(end - off < 1024 ? end - off : 1024));
    assert(probes > 0);
    for (y = 0; y < vt_rows(vtTerm); y++) {
        const vt_cell *row = vt_row(vtTerm, y, &n);

        for (x = 0; x < n; x++) {
            UBYTE b[5];
            b[0] = (UBYTE)(row[x].ch & 255); b[1] = (UBYTE)(row[x].ch >> 8);
            b[2] = (UBYTE)row[x].fg; b[3] = (UBYTE)row[x].bg; b[4] = (UBYTE)row[x].attr;
            crc = crc32_update(crc, b, sizeof(b));
        }
    }
    crc ^= 0xFFFFFFFFUL;
    assert(crc == ART_GRID_CRC);
    VtClose();
}

/* --- Font probe: UTF-8 box drawing and accents in the font's own encoding --- */

/* A terminal whose font has glyph 0xC4 as the A-diaeresis of Latin-1
 * (ibm = 0) or as the IBM horizontal line (ibm = 1), opened through VtOpen
 * with the given terminal type and character set. */
static void probe_begin(int ibm, UBYTE termType, UBYTE charset)
{
    static const UBYTE a_diaeresis[8] = { 0x66, 0x00, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x00 };
    int r;

    setup();
    for (r = 0; r < 8; r++)
        fontData[r][0xC4] = ibm ? (UBYTE)(r == 4 ? 0xFF : 0x00) : a_diaeresis[r];
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    memset(&prefs, 0, sizeof(prefs));
    prefs.State = APP_RENDERER_BUILTIN;
    prefs.TermType = termType;
    prefs.Charset = charset;
    assert(VtOpen() == 0);
    probes = 0;
    calls[0] = 0;
}

/* TRUE when row r of the first cell holds the byte b in the foreground pen 7. */
static int cell_row_is(int r, UBYTE b)
{
    return planes[0][(TOP + r) * BPR] == b && planes[1][(TOP + r) * BPR] == b &&
           planes[2][(TOP + r) * BPR] == b && planes[3][(TOP + r) * BPR] == 0;
}

/* The font decides what a decoded code point looks like. Through VtWrite:
 * a Latin-1 font gets the box line drawn, not its A-diaeresis at 0xC4; the
 * CP437 byte stream is still passed through untouched; an IBM font gets the
 * accent at its CP437 code. The rasteriser, the encoding rule and the probe
 * have their own tests; this one proves they are wired into the draw path. */
static void test_font_probe_reaches_the_grid_through_vtwrite(void)
{
    static const UBYTE a_diaeresis[8] = { 0x66, 0x00, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x00 };
    int r;

    probe_begin(0, TERMTYPE_UNIX, CHARSET_UTF8);
    vt_put("\xE2\x94\x80");                             /* U+2500 */
    for (r = 0; r < 8; r++)
        assert(cell_row_is(r, r == 4 ? 0xFF : 0x00));    /* the drawn line, not A-diaeresis */
    VtClose();

    probe_begin(0, TERMTYPE_BBS, CHARSET_CP437);
    vt_put("\xC4");                                     /* the BBS byte stays the font's glyph */
    for (r = 0; r < 8; r++)
        assert(cell_row_is(r, a_diaeresis[r]));
    VtClose();

    probe_begin(1, TERMTYPE_UNIX, CHARSET_UTF8);
    vt_put("\xC3\xA9");                                 /* U+00E9 e-acute: CP437 0x82 */
    for (r = 0; r < 8; r++)
        assert(cell_row_is(r, (UBYTE)(0x82 ^ (r * 37))));
    VtClose();
}

/* The covered terminal has no bitmap to write to: the drawn line goes out as
 * RectFill runs (the cell background, then one fg run on row 4). */
static void test_a_drawn_cell_on_a_covered_terminal_goes_out_as_rectfills(void)
{
    static UBYTE before[sizeof(planes)];

    probe_begin(0, TERMTYPE_UNIX, CHARSET_UTF8);
    screen.LayerInfo.top_layer = &menuLayer;        /* a menu strip over the terminal */
    memcpy(before, planes, sizeof(planes));
    calls[0] = 0;
    vt_put("\xE2\x94\x80");
    assert(memcmp(before, planes, sizeof(planes)) == 0);
    assert(strstr(calls, "RectFill(0,0,7,7 pen0)\nRectFill(0,4,7,4 pen7)"));
    VtClose();
}

/* --- MA2: the xterm personality, UTF-8 and the 256-colour / 24-bit pens --- */

/* A fresh Unix terminal: xterm, UTF-8 characters, the default sixteen pens. */
static void xterm_begin_on(UBYTE depth, ULONG flags)
{
    static const UWORD ansi[16] = {
        0x000, 0xA00, 0x0A0, 0xA50, 0x00A, 0xA0A, 0x0AA, 0xAAA,
        0x555, 0xF55, 0x5F5, 0xFF5, 0x55F, 0xF5F, 0x5FF, 0xFFF
    };

    setup();
    bm.Depth = depth;
    bitmapFlags = flags;
    assert(term_init_area(&screen, &winRp, 0, TOP, 0, TOP, W, H - TOP, &font) == 0);
    obtainCalls = releaseCalls = obtainFail = 0;
    memset(&prefs, 0, sizeof(prefs));
    prefs.State = APP_RENDERER_BUILTIN;
    prefs.TermType = TERMTYPE_UNIX;
    prefs.Charset = CHARSET_UTF8;
    memcpy(prefs.AnsiColors, ansi, sizeof(ansi));
    assert(VtOpen() == 0);
    /* The MA2 tests below assert the IBM-font mapping (UTF-8 box and blocks
     * reach term_glyph as CP437 codes); this test font's 0xC4 is not a line,
     * so the probe would say Latin-1. Force the IBM font here; the probe
     * itself is covered by the font-probe reachability test. */
    vtFontEnc = VT_ENC_CP437;
    VtApplyPersonality();
    probes = 0;
    calls[0] = 0;
}

static void xterm_begin(void)
{
    xterm_begin_on(4, BMF_STANDARD);
}

/* MA2-5 / MA2-11: the engine reads the UTF-8 bytes itself, also when a
 * sequence is split between two packets; the glyph writer gets the CP437
 * code of the code point. */
static void test_ma2_5_11_utf8_reaches_the_grid_whole_even_split_across_writes(void)
{
    int n;
    const vt_cell *row;

    xterm_begin();
    assert(Vt_EngineDecodesUtf8(&prefs));
    vt_put("\xE2\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0x2588 && row[1].ch == ' ');
    assert(probes == 1 && probe[0].code == 0xDB);
    VtClose();

    xterm_begin();
    vt_put("\xE2");
    assert(probes == 0);                        /* half a character draws nothing */
    vt_put("\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0x2588 && row[1].ch == ' ');
    assert(probes == 1 && probe[0].code == 0xDB && probe[0].x == 0);
    VtClose();
}

/* MA2-14 through VtWrite: Unix + IBM PC site; the first valid UTF-8 sequence
 * switches the engine for the rest of the connection, prefs untouched. */
static void unix_cp437_begin(void)
{
    VtSniffReset();
    xterm_begin();
    prefs.Charset = CHARSET_CP437;
    VtApplyPersonality();
}

static void test_ma2_14_stream_switch_to_utf8_through_vtwrite(void)
{
    int n;
    const vt_cell *row;

    unix_cp437_begin();
    vt_put("abc");
    vt_put("\xE2\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch == 0x2588 && row[4].ch == ' ');       /* full block, one cell */
    vt_put("\xE2\x96\x88");                               /* stays UTF-8 afterwards */
    row = vt_row(vtTerm, 0, &n);
    assert(row[4].ch == 0x2588);
    assert(prefs.Charset == CHARSET_CP437);                /* never saved */
    VtClose();

    unix_cp437_begin();                                    /* art first: CP437 stays */
    vt_put("\xC9\xCD\xBB");
    vt_put("\xE2\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0x2554 && row[3].ch != 0x2588);
    VtClose();

    unix_cp437_begin();                                    /* Latin-1 text: never */
    vt_put("\xE9t\xE9");
    vt_put("\xC3\xA9");
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch != 0xE9 || row[4].ch != ' ');
    VtClose();

    unix_cp437_begin();                                    /* new connection re-arms */
    vt_put("a\xE9\xE9");                                   /* E9 E9: invalid, decided */
    VtSniffReset();
    vt_put("\xE2\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch == 0x2588);
    VtClose();
}

/* MA2-14: a valid sequence split across two writes is not drawn as CP437 first;
 * an invalid one still shows both bytes. */
static void test_ma2_14_split_sequence_held_back_through_vtwrite(void)
{
    int n;
    const vt_cell *row;

    unix_cp437_begin();
    vt_put("abc\xE2");
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch == ' ');                              /* lead byte not drawn yet */
    vt_put("\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch == 0x2588 && row[4].ch == ' ');
    VtClose();

    unix_cp437_begin();
    vt_put("abc\xE2");
    vt_put("A");                                           /* invalid: both bytes CP437 */
    row = vt_row(vtTerm, 0, &n);
    assert(row[3].ch == 0x0393 && row[4].ch == 'A' && row[5].ch == ' ');
    VtClose();

    unix_cp437_begin();                                    /* reset drops the held byte */
    vt_put("\xE2");
    VtSniffReset();
    vt_put("\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch != 0x2588);
    VtClose();
}

/* MA2-14: a held prefix lives only within one receive burst. The loop found no
 * more data (VtFlushHeld): a lone Latin-1/CP437 byte is drawn at once, and the
 * sniffer stays undecided; a split full block with no flush in between is whole. */
static void test_ma2_14_flush_draws_held_byte_split_block_still_whole(void)
{
    int n;
    const vt_cell *row;

    unix_cp437_begin();
    vt_put("a\xE9");
    VtFlushHeld();
    row = vt_row(vtTerm, 0, &n);
    assert(row[1].ch == 0x0398 && row[2].ch == ' ');       /* CP437 theta, column 1 */
    vt_put("\xE2\x96\x88");                               /* undecided: a valid one still switches */
    row = vt_row(vtTerm, 0, &n);
    assert(row[2].ch == 0x2588);
    VtClose();

    unix_cp437_begin();
    vt_put("a\xE2");
    vt_put("\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[1].ch == 0x2588 && row[2].ch == ' ');
    VtClose();
}

/* MA2-14: a menu change or settings apply must not undo the switch. */
static void test_ma2_14_apply_personality_keeps_the_switch(void)
{
    int n;
    const vt_cell *row;

    unix_cp437_begin();
    vt_put("\xC3\xA9");                                   /* fires */
    VtApplyPersonality();
    vt_put("\xE2\x96\x88");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0xE9 && row[1].ch == 0x2588);    /* same personality: no reset */
    prefs.Charset = CHARSET_LATIN1;                        /* the user's change wins */
    VtApplyPersonality();
    vt_put("\xE9");
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0xE9);
    VtClose();
}

/* An entry session that ends (or starts) in the same personality: EndEntrySession()
 * applies the global Terminal Type again. The screen, with the session's text and
 * the disconnect note, stays; only a real personality change resets the engine. */
static void test_a_session_end_in_the_same_personality_keeps_the_text(void)
{
    int n;
    const vt_cell *row;

    vt_begin();
    vt_put("KEEPME\r\nConnection closed");
    VtApplyPersonality();                                  /* EndEntrySession() */
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 'K' && row[5].ch == 'E');
    row = vt_row(vtTerm, 1, &n);
    assert(row[0].ch == 'C');
    prefs.TermType = prefs.TermType == TERMTYPE_UNIX ? TERMTYPE_BBS : TERMTYPE_UNIX;
    VtApplyPersonality();                                  /* a real change resets */
    row = vt_row(vtTerm, 0, &n);
    assert(row[0].ch == 0 || row[0].ch == ' ');
    VtClose();
}

/* MA2-6: what the IBM font has is drawn, the rest is one '?' per cell, a wide
 * character fills its second cell with a blank. */
static void test_ma2_6_glyph_map_and_fallback(void)
{
    xterm_begin();
    vt_put("\xE2\x94\x80");                    /* U+2500 */
    assert(probes == 1 && probe[0].code == 0xC4);
    probes = 0;
    vt_put("\xC3\xA9");                         /* U+00E9 */
    assert(probes == 1 && probe[0].code == 0x82);
    probes = 0;
    vt_put("\xE2\x98\x83");                    /* U+2603: no glyph in the font */
    assert(probes == 1 && probe[0].code == '?');
    probes = 0;
    vt_put("\xE4\xB8\xAD");                    /* U+4E2D: two cells wide */
    assert(probes == 2);
    assert(probe[0].code == '?' && probe[0].x == 3);
    assert(probe[1].code == ' ' && probe[1].x == 4);
    VtClose();
}

/* MA2-8: 256-colour and 24-bit red reach the glyph writer as the same pen,
 * the nearest of the sixteen. */
static void test_ma2_8_256_colour_and_24_bit_give_the_same_pen(void)
{
    xterm_begin();
    vt_put("\x1b[38;5;196mA");
    assert(probes == 1 && probe[0].fg == 1);
    probes = 0;
    vt_put("\x1b[38;2;255;0;0mA");
    assert(probes == 1 && probe[0].fg == 1);
    probes = 0;
    vt_put("\x1b[48;5;21mA");
    assert(probes == 1 && probe[0].bg == 4);
    probes = 0;
    vt_put("\x1b[0mA");                          /* the defaults: pens 7 and 0 */
    assert(probes == 1 && probe[0].fg == 7 && probe[0].bg == 0);
    VtClose();
}

/* ---- MA2-9: 24-bit colour on a true-colour (RTG) screen ---- */
static void tc_end(void)
{
    VtClose();
    bm.Depth = 4;
    bitmapFlags = BMF_STANDARD;
}

static void test_ma2_9_rtg_rgb_gets_an_obtained_pen_once_and_releases_it(void)
{
    xterm_begin_on(24, 0);
    vt_put("\x1b[38;2;18;52;86mX");
    assert(obtainCalls == 1);
    assert(obtainR == 0x12121212UL && obtainG == 0x34343434UL && obtainB == 0x56565656UL);
    assert(obtainTags[1] == PRECISION_EXACT && obtainTags[3] == 1);
    assert(probes == 1 && probe[0].fg == 16 && term_pen_map[16] == 50);
    assert(strstr(calls, "fg50 bg0"));                  /* SetAPen got the stub's pen */
    vt_put("\x1b[38;2;18;52;86mY");
    assert(obtainCalls == 1);                           /* cached */
    vt_put("\x1b[38;5;196mZ");                          /* a second colour: a second pen */
    assert(obtainCalls == 2 && probe[2].fg == 17);
    tc_end();
    assert(releaseCalls == 2);                          /* every obtained pen */
    assert(vtTcN == 0);
}

static void test_ma2_9_planar_4_bit_never_asks_for_a_pen(void)
{
    xterm_begin();
    vt_put("\x1b[38;2;255;0;0mA\x1b[38;5;21mB\x1b[31mC");
    assert(obtainCalls == 0);
    assert(probe[0].fg == Vt_PenFor(VT_COLOR_RGB | 0xFF0000UL, prefs.AnsiColors));
    assert(probe[1].fg == Vt_PenFor(21, prefs.AnsiColors));
    assert(probe[2].fg == 1);
    VtClose();
    assert(releaseCalls == 0);
    /* the contrast: the same bytes on a true-colour screen do ask */
    xterm_begin_on(24, 0);
    vt_put("\x1b[38;2;255;0;0mA");
    assert(obtainCalls == 1 && probe[0].fg == 16);
    tc_end();
}

static void test_ma2_9_the_241st_colour_falls_back_to_the_16_pen_answer(void)
{
    char seq[40];
    int i;

    xterm_begin_on(24, 0);
    for (i = 0; i < 240; i++) {
        snprintf(seq, sizeof(seq), "\x1b[38;2;%d;%d;1mX", i, i / 2);
        vt_put(seq);
        calls[0] = 0;                                   /* the log is a fixed buffer */
    }
    assert(obtainCalls == 240);
    probes = 0;
    vt_put("\x1b[38;2;255;0;0mX");
    assert(obtainCalls == 240);                         /* the cache is full: no 241st pen */
    assert(probes == 1 && probe[0].fg == Vt_PenFor(VT_COLOR_RGB | 0xFF0000UL, prefs.AnsiColors));
    tc_end();
    assert(releaseCalls == 240);
}

static void test_ma2_9_a_failed_obtain_falls_back_to_the_16_pen_answer(void)
{
    xterm_begin_on(24, 0);
    obtainFail = 1;
    vt_put("\x1b[38;2;255;0;0mX");
    assert(obtainCalls == 1 && probes == 1);
    assert(probe[0].fg == Vt_PenFor(VT_COLOR_RGB | 0xFF0000UL, prefs.AnsiColors));
    tc_end();
    assert(releaseCalls == 0);
}

/* ---- MA3: mouse, title, bracketed paste through the real entry points ---- */
static void ma3_reset_sentinels(void)
{
    sentLen = sentCalls = titleCalls = 0;
    titleSet[0] = 0;
}

static void test_ma3_1_2_osc_title_reaches_the_window_as_latin1(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    vt_put("\x1b]0;hello\x07");
    assert(titleCalls == 1 && strcmp(titleSet, "hello") == 0);          /* MA3-1 */
    vt_put("\x1b]2;two\x07");
    assert(titleCalls == 2 && strcmp(titleSet, "two") == 0);
    vt_put("\x1b]0;st\x1b\\");                                          /* ST terminator */
    assert(titleCalls == 3 && strcmp(titleSet, "st") == 0);
    vt_put("\x1b]0;caf\xC3\xA9\x07");                                    /* MA3-2: UTF-8 in, Latin-1 out */
    assert(strcmp(titleSet, "caf\xE9") == 0);
    VtClose();
}

static void test_ma3_3_title_is_restored_on_reset_and_session_end(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    vt_put("\x1b]0;hello\x07");
    vt_put("\x1b" "c");                                                  /* RIS */
    assert(strcmp(titleSet, MainWindowTitle) == 0);
    vt_put("\x1b]0;again\x07");
    assert(strcmp(titleSet, "again") == 0);
    VtTitleRestore();                                                     /* the session ended */
    assert(strcmp(titleSet, MainWindowTitle) == 0);
    ma3_reset_sentinels();
    vt_put("after");                                                      /* the old title does not come back */
    assert(titleCalls == 0);
    VtClose();
}

/* LIVE A1200: on DCTelnet's own screen the window is a backdrop with no title
 * bar, so an OSC title must reach the screen bar; on Workbench it must not. */
static void test_ma3_2_title_shows_in_screen_bar_on_own_screen(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    ledCalls = 0;
    window.Flags = WFLG_BACKDROP;
    vt_put("\x1b]0;own screen\x07");
    assert(titleCalls == 1 && screenTitleSet != (const UBYTE *)(size_t)0xFFFFFFFFUL && strcmp((const char *)screenTitleSet, "own screen") == 0);
    assert(ledCalls == 1);                                                /* the bar redraw erased the LEDs */
    window.Flags = 0;
    vt_put("\x1b]0;workbench\x07");
    assert(screenTitleSet == (const UBYTE *)(size_t)0xFFFFFFFFUL && ledCalls == 1);
    VtClose();
}

static void test_ma3_4_5_mouse_press_release_bytes_and_selection_fallthrough(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    assert(!VtMouseOwned(FALSE));                                         /* MA3-5: no mode, selects */
    VtMouseButton(0, TRUE, 10, 5, 0);
    assert(sentCalls == 0);
    vt_put("\x1b[?1000h\x1b[?1006h");
    assert(VtMouseOwned(FALSE) && !VtMouseOwned(TRUE));                   /* Shift keeps selecting */
    VtMouseButton(0, TRUE, 10, 5, 0);                                     /* MA3-4 */
    assert(sentLen == 10 && memcmp(sentBuf, "\x1b[<0;11;6M", 10) == 0);
    sentLen = 0;
    VtMouseButton(0, FALSE, 10, 5, 0);
    assert(sentLen == 10 && memcmp(sentBuf, "\x1b[<0;11;6m", 10) == 0);
    sentLen = 0;
    VtMouseButton(2, TRUE, 10, 5, 0);
    assert(memcmp(sentBuf, "\x1b[<2;11;6M", 10) == 0);
    sentLen = 0;
    VtMouseButton(64, TRUE, 10, 5, 0);
    assert(sentLen == 11 && memcmp(sentBuf, "\x1b[<64;11;6M", 11) == 0);
    VtClose();
}

static void test_ma3_6_motion_modes_and_the_mouse_move_flag(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    window.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEMOVE;
    VtOpen();                                                             /* built-in: no selection to drag */
    assert(!(window.IDCMPFlags & IDCMP_MOUSEMOVE));
    vt_put("\x1b[?1000h\x1b[?1006h");
    assert(!(window.IDCMPFlags & IDCMP_MOUSEMOVE));
    vt_put("\x1b[?1002h");
    assert(window.IDCMPFlags & IDCMP_MOUSEMOVE);
    VtMouseMove(3, 3, 0);                                                 /* no button held: nothing */
    assert(sentCalls == 0);
    VtMouseButton(0, TRUE, 3, 3, 0);
    sentLen = sentCalls = 0;
    VtMouseMove(4, 3, 0);                                                 /* drag */
    assert(sentLen == 10 && memcmp(sentBuf, "\x1b[<32;5;4M", 10) == 0);
    VtMouseButton(0, FALSE, 4, 3, 0);
    sentLen = sentCalls = 0;
    VtMouseMove(5, 3, 0);
    assert(sentCalls == 0);
    vt_put("\x1b[?1002l\x1b[?1003h");
    VtMouseMove(6, 3, 0);                                                 /* ?1003: all motion */
    assert(sentLen == 10 && memcmp(sentBuf, "\x1b[<35;7;4M", 10) == 0);
    vt_put("\x1b[?1003l");
    assert(!(window.IDCMPFlags & IDCMP_MOUSEMOVE));
    VtClose();
}

/* With ?1002/?1003 on and no drag, the host got no motion reports: Intuition
 * delivers IDCMP_MOUSEMOVE only while ReportMouse is on, so it must follow the
 * mode, and a selection drag ending must not turn it off under a live mode. */
static void test_any_motion_mode_turns_report_mouse_on_without_a_drag(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    window.IDCMPFlags = IDCMP_RAWKEY;
    selDragging = FALSE;
    VtOpen();
    assert(!reportMouse);
    vt_put("\x1b[?1003h");
    assert(reportMouse);
    selDragging = TRUE;                                                   /* a drag ends under the mode */
    TerminalMouseReport();
    selDragging = FALSE;
    TerminalMouseReport();
    assert(reportMouse);
    vt_put("\x1b[?1003l\x1b[?1002h");
    assert(reportMouse);
    vt_put("\x1b[?1002l");
    assert(!reportMouse);
    VtClose();
}

/* The Modern bar lights the button under the pointer from the terminal's
 * moves (the terminal keeps the focus): the built-in renderer must not drop
 * IDCMP_MOUSEMOVE while the bar is open, mouse mode or not. */
static void test_modern_tool_bar_hover_keeps_the_mouse_move_flag(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    window.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEMOVE;
    toolBarHover = TRUE;
    VtOpen();
    assert(window.IDCMPFlags & IDCMP_MOUSEMOVE);
    vt_put("\x1b[?1003h\x1b[?1003l");                                    /* a mode ending keeps it */
    assert(window.IDCMPFlags & IDCMP_MOUSEMOVE);
    toolBarHover = FALSE;                                                 /* the bar closed */
    TerminalMouseReport();
    assert(!(window.IDCMPFlags & IDCMP_MOUSEMOVE));
    VtClose();
}

/* The first-run screen mode crash: VtOpen() synced the mouse flags through
 * VtSyncMouseIdcmp(), which called TerminalMouseReport(), which called
 * VtSyncMouseIdcmp() again, until the stack overran. Opening the terminal with
 * a window decides ReportMouse exactly once, and the flags still follow. */
static void test_opening_the_terminal_window_sets_report_mouse_once(void)
{
    xterm_begin();
    ma3_reset_sentinels();
    window.IDCMPFlags = IDCMP_RAWKEY;
    selDragging = FALSE;
    toolBarHover = FALSE;
    reportCalls = 0;
    assert(VtOpen() == 0);
    assert(reportCalls == 1);
    assert(!reportMouse);
    reportCalls = 0;
    vt_put("\x1b[?1003h");                                              /* a write syncs once too */
    assert(reportCalls == 1);
    assert(reportMouse);
    assert(window.IDCMPFlags & IDCMP_MOUSEMOVE);
    VtClose();
}

/* --- MA4: engine scrollback for the built-in renderer --- */

#define SB_ART_ROWS 200

static void sb_begin(ULONG state, ULONG lines)
{
    setup();
    memset(&prefs, 0, sizeof(prefs));
    prefs.State = state;
    prefs.nScrollbackLines = lines;
    assert(VtOpen() == 0);
    probes = 0;
}

/* 200 numbered rows ("L001" ... "L200") through VtWrite, no trailing newline. */
static void sb_art(int rows)
{
    char line[16];
    int i;

    for (i = 1; i <= rows; i++) {
        snprintf(line, sizeof(line), "%sL%03d", i > 1 ? "\r\n" : "", i);
        vt_put(line);
    }
}

/* The text of grid row y as last drawn: the probe's glyphs of that row. */
static void sb_screen_row(int y, char out[5])
{
    int i;

    memset(out, ' ', 4);
    out[4] = 0;
    for (i = 0; i < probes && i < PROBE_MAX; i++)
        if (probe[i].y == y && probe[i].x < 4)
            out[probe[i].x] = (char)probe[i].code;
}

static void sb_expect_rows(int first)
{
    char want[8], got[5];
    int y;

    for (y = 0; y < term_rows; y++) {
        snprintf(want, sizeof(want), "L%03d", first + y);
        sb_screen_row(y, got);
        assert(strcmp(want, got) == 0);
    }
}

static void test_ma4_2_long_art_fills_the_engine_scrollback_and_only_when_asked(void)
{
    int n;
    const vt_cell *r;

    sb_begin(APP_SCROLLBACK_ENABLED, 500);
    assert(newSb == 500 && term_rows == 30);
    sb_art(SB_ART_ROWS);
    assert(vt_scrollback_lines(vtTerm) == SB_ART_ROWS - term_rows);    /* 170 */
    r = vt_row(vtTerm, -(SB_ART_ROWS - term_rows), &n);                  /* the oldest line */
    assert(r && r[0].ch == 'L' && r[1].ch == '0' && r[2].ch == '0' && r[3].ch == '1');
    r = vt_row(vtTerm, 0, &n);                                           /* grid top: row 171 */
    assert(r && r[1].ch == '1' && r[2].ch == '7' && r[3].ch == '1');
    VtClose();

    /* off by default: no scrollback memory is asked for, none is kept */
    sb_begin(0, 500);
    assert(newSb == 0);
    sb_art(SB_ART_ROWS);
    assert(vt_scrollback_lines(vtTerm) == 0);
    VtClose();
    sb_begin(APP_SCROLLBACK_ENABLED, 0);                                  /* on, zero lines */
    assert(newSb == 0);
    VtClose();

    /* the preference is read at every VtOpen (a display reopen) and on the menu
     * toggle (VtApplyScrollback): the live engine takes the new size */
    sb_begin(APP_SCROLLBACK_ENABLED, 40);
    assert(newSb == 40);
    newCalls = 0;
    prefs.State = 0;
    assert(VtOpen() == 0 && vtTermSb == 0);
    prefs.State = APP_SCROLLBACK_ENABLED;
    prefs.nScrollbackLines = 100000UL;
    VtApplyScrollback();
    assert(vtTermSb == 32767);                                           /* clamped to the engine's int */
    assert(newCalls == 0);
    VtClose();
}

/* A1200, built-in renderer, connected: Settings > ScrollBack Lines.. (or the
 * ScrollBack toggle) made a new engine (VtOpen after a full reopen): the
 * screen and the scrollback were gone. The live engine takes the new size in
 * place: the screen stays, and the newest scrollback lines that fit. */
static void test_a_scrollback_size_change_keeps_the_screen_and_the_newest_lines(void)
{
    const vt_cell *r;
    int n;

    sb_begin(APP_SCROLLBACK_ENABLED, 500);
    sb_art(SB_ART_ROWS);                                                 /* grid L171..L200, 170 back */
    VtScrollView(5);                                                     /* the user is looking back */
    newCalls = 0;
    prefs.nScrollbackLines = 40;                                         /* ScrollBack Lines.. */
    VtApplyScrollback();
    assert(newCalls == 0);                                               /* the same engine */
    assert(vtScrollOff == 0);                                            /* back on the live grid */
    assert(vt_scrollback_lines(vtTerm) == 40);
    r = vt_row(vtTerm, 0, &n);                                           /* the screen: still L171 */
    assert(r && r[1].ch == '1' && r[2].ch == '7' && r[3].ch == '1');
    r = vt_row(vtTerm, -40, &n);                                         /* the oldest kept: L131 */
    assert(r && r[1].ch == '1' && r[2].ch == '3' && r[3].ch == '1');

    /* the same through a display reopen (VtOpen): grows back, the lines stay */
    VtReleasePens();
    prefs.nScrollbackLines = 300;
    assert(VtOpen() == 0);
    assert(newCalls == 0 && vtTermSb == 300);
    assert(vt_scrollback_lines(vtTerm) == 40);
    r = vt_row(vtTerm, 0, &n);
    assert(r && r[1].ch == '1' && r[2].ch == '7' && r[3].ch == '1');
    VtClose();
}

static void test_ma4_3_shift_page_up_draws_the_previous_page_and_page_down_returns(void)
{
    char got[5];
    int i;

    sb_begin(APP_SCROLLBACK_ENABLED, 500);
    sb_art(SB_ART_ROWS);                       /* grid shows L171..L200 */
    probes = 0;
    assert(VtScrollKey(0x48, FALSE) == FALSE); /* plain Page Up belongs to the host */
    assert(probes == 0 && vtScrollOff == 0);

    assert(VtScrollKey(0x48, TRUE) == TRUE);   /* sentinel: the scrollback draw ran */
    assert(vtScrollOff == term_rows - 1 && probes == term_rows * term_cols);
    sb_expect_rows(171 - (term_rows - 1));     /* L142 .. L171 */
    assert(cursor_visible == 0);               /* no cursor in the history */

    for (i = 0; i < 20; i++)                   /* far past the top: clamped */
        VtScrollKey(0x48, TRUE);
    assert(vtScrollOff == SB_ART_ROWS - term_rows);
    probes = 0;
    VtScrollView(0);
    VtScrollKey(0x48, TRUE);                   /* already at the top: nothing redrawn */
    assert(probes == 0);
    probes = 0;
    VtScrollView(-vtScrollOff);                /* a full redraw of the top view first */
    VtScrollView(SB_ART_ROWS);
    sb_expect_rows(1);                         /* the art's first row is the top row */

    probes = 0;
    while (vtScrollOff)
        VtScrollKey(0x49, TRUE);
    probes = 0;
    VtScrollView(0);
    VtRedraw();                                /* the live grid, as the engine holds it */
    sb_expect_rows(171);
    assert(vtScrollOff == 0);

    /* output while scrolled back snaps to the live view first */
    VtScrollKey(0x48, TRUE);
    assert(vtScrollOff != 0);
    probes = 0;
    vt_put("\r\nL201");
    assert(vtScrollOff == 0);
    sb_screen_row(term_rows - 1, got);
    assert(strcmp(got, "L201") == 0);

    /* the wheel: 3 lines per click, not while a program owns the mouse */
    assert(VtScrollKey(0x7A, FALSE) == TRUE && vtScrollOff == 3);
    assert(VtScrollKey(0x7B, FALSE) == TRUE && vtScrollOff == 0);
    vt_put("\033[?1000h");
    assert(VtScrollKey(0x7A, FALSE) == FALSE && vtScrollOff == 0);
    VtClose();

    /* scrollback off: the keys are not taken, they go to the host */
    sb_begin(0, 500);
    sb_art(SB_ART_ROWS);
    assert(VtScrollKey(0x48, TRUE) == FALSE && vtScrollOff == 0);
    VtClose();
}

static void test_ma4_3_a_scrollback_line_narrower_than_the_grid_is_blank_filled(void)
{
    sb_begin(APP_SCROLLBACK_ENABLED, 500);
    sb_art(60);
    vt_resize(vtTerm, term_cols - 10, term_rows);
    probes = 0;
    VtDraw(NULL, 0, 0, term_cols, term_rows);
    assert(probes == term_rows * term_cols);
    assert(probe[term_cols - 1].code == ' ' && probe[term_cols - 1].x == term_cols - 1);
    VtClose();
}

static void test_ma4_4_the_alt_screen_leaves_the_scrollback_alone(void)
{
    int before;

    sb_begin(APP_SCROLLBACK_ENABLED, 500);
    sb_art(100);
    before = vt_scrollback_lines(vtTerm);
    assert(before == 100 - term_rows);
    vt_put("\033[?1049h");
    sb_art(SB_ART_ROWS);                       /* a full-screen program scrolling */
    assert(vt_scrollback_lines(vtTerm) == before);
    probes = 0;
    assert(VtScrollKey(0x48, TRUE) == TRUE && vtScrollOff == 0 && probes == 0);  /* nothing to show */
    vt_put("\033[?1049l");
    assert(vt_scrollback_lines(vtTerm) == before);
    VtClose();
}

/* The cursor is a solid block in ANSI bright white with the character in the
 * cell's background pen; on a white background the block is black. Only pens
 * 0-15 (the ANSI set) at any depth, so a 32 colour screen needs no extra pens
 * and the cursor on black is white, not the complement (magenta at 16). */
static void test_cursor_on_a_black_cell_is_white_at_16_and_32_colours_without_extra_pens(void)
{
    UBYTE block, text;
    int bg, p, r;

    for (bg = 0; bg < 16; bg++) {               /* every ANSI background */
        term_cursor_pens((UBYTE)bg, &block, &text);
        assert(block < 16 && text == bg);       /* ANSI pens only: nothing reserved */
        assert(block != bg);                    /* always visible */
    }
    term_cursor_pens(0, &block, &text);
    assert(block == 15 && text == 0);           /* black cell: bright white block */
    term_cursor_pens(7, &block, &text);
    assert(block == 0 && text == 7);            /* white background: black block */
    term_cursor_pens(15, &block, &text);
    assert(block == 0 && text == 15);           /* bright white background: black block */

    /* through the product's entry points, drawn straight into the planes */
    setup();
    assert(VtOpen() == 0);
    vt_write_str("A\x1b[1D");                  /* the cursor on the A, black cell */
    assert(cursor_drawn == 1);
    for (r = 0; r < 8; r++) {
        UBYTE glyph = (UBYTE)('A' ^ (r * 37));
        for (p = 0; p < 4; p++)                 /* glyph pixels pen 0, the rest pen 15 */
            assert(planes[p][(TOP + r) * BPR] == (UBYTE)~glyph);
    }
    assert(calls[0] == 0);                      /* no Complement, no RastPort call */
    cursor_hide();                              /* redrawn from the grid: white A on black */
    for (r = 0; r < 8; r++) {
        UBYTE glyph = (UBYTE)('A' ^ (r * 37));
        for (p = 0; p < 3; p++)
            assert(planes[p][(TOP + r) * BPR] == glyph);
        assert(planes[3][(TOP + r) * BPR] == 0);
    }
    cursor_show();
    vt_write_str("\x1b[47;30mB\x1b[1D");     /* white background: the block is black */
    for (r = 0; r < 8; r++) {
        UBYTE glyph = (UBYTE)('B' ^ (r * 37));
        for (p = 0; p < 4; p++)                 /* the B in pen 7 on pen 0: planes 0-2 the glyph */
            assert(planes[p][(TOP + r) * BPR] == (p < 3 ? glyph : 0));
    }
    VtClose();
}

/* KG-8: a kitty picture that reaches DCTelnet's receive path (VtWrite,
 * what ConWrite calls for the built-in renderer) is parsed and placed by
 * the engine and drawn by VtDrawImages: the sentinel counts its runs, the
 * chunky write gets the pen VtPenFor gives the picture's colour, and the
 * a=q query is answered OK through send_data. */
static void test_kg8_a_kitty_picture_through_vtwrite_is_drawn_and_a_query_answered(void)
{
    char want[64];
    UBYTE pen;

    xterm_begin();
    chunkyCalls = 0;
    vtImgRuns = 0;
    sentLen = 0;
    /* 2 x 2 red pixels, 24-bit RGB, transmitted and shown at the cursor */
    vt_put("\033_Gf=24,s=2,v=2,a=T;/wAA/wAA/wAA/wAA\033\\");
    assert(vt_kitty_placed(vtTerm) == 1);
    assert(vtImgRuns == 2);             /* kitty: one run per pixel row */
    assert(chunkyCalls == 2);
    pen = term_pen_map[VtPenFor((vt_color)(VT_COLOR_RGB | 0xFF0000UL))];
    snprintf(want, sizeof(want), "Chunky(%d,%d,%d,%d pen%d)", TERM_PX(0), TERM_PY(0), TERM_PX(0) + 1, TERM_PY(0), pen);
    assert(strstr(calls, want));
    snprintf(want, sizeof(want), "Chunky(%d,%d,%d,%d pen%d)", TERM_PX(0), TERM_PY(0) + 1, TERM_PX(0) + 1, TERM_PY(0) + 1, pen);
    assert(strstr(calls, want));
    /* the support query: answered, nothing placed */
    vt_put("\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\");
    assert(sentLen > 0 && sentLen < (int)sizeof(sentBuf));
    assert(sentLen == 12 && memcmp(sentBuf, "\033_Gi=31;OK\033\\", 12) == 0);
    assert(vt_kitty_placed(vtTerm) == 1);
}

/* KG-10: VtOpen hands the engine the screen mode's Resolution as the pixel
 * aspect (PAL hires 22:44 style), and square when the mode is unknown; a
 * display reopen on another mode updates it. */
static void test_kg10_the_screens_pixel_aspect_reaches_the_engine(void)
{
    modeRx = 22; modeRy = 44;
    aspectCalls = 0;
    xterm_begin();
    assert(aspectCalls >= 1 && aspectRx == 22 && aspectRy == 44);
    /* reopen on a square-pixel mode: the surviving engine is told again */
    modeRx = 44; modeRy = 44;
    aspectCalls = 0;
    xterm_begin();
    assert(aspectCalls >= 1 && aspectRx == 44 && aspectRy == 44);
    /* unknown mode: square (0) */
    modeRx = 0;
    aspectCalls = 0;
    xterm_begin();
    assert(aspectCalls >= 1 && aspectRx == 0 && aspectRy == 0);
}

/* RIP-H2: a RIP canvas (vtengine image kind 4, z = -1, its own palette, index 0
 * opaque) reaches the screen through VtWrite and VtDrawImages like a sixel:
 * the filled bar gets the pen of its palette colour, and canvas index 0 around
 * it is painted in the default background pen, not left as the text cell. */
static void test_rip_h2_a_rip_canvas_through_vtwrite_is_drawn_with_index_0_opaque(void)
{
    vt_image_view v;
    unsigned long runs0;
    UBYTE barpen, bgpen;

    xterm_begin();
    vt_put("\033c\033[2J");                 /* no picture left from the tests before */
    assert(vt_images(vtTerm) == 0);
    memset(chunkyFb, 0xFF, sizeof(chunkyFb));
    chunkyCalls = 0;
    vtImgRuns = 0;
    prefs.RipMode = RIPMODE_ON;
    VtApplyRip();                           /* the preference, not the engine call, turns RIP on */
    vt_put("\033[2!\r\n");                  /* a RIP line starts a line */
    /* fill style solid, colour 9; bar (10,10)-(100,100) */
    vt_put("!|S0109|B0A0A2S2S\r\n");
    assert(vt_images(vtTerm) == 1);
    runs0 = vtImgRuns;
    assert(runs0 > 0 && chunkyCalls > 0);       /* VtDrawImages ran for the canvas */
    assert(vt_row_image(vtTerm, 0, 0, &v) == 1);
    assert(v.kitty == 0 && v.z < 0 && v.w == 640 && v.h == 350);
    assert(v.pal[9] != 0 && v.pix[50 * 640 + 50] == 9 && v.pix[200 * 640 + 150] == 0);
    barpen = term_pen_map[VtPenFor((vt_color)(VT_COLOR_RGB | (v.pal[9] & 0xFFFFFFUL)))];
    bgpen = term_pen_map[VtPenFor(VT_COLOR_DEFAULT_BG)];
    assert(chunkyFb[(TERM_PY(0) + 50) * FB_W + TERM_PX(0) + 50] == barpen);     /* inside the bar */
    assert(chunkyFb[(TERM_PY(0) + 200) * FB_W + TERM_PX(0) + 150] == bgpen);    /* index 0: painted, not skipped */
    /* a later change redraws through the same path (the canvas serial moved) */
    vtImgRuns = 0;
    vt_put("!|S0101|B0A0A2S2S\r\n");
    assert(vtImgRuns > 0);
}

/* RIP palette entry 0 is a real colour: set non-black, the canvas pixels of
 * index 0 draw with that colour's pen, not the default background pen (only a
 * sixel's index 0 means unset). */
static void test_rip_index_0_is_the_palette_colour_not_the_background(void)
{
    vt_image_view v;
    UBYTE pen0, bgpen;

    xterm_begin();
    vt_put("\033c\033[2J");
    memset(chunkyFb, 0xFF, sizeof(chunkyFb));
    prefs.RipMode = RIPMODE_ON;
    VtApplyRip();
    vt_put("\033[2!\r\n");
    vt_put("!|a0004|S0109|B0A0A2S2S\r\n");   /* entry 0 -> EGA colour 4; a bar of colour 9 */
    assert(vt_row_image(vtTerm, 0, 0, &v) == 1);
    assert(v.kitty == 0 && v.bg0 == 0 && v.pix[200 * 640 + 150] == 0);
    assert((v.pal[0] & 0xFFFFFFUL) != 0);
    pen0 = term_pen_map[VtPenFor((vt_color)(VT_COLOR_RGB | (v.pal[0] & 0xFFFFFFUL)))];
    bgpen = term_pen_map[VtPenFor(VT_COLOR_DEFAULT_BG)];
    assert(pen0 != bgpen);
    assert(chunkyFb[(TERM_PY(0) + 200) * FB_W + TERM_PX(0) + 150] == pen0);
}

/* RIP-H3: the per-site RIPscrip preference reaches the engine. The test drives
 * VtApplyRip (open, menu and site change all call it) and the byte path VtWrite:
 * Off leaves the RIP line as text (no canvas, no parsed command); Auto and On
 * draw it; Off again drops the canvas. vt_rip_commands is the sentinel that the
 * adapter ran. */
static const char ripLine[] = "!|S0109|B0A0A2S2S\r\n";

static void rip_h3_feed_with(UBYTE mode, int *images, long *cmds)
{
    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    assert(vt_images(vtTerm) == 0);
    prefs.RipMode = mode;
    VtApplyRip();
    vt_put(ripLine);
    *images = vt_images(vtTerm);
    *cmds = vt_rip_commands(vtTerm);
}

static void test_rip_h3_the_ripscrip_preference_reaches_the_engine(void)
{
    int images, n;
    long cmds;
    const vt_cell *row;

    rip_h3_feed_with(RIPMODE_OFF, &images, &cmds);
    assert(images == 0 && cmds == 0);
    row = vt_row(vtTerm, 1, &n);
    assert(n >= 3 && row[0].ch == '!' && row[1].ch == '|' && row[2].ch == 'S');   /* kept as text */

    rip_h3_feed_with(RIPMODE_ON, &images, &cmds);
    assert(images == 1 && cmds >= 2);

    rip_h3_feed_with(RIPMODE_AUTO, &images, &cmds);     /* the default; the engine treats a valid line as RIP */
    assert(images == 1 && cmds >= 2);

    /* Auto stays text until the host speaks RIP; On is active at once */
    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    prefs.RipMode = RIPMODE_OFF;                        /* a fresh adapter: On -> Auto keeps the engine active */
    VtApplyRip();
    prefs.RipMode = RIPMODE_AUTO;
    VtApplyRip();
    assert(vt_rip_active(vtTerm) == 0);
    vt_put("plain text\r\n");
    assert(vt_rip_active(vtTerm) == 0 && vt_images(vtTerm) == 0);
    vt_put("\033[!");
    assert(vt_rip_active(vtTerm) == 1);
    vt_put(ripLine);
    assert(vt_images(vtTerm) == 1);
    prefs.RipMode = RIPMODE_ON;
    VtApplyRip();
    assert(vt_rip_active(vtTerm) == 1);
    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    prefs.RipMode = RIPMODE_OFF;
    VtApplyRip();
    prefs.RipMode = RIPMODE_AUTO;
    VtApplyRip();
    vt_put(ripLine);                                    /* a valid !| line also starts Auto */
    assert(vt_rip_active(vtTerm) == 1 && vt_images(vtTerm) == 1);

    prefs.RipMode = RIPMODE_OFF;                        /* changed while connected */
    VtApplyRip();
    assert(vt_rip_commands(vtTerm) == 0);               /* the adapter is gone: the next RIP line stays text */
    images = vt_images(vtTerm);
    vt_put(ripLine);
    assert(vt_images(vtTerm) == images);
}

/* The per-site RIPscrip Fonts preference reaches the engine through VtApplyRip:
 * Stroke draws RIP font style 1 with Hershey strokes (the sentinel counts the
 * stroke glyphs), Bitmap with the 8x8 font, and the canvas pixels differ. */
static unsigned long rip_font_canvas_sum(UBYTE fonts, long *strokeGlyphs)
{
    vt_image_view v;
    unsigned long sum = 0;
    long i, g0 = vt_rip_stroke_glyphs();

    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    prefs.RipMode = RIPMODE_OFF;
    VtApplyRip();
    prefs.RipMode = RIPMODE_ON;
    prefs.RipFonts = fonts;
    VtApplyRip();
    vt_put("!|c0F|Y01000200|@2S3CHgRip, a|TZy&\r\n");   /* style 1 (triplex), size 2 */
    assert(vt_row_image(vtTerm, 0, 0, &v) == 1);
    for (i = 0; i < (long)v.w * v.h; i++)
        sum = sum * 31 + v.pix[i];
    *strokeGlyphs = vt_rip_stroke_glyphs() - g0;
    return sum;
}

static void test_rip_fonts_preference_reaches_the_engine(void)
{
    long strokeN, bitmapN;
    unsigned long stroke = rip_font_canvas_sum(RIPFONT_STROKE, &strokeN);
    unsigned long bitmap = rip_font_canvas_sum(RIPFONT_BITMAP, &bitmapN);

    assert(strokeN > 0 && bitmapN == 0);
    assert(stroke != bitmap);
}

/* H4: ESC[! through the data path (VtWrite) on Auto switches to the site's RIP
 * screen once, a plain text session never does, RIP ending (ESC[1!) or the
 * session ending puts the previous screen back. */
static void test_rip_h4_the_screen_switch_is_wired_through_the_data_path(void)
{
    const ULONG siteMode = 0x29000UL, appMode = 0x11000UL;

    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    VtRipEnd();
    prefs.RipMode = RIPMODE_OFF;
    VtApplyRip();
    prefs.RipMode = RIPMODE_AUTO;
    VtApplyRip();
    prefs.DisplayID = appMode;
    prefs.State = (prefs.State & ~APP_RENDERER_ALL) | APP_RENDERER_CONSOLE_DEVICE;
    prefs.RipScreen = RIPSCREEN_MODE;
    prefs.RipDisplayID = siteMode; prefs.RipWidth = 640; prefs.RipHeight = 480;
    ripReopens = 0; ripDefaultAsked = 0;

    vt_put("plain text\r\nmore \033[1m bold\033[0m\r\n");
    assert(ripReopens == 0 && prefs.DisplayID == appMode);

    vt_put("\033[!");
    assert(vt_rip_active(vtTerm) == 1);
    assert(ripReopens == 1);                                /* the sentinel: the hook ran, once */
    assert(ripReopenWas == appMode && ripReopenDisplayID == siteMode);
    assert(ripReopenRenderer == APP_RENDERER_BUILTIN);      /* D4: the built-in renderer */
    assert(prefs.DisplayWidth == 640 && prefs.DisplayHeight == 480);
    assert(ripDefaultAsked == 0);                           /* a chosen mode needs no default */
    vt_put("more RIP\r\n");
    assert(ripReopens == 1);                                /* not again while it stays active */

    vt_put("\033[1!");                                      /* RIP off: the screen as it was */
    assert(vt_rip_active(vtTerm) == 0);
    assert(ripReopens == 2 && prefs.DisplayID == appMode);
    assert((prefs.State & APP_RENDERER_ALL) == APP_RENDERER_CONSOLE_DEVICE);

    prefs.RipScreen = RIPSCREEN_DEFAULT;                    /* no site mode: the default rule decides */
    vt_put("\033[!");
    assert(ripReopens == 3 && ripDefaultAsked == 1 && prefs.DisplayID == 0x91000UL);
    VtRipEnd();                                             /* the session ends */
    assert(ripReopens == 4 && prefs.DisplayID == appMode);
    assert((prefs.State & APP_RENDERER_ALL) == APP_RENDERER_CONSOLE_DEVICE);
    prefs.RipScreen = RIPSCREEN_DEFAULT;
    prefs.State = (prefs.State & ~APP_RENDERER_ALL) | APP_RENDERER_BUILTIN;
}

/* The RIP switch is temporary for the run: in a quick connect (no entry
 * session) the live prefs are what SavePrefs writes, so what it would save
 * must still be the pre-RIP screen and renderer while RIP is active. */
static void test_rip_h4_the_screen_switch_never_reaches_the_prefs_file(void)
{
    const ULONG siteMode = 0x29000UL, appMode = 0x11000UL;
    static struct PrefsStruct toSave;

    xterm_begin();
    vt_put("\033c\033[2J\r\n");
    VtRipEnd();
    prefs.RipMode = RIPMODE_AUTO;
    VtApplyRip();
    prefs.DisplayID = appMode; prefs.DisplayWidth = 800; prefs.DisplayHeight = 600; prefs.DisplayDepth = 8;
    prefs.State = (prefs.State & ~APP_RENDERER_ALL) | APP_RENDERER_CONSOLE_DEVICE;
    prefs.RipScreen = RIPSCREEN_MODE;
    prefs.RipDisplayID = siteMode; prefs.RipWidth = 640; prefs.RipHeight = 480;
    sessionSettingsId = 0;
    SitePrefs_HandInit(&handChanges, &prefs);
    ripReopens = 0;

    vt_put("\033[!");
    assert(ripReopens == 1 && prefs.DisplayID == siteMode);     /* the sentinel: the switch ran */
    SitePrefs_ForSave(&toSave, &prefs, &prefs, FALSE);          /* SavePrefs, quick connect */
    SitePrefs_HandForSave(&handChanges, &toSave);
    assert(toSave.DisplayID == appMode);
    assert(toSave.DisplayWidth == 800 && toSave.DisplayHeight == 600 && toSave.DisplayDepth == 8);
    assert((toSave.State & APP_RENDERER_ALL) == APP_RENDERER_CONSOLE_DEVICE);

    vt_put("\033[1!");                                          /* RIP off: nothing left to take back */
    assert(prefs.DisplayID == appMode);
    SitePrefs_ForSave(&toSave, &prefs, &prefs, FALSE);
    SitePrefs_HandForSave(&handChanges, &toSave);
    assert(toSave.DisplayID == appMode && memcmp(&toSave, &prefs, sizeof(toSave)) == 0);
    prefs.RipScreen = RIPSCREEN_DEFAULT;
    prefs.State = (prefs.State & ~APP_RENDERER_ALL) | APP_RENDERER_BUILTIN;
}

int main(void)
{
    test_the_terminal_starts_below_the_title_bar_and_matches_the_window_rows();
    test_cursor_on_a_black_cell_is_white_at_16_and_32_colours_without_extra_pens();
    test_a_covered_terminal_draws_only_through_the_rastport();
    test_uncovered_again_it_draws_directly();
    test_an_rtg_bitmap_always_takes_the_rastport();
    test_the_pen_map_colours_the_rastport_path_and_stops_direct_drawing();
    test_an_8_plane_screen_takes_the_rastport();
    test_a_16_pixel_font_gives_40_columns();
    test_vtopen_twice_never_draws_through_the_previous_engine();
    test_a_display_reopen_keeps_the_text_and_draws_it_again();
    test_a_reopen_in_another_display_mode_starts_blank();
    test_a_menu_cannot_open_while_the_terminal_draws_into_the_bitmap();
    test_a_menu_open_over_the_terminal_is_drawn_around();
    test_a_menu_opened_while_an_answer_is_sent_is_drawn_around();
    test_ma1_2_line_art_lands_in_the_grid_and_reaches_the_glyph_writer();
    test_ma1_3_blocks_and_shades_map_to_themselves_never_to_the_fallback();
    test_ma1_1_latin1_byte_reaches_the_glyph_writer_unchanged();
    test_ma1_4_sixteen_colours();
    test_ma1_5_6_7_ice_colours_decide_what_blink_draws_as();
    test_ma1_8_cursor_save_restore_position_and_erase();
    test_ma1_init_strings_draw_nothing_and_leave_the_cursor();
    test_ma1_9_the_sample_art_file_gives_the_known_grid();
    test_font_probe_reaches_the_grid_through_vtwrite();
    test_a_drawn_cell_on_a_covered_terminal_goes_out_as_rectfills();
    test_ma2_5_11_utf8_reaches_the_grid_whole_even_split_across_writes();
    test_ma2_14_stream_switch_to_utf8_through_vtwrite();
    test_ma2_14_split_sequence_held_back_through_vtwrite();
    test_ma2_14_flush_draws_held_byte_split_block_still_whole();
    test_ma2_14_apply_personality_keeps_the_switch();
    test_a_session_end_in_the_same_personality_keeps_the_text();
    test_ma2_6_glyph_map_and_fallback();
    test_ma2_8_256_colour_and_24_bit_give_the_same_pen();
    test_ma2_9_rtg_rgb_gets_an_obtained_pen_once_and_releases_it();
    test_ma2_9_planar_4_bit_never_asks_for_a_pen();
    test_ma2_9_the_241st_colour_falls_back_to_the_16_pen_answer();
    test_ma2_9_a_failed_obtain_falls_back_to_the_16_pen_answer();
    test_ma3_1_2_osc_title_reaches_the_window_as_latin1();
    test_ma3_3_title_is_restored_on_reset_and_session_end();
    test_ma3_2_title_shows_in_screen_bar_on_own_screen();
    test_ma3_4_5_mouse_press_release_bytes_and_selection_fallthrough();
    test_ma3_6_motion_modes_and_the_mouse_move_flag();
    test_any_motion_mode_turns_report_mouse_on_without_a_drag();
    test_modern_tool_bar_hover_keeps_the_mouse_move_flag();
    test_opening_the_terminal_window_sets_report_mouse_once();
    test_ma4_2_long_art_fills_the_engine_scrollback_and_only_when_asked();
    test_a_scrollback_size_change_keeps_the_screen_and_the_newest_lines();
    test_ma4_3_shift_page_up_draws_the_previous_page_and_page_down_returns();
    test_ma4_3_a_scrollback_line_narrower_than_the_grid_is_blank_filled();
    test_ma4_4_the_alt_screen_leaves_the_scrollback_alone();
    test_kg8_a_kitty_picture_through_vtwrite_is_drawn_and_a_query_answered();
    test_kg10_the_screens_pixel_aspect_reaches_the_engine();
    test_rip_h2_a_rip_canvas_through_vtwrite_is_drawn_with_index_0_opaque();
    test_rip_index_0_is_the_palette_colour_not_the_background();
    test_rip_h3_the_ripscrip_preference_reaches_the_engine();
    test_rip_fonts_preference_reaches_the_engine();
    test_rip_h4_the_screen_switch_is_wired_through_the_data_path();
    test_rip_h4_the_screen_switch_never_reaches_the_prefs_file();
    printf("term_engine: all assertions passed\n");
    return 0;
}
