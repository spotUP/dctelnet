/* tests/shim/proto/dos.h -- the few dos.library calls VtDownload_Sink/Read make, declared
 * for host tests. The test supplies the definitions. */
#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <exec/types.h>

typedef LONG BPTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;

#define SHARED_LOCK  (-2)
#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define OFFSET_BEGINNING (-1)
#define OFFSET_END       1

BPTR Lock(STRPTR name, LONG mode);
BPTR CreateDir(STRPTR name);
void UnLock(BPTR lock);
BPTR Open(STRPTR name, LONG mode);
LONG Write(BPTR fh, APTR buf, LONG len);
LONG Read(BPTR fh, APTR buf, LONG len);
LONG Seek(BPTR fh, LONG pos, LONG mode);
LONG Close(BPTR fh);
BOOL AddPart(STRPTR dir, CONST_STRPTR file, ULONG size);
#endif
