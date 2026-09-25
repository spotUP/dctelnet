/* src/sgr_remap.c -- WB terminal SGR pen remap (pure logic, host-tested). */
#include "sgr_remap.h"

static ULONG color_dist(ULONG a, ULONG b)
{
    LONG dr = (LONG)((a >> 24) & 0xFF) - (LONG)((b >> 24) & 0xFF);
    LONG dg = (LONG)((a >> 16) & 0xFF) - (LONG)((b >> 16) & 0xFF);
    LONG db = (LONG)((a >> 8) & 0xFF) - (LONG)((b >> 8) & 0xFF);

    /* Luminance-weighted: green carries, blue trails. Fits in ULONG:
     * 255^2 * (30 + 59 + 11) = 6.5M. */
    return (ULONG)(dr * dr * 30 + dg * dg * 59 + db * db * 11);
}

UBYTE SgrNearestPen(ULONG target, const ULONG palette[8])
{
    UBYTE best = 0;
    ULONG bestDist;
    UBYTE i;

    if (palette == NULL)
        return 0;
    bestDist = color_dist(target, palette[0]);
    for (i = 1; i < 8; i++)
    {
        ULONG d = color_dist(target, palette[i]);
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void SgrBuildMap(const ULONG ansi32[16], const ULONG palette[8],
                 UBYTE penmap[16])
{
    UBYTE i;

    if (ansi32 == NULL || palette == NULL || penmap == NULL)
        return;
    for (i = 0; i < 16; i++)
        penmap[i] = SgrNearestPen(ansi32[i], palette) & 0x07;
}

/* Emit one rewritten param (max 7 bytes: "030;040" for reset). */
static size_t emit_param(LONG param, const UBYTE penmap[16], UBYTE *out)
{
    size_t pos = 0;

    /* Fixed 3-digit colour params ("031", never "31"): ibmcon parses
     * multi-digit values (jump table covers 0-49), so this stays valid
     * and keeps the writer branchless. */
    if (param >= 30 && param <= 37)
    {
        ULONG p = (ULONG)(30 + (penmap[param - 30] & 0x07));
        out[pos++] = (UBYTE)('0' + p / 100);
        out[pos++] = (UBYTE)('0' + (p / 10) % 10);
        out[pos++] = (UBYTE)('0' + p % 10);
    }
    else if (param >= 40 && param <= 47)
    {
        ULONG p = (ULONG)(40 + (penmap[param - 40] & 0x07));
        out[pos++] = (UBYTE)('0' + p / 100);
        out[pos++] = (UBYTE)('0' + (p / 10) % 10);
        out[pos++] = (UBYTE)('0' + p % 10);
    }
    else if (param == 0)
    {
        /* Reset -> mapped white-on-black, never the device default that
         * shows blue on Workbench. */
        ULONG f = (ULONG)(30 + (penmap[7] & 0x07));
        ULONG b = (ULONG)(40 + (penmap[0] & 0x07));
        out[pos++] = (UBYTE)('0' + f / 100);
        out[pos++] = (UBYTE)('0' + (f / 10) % 10);
        out[pos++] = (UBYTE)('0' + f % 10);
        out[pos++] = ';';
        out[pos++] = (UBYTE)('0' + b / 100);
        out[pos++] = (UBYTE)('0' + (b / 10) % 10);
        out[pos++] = (UBYTE)('0' + b % 10);
    }
    else if (param == 39)
    {
        ULONG p = (ULONG)(30 + (penmap[7] & 0x07));
        out[pos++] = (UBYTE)('0' + p / 100);
        out[pos++] = (UBYTE)('0' + (p / 10) % 10);
        out[pos++] = (UBYTE)('0' + p % 10);
    }
    else if (param == 49)
    {
        ULONG p = (ULONG)(40 + (penmap[0] & 0x07));
        out[pos++] = (UBYTE)('0' + p / 100);
        out[pos++] = (UBYTE)('0' + (p / 10) % 10);
        out[pos++] = (UBYTE)('0' + p % 10);
    }
    else
    {
        /* Styles/unknown: copy back decimally. */
        char tmp[8];
        int len = 0, i;
        LONG v = param < 0 ? 0 : param;
        if (v == 0)
            tmp[len++] = '0';
        while (v > 0 && len < 7)
        {
            tmp[len++] = (char)('0' + v % 10);
            v /= 10;
        }
        for (i = len - 1; i >= 0; i--)
            out[pos++] = (UBYTE)tmp[i];
    }
    return pos;
}

size_t SgrRemap(const UBYTE *in, size_t inLen, const UBYTE penmap[16],
                UBYTE *out, size_t outLen)
{
    size_t i = 0, o = 0;

    if (in == NULL || penmap == NULL || out == NULL)
        return 0;

    while (i < inLen)
    {
        /* SGR opener? */
        if ((in[i] == 0x1B && i + 1 < inLen && in[i + 1] == '[') ||
            in[i] == 0x9B)
        {
            size_t j = i + (in[i] == 0x1B ? 2 : 1);
            int isEsc = (in[i] == 0x1B);

            while (j < inLen && ((in[j] >= '0' && in[j] <= '9') || in[j] == ';'))
                j++;

            if (j < inLen && in[j] == 'm')
            {
                /* SGR: rewrite params. */
                size_t k = i + (isEsc ? 2 : 1);
                LONG p = -1;
                int first = 1;
                UBYTE pbuf[8];

                /* Room check per emit below; +3 headroom for opener. */
                if (o + 3 > outLen)
                    return 0;
                if (isEsc)
                {
                    out[o++] = 0x1B;
                    out[o++] = '[';
                }
                else
                    out[o++] = 0x9B;

                while (k <= j)
                {
                    LONG v = (k == j || in[k] == ';') ? p : -2;
                    size_t w;
                    if (v == -1)
                        v = 0;      /* empty param (incl. bare ESC[m) */
                    if (v >= 0)
                    {
                        if (o + 8 > outLen)
                            return 0;
                        if (!first)
                            out[o++] = ';';
                        first = 0;
                        w = emit_param(v, penmap, pbuf);
                        {
                            size_t t;
                            for (t = 0; t < w; t++)
                                out[o++] = pbuf[t];
                        }
                    }
                    if (k < j && in[k] == ';')
                        p = -1;
                    else if (k < j)
                        p = (p < 0) ? (in[k] - '0') : (p * 10 + (in[k] - '0'));
                    k++;
                }
                if (o + 1 > outLen)
                    return 0;
                out[o++] = 'm';
                i = j + 1;
                continue;
            }
            /* Some other sequence: copy the opener byte, rescan. */
            if (o + 1 > outLen)
                return 0;
            out[o++] = in[i++];
            continue;
        }
        if (o + 1 > outLen)
            return 0;
        out[o++] = in[i++];
    }

    return o;
}
