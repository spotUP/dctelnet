/* src/vtdownload.c -- the engine's download sink (see vtdownload.h). */
#include <proto/dos.h>
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
