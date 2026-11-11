/* --- the built-in renderer's terminal: vtcon parses, retro32-term draws ---
 *
 * Included by DCTelnet.c after term-engine.c, whose drawing it uses: the
 * glyph writer, the cell-aligned fill and scroll, the cursor flip and the
 * covered/RastPort switch stay exactly as they were. What changed is the
 * parser: bytes from the BBS go to vtcon's engine (third_party/vtcon, the
 * pcansi personality: ANSI.SYS / BBS art), which keeps the cell grid and
 * tells this file what to draw:
 *
 * - damage: cells whose content changed, drawn from the grid, one glyph
 *   each through term_glyph() with the cell's resolved pens;
 * - scroll: rows that moved, one term_rect_scroll(), the vacated rows in
 *   the default background (the engine's scroll contract);
 * - reply: DSR / DA answers, sent to the BBS.
 *
 * The engine orders every scroll before the damage that follows it, so a
 * damaged cell is drawn at once from the grid as it is now.
 *
 * term-engine.c's own parser (term_feed) is no longer called. Its cursor
 * position (cur_x, cur_y) is kept equal to the engine's after every write,
 * because the cursor flip and the RastPort glyph runs read it.
 */
#include "vtengine.h"
#include "glyphmap.h"
#include "vtpersonality.h"
#include "vtpens.h"
#include "pen_rule.h"
#include "pic_pens.h"
#include "vtinput.h"
#include "glyphraster.h"
#include "vtdownload.h"
#include "site_prefs.h"
#include "screenfont.h"

/* The glyph for a code point the font cannot show: the font's '?', one cell. */
#define VT_FALLBACK_GLYPH ((UBYTE)'?')

static vt_term *vtTerm;

/* The encoding of the terminal's font (VtProbeFont) and the one the glyph map
 * is asked for (Vt_GlyphEncoding: CP437 sessions stay byte pass-through). */
static enum vt_font_enc vtFontEnc;
static enum vt_font_enc vtGlyphEnc;

/* MA4: how many lines the view is scrolled back into the engine's scrollback;
 * 0 is the live grid. Grid row y shows engine row y - vtScrollOff. */
static int vtScrollOff;

/* Engine scrollback lines: on in a first run's prefs (Prefs_FirstRunState());
 * with the preference off the engine allocates none. */
static int VtScrollbackCap(void)
{
    if (!(prefs.State & APP_SCROLLBACK_ENABLED))
        return 0;
    return prefs.nScrollbackLines > 32767UL ? 32767 : (int)prefs.nScrollbackLines;
}

/* MA2-9: on a screen with more than 4 planes (32 to 256 colour palette
 * screens, RTG true or high colour; not HAM, not Extra Half-Brite, graphics
 * V39 or later: Vt_ObtainsPens()) a 256-colour or RGB cell
 * gets a pen of its own colour from ObtainBestPen(), cached per RGB in slots
 * 16-255 of the engine's pen map. The rule is vtcon's pen_rule.h, the one
 * UP-Term uses for its palette screens: an exact entry while the colour map
 * has a free one, else the nearest pen it has. Every other screen, the
 * default colours and c < 16 keep Vt_PenFor() and never call it (the 68000
 * path). */
#define VT_TC_MAX 240
static struct { ULONG rgb; LONG pen; } vtTc[VT_TC_MAX];
static int vtTcN, vtTcLast;
static BOOL vtTrueColour;

/* pen_rule.h's best(): the screen's ObtainBestPen, exact (OBP_FailIfBad) or
 * the nearest pen. Same call as vtcon's render/pens_amiga.c vr_pen_best. */
static long VtPenBest(void *u, unsigned long rgb, int exact)
{
    return (long)ObtainBestPen(scr->ViewPort.ColorMap,
                               ((rgb >> 16) & 0xFF) * 0x01010101UL,
                               ((rgb >> 8) & 0xFF) * 0x01010101UL,
                               (rgb & 0xFF) * 0x01010101UL,
                               OBP_Precision, exact ? PRECISION_EXACT : PRECISION_IMAGE,
                               OBP_FailIfBad, exact ? TRUE : FALSE, TAG_END);
}

/* The pen to draw colour c with. */
static UBYTE VtPenFor(vt_color c)
{
    ULONG rgb;
    LONG pen;
    int i;

    if (!vtTrueColour || c < 16 || c == VT_COLOR_DEFAULT || c == VT_COLOR_DEFAULT_BG)
        return Vt_PenFor(c, prefs.AnsiColors);
    rgb = c < 256 ? Vt_Xterm256Rgb((int)c) : (ULONG)VT_RGB_OF(c);
    if (vtTcN && vtTc[vtTcLast].rgb == rgb)
        return (UBYTE)(16 + vtTcLast);
    for (i = 0; i < vtTcN; i++) {
        if (vtTc[i].rgb == rgb) {
            vtTcLast = i;
            return (UBYTE)(16 + i);
        }
    }
    if (vtTcN >= VT_TC_MAX)
        return Vt_PenFor(c, prefs.AnsiColors);
    pen = vr_obtain_rule(VtPenBest, NULL, rgb);
    if (pen < 0)
        return Vt_PenFor(c, prefs.AnsiColors);
    vtTc[vtTcN].rgb = rgb;
    vtTc[vtTcN].pen = pen;
    term_set_pen((UBYTE)(16 + vtTcN), (UBYTE)pen);
    vtTcLast = vtTcN;
    return (UBYTE)(16 + vtTcN++);
}

/* KG: pictures (kitty graphics, APC G; sixel uses the same store). The
 * engine parses, decodes (its own inflate and PNG) and places them; this
 * draws their tiles over the cells VtDraw drew. Ported from vtcon's
 * render/amiga_render.c (draw_images, img_run, img_cell, img_write_idx,
 * img_write_idx_keep, under_text) onto retro32-term's coordinates and
 * DCTelnet's pens: a picture colour gets the pen VtPenFor gives the same
 * RGB text colour (an obtained pen on a screen over 4 planes, else the nearest of the
 * sixteen ANSI pens). */
#define VT_IMG_W 1280                   /* the widest run, in pixels */
static UBYTE vtImgBuf[8 * VT_IMG_W];    /* one cell row of a run: 8 pixels high */
static UBYTE vtImgMap[256];             /* the picture's palette index -> screen pen */
static long vtImgSerial = -1;           /* the picture vtImgMap was made for */
static unsigned long vtImgRuns;         /* sentinel: picture runs written */
static unsigned long vtImgFills;        /* sentinel: flat runs sent out as RectFill */
/* Plan H2c: a row's run of one pen at least this many pixels long goes to the
 * blitter as a RectFill; only the mixed spans between go through the chunky blit. */
#define VT_IMG_FLAT_MIN 32
/* Plan H2c: --replay times the OS blit calls alone (E-clock counts summed while
 * vtImgTime is set), so a redraw splits into blit time and the rest. */
extern struct Device *TimerBase;
static BOOL vtImgTime;
static ULONG vtImgBlitCounts;

/* E-clock counts over n runs as microseconds per run, without 64-bit maths.
 * (counts / n) * 1000 wraps past 2^32 at about 6 s per run on a 709379 Hz
 * PAL E-clock (an A500 RIP redraw is slower than that), so the whole seconds
 * and the remainder are scaled apart: no overflow below about 4294 s. */
static ULONG VtEClockUs(ULONG counts, ULONG n, ULONG perSecond)
{
    ULONG c, r;

    if (!n || !perSecond)
        return 0;
    c = counts / n;
    r = c % perSecond;
    return (c / perSecond) * 1000000UL + ((r * 1000UL) / perSecond) * 1000UL
         + (((r * 1000UL) % perSecond) * 1000UL) / perSecond;
}

/* FNV-1a over the RIPscrip canvas, one byte a pixel: the same sum as vtcon's
 * tests/test_rip.c canvas_sum, so a run here compares with its goldens. 0 when
 * the terminal holds no canvas. Used by `DCTelnet --replay`. */
static ULONG VtCanvasSum(void)
{
    vt_image_view v;
    ULONG h = 2166136261UL;
    LONG i, n;

    if (!vtTerm || !vt_row_image(vtTerm, 0, 0, &v))
        return 0;
    n = (LONG)v.w * v.h;
    for (i = 0; i < n; i++)
        h = (h ^ v.pix[i]) * 16777619UL;
    return h;
}

/* Pictures always go through the window's RastPort: the direct planar path
 * writes 4-bit colour numbers only. Uncovered, retro32-term draws glyphs
 * straight into the planes (term_rp NULL) but its window RastPort and the
 * TERM_PX / TERM_PY offsets stay valid for it. */
static struct RastPort *VtImgRp(void)
{
    return term_rp ? term_rp : term_win_rp;
}

/* Kitty pictures on a palette screen of 5 to 7 planes (32 to 128 colours). DCTelnet keeps pens 0-19 for itself
 * (PEN_EXCLUSIVE), so a 5-plane screen has 12 free pens: a picture's palette (up to 256 colours, kit_quant) cannot
 * be given pens first-come, or it shows the colours of its top rows only. Like vtcon's render/amiga_render.c
 * (pic_make, pic_slot, pic_sweep, img_write_idx) the picture instead keeps its own RGB (vt_set_image_rgb_copy),
 * the shared picker (render/pic_pens.h pp_hist, pp_pick) gives the free pens to the colours covering most of it,
 * and every pixel is dithered from its own colour onto {the 16 ANSI pens + those pens}. These pens live in a set of
 * their own (VtPic slots), not in vtTc, and go back when their picture does (replaced, deleted, VtReleasePens). */
#define VT_PIC_SLOTS 4
struct VtPic {
    long serial;                        /* the picture this slot was made for; 0 free */
    long key;                           /* what the slot matches on: the serial, a RIP canvas's palette hash (VtImgKey) */
    unsigned long stamp;                /* last use, for the least recently used */
    UBYTE held[PM_MAXPENS];             /* the pens obtained for the picture (to release) */
    int nheld;
    UBYTE cpen[PM_MAXPENS];             /* candidate -> screen pen: the ANSI pens, then held */
    unsigned long usable;               /* candidate bit mask for pm_match */
    UBYTE *mem;                         /* pm_palette, then PP_BINS * 3 plan bytes, then PP_BINS * 3 bin colours; an
                                         * indexed (sixel) picture: the pm_palette, then a pp_plan whose a, b are screen pens */
    BOOL indexed;                       /* made from the picture's palette (sixel), not from its RGB copy (kitty) */
};
static struct VtPic vtPic[VT_PIC_SLOTS];
static struct VtPic *vtPicCur;          /* the slot of the picture being drawn; 0 the plain palette map */
static unsigned long vtPicUse, vtPicFrees;
static BOOL vtPicPens;                  /* this screen picks pens for pictures */
static unsigned long vtPicPicks;        /* sentinel: pictures whose pens were picked (pp_pick) */
static unsigned long vtPicFree;         /* pens a picture may hold on this screen: 2^depth less DCTelnet's 20 */
static unsigned long vtPicWt[PP_MAXCOL]; /* pixels per palette entry of the sixel picture being made (VtSixNeedsPicker) */

