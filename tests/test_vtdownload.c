/* tests/test_vtdownload.c -- DCTelnet's sink for the files the engine saves (iTerm2
 * inline=0, vt_set_download_sink): <Download Path>/<host>/<name>. */
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

    /* No Download Path set: nothing is written, the sink says so. */
    prefs.DownloadPath[0] = 0;
    assert(VtDownload_Sink("bbs.example", "x", data, 1) == 0);

    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    assert(system(cmd) == 0);
    printf("vtdownload: all assertions passed\n");
    return 0;
}
