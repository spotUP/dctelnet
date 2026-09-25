/* test/host/exec/types.h -- minimal Amiga exec/types.h shim for host tests.
 *
 * site_prefs.h includes DCTelnet.h, which needs the Amiga base types.
 * Only used with -Ihost in test/Makefile; never in the vbcc/Amiga build.
 */
#ifndef HOST_EXEC_TYPES_H
#define HOST_EXEC_TYPES_H

typedef long LONG;
typedef unsigned long ULONG;
typedef short WORD;
typedef unsigned short UWORD;
typedef signed char BYTE;
typedef unsigned char UBYTE;
typedef long BOOL;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Amiga string/char type from the real exec/types.h. */
typedef char TEXT;

/* Opaque forward declarations: DCTelnet.h only uses pointers to these. */
struct DrawInfo;
struct List;
struct Menu;
struct MenuItem;
struct MsgPort;
struct NewGadget;
struct NewWindow;
struct RastPort;
struct Screen;
struct Task;
struct TextFont;
struct Window;

#endif /* HOST_EXEC_TYPES_H */