static void VtPicRelease(struct VtPic *s)
{
    int i;

    for (i = 0; i < s->nheld; i++)
        ReleasePen(scr->ViewPort.ColorMap, (ULONG)s->held[i]);
    s->nheld = 0;
    s->serial = 0;
    s->key = 0;
    if (s->mem) {
        FreeVec(s->mem);
        s->mem = NULL;
    }
}

static void VtPicReleaseAll(void)
{
    int i;

    for (i = 0; i < VT_PIC_SLOTS; i++)
        VtPicRelease(&vtPic[i]);
    vtPicCur = NULL;
    vtImgSerial = -1;
    vtPicFrees = 0;     /* a new engine counts its frees from 0 */
}

/* A picture goes (deleted, replaced, a cleared screen): its pens go with it. One counter compare per pass. */
static void VtPicSweep(void)
{
    int i;
    unsigned long f;

    if (!vtTerm)
        return;
    f = vt_image_frees(vtTerm);
    if (f == vtPicFrees)
        return;
    vtPicFrees = f;
    for (i = 0; i < VT_PIC_SLOTS; i++)
        if (vtPic[i].serial && !vt_image_alive(vtTerm, vtPic[i].serial)) {
            if (vtPicCur == &vtPic[i]) {
                vtPicCur = NULL;
                vtImgSerial = -1;
            }
            VtPicRelease(&vtPic[i]);
        }
}

/* ANSI pen colour n (RGB4 in the preferences) as 0xRRGGBB */
static unsigned long VtAnsiRgb(int n)
{
    UWORD c = prefs.AnsiColors[n];

    return ((unsigned long)((c >> 8) & 15) * 17UL << 16) | ((unsigned long)((c >> 4) & 15) * 17UL << 8) |
           (unsigned long)(c & 15) * 17UL;
}

#define VT_RIP_NPAL 16                  /* a RIP canvas: 16 palette entries, index 0 a real colour, z -1 */

/* A RIP canvas (not a sixel or a kitty picture). */
static BOOL VtIsRipCanvas(const vt_image_view *v)
{
    return !v->kitty && !v->bg0 && v->fmt == 0 && v->z < 0 && v->npal == VT_RIP_NPAL;
}

/* What a picture's pens are made for. A RIP canvas gets a new serial with every command (the image changes under
 * the same palette), so its key is the palette: pens are picked again only when the 16 colours change. Anything
 * else: the serial. */
static long VtImgKey(const vt_image_view *v)
{
    unsigned long h = 2166136261UL;
    int i;

    if (!VtIsRipCanvas(v))
        return v->serial;
    for (i = 0; i < VT_RIP_NPAL; i++)
        h = (h ^ (v->pal[i] & 0xFFFFFFUL)) * 16777619UL;
    return (long)((h & 0x3FFFFFFFUL) | 0x40000000UL);     /* clear of the small serials */
}

static BOOL VtPicMake(struct VtPic *s, const vt_image_view *v)
{
    unsigned long *wt = vtPicWt;
    unsigned long cand[PM_MAXPENS], pick[PM_MAXPENS], pal[PP_MAXCOL];
    pp_hist_work *wk;
    pm_palette *cp;
    UBYTE *plan3;
    int i, nf = 0, nc, got, np;
    LONG p;

    s->nheld = 0;
    s->indexed = v->rgb == NULL;
    s->mem = (UBYTE *)AllocVec(sizeof(pm_palette) + PP_BINS * 6, MEMF_ANY | MEMF_CLEAR);
    wk = s->indexed ? NULL : (pp_hist_work *)AllocVec(sizeof(pp_hist_work), MEMF_ANY);
    if (!s->mem || (!wk && !s->indexed)) {
        if (wk)
            FreeVec(wk);
        if (s->mem) {
            FreeVec(s->mem);
            s->mem = NULL;
        }
        return FALSE;
    }
    if (s->indexed) {                   /* a sixel: the palette is the colour list, vtPicWt (VtSixNeedsPicker) its weights */
        np = v->npal < PP_MAXCOL ? v->npal : PP_MAXCOL;
        wt[0] = 0;
        for (i = 0; i < np; i++)
            pal[i] = v->pal[i] & 0xFFFFFFUL;
    } else {
        np = pp_hist(v->pix, v->rgb, (long)v->w * v->h, wk, pal, wt, s->mem + sizeof(pm_palette) + PP_BINS * 3);
        FreeVec(wk);
    }
    vtPicPicks++;
    for (i = 0; i < 16 && nf < PM_MAXPENS; i++) {
        s->cpen[nf] = term_pen_map[i];
        cand[nf++] = VtAnsiRgb(i);
    }
    got = pp_pick(pal, wt, np, cand, nf, PM_MAXPENS - nf, pick);
    for (i = 0; i < got; i++) {         /* heaviest first: the free pens run out on the lightest */
        p = VtPenBest(NULL, pick[i], 1);
        if (p < 0)
            break;
        s->held[s->nheld++] = (UBYTE)p;
    }
    if (i < got) {                      /* fewer pens than colours: cut again for the number there are */
        int k = i;

        for (i = 0; i < s->nheld; i++)
            ReleasePen(scr->ViewPort.ColorMap, (ULONG)s->held[i]);
        s->nheld = 0;
        got = pp_pick(pal, wt, np, cand, nf, k, pick);
        for (i = 0; i < got; i++) {
            p = VtPenBest(NULL, pick[i], 1);
            if (p < 0)
                break;
            s->held[s->nheld++] = (UBYTE)p;
        }
        got = i;
    }
    nc = nf;
    for (i = 0; i < got; i++) {
        s->cpen[nc] = s->held[i];
        cand[nc++] = pick[i];
    }
    cp = (pm_palette *)s->mem;
    plan3 = s->mem + sizeof(pm_palette);
    if (s->indexed) {                   /* every palette entry's two pens, once: a, b as screen pens */
        pp_plan *pl = (pp_plan *)plan3;

        pp_plan_make(pl, cp, pal, np, cand, nc);
        for (i = 1; i < np; i++) {
            pl->a[i] = s->cpen[pl->a[i]];
            pl->b[i] = s->cpen[pl->b[i]];
        }
    } else {
        pm_palette_set(cp, cand, nc);
        s->usable = nc >= 32 ? 0xFFFFFFFFUL : ((1UL << nc) - 1);
        for (i = 0; i < PP_BINS; i++)
            plan3[i * 3 + 2] = PP_UNSET;
    }
    s->serial = v->serial;
    s->key = VtImgKey(v);
    return TRUE;
}

/* The slot of picture v: the one made for it, else the least recently used, made now. NULL: none (no memory) */
static struct VtPic *VtPicSlot(const vt_image_view *v)
{
    int i, old = 0;
    long key = VtImgKey(v);

    for (i = 0; i < VT_PIC_SLOTS; i++) {
        if (vtPic[i].key == key) {
            vtPic[i].serial = v->serial;        /* the same pens, the picture that has them now (VtPicSweep) */
            vtPic[i].stamp = ++vtPicUse;
            return &vtPic[i];
        }
        if (vtPic[i].key == 0 || vtPic[i].stamp < vtPic[old].stamp)
            old = i;
    }
    VtPicRelease(&vtPic[old]);
    if (!VtPicMake(&vtPic[old], v))
        return NULL;
    vtPic[old].stamp = ++vtPicUse;
    return &vtPic[old];
}

/* A sixel needs the picker when it uses more colours than the pens left free: below that the plain per-colour pens
 * are exact. Leaves the palette weights in vtPicWt for VtPicMake. */
static BOOL VtSixNeedsPicker(const vt_image_view *v)
{
    int i, used = 0, np = v->npal < PP_MAXCOL ? v->npal : PP_MAXCOL;

    if (VtIsRipCanvas(v)) {             /* its pens are keyed on the palette (VtImgKey), so the weights come from it too */
        vtPicWt[0] = 0;
        for (i = 1; i < np; i++)
            vtPicWt[i] = 1;
    } else
        pp_weights(v->pix, (long)v->w * v->h, np, vtPicWt);
    for (i = 1; i < np; i++)
        used += vtPicWt[i] != 0;
    return (unsigned long)used + vtTcN > vtPicFree;
}

/* RIP colours on the screen's own pens. While a RIP canvas is up, the screen's pens 0-15 hold the RIP palette
 * (VtRipShowColours: DCTelnet.c) and RIP colour i is drawn with pen i, so the user's ANSI palette cannot turn
 * an EGA colour into another one. vtRipPens: they are loaded; the user's palette comes back when the canvas goes
 * or any other picture is drawn (VtRipRestoreColours), and DCTelnet.c's LoadAnsiPalette tells when it
 * overwrote them (VtRipColoursLost). */
static BOOL vtRipPens;
static unsigned long vtRipPensLoads;    /* sentinel: the RIP palette went onto the pens */

static void VtRipColoursLost(void)
{
    vtRipPens = FALSE;
    vtImgSerial = -1;
}

static void VtRipPensRestore(void)
{
    if (!vtRipPens)
        return;
    vtRipPens = FALSE;
    vtImgSerial = -1;
    VtRipRestoreColours();
}

static void VtImgColours(const vt_image_view *v)
{
    int i;
    long key = VtImgKey(v);

    if (vtImgSerial == key)
        return;
    if (!VtIsRipCanvas(v))
        VtRipPensRestore();
    vtImgSerial = key;
    if (VtIsRipCanvas(v)) {
        ULONG rgb[VT_RIP_NPAL];

        for (i = 0; i < VT_RIP_NPAL; i++)
            rgb[i] = v->pal[i] & 0xFFFFFFUL;
        if (VtRipShowColours(rgb)) {
            vtRipPens = TRUE;
            vtRipPensLoads++;
            vtPicCur = NULL;
            for (i = 0; i < VT_RIP_NPAL; i++)
                vtImgMap[i] = term_pen_map[i];
            return;
        }
    }
    /* A sixel's index 0 is its unset pixels (the default background); a RIP
     * canvas or a kitty picture reads it as a palette colour. */
    vtImgMap[0] = (!v->kitty && !v->bg0) ? term_pen_map[VtPenFor((vt_color)(VT_COLOR_RGB | (v->pal[0] & 0xFFFFFFUL)))]
                                         : term_pen_map[VtPenFor(VT_COLOR_DEFAULT_BG)];
    vtPicCur = NULL;
    if (vtPicPens && v->kitty && v->rgb && v->fmt == 0 && (vtPicCur = VtPicSlot(v)) != NULL)
        return;
    if (vtPicPens && !v->kitty && !v->rgb && v->fmt == 0 && VtSixNeedsPicker(v) && (vtPicCur = VtPicSlot(v)) != NULL) {
        return;
    }
    for (i = 1; i < v->npal && i < 256; i++)
        vtImgMap[i] = term_pen_map[VtPenFor((vt_color)(VT_COLOR_RGB | (v->pal[i] & 0xFFFFFFUL)))];
}

