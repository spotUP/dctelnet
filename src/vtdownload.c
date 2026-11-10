/* src/vtdownload.c -- the engine's download sink and file reader (see vtdownload.h). */
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include "prefs.h"
#include "vtdownload.h"

/* Makes the drawer when it is not there yet. */
static BOOL DrawerExists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);

    if (!lock) lock = CreateDir((STRPTR)path);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

int VtDownload_Sink(const char *host, const char *name, const unsigned char *data, long len)
{
    char path[sizeof(prefs.DownloadPath) + 160];
    BPTR fh;
    BOOL ok;

    if (!prefs.DownloadPath[0] || !host || !name || strlen((const char *)prefs.DownloadPath) + strlen(host) + strlen(name) + 3 > sizeof(path))
        return 0;
    strcpy(path, (const char *)prefs.DownloadPath);
    if (!AddPart((STRPTR)path, (CONST_STRPTR)host, sizeof(path)) || !DrawerExists(path)) return 0;
    if (!AddPart((STRPTR)path, (CONST_STRPTR)name, sizeof(path))) return 0;
    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) return 0;
    ok = len == 0 || Write(fh, (APTR)data, len) == len;
    Close(fh);
    return ok ? 1 : 0;
}

/* <offset>, <size> bytes (0: to the end) of the file at <path>, into a malloc() buffer. */
static int ReadPart(const char *path, long offset, long size, unsigned char **buf, long *n)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    long len;
    unsigned char *mem;

    if (!fh) return 0;
    len = Seek(fh, 0, OFFSET_END) < 0 ? -1 : Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || offset < 0 || size < 0 || offset > len) { Close(fh); return 0; }
    len -= offset;
    if (size && size < len) len = size;
    if (len > VTDOWNLOAD_READ_MAX || (offset && Seek(fh, offset, OFFSET_BEGINNING) < 0)) { Close(fh); return 0; }
    mem = (unsigned char *)malloc(len ? (size_t)len : 1);
    if (!mem || (len && Read(fh, mem, len) != len)) { free(mem); Close(fh); return 0; }
    Close(fh);
    *buf = mem;
    *n = len;
    return 1;
}

int VtDownload_Read(const char *host, const char *name, long offset, long size, unsigned char **buf, long *n)
{
    char path[sizeof(prefs.DownloadPath) + 160];

    if (!name || !name[0] || strchr(name, '/') || strchr(name, ':') || !buf || !n || strlen(name) > 120)
        return 0;
    if (prefs.DownloadPath[0] && host && strlen(host) < 32) {
        strcpy(path, (const char *)prefs.DownloadPath);
        if (AddPart((STRPTR)path, (CONST_STRPTR)host, sizeof(path)) &&
            AddPart((STRPTR)path, (CONST_STRPTR)name, sizeof(path)) &&
            ReadPart(path, offset, size, buf, n))
            return 1;
    }
    strcpy(path, "PROGDIR:Icons");
    return AddPart((STRPTR)path, (CONST_STRPTR)name, sizeof(path)) && ReadPart(path, offset, size, buf, n);
}

void VtDownload_Release(void *buf)
{
    free(buf);
}
