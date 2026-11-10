/* tests/test_vtdownload.c -- DCTelnet's sink for the files the engine saves (iTerm2
 * inline=0, vt_set_download_sink): <Download Path>/<host>/<name>; and the reader for
 * the files it asks for (RIP 1I icons): the board's folder, then PROGDIR:Icons/. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <exec/types.h>
#include "prefs.h"
#include "vtdownload.h"

struct PrefsStruct prefs;

static long slurp(const char *path, char *buf, size_t max)
{
    FILE *f = fopen(path, "rb");
    long n;
    assert(f);
    n = (long)fread(buf, 1, max, f);
    fclose(f);
    return n;
}

int main(void)
{
    char dir[64] = "/tmp/dctelnet-vtdl-XXXXXX", cmd[128], path[160], got[32];
    static const unsigned char data[5] = { 'h', 'e', 'l', 'l', 'o' };

    assert(mkdtemp(dir));
    strcpy((char *)prefs.DownloadPath, dir);

    /* The host drawer is made, the file lands in it with every byte. */
    assert(VtDownload_Sink("bbs.example", "hello.txt", data, 5) == 1);
    snprintf(path, sizeof(path), "%s/bbs.example/hello.txt", dir);
    assert(slurp(path, got, sizeof(got)) == 5 && memcmp(got, "hello", 5) == 0);

    /* A second file to the same host reuses the drawer. */
    assert(VtDownload_Sink("bbs.example", "two.bin", data, 3) == 1);
    snprintf(path, sizeof(path), "%s/bbs.example/two.bin", dir);
    assert(slurp(path, got, sizeof(got)) == 3);

    /* The engine's host for a local session is "local". */
    assert(VtDownload_Sink("local", "a.txt", data, 1) == 1);
    snprintf(path, sizeof(path), "%s/local/a.txt", dir);
    assert(slurp(path, got, sizeof(got)) == 1);

    /* The reader: the board's folder holds the file it saved. */
    {
        unsigned char *buf = NULL;
        long n = -1;
        char cwd[256];

        assert(VtDownload_Read("bbs.example", "hello.txt", 0, 0, &buf, &n) == 1);
        assert(n == 5 && memcmp(buf, "hello", 5) == 0);
        VtDownload_Release(buf);
        /* offset and size, as kitty asks; size past the end stops at the end */
        assert(VtDownload_Read("bbs.example", "hello.txt", 1, 3, &buf, &n) == 1);
        assert(n == 3 && memcmp(buf, "ell", 3) == 0);
        VtDownload_Release(buf);
        assert(VtDownload_Read("bbs.example", "hello.txt", 3, 99, &buf, &n) == 1 && n == 2);
        VtDownload_Release(buf);
        assert(VtDownload_Read("bbs.example", "hello.txt", 6, 0, &buf, &n) == 0);
        /* a path is refused: a kitty t=f path or a climb out of the folder */
        assert(VtDownload_Read("bbs.example", "bbs.example/hello.txt", 0, 0, &buf, &n) == 0);
        assert(VtDownload_Read("bbs.example", "RAM:hello.txt", 0, 0, &buf, &n) == 0);
        assert(VtDownload_Read("bbs.example", "", 0, 0, &buf, &n) == 0);
        /* another board's folder does not have it, and PROGDIR:Icons/ has none */
        assert(VtDownload_Read("other.example", "hello.txt", 0, 0, &buf, &n) == 0);
        /* PROGDIR:Icons/ is the fallback; the board's own file wins over it.
         * fake_dos takes "PROGDIR:Icons" as a drawer under the working directory. */
        assert(getcwd(cwd, sizeof(cwd)) && chdir(dir) == 0);
        assert(system("mkdir -p PROGDIR:Icons && printf icon > PROGDIR:Icons/x.icn && printf shared > PROGDIR:Icons/hello.txt") == 0);
        assert(VtDownload_Read("other.example", "x.icn", 0, 0, &buf, &n) == 1 && n == 4 && memcmp(buf, "icon", 4) == 0);
        VtDownload_Release(buf);
        assert(VtDownload_Read("bbs.example", "hello.txt", 0, 0, &buf, &n) == 1 && n == 5);
        VtDownload_Release(buf);
        assert(VtDownload_Read("other.example", "hello.txt", 0, 0, &buf, &n) == 1 && n == 6);
        VtDownload_Release(buf);
        /* no Download Path: only PROGDIR:Icons/ */
        prefs.DownloadPath[0] = 0;
        assert(VtDownload_Read("bbs.example", "x.icn", 0, 0, &buf, &n) == 1 && n == 4);
        VtDownload_Release(buf);
        assert(chdir(cwd) == 0);
        strcpy((char *)prefs.DownloadPath, dir);
    }

    /* No Download Path set: nothing is written, the sink says so. */
    prefs.DownloadPath[0] = 0;
    assert(VtDownload_Sink("bbs.example", "x", data, 1) == 0);

    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    assert(system(cmd) == 0);
    printf("vtdownload: all assertions passed\n");
    return 0;
}