static void VtImgBlit(struct RastPort *rp, WORD dx, WORD dy, LONG w, LONG h, const UBYTE *buf, LONG stride);

/* One flat rectangle of a band: pixels x0..x1 (inclusive), rows y0..y1 of the band, one pen. */
typedef struct {
    WORD x0, x1;
    BYTE y0, y1;
    UBYTE pen;
} VtImgFlat;

/* One row of the picture through the pen map: dst[i] = map[src[i]], n >= 1.
 * On the 68000 a C loop with LONG indices costs about 80 cycles a pixel (index
 * widened and scaled, pointer re-derived); move.b (a1)+ / move.b (a0,d2.w),(a2)+ /
 * dbf is 8 + 18 + 10 = 36. d2 and a2 are callee-saved, so the text saves them. */
#if defined(__VBCC__) && defined(__M68K__)
static void VtImgMapRow(__reg("a0") const UBYTE *map, __reg("a1") const vt_u8 *src,
                        __reg("d1") UBYTE *dst, __reg("d0") UWORD n) =
    "\tmovem.l\ta2/d2,-(sp)\n"
    "\tmove.l\td1,a2\n"
    "\tmoveq\t#0,d2\n"
    "\tsubq.w\t#1,d0\n"
    "\tmove.b\t(a1)+,d2\n"
    "\tmove.b\t(a0,d2.w),(a2)+\n"
    "\tdbf\td0,*-6\n"
    "\tmovem.l\t(sp)+,a2/d2\n";
#else
static void VtImgMapRow(const UBYTE *map, const vt_u8 *src, UBYTE *dst, UWORD n)
{
    while (n--)
        *dst++ = map[*src++];
}
#endif

/* A row's runs of one pen >= VT_IMG_FLAT_MIN: a run that repeats the previous
 * row's (same x0, x1, pen) grows that rectangle taller. Returns the count in fl.
 * The run scan is a pointer walk (about 20 cycles a pixel); the previous rectangles
 * are searched once per run, never per pixel. */
static int VtImgFlats(VtImgFlat *fl, int n, const UBYTE *row, LONG w, int k)
{
    const UBYTE *p = row, *end = row + w;
    int was = n;

    while (p < end) {
        const UBYTE *q = p;
        UBYTE pen = *p;
        while (++q < end && *q == pen)
            ;
        if (q - p >= VT_IMG_FLAT_MIN) {
            WORD a = (WORD)(p - row), x1 = (WORD)(q - row - 1);
            int m;
            for (m = 0; m < was; m++)
                if (fl[m].y1 == k - 1 && fl[m].x0 == a && fl[m].x1 == x1 && fl[m].pen == pen)
                    break;
            if (m < was) {
                fl[m].y1 = (BYTE)k;
            } else {
                fl[n].x0 = a; fl[n].x1 = x1;
                fl[n].y0 = fl[n].y1 = (BYTE)k;
                fl[n].pen = pen;
                n++;
            }
        }
        p = q;
    }
    return n;
}

/* Row k's mixed spans, the pixels the flat rectangles leave: sp gets [start, end)
 * pairs, the count is returned. One pass over the n rectangles per row (the rows
 * of a band are 8), the covering ones insertion-sorted by x0, then a cursor that
 * only moves right; the old code rescanned all n rectangles for every pixel. */
#define VT_IMG_SPANS_MAX (VT_IMG_W / VT_IMG_FLAT_MIN + 2)
static int VtImgSpans(const VtImgFlat *fl, int n, int k, LONG w, WORD *sp)
{
    WORD cx0[VT_IMG_W / VT_IMG_FLAT_MIN + 1], cx1[VT_IMG_W / VT_IMG_FLAT_MIN + 1];
    int nc = 0, i, j, ns = 0;
    WORD x = 0;

    for (i = 0; i < n; i++) {
        if (fl[i].y0 <= k && k <= fl[i].y1) {
            WORD a = fl[i].x0, b = fl[i].x1;
            for (j = nc; j > 0 && cx0[j - 1] > a; j--) {
                cx0[j] = cx0[j - 1];
                cx1[j] = cx1[j - 1];
            }
            cx0[j] = a;
            cx1[j] = b;
            nc++;
        }
    }
    for (i = 0; i < nc; i++) {
        if (cx0[i] > x) {
            sp[2 * ns] = x;
            sp[2 * ns + 1] = cx0[i];
            ns++;
        }
        x = cx1[i] + 1;
    }
    if (x < w) {
        sp[2 * ns] = x;
        sp[2 * ns + 1] = (WORD)w;
        ns++;
    }
    return ns;
}

/* w x h pixels of v to RastPort pixel (dx, dy): output row k shows picture row rows[k] (a RIP canvas fitted to the
 * screen's pixel aspect skips or repeats rows); rows NULL: picture rows sy to sy + h - 1. Columns from sx. */
static void VtImgWriteRows(const vt_image_view *v, LONG sx, LONG sy, const LONG *rows, LONG w, LONG h, WORD dx, WORD dy)
{
    LONG stride = (w + 15) & ~15L;      /* WritePixelArray8 wants rows of 16 */
    LONG k, j;
    static VtImgFlat flat[8 * (VT_IMG_W / VT_IMG_FLAT_MIN)];
    int nflat = 0, i;
    struct EClockVal ea, eb;
    UBYTE oldPen;
    UBYTE oldMode;

    struct RastPort *rp = VtImgRp();

    /* w < 1 must return here: VtImgMapRow's dbf loop would run 65536 times for 0 */
    if (w < 1 || h < 1 || stride > VT_IMG_W || h > 8 || !rp)
        return;
    VtImgColours(v);
    {   /* row pointers advance by addition: no per-row multiply (about 200 cycles each) */
        const vt_u8 *src = v->pix + (rows ? rows[0] : sy) * v->w + sx;
        UBYTE *d = vtImgBuf;
        for (k = 0; k < h; k++) {
            LONG yy = sy + k;           /* the picture row of output row k (dither phase and RGB follow it) */

            if (rows) {
                yy = rows[k];
                src = v->pix + yy * v->w + sx;
            }
            if (vtPicCur && vtPicCur->indexed) {
                const pp_plan *pl = (const pp_plan *)(vtPicCur->mem + sizeof(pm_palette));

                for (j = 0; j < w; j++)
                    d[j] = src[j] ? (UBYTE)pp_pen_at(pl, src[j], (int)(sx + j), (int)yy) : vtImgMap[0];
            } else if (vtPicCur) {
                UBYTE *plan3 = vtPicCur->mem + sizeof(pm_palette), *bc = plan3 + PP_BINS * 3;
                const UBYTE *px = v->rgb + (yy * v->w + sx) * 3;

                for (j = 0; j < w; j++)
                    d[j] = src[j] ? vtPicCur->cpen[pp_pen_bin(plan3, bc, (pm_palette *)vtPicCur->mem, vtPicCur->usable,
                                                              px + j * 3, (int)(sx + j), (int)yy)]
                                  : vtImgMap[0];
            } else {
                VtImgMapRow(vtImgMap, src, d, (UWORD)w);
            }
            nflat = VtImgFlats(flat, nflat, d, w, (int)k);
            src += v->w;
            d += stride;
        }
    }
    vtImgRuns++;
    term_blit_sync();                   /* the glyph blits are done with the planes */
    if (vtImgTime)
        ReadEClock(&ea);
    if (!nflat) {
        VtImgBlit(rp, dx, dy, w, h, vtImgBuf, stride);
    } else {
        oldPen = rp->FgPen;
        oldMode = rp->DrawMode;
        SetDrMd(rp, JAM2);
        for (i = 0; i < nflat; i++) {
            SetAPen(rp, flat[i].pen);
            RectFill(rp, dx + flat[i].x0, dy + flat[i].y0, dx + flat[i].x1, dy + flat[i].y1);
            vtImgFills++;
        }
        SetAPen(rp, oldPen);
        SetDrMd(rp, oldMode);
        /* the mixed spans between the flat runs, row by row */
        for (k = 0; k < h; k++) {
            WORD sp[2 * VT_IMG_SPANS_MAX];
            const UBYTE *row = vtImgBuf + k * stride;
            int ns = VtImgSpans(flat, nflat, (int)k, w, sp);
            for (i = 0; i < ns; i++)
                VtImgBlit(rp, dx + sp[2 * i], dy + k, sp[2 * i + 1] - sp[2 * i], 1, row + sp[2 * i], stride);
        }
    }
    if (vtImgTime) {
        ReadEClock(&eb);
        vtImgBlitCounts += eb.ev_lo - ea.ev_lo;   /* one run is far below 2^32 counts */
    }
}

/* buf (rows of stride bytes) to RastPort pixel (dx, dy), w x h. */
static void VtImgBlit(struct RastPort *rp, WORD dx, WORD dy, LONG w, LONG h, const UBYTE *buf, LONG stride)
{
    if (((struct Library *)GfxBase)->lib_Version >= 40) {
        WriteChunkyPixels(rp, dx, dy, dx + w - 1, dy + h - 1, (UBYTE *)buf, stride);
    } else if (((struct Library *)GfxBase)->lib_Version >= 39) {
        /* Kickstart 3.0: WritePixelArray8 and its one-row scratch rastport */
        struct RastPort tmp = *rp;

        tmp.Layer = NULL;
        tmp.BitMap = AllocBitMap((ULONG)stride, 1, GetBitMapAttr(rp->BitMap, BMA_DEPTH), 0, rp->BitMap);
        if (tmp.BitMap) {
            WritePixelArray8(rp, dx, dy, dx + w - 1, dy + h - 1, (UBYTE *)buf, &tmp);
            FreeBitMap(tmp.BitMap);
        }
    }
}

/* w x h pixels of v from (sx, sy) to RastPort pixel (dx, dy). */
static void VtImgWrite(const vt_image_view *v, LONG sx, LONG sy, LONG w, LONG h, WORD dx, WORD dy)
{
    VtImgWriteRows(v, sx, sy, NULL, w, h, dx, dy);
}

/* Cells [a, b) of screen row y show picture v. A kitty picture's index 0
 * is transparent: only the runs of the pixels that paint go out. */
