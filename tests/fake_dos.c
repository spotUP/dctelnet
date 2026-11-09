/* tests/fake_dos.c -- the dos.library calls VtDownload_Sink makes, on the host's
 * file system. Paths are the Amiga's "dir/file" with the volume taken as-is. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <exec/types.h>
#include <proto/dos.h>

static FILE *files[8];

BPTR Lock(STRPTR name, LONG mode)
{
    struct stat st;
    (void)mode;
    return stat(name, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
}
BPTR CreateDir(STRPTR name) { return mkdir(name, 0755) == 0 ? 1 : 0; }
void UnLock(BPTR lock) { (void)lock; }
BPTR Open(STRPTR name, LONG mode)
{
    int i;
    (void)mode;
    for (i = 1; i < 8; i++)
        if (!files[i]) { files[i] = fopen(name, "wb"); return files[i] ? i : 0; }
    return 0;
}
LONG Write(BPTR fh, APTR buf, LONG len) { return (LONG)fwrite(buf, 1, (size_t)len, files[fh]); }
LONG Close(BPTR fh) { fclose(files[fh]); files[fh] = NULL; return 1; }
BOOL AddPart(STRPTR dir, CONST_STRPTR file, ULONG size)
{
    size_t n = strlen(dir);
    if (n && dir[n - 1] != '/' && dir[n - 1] != ':') dir[n++] = '/';
    if (n + strlen(file) + 1 > size) return FALSE;
    strcpy(dir + n, file);
    return TRUE;
}