static void VtImgRun(const vt_image_view *v, int a, int b, int y)
{
    int cw = 1 << term_xs;
    LONG sx = (LONG)(a - v->col0) * v->cw - v->px0, sy = v->py, w = (LONG)(b - a) * cw, h = 8, k, j, j0;
    WORD dx = TERM_PX(a), dy = TERM_PY(y);

    if (sx < 0) {                       /* the first cell: the picture starts px0 pixels in */
        dx = (WORD)(dx - sx);
        w += sx;
        sx = 0;
    }
    if (sy < 0) {                       /* the first row: py0 pixels down */
        dy = (WORD)(dy - sy);
        h += sy;
        sy = 0;
    }
    if (v->dh > 0 && v->dh != v->h && v->fmt == 0 && !v->kitty) {
        /* a RIP canvas fitted to the screen's pixel aspect: output row oy shows picture row vt_scale_row */
        WORD dy0 = TERM_PY(y);

        if (sx >= v->w)
            return;
        if (w > v->w - sx)
            w = v->w - sx;
        {   /* the cell row's output rows in one write: rows[] maps each to its picture row */
            LONG rows[8], n = 0, first = -1;

            for (k = 0; k < 8 && w > 0; k++) {
                LONG oy = v->py + k;

                if (oy < 0)
                    continue;
                if (oy >= v->dh)
                    break;
                if (first < 0)
                    first = k;
                rows[n++] = vt_scale_row((int)oy, v->h, v->dh);
            }
            if (n > 0)
                VtImgWriteRows(v, sx, 0, rows, w, n, dx, (WORD)(dy0 + first));
        }
        return;
    }
    if (v->fmt != 0 || sx >= v->w || sy >= v->h)
        return;                         /* fmt 1 (RGBA) is made only when asked (vt_set_image_rgb) */
    if (w > v->w - sx)
        w = v->w - sx;
    if (h > v->h - sy)
        h = v->h - sy;
    if (w <= 0 || h <= 0)
        return;
    if (!v->kitty) {
        VtImgWrite(v, sx, sy, w, h, dx, dy);
        return;
    }
    for (k = 0; k < h; k++) {
        const vt_u8 *row = v->pix + (sy + k) * v->w + sx;
        for (j = 0; j < w;) {
            while (j < w && !row[j])
                j++;
            j0 = j;
            while (j < w && row[j])
                j++;
            if (j > j0)
                VtImgWrite(v, sx + j0, sy + k, j - j0, 1, (WORD)(dx + j0), (WORD)(dy + k));
        }
    }
}

/* May cell c take a tile of v: over the text in a VT_CELL_IMAGE cell, under
 * it (z < 0) in a VT_CELL_UNDER cell, and with z <= -1073741824 only where
 * the background is the default. */
static BOOL VtImgCell(const vt_image_view *v, const vt_cell *c)
{
    vt_color f, b;

    if (v->z >= 0)
        return (c->pad & VT_CELL_IMAGE) != 0;
    if (!(c->pad & VT_CELL_UNDER))
        return FALSE;
    if (v->z > -1073741824L)
        return TRUE;
    vt_resolve_colors(vtTerm, c, &f, &b);
    return b == VT_COLOR_DEFAULT_BG;
}

/* The glyphs of cells [a, b) of row y again over a picture under them, in
 * JAM1 (printable ASCII; the background is the picture's). */
static void VtImgUnderText(const vt_cell *c, int a, int b, int y)
{
    struct RastPort *rp = VtImgRp();
    int x;

    for (x = a; x < b; x++) {
        vt_color f, bg;
        UBYTE ch;

        if (!(c[x].pad & VT_CELL_UNDER) || c[x].ch <= ' ' || c[x].ch >= 0x7F || c[x].width != 1)
            continue;
        vt_resolve_colors(vtTerm, &c[x], &f, &bg);
        ch = (UBYTE)c[x].ch;
        SetAPen(rp, term_pen_map[VtPenFor(f)]);
        SetDrMd(rp, JAM1);
        Move(rp, TERM_PX(x), (WORD)(TERM_PY(y) + term_baseline));
        Text(rp, (STRPTR)&ch, 1);
    }
    SetDrMd(rp, JAM2);
}

/* The canvas area a RIP command changed (vt_rip_take_dirty), in canvas pixels, x1 and y1 exclusive: while
 * vtRipClipOn, the blit of a RIP canvas covers only the cells that area touches. Set by VtRipFlush around the
 * draw of one command; every other draw (cursor, menu, resize, redraw) is whole. */
static BOOL vtRipClipOn;
static int vtRipClip[4];

/* Cell x of the row v is a tile of may be drawn: always, but under the clip a canvas cell the area misses. */
static BOOL VtClipHit(const vt_image_view *v, int x)
{
    int lo, top;

    if (!vtRipClipOn || !VtIsRipCanvas(v))
        return TRUE;
    lo = (x - v->col0) * v->cw - v->px0;
    top = v->py;
    return lo < vtRipClip[2] && lo + v->cw > vtRipClip[0] && top < vtRipClip[3] && top + v->ch > vtRipClip[1];
}

/* One finished RIP command is drawn: the engine flushes its damage (every row the canvas is on, whole width),
 * and the host blits only the area the command changed. Text in the same stretch moves the tiles and makes the
 * whole canvas dirty (the engine's rip_reanchor), so it is never left stale. */
static void VtRipFlush(void)
{
    vtRipClipOn = vt_rip_take_dirty(vtTerm, &vtRipClip[0], &vtRipClip[1], &vtRipClip[2], &vtRipClip[3]) != 0;
    vt_flush(vtTerm);
    vtRipClipOn = FALSE;
}

/* The pictures on grid rows [y0, y1), cells [x0, x1), in vt_row_image's
 * order: those under the text first, the row's glyphs over them, then
 * those over the text. */
static void VtDrawImages(int x0, int y0, int x1, int y1)
{
    vt_image_view v;
    const vt_cell *c;
    int y, i, x, a, xe, n, gy;
    BOOL under;

    if (!VtImgRp())
        return;
    term_flush();                       /* the text runs land first */
    for (y = y0; y < y1; y++) {
        gy = y - vtScrollOff;
        c = vt_row(vtTerm, gy, &n);
        if (!c || (!vtScrollOff && vt_row_size(vtTerm, gy)))
            continue;                   /* a double-size row shows no pictures */
        under = FALSE;
        for (i = 0;; i++) {
            int more = vt_row_image(vtTerm, gy, i, &v);
            if (under && (!more || v.z >= 0)) {
                VtImgUnderText(c, x0, x1 < n ? x1 : n, y);
                under = FALSE;
            }
            if (!more)
                break;
            xe = v.col0 + (v.px0 + v.w + v.cw - 1) / v.cw;
            if (xe > x1)
                xe = x1;
            if (xe > n)
                xe = n;
            for (x = v.col0 > x0 ? v.col0 : x0; x < xe;) {
                if (!VtImgCell(&v, &c[x]) || !VtClipHit(&v, x)) {
                    x++;
                    continue;
                }
                for (a = x; x < xe && VtImgCell(&v, &c[x]) && VtClipHit(&v, x); x++)
                    ;
                VtImgRun(&v, a, x, y);
                if (v.z < 0)
                    under = TRUE;
            }
        }
    }
}

/* Row gy shows an opaque RIP canvas: its cells under the text are the canvas's, blitted over, so the text
 * run need not blank them first (that blank, then the blit, was the flicker of every RIP command). */
static BOOL VtRowOnCanvas(int gy)
{
    vt_image_view v;
    int i;

    for (i = 0; vt_row_image(vtTerm, gy, i, &v); i++)
        if (VtIsRipCanvas(&v))
            return TRUE;
    return FALSE;
}

/* Cell columns of a row that an opaque kitty picture drawn over the text (z >= 0) covers whole: VtDrawImages
 * paints every pixel of them, so the text pass need not blank them first. That blank (black background), then the
 * blit, showed on screen for as long as the blit took: black bands moving down a picture the door scrolls (it
 * repaints the log and places the picture again for every step, so every picture row is damaged). */
#define VT_COVER_MAX 4
typedef struct {
    int n;
    int a[VT_COVER_MAX], b[VT_COVER_MAX];       /* cells [a, b) */
} VtCover;

static BOOL VtCellCovered(const vt_image_view *v, int x)
{
    int cw = 1 << term_xs, k, j;
    LONG sx = (LONG)(x - v->col0) * v->cw - v->px0;
    const vt_u8 *p;

    if (sx < 0 || sx + cw > v->w || v->py < 0 || v->py + 8 > v->h)
        return FALSE;                   /* an edge cell: part of it is not the picture's */
    p = v->pix + (LONG)v->py * v->w + sx;
    for (k = 0; k < 8; k++, p += v->w)
        for (j = 0; j < cw; j++)
            if (!p[j])
                return FALSE;           /* a transparent pixel: the text under it shows */
    return TRUE;
}

static void VtRowCover(int gy, const vt_cell *c, int n, VtCover *cv)
{
    vt_image_view v;
    int i, x, xe;

    cv->n = 0;
    for (i = 0; vt_row_image(vtTerm, gy, i, &v); i++) {
        if (!v.kitty || v.fmt != 0 || v.z < 0 || (v.dh > 0 && v.dh != v.h))
            continue;
        xe = v.col0 + (v.px0 + v.w + v.cw - 1) / v.cw;
        if (xe > n)
            xe = n;
        for (x = v.col0 > 0 ? v.col0 : 0; x < xe && cv->n < VT_COVER_MAX; x++) {
            int a = x;

            while (x < xe && (c[x].pad & VT_CELL_IMAGE) && VtCellCovered(&v, x))
                x++;
            if (x > a) {
                cv->a[cv->n] = a;
                cv->b[cv->n++] = x;
            }
        }
    }
}

/* Draw cells x0 <= x < x1 of rows y0 <= y < y1 from the grid. */
static void VtDraw(void *user, int x0, int y0, int x1, int y1)
{
    int x, y, n;
    /* vt_new() draws its blank screen before it returns the terminal:
     * term_init_area() has cleared the area already. */
    if (!vtTerm)
        return;
    for (y = y0; y < y1; y++) {
        const vt_cell *c = vt_row(vtTerm, y - vtScrollOff, &n);
        if (!c)
            continue;
        BOOL onCanvas = !term_cursor_paint && vt_images(vtTerm) && VtImgRp() && VtRowOnCanvas(y - vtScrollOff) &&
                        (vtScrollOff || !vt_row_size(vtTerm, y));

        VtCover cover;
        int ci = 0;

        cover.n = 0;
        if (!term_cursor_paint && vt_images(vtTerm) && VtImgRp() && (vtScrollOff || !vt_row_size(vtTerm, y)))
            VtRowCover(y - vtScrollOff, c, n, &cover);
        cur_y = (WORD)y;
        term_sync_row_base();
        for (x = x0; x < x1 && x < n; x++) {
            vt_color fg, bg;
            vt_u32 ch = c[x].ch;
            vt_cell cell = c[x];
            UBYTE code;

            if (onCanvas && (c[x].pad & VT_CELL_UNDER))
                continue;               /* the canvas is blitted over this cell (VtDrawImages) */
            for (ci = 0; ci < cover.n; ci++)
                if (x >= cover.a[ci] && x < cover.b[ci])
                    break;
            if (ci < cover.n)
                continue;               /* an opaque picture is blitted over this cell (VtDrawImages) */
            /* Blink is never animated (no timer; the owner's decision, MA1-7
             * exemption). With iCE colours on (BLINK_ICE) the engine turns
             * blink on a dark background into the bright background; with
             * them off (BLINK_STEADY) the flag is cleared here, so the cell is
             * drawn as it was sent. */
            if (prefs.Blink == BLINK_STEADY)
                cell.attr &= (vt_attr)~VT_ATTR_BLINK;
            vt_resolve_colors(vtTerm, &cell, &fg, &bg);
            term_pen_fg = VtPenFor(fg);
            term_pen_bg = VtPenFor(bg);
            if (term_cursor_paint) {    /* the cursor block: this cell, other pens */
                UBYTE block, text;
                term_cursor_pens(term_pen_bg, &block, &text);
                term_pen_bg = block;
                term_pen_fg = text;
            }
            atr_under = (c[x].attr & VT_ATTR_UNDERLINE) != 0;
            if (c[x].width == 0) {
                /* the second cell of a wide character: the font's cells are
                 * one wide, the first cell drew the fallback glyph */
                code = (UBYTE)' ';
            } else if (c[x].width > 1) {
                code = VT_FALLBACK_GLYPH;
            } else if (ch >= 0x20 && ch < 0x7F) {
                code = (UBYTE)ch;
            } else {
                /* A CP437 session (PC ANSI) is a byte pass-through: the byte
                 * the BBS sent is the font's character, the font decides
                 * the look. A Unicode session maps the code point into the
                 * font's own encoding (vtGlyphEnc, probed from the font);
                 * what the font lacks (box, blocks, diagonals) is drawn. */
                vt_glyph g = vt_map_glyph(ch, vtGlyphEnc);
                if (g.kind != VT_GLYPH_FONT) {
                    UWORD rows[8];
                    GlyphRaster_Draw(g, 1 << term_xs, 8, rows);
                    term_glyph_bits((WORD)x, rows);
                    continue;
                }
                code = g.code;
            }
            term_glyph((WORD)x, code);
        }
        /* a scrollback line from before a resize can be narrower than the grid */
        if (x < x1) {
            vt_cell blank;
            vt_color bfg, bbg;
            memset(&blank, 0, sizeof(blank));
            blank.ch = ' ';
            blank.width = 1;
            blank.fg = VT_COLOR_DEFAULT;
            blank.bg = VT_COLOR_DEFAULT;
            vt_resolve_colors(vtTerm, &blank, &bfg, &bbg);
            term_pen_fg = VtPenFor(bfg);
            term_pen_bg = VtPenFor(bbg);
            atr_under = 0;
            for (; x < x1; x++)
                term_glyph((WORD)x, (UBYTE)' ');
        }
    }
    atr_under = 0;
    VtPicSweep();       /* the pens of pictures the engine no longer has (also when none is left) */
    if (vt_images(vtTerm))
        VtDrawImages(x0, y0, x1, y1);
}

/* The engine's cell source for the cursor: the one cell, from the grid. */
static void VtCursorCell(WORD x, WORD y)
{
    VtDraw(NULL, x, y, x + 1, y + 1);
    atr_under = 0;
}

static void VtScroll(void *user, int top, int bottom, int n)
{
    /* pcansi's default background is colour 0 */
    term_rect_scroll(0, (WORD)top, term_cols, (WORD)(bottom - top), 0, (WORD)-n, 0);
}

/* The screen's layers while the engine draws straight into its bitmap.
 * It does only while no layer lies over the terminal (a requester, another
 * DCTelnet window, an open menu): such a layer owns those pixels. Intuition
 * takes the screen's LayerInfo to make a layer, so holding it from the look
 * to the end of the drawing (the blitter's too) keeps a menu or window from
 * opening in between and being drawn over. */
static BOOL vtLayersHeld;

/* Is a layer in front of the terminal window over its inner area? The
 * caller holds the screen's LayerInfo. */
static BOOL VtCovered(void)
{
    struct Layer *l;
    WORD x0 = win->LeftEdge + win->BorderLeft;
    WORD y0 = win->TopEdge + win->BorderTop;
    WORD x1 = win->LeftEdge + win->Width - win->BorderRight - 1;
    WORD y1 = win->TopEdge + win->Height - win->BorderBottom - 1;

    for (l = scr->LayerInfo.top_layer; l && l != win->WLayer; l = l->back)
        if (l->bounds.MinX <= x1 && l->bounds.MaxX >= x0
         && l->bounds.MinY <= y1 && l->bounds.MaxY >= y0)
            return TRUE;
    return FALSE;
}

/* Before drawing. Covered, the engine draws through the window's RastPort,
 * whose layer clips, and the lock is given back at once; otherwise it draws
 * straight into the bitmap and the layers stay locked until
 * VtLayersUnlock(). Without layers.library: the RastPort. */
static void VtLayersLock(void)
{
    BOOL covered = TRUE;

    if (!scr || !win)
        return;
    if (LayersBase) {
        LockLayerInfo(&scr->LayerInfo);
        covered = VtCovered();
    }
    term_covered(covered);
    if (covered && LayersBase)
        UnlockLayerInfo(&scr->LayerInfo);
    else if (!covered)
        vtLayersHeld = TRUE;
}

static void VtLayersUnlock(void)
{
    if (!vtLayersHeld)
        return;
    term_blit_sync();       /* the blitter is done with the planes too */
    vtLayersHeld = FALSE;
    UnlockLayerInfo(&scr->LayerInfo);
}

/* A write's drawing: the layers looked at (and held when the engine draws
 * straight into the bitmap), the cursor hidden. Opened only around what goes
 * on the screen, never across the engine's parse: a RIP picture rasterises
 * into the engine's canvas for seconds on an A500, and Intuition needs the
 * LayerInfo to open a menu or move a window meanwhile (RMB-FREEZE-RIG2). */
static BOOL vtDrawOpen;
static void VtSyncCursor(void);
static BOOL vtInWrite;      /* the engine parses: its draws open the drawing */

static void VtDrawBegin(void)
{
    if (vtDrawOpen)
        return;
    vtDrawOpen = TRUE;
    VtLayersLock();
    cursor_hide();
}

/* The layers given back mid-write; the cursor stays hidden. */
static void VtDrawPause(void)
{
    if (!vtDrawOpen)
        return;
    term_flush();
    VtLayersUnlock();
    vtDrawOpen = FALSE;
}

static void VtDrawEnd(void)
{
    if (!vtDrawOpen)
        return;
    term_flush();
    VtSyncCursor();
    cursor_show();
    VtLayersUnlock();
    vtDrawOpen = FALSE;
}

/* The engine's damage and scroll calls: in a write, the drawing opens first. */
static void VtDamage(void *user, int x0, int y0, int x1, int y1)
{
    if (vtInWrite)
        VtDrawBegin();
    VtDraw(user, x0, y0, x1, y1);
}

static void VtScrollCb(void *user, int top, int bottom, int n)
{
    if (vtInWrite)
        VtDrawBegin();
    VtScroll(user, top, bottom, n);
}

/* An answer to the BBS, in the middle of a write: sent with the layers
 * unlocked (a send that waits would hold up every window on the screen);
 * the next draw looks at them again. */
static void VtReply(void *user, const vt_u8 *buf, long len)
{
    VtDrawPause();
    send_data((const UBYTE *)buf, len);
}

/* Around one engine parse: the cursor off first, from the cells it was
 * drawn over (the parse changes them, a scroll moves them), then the layers
 * free for the parse; the engine's draws take them again, and the end draws
 * the cursor where the engine left it. */
static void VtParseBegin(void)
{
    VtDrawBegin();
    VtDrawPause();
    vtInWrite = TRUE;
}

static void VtParseEnd(void)
{
    vtInWrite = FALSE;
    VtDrawBegin();
    VtDrawEnd();
}

/* After the engine has drawn: the cursor where the engine has it. */
static void VtSyncCursor(void)
{
    int x, y;
    vt_cursor(vtTerm, &x, &y);
    cur_x = (WORD)x;
    cur_y = (WORD)y;
    term_sync_row_base();
    cursor_visible = vtScrollOff == 0 && (vt_modes(vtTerm) & VT_MODE_CURSOR_VISIBLE) != 0;
}

/* MA2-14: per connection, whether the host turned out to speak UTF-8. */
static struct VtUtf8Sniffer vtSniff;

/* The personality and character set the preferences ask for (Terminal >
 * Terminal Type, Character Set): at open, when either menu changes, and when
 * an entry session starts or ends. vt_set_personality() also resets the
 * engine (clears the screen), so it runs only for a real change: a session
 * that ends in the same personality keeps its text and the disconnect note. */
static void VtApplyPersonality(void)
{
    struct VtPersonality pers = Vt_PersonalityForConnection(&prefs, &vtSniff);

    vtGlyphEnc = Vt_GlyphEncoding(pers.charset, vtFontEnc);
    if (!vtTerm)
        return;
    if (vt_personality(vtTerm) != pers.personality)
        vt_set_personality(vtTerm, pers.personality);
    vt_set_charset(vtTerm, pers.charset);
}

/* RIPscrip (Terminal > RIPscrip, prefs.RipMode): at open, when the menu or a
 * site changes it. The engine modes: 0 is plain text (drops a canvas), 1 is RIP
 * active at once, 2 is Auto: plain text until the host sends ESC[! or a valid
 * !| line (vt_rip_active tells). Auto -> 2, On -> 1, Off -> 0. */
static void VtApplyRipMode(void)
{
    if (!vtTerm)
        return;
    vt_set_rip(vtTerm, Prefs_RipEngineMode(&prefs));
    vt_set_rip_stroke(vtTerm, prefs.RipFonts == RIPFONT_STROKE ? 1 : 0);
}

static void VtRipWatch(void);
static void VtRedraw(void);

/* The menu and site changes: the engine mode, then at once what it did to the screen (RIP Off drops the
 * canvas and returns to the text screen now, not with the next byte from the BBS). */
static void VtApplyRip(void)
{
    BOOL was;

    if (!vtTerm)
        return;
    was = vt_rip_active(vtTerm) != 0;
    VtApplyRipMode();
    VtRipWatch();
    if (was != (vt_rip_active(vtTerm) != 0))
        VtRedraw();                     /* the canvas is gone (or RIP is on): every cell again */
}

/* RIPscrip screen switch (D4/D5/D12), the one place that watches the engine's
 * RIP state: VtWrite calls it after every vt_write. The engine going RIP
 * active (ESC[!, a valid !| line, or the On mode) puts the session on the
 * built-in renderer and on the site's RIP screen; RIP going inactive (ESC[1!, a
 * reset) or the session ending (VtRipEnd) puts back what was there. The reopen
 * goes through VtRipScreenChanged -> RequestDisplayReopen, the path a screen
 * mode or font change takes. The saved settings are the live prefs before the
 * switch; the RIP ones are never written to the prefs file: the switch is
 * recorded in handChanges (a hand change while connected), which SavePrefs
 * takes back in a quick connect as in an entry session. */
static BOOL vtRipSeen;                  /* the switch is in force */
static struct PrefsStruct vtRipSaved;   /* the settings it replaced */

static void VtRipWatch(void)
{
    struct PrefsStruct before;
    ULONG id;
    UWORD w, h;
    BOOL fit;

    if (!vtTerm)
        return;
    if (vtRipSeen == (vt_rip_active(vtTerm) != 0))
        return;
    before = prefs;
    if (vtRipSeen)                      /* RIP ended: the previous screen and renderer */
    {
        VtRipPensRestore();
        prefs.State = (prefs.State & ~APP_RENDERER_ALL) | (vtRipSaved.State & APP_RENDERER_ALL);
        prefs.DisplayID = vtRipSaved.DisplayID;
        prefs.DisplayWidth = vtRipSaved.DisplayWidth;
        prefs.DisplayHeight = vtRipSaved.DisplayHeight;
        prefs.DisplayDepth = vtRipSaved.DisplayDepth;
        vtRipSeen = FALSE;
    }
    else
    {
        vtRipSaved = prefs;
        vtRipSeen = TRUE;
        prefs.State = (prefs.State & ~APP_RENDERER_ALL) | APP_RENDERER_BUILTIN;     /* D4 */
        if (prefs.RipScreen == RIPSCREEN_MODE)
        {
            id = prefs.RipDisplayID; w = prefs.RipWidth; h = prefs.RipHeight; fit = FALSE;
        }
        else if (prefs.RipScreen == RIPSCREEN_FIT)
            fit = TRUE;
        else
            fit = VtRipDefaultScreen(&id, &w, &h);      /* TRUE: no 1:1 mode fits (D12) */
        if (!fit)
        {
            prefs.DisplayID = id;
            prefs.DisplayWidth = w;
            prefs.DisplayHeight = h;
            prefs.DisplayDepth = 4;
        }
    }
    /* A temporary change like one made by hand while connected: it applies for
     * the run and SavePrefs takes it back (site_prefs.c), in a quick connect
     * too, where the live prefs are what gets saved. Switching back undoes it. */
    SitePrefs_CarryChange(&handChanges.after, &before, &prefs);
    VtRipScreenChanged(&before);
}

/* RIP Auto, plan H2c part C: a chunk in which RIP turns active halfway has its
 * rest drawn command by command on the RIP screen, not in the one flush the
 * plain-text path would make. The rest waits here while the screen reopens that
 * VtRipWatch asked for (DCTelnet.c: shouldRestart / shouldReopenConsole), and is
 * fed by VtHeldResume() once the new display is open. Bytes that arrive meanwhile
 * go behind it, so the order never changes. Heap, owned here; freed when the
 * remainder is fed, on a new connection (VtSniffReset), a settings end (VtRipEnd)
 * and with the terminal (VtClose). */
static UBYTE *vtHeld;
static long vtHeldLen;

static void VtHeldFree(void)
{
    free(vtHeld);
    vtHeld = NULL;
    vtHeldLen = 0;
}

/* Append behind what is held; FALSE (nothing appended) when memory is short. */
static BOOL VtHeldAppend(const UBYTE *data, long len)
{
    UBYTE *grown = (UBYTE *)realloc(vtHeld, (size_t)(vtHeldLen + len));

    if (!grown)
        return FALSE;
    memcpy(grown + vtHeldLen, data, (size_t)len);
    vtHeld = grown;
    vtHeldLen += len;
    return TRUE;
}

/* The session ended (or its settings did): a RIP switch still in force is undone. */
static void VtRipEnd(void)
{
    VtHeldFree();
    if (vtRipSeen && vtTerm)
    {
        vt_set_rip(vtTerm, 0);          /* inactive: VtRipWatch puts the screen back */
        VtRipWatch();
        VtApplyRipMode();
    }
}

/* The host of the running connection: inline=0 files land in Download Path/<host>/.
 * Kept here so a reopened display (VtOpen) gets it back; "" is "local". */
static char vtDlHost[52];

/* RIP 1I icons: the engine asks for a bare file name; it comes from the board's
 * folder (the sink's host, vt_download_host) or PROGDIR:Icons (VtDownload_Read). */
static int VtFileRead(const char *path, long offset, long size, vt_u8 **buf, long *n)
{
    return VtDownload_Read(vtTerm ? vt_download_host(vtTerm) : NULL, path, offset, size, buf, n);
}

static void VtFileFree(vt_u32 *buf)
{
    VtDownload_Release(buf);
}

static void VtSetDownloadHost(const char *host)
{
    strncpy(vtDlHost, host ? host : "", sizeof(vtDlHost) - 1);    /* static: last byte stays 0 */
    if (vtTerm)
        vt_set_download_sink(vtTerm, VtDownload_Sink, vtDlHost);
}

static void VtSniffReset(void)
{
    VtHeldFree();
    Vt_Utf8SnifferReset(&vtSniff);
}

/* MA3-2/3: the window title a program set (OSC 0/2), as Latin-1; none (or the
 * session over) gives DCTelnet's own text back. */
static char vtTitleShown[256];  /* what the title bar holds (Latin-1) */
static char vtTitleSeen[256];   /* the engine's title as last looked at (UTF-8) */

static void VtSetTitle(const char *utf8)
{
    char text[sizeof(vtTitleShown)];
    const char *screenTitle;
    ULONG keepScreenTitle;

    Vt_TitleText(utf8, MainWindowTitle, text, sizeof(text));
    if (!win || strcmp(text, vtTitleShown) == 0)
        return;
    strcpy(vtTitleShown, text);
    screenTitle = Vt_ScreenTitleFor(win->Flags, WFLG_BACKDROP, vtTitleShown);
    keepScreenTitle = 0xFFFFFFFFUL;         /* -1: the screen title stays */
    SetWindowTitles(win, (UBYTE *)vtTitleShown,
                    screenTitle ? (UBYTE *)screenTitle : (UBYTE *)(size_t)keepScreenTitle);
    if (screenTitle)
        LEDs();                             /* the bar redraw erased the activity LEDs */
}

static void VtTitle(void *user, const char *utf8)
{
    strncpy(vtTitleSeen, utf8, sizeof(vtTitleSeen) - 1);
    VtSetTitle(utf8);
}

/* After a write: a reset (ESC c) clears the engine's title without a callback. */
static void VtTitleSync(void)
{
    const char *t = vt_title(vtTerm);

    if (strcmp(t, vtTitleSeen) == 0)
        return;
    strncpy(vtTitleSeen, t, sizeof(vtTitleSeen) - 1);
    VtSetTitle(t);
}

/* MA3-3: back to DCTelnet's own title (session over); the engine's old title
 * is not applied again by the next write. */
static void VtTitleRestore(void)
{
    if (vtTerm)
        strncpy(vtTitleSeen, vt_title(vtTerm), sizeof(vtTitleSeen) - 1);
    VtSetTitle("");
}

/* MA3-4..6: the mouse as the program on the far side asked for it. */
static struct VtMouse vtMouse;

/* TRUE when ReportMouse must be on: a selection drag, or a motion mode. */
static BOOL VtReportMouseWanted(BOOL dragging)
{
    return (BOOL)(dragging || (Vt_IdcmpFor(0, IDCMP_MOUSEMOVE, vtTerm, FALSE) & IDCMP_MOUSEMOVE));
}

/* IDCMP_MOUSEMOVE while ?1002 or ?1003 is on, and while the Modern tool bar
 * is open: the terminal keeps the focus, and its moves light the bar's
 * buttons (ToolBarHover). The built-in renderer has no selection to drag. */
static void VtSyncMouseIdcmp(void)
{
    ULONG want;

    if (!win || !vtTerm)
        return;
    want = Vt_IdcmpFor(win->IDCMPFlags, IDCMP_MOUSEMOVE, vtTerm, ToolBarWantsHover());
    if (want != win->IDCMPFlags)
        ModifyIDCMP(win, want);
}

/* The terminal window reports pointer moves while a selection is dragged
 * (selDragging, DCTelnet.c), as the host's ?1002/?1003 ask, and always while
 * the Modern tool bar is open: the terminal keeps the focus, so the bar's own
 * window never sees the pointer cross its buttons. Intuition sends no
 * IDCMP_MOUSEMOVE without ReportMouse, whatever the window's IDCMP flags say,
 * so this one call sets both. It is the only place that decides them:
 * VtSyncMouseIdcmp() must not call back here, or every VtOpen() recurses until
 * the stack overruns (the first-run screen mode crash). */
static void TerminalMouseReport(void)
{
    if (win)
        ReportMouse((BOOL)(VtReportMouseWanted(selDragging) || ToolBarWantsHover()), win);
    VtSyncMouseIdcmp();
}

/* TRUE when the program owns the mouse (a mouse mode is on, Shift not held):
 * the event is for it and selecting must not start. */
static BOOL VtMouseOwned(BOOL shift)
{
    return vtTerm && Vt_MouseOwned(vtTerm, shift);
}

/* A button or wheel event at cell x, y (from 0): the report goes out. */
static void VtMouseButton(int button, BOOL down, int x, int y, int mods)
{
    UBYTE out[32];
    int n;

    if (!vtTerm)
        return;
    n = Vt_MouseButton(&vtMouse, vtTerm, button, down, x, y, mods, out);
    if (n > 0)
        VtReply(NULL, out, n);
}

static void VtMouseMove(int x, int y, int mods)
{
    UBYTE out[32];
    int n;

    if (!vtTerm)
        return;
    n = Vt_MouseMove(&vtMouse, vtTerm, x, y, mods, out);
    if (n > 0)
        VtReply(NULL, out, n);
}

/* RIPscrip mouse fields (H5, D22): a plain left click in window coordinates
 * mx, my. TRUE when a field answered; its command went to the BBS through
 * VtReply, the path the engine's own replies take, and the click is consumed.
 * The pointer is in the window's own coordinates; the picture is drawn 1:1 at
 * TERM_PX(0), TERM_PY(0) in the RastPort's (GimmeZeroZero: inside the border). */
static BOOL VtRipClick(WORD mx, WORD my, UWORD qual)
{
    UBYTE out[VT_RIP_OUT_MAX];
    int n, bx = 0, by = 0;

    if (!vtTerm || !win)
        return FALSE;
    if (win->Flags & WFLG_GIMMEZEROZERO) {
        bx = win->BorderLeft;
        by = win->BorderTop;
    }
    n = Vt_RipClick(vtTerm, mx - bx - TERM_PX(0), my - by - TERM_PY(0),
                    (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, out);
    if (n > 0)
        VtReply(NULL, out, n);
    return n > 0;
}

/* A typed character c: a RIP button's hotkey sends its command instead. */
static BOOL VtRipKey(UBYTE c)
{
    UBYTE out[VT_RIP_OUT_MAX];
    int n;

    if (!vtTerm)
        return FALSE;
    n = Vt_RipKey(vtTerm, c, out);
    if (n > 0)
        VtReply(NULL, out, n);
    return n > 0;
}

static void VtClose(void);
static void VtReleasePens(void);

/* The encoding of the terminal's font, from its glyph 0xC4: a line in an IBM
 * (CP437) font, a letter in an Amiga (Latin-1) one. After the font is
 * extracted (term_init_area, term_set_font). */
static void VtProbeFont(void)
{
    UWORD rows[8];
    int r;

    for (r = 0; r < 8; r++)
        rows[r] = term_xs == 3 ? (UWORD)((UWORD)font8[(0xC4 << 3) + r] << 8) : font16[(0xC4 << 3) + r];
    vtFontEnc = ScreenFont_GlyphIsHLine(rows, 8, 1 << term_xs) ? VT_ENC_CP437 : VT_ENC_LATIN1;
    vtGlyphEnc = Vt_GlyphEncoding(Vt_PersonalityForConnection(&prefs, &vtSniff).charset, vtFontEnc);
}

/* The scrollback line count the live engine holds (vt_new, vt_set_scrollback). */
static int vtTermSb;

static void VtRedraw(void);
static void VtApplyScrollback(void);

/* TRUE when the last VtOpen() kept the live engine and drew its cells again
 * (a display reopen); FALSE when it made a blank one. OpenDisplay() writes
 * its welcome screen, which starts with a form feed that clears, only on a
 * blank terminal. */
static BOOL vtKeptText;

/* Pictures keep their look on a screen whose pixels are not square (PAL
 * hires 1:2): the engine scales them by the display mode's Resolution. An
 * unknown mode, or none, tells it square. Same source as vtcon's
 * vtwin_set_screen(). */
static void VtSetAspect(void)
{
    struct DisplayInfo di;
    ULONG mode;
    int rx = 0, ry = 0;

    if (!vtTerm)
        return;
    if (scr && ((struct Library *)GfxBase)->lib_Version >= 39) {
        mode = GetVPModeID(&scr->ViewPort);
        if (mode != (ULONG)INVALID_ID &&
            GetDisplayInfoData(0, (UBYTE *)&di, sizeof(di), DTAG_DISP, mode)) {
            rx = di.Resolution.x;
            ry = di.Resolution.y;
        }
    }
    vt_set_pixel_aspect(vtTerm, rx, ry);
}

/* The terminal for the grid term_init_area() has just laid out (and
 * cleared). 0 when it is ready.
 *
 * The engine's cells belong to the session, not to the display: a display
 * reopen (Tool Bar, Title Bar, Full-screen, iconify) closes the window and
 * this runs again for the new one. An engine still alive is kept, given the
 * scrollback size the preferences ask for (VtApplyScrollback), resized to the
 * new grid and drawn again. Only a first open makes an engine. */
static int VtOpen(void)
{
    static const vt_callbacks cb = { VtDamage, VtScrollCb, VtReply, NULL, VtTitle, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL,
                                     NULL };  /* clipboard, cwd, notify, music, RIP level-9 blocks: not used */

    vtScrollOff = 0;
    term_cell_source = VtCursorCell;
    VtProbeFont();
    VtReleasePens();    /* a reopen never leaks the previous display's pens (nothing held: no-op) */
    vtTrueColour = FALSE;
    vtPicPens = FALSE;
    if (scr && scr->RastPort.BitMap) {
        struct BitMap *bm = scr->RastPort.BitMap;
        ULONG depth = ((struct Library *)GfxBase)->lib_Version >= 39 ? GetBitMapAttr(bm, BMA_DEPTH) : bm->Depth;

        UWORD gfxVer = ((struct Library *)GfxBase)->lib_Version;
        ULONG modeId = gfxVer >= 39 ? GetVPModeID(&scr->ViewPort) : 0;

        vtTrueColour = Vt_ObtainsPens(depth, modeId, gfxVer);
        vtPicPens = vtTrueColour && depth > 4 && depth < 8;     /* a palette of 32 to 128 pens, shared with the text */
        vtPicFree = vtPicPens ? (1UL << depth) - 20 : 0;
    }
    if (vtTerm) {
        VtReleasePens();    /* the pens were the old display's */
        VtApplyScrollback();
        vt_set_image_rgb_copy(vtTerm, vtPicPens);
        vt_resize(vtTerm, term_cols, term_rows);
        vt_set_cell_pixels(vtTerm, 1 << term_xs, 8);   /* pictures: the new font's cells */
        VtSetAspect();      /* the new display may be another mode */
        Vt_MouseReset(&vtMouse);
        vtTitleSeen[0] = vtTitleShown[0] = 0;
        TerminalMouseReport();
        VtRedraw();         /* the cells again, on the cleared area */
        vtKeptText = TRUE;
        return 0;
    }
    vtKeptText = FALSE;
    VtClose();      /* vt_new() draws through VtDraw(): vtTerm must not be the freed one */
    vtTermSb = VtScrollbackCap();
    vtTerm = vt_new(term_cols, term_rows, vtTermSb, &cb, NULL);
    if (!vtTerm)
        return 1;
    vt_set_cell_pixels(vtTerm, 1 << term_xs, 8);       /* pictures are placed on these cells */
    vt_set_image_rgb_copy(vtTerm, vtPicPens);
    VtSetAspect();
    VtApplyPersonality();
    VtApplyRipMode();
    vt_set_download_sink(vtTerm, VtDownload_Sink, vtDlHost);   /* inline=0 files: Download Path/<host>/<name> */
    vt_set_image_decoder(vtTerm, NULL, VtFileFree);             /* no PNG decoder; frees what VtFileRead gave */
    vt_set_file_reader(vtTerm, VtFileRead, NULL);              /* RIP 1I icons; no unlink, kitty t=t stays refused */
    Vt_MouseReset(&vtMouse);
    vtTitleSeen[0] = vtTitleShown[0] = 0;
    TerminalMouseReport();
    VtSyncCursor();
    return 0;
}

/* Every cell again from the grid, after a preference that changes how the
 * cells look (Terminal > iCE Colours). */
static void VtRedraw(void)
{
    if (!vtTerm)
        return;
    VtLayersLock();
    cursor_hide();
    VtDraw(NULL, 0, 0, term_cols, term_rows);
    term_flush();
    VtSyncCursor();
    cursor_show();
    VtLayersUnlock();
}

/* The true-colour pens go back to the screen they came from: before that
 * screen closes (CloseConsoleDevice), and when a new display takes over. */
static void VtReleasePens(void)
{
    VtPicReleaseAll();
    while (vtTcN > 0)
        ReleasePen(scr->ViewPort.ColorMap, (ULONG)vtTc[--vtTcN].pen);
    vtTcLast = 0;
    vtImgSerial = -1;   /* the picture pens came from VtPenFor's cache */
}

/* The engine and its cells go: quitting, or another renderer. */
static void VtClose(void)
{
    VtHeldFree();
    VtReleasePens();
    if (vtTerm) {
        vt_free(vtTerm);
        vtTerm = NULL;
    }
}

/* Scroll the view delta lines back (> 0) or forward (< 0) through the engine's
 * scrollback, clamped to what it holds; no scrollback on the alt screen (vim).
 * Returns the offset now shown. */
static int VtScrollView(int delta)
{
    int want = vtScrollOff + delta;
    int max = (vtTerm && !(vt_modes(vtTerm) & VT_MODE_ALT_SCREEN)) ? vt_scrollback_lines(vtTerm) : 0;

    if (want > max)
        want = max;
    if (want < 0)
        want = 0;
    if (!vtTerm || want == vtScrollOff)
        return vtScrollOff;
    vtScrollOff = want;
    VtLayersLock();
    cursor_hide();
    VtDraw(NULL, 0, 0, term_cols, term_rows);
    term_flush();
    VtSyncCursor();
    cursor_show();
    VtLayersUnlock();
    return vtScrollOff;
}

/* The preferences' scrollback size (Settings > ScrollBack, ScrollBack
 * Lines..) on the live engine, in place: the screen and the newest scrollback
 * lines that fit stay (vt_set_scrollback). The view goes back to the live grid
 * first, so it never shows a line that went. Without the memory for the new
 * size the engine keeps its old size and lines, and the next call tries again. */
static void VtApplyScrollback(void)
{
    int want = VtScrollbackCap();

    if (!vtTerm || want == vtTermSb)
        return;
    if (vtScrollOff)
        VtScrollView(-vtScrollOff);
    if (vt_set_scrollback(vtTerm, want))
        vtTermSb = want;
}

/* A key or wheel event for the scrollback: Shift+Page Up/Down scroll a page
 * (plain Page Up still goes to the host: less and vim use it), the NewMouse
 * wheel scrolls 3 lines unless a program owns the mouse. TRUE when taken. */
static BOOL VtScrollKey(UWORD rawkey, BOOL shift)
{
    int page = vtTerm ? vt_rows(vtTerm) - 1 : 0;
    int wheel = Vt_WheelButton(rawkey);

    if (!vtTerm || VtScrollbackCap() == 0)
        return FALSE;
    if (rawkey == 0x48 && shift)
        VtScrollView(page);
    else if (rawkey == 0x49 && shift)
        VtScrollView(-page);
    else if (wheel >= 0 && !VtMouseOwned(shift))
        VtScrollView(wheel == 64 ? 3 : -3);
    else
        return FALSE;
    return TRUE;
}

/* Plan H2c: bytes into the engine. RIP not active: vt_write, one parse and one
 * draw. RIP active: the bytes go in as slices that end at each '|' or LF (a split
 * only sets the flush granularity; the engine's parser is stream-safe and stays the
 * only one), and the drawing goes out whenever the engine finished a RIP command
 * since the last flush, so a picture builds up command by command the way it did on
 * the old machines. Each flush takes the layers for its drawing and gives them back
 * (VtDrawPause), so a menu can still open between commands. The bytes after the
 * last finished command are drawn by a last vt_flush, as vt_write would. */
/* RIP Auto, back to text: AmiExpress never sends ESC[1!, so a clear screen (ESC[2J) that no RIP command
 * follows in the same write means the BBS has gone back to plain text. VtClsScan finds the clear in the
 * bytes of the RIP path (its state survives a split between writes). The leave has to come before the
 * bytes after the clear are laid out: a bulletin's RIP text window (|w) is the scroll region with the
 * origin mode on, and a screen drawn while it still holds lands in those rows only (the door's menu in
 * the lower half of the screen, 2026-11-11). So VtFeedRip cuts the write at the clear and looks ahead:
 * no RIP introducer in the rest of the write (VtRipFollows) leaves RIP there and then, and the rest goes
 * in as text. With a possible introducer the old rule decides at the end of the write (VtRipAutoLeave):
 * RIP is dropped when the engine ran no command after the clear. RIP On stays on. */
static UBYTE vtClsState;                /* 0 ground, 1 after ESC, 2 after ESC [, 3 after ESC [ 2 */
static BOOL vtClsPending;
static long vtClsCmds;                  /* vt_rip_commands at the clear */
static unsigned long vtRipClsLeaves;    /* sentinel: leaves made at the clear, before the bytes after it */

/* The count of bytes up to and with the J of the first ESC[2J in d[0..n), 0 when there is none. */
static long VtClsScan(const UBYTE *d, long n)
{
    long i;

    for (i = 0; i < n; i++) {
        UBYTE b = d[i];

        if (b == 27)
            vtClsState = 1;
        else if (vtClsState == 1 && b == '[')
            vtClsState = 2;
        else if (vtClsState == 2 && b == '2')
            vtClsState = 3;
        else if (vtClsState == 3 && b == 'J') {
            vtClsState = 0;
            return i + 1;
        } else
            vtClsState = 0;
    }
    return 0;
}

/* Can a RIP command start in d[0..n)? Its introducers are !| and SOH| / STX|; one cut off by the end of
 * the write counts too. Deliberately wide (any column, not only a line start): a yes only puts the
 * decision off to the end of the write, where the engine's own count of commands makes it. */
static BOOL VtRipFollows(const UBYTE *d, long n)
{
    long i;

    for (i = 0; i < n; i++)
        if ((d[i] == '!' || d[i] == 1 || d[i] == 2) && (i + 1 == n || d[i + 1] == '|'))
            return TRUE;
    return FALSE;
}

static void VtRipLeave(void)
{
    vt_set_rip(vtTerm, 0);              /* drops the canvas and a |w text window; VtRipWatch (after the write)
                                         * puts the text screen back */
    VtApplyRipMode();                   /* Auto again: text until the BBS speaks RIP */
}

static void VtRipAutoLeave(void)
{
    BOOL leave = vtClsPending && vt_rip_commands(vtTerm) == vtClsCmds && Prefs_RipEngineMode(&prefs) == 2;

    vtClsPending = FALSE;
    if (!leave || !vt_rip_active(vtTerm))
        return;
    VtRipLeave();
}

/* RIP drawing is flushed once per write, not per command: the engine keeps the union of the changed area
 * (vt_rip_take_dirty), so one blit covers a whole buffer of commands. A long buffer still draws as it goes:
 * VtRipFlushDue says when VT_RIP_FLUSH_MS have passed since the last flush (E-clock, TimerBase). */
#define VT_RIP_FLUSH_MS 100
static ULONG vtRipFlushLo;
static unsigned long vtRipFlushes;      /* sentinel: flushes made by VtFeedRip */

static BOOL VtRipFlushDue(void)
{
    struct EClockVal e;
    ULONG freq, d;

    if (!TimerBase)
        return FALSE;
    freq = ReadEClock(&e);
    d = e.ev_lo - vtRipFlushLo;
    if (freq == 0 || d / (freq / 1000UL + 1) < VT_RIP_FLUSH_MS)
        return FALSE;
    vtRipFlushLo = e.ev_lo;
    return TRUE;
}

static void VtFeedText(const UBYTE *data, long len);

static void VtFeedRip(const UBYTE *data, long len)
{
    long done = vt_rip_commands(vtTerm);

    if (TimerBase) {
        struct EClockVal e;

        ReadEClock(&e);
        vtRipFlushLo = e.ev_lo;
    }
    while (len > 0) {
        const UBYTE *bar = memchr(data, '|', (size_t)len);
        const UBYTE *lf = memchr(data, '\n', (size_t)len);
        const UBYTE *end = bar && (!lf || bar < lf) ? bar : lf;
        long n = end ? (long)(end - data) + 1 : len;
        long cls = VtClsScan(data, n);

        if (cls)
            n = cls;                    /* the slice ends with the clear: what follows waits for the verdict */
        vt_feed(vtTerm, data, n);
        data += n;
        len -= n;
        if (cls) {
            if (Prefs_RipEngineMode(&prefs) == 2 && vt_rip_active(vtTerm) && !VtRipFollows(data, len)) {
                vtClsPending = FALSE;
                vtRipClsLeaves++;
                VtRipLeave();           /* before the rest is laid out: it gets the whole text screen */
                VtFeedText(data, len);
                return;
            }
            vtClsPending = TRUE;
            vtClsCmds = vt_rip_commands(vtTerm);
        }
        if (vt_rip_commands(vtTerm) != done) {
            done = vt_rip_commands(vtTerm);
            if (len > 0 && VtRipFlushDue()) {
                vtRipFlushes++;
                VtRipFlush();
                VtDrawPause();
            }
        }
    }
    vtRipFlushes++;
    VtRipFlush();
}

/* The bytes of one VtWrite, after the UTF-8 sniffer. Plan H2c part C: in RIP Auto
 * with RIP not yet active the chunk goes in as slices that end at each LF, and
 * when a slice turns RIP active the rest is held (vtHeld) for VtWriteIn to feed
 * through VtFeedRip once the RIP screen is up. Off and On, and Auto already
 * active, are as before. */
static void VtFeed(const UBYTE *data, long len)
{
    if (len <= 0)
        return;
    if (vtHeldLen) {                    /* behind what waits: byte order never changes */
        if (VtHeldAppend(data, len))
            return;
        while (vtHeldLen) {             /* no memory: the held bytes go first, then these */
            UBYTE *held = vtHeld;
            long n = vtHeldLen;

            vtHeld = NULL;
            vtHeldLen = 0;
            VtFeedRip(held, n);
            free(held);
        }
    }
    if (vt_rip_active(vtTerm)) {
        VtFeedRip(data, len);
        return;
    }
    if (Prefs_RipEngineMode(&prefs) != 2) {
        vt_write(vtTerm, data, len);
        return;
    }
    VtFeedText(data, len);
}

/* RIP Auto, RIP not active: slices that end at each LF; a slice that turns RIP active has the rest held
 * (or fed command by command when memory is short). Also the rest of a write after a clear that left RIP
 * (VtFeedRip). */
static void VtFeedText(const UBYTE *data, long len)
{
    while (len > 0) {
        const UBYTE *lf = memchr(data, '\n', (size_t)len);
        long n = lf ? (long)(lf - data) + 1 : len;

        vt_feed(vtTerm, data, n);
        data += n;
        len -= n;
        if (len > 0 && vt_rip_active(vtTerm)) {
            vt_flush(vtTerm);
            if (!VtHeldAppend(data, len))
                VtFeedRip(data, len);   /* no memory: the rest goes out now, unheld */
            return;
        }
    }
    vt_flush(vtTerm);
}

/* Bytes from the BBS (or DCTelnet's own messages): parse, draw what
 * changed, put the cursor back. Drawing happens with the cursor hidden. */
static void VtWriteIn(const UBYTE *data, long len, BOOL sniff)
{
    if (vtScrollOff)
        VtScrollView(-vtScrollOff);     /* new output: back to the live grid */
    VtParseBegin();
    /* Unix host, Character Set not UTF-8: the first valid UTF-8 sequence moves
     * the engine to UTF-8 for the rest of the connection; prefs stay as they are.
     * A trailing incomplete prefix is held back until the next write decides it. */
    if (sniff && (Vt_Utf8SnifferActive(&prefs) || vtSniff.nheld)) {
        UBYTE pre[3];
        long npre, ndata;

        if (Vt_Utf8SnifferFeed(&vtSniff, &prefs, data, len, pre, &npre, &ndata))
            vt_set_charset(vtTerm, VT_CS_UTF8);
        if (npre)
            VtFeed(pre, npre);
        VtFeed(data, ndata);
    } else
        VtFeed(data, len);
    /* vtInWrite stays set through the held tail below: its draws go into the screen
     * bitmap and must open the drawing (LayerInfo held, cover refreshed) like the
     * parse's; VtParseEnd clears it. */
    VtRipAutoLeave();
    VtRipWatch();                   /* the one place RIP going active/inactive is noticed */
    /* RIP Auto went active halfway through the chunk: when no screen reopen was asked
     * for (RIPSCREEN_FIT keeps the screen) the rest goes out now, command by command;
     * with a reopen pending it stays held for VtHeldResume. */
    while (vtHeldLen && !VtReopenPending()) {
        UBYTE *held = vtHeld;
        long n = vtHeldLen;

        vtHeld = NULL;
        vtHeldLen = 0;
        VtFeedRip(held, n);
        free(held);
        VtRipAutoLeave();
        VtRipWatch();
    }
    VtParseEnd();
    VtTitleSync();
    TerminalMouseReport();          /* ?1002 / ?1003 may have changed */
}

static void VtWrite(const UBYTE *data, long len)
{
    VtWriteIn(data, len, TRUE);
}

/* The main loop, after a display reopen has finished: the remainder held when RIP
 * turned active mid-chunk is drawn now, on the RIP screen, through VtWrite's own
 * path (layers, cursor, title). Nothing held, or a reopen still queued: nothing. */
static void VtHeldResume(void)
{
    if (vtTerm && vtHeldLen && !VtReopenPending())
        VtWriteIn(NULL, 0, FALSE);
}

/* A prefix VtWrite held back is waiting to be drawn: the receive loop polls
 * instead of sleeping, so VtFlushHeld() runs as soon as the socket is idle. */
static BOOL VtHeldPending(void)
{
    return vtSniff.nheld != 0;
}

/* The receive loop found no more pending data: a prefix VtWrite held back
 * waits for nothing, so it is drawn as the current character set. */
static void VtFlushHeld(void)
{
    UBYTE pre[3];
    long n;

    if (!vtSniff.nheld)
        return;
    if (vtScrollOff)
        VtScrollView(-vtScrollOff);
    n = Vt_Utf8SnifferTakeHeld(&vtSniff, pre);
    VtParseBegin();
    vt_write(vtTerm, pre, n);
    VtParseEnd();
}
