
// DCTelnet - ADDRESS BOOK, EDITPROFILE, FUNCTION KEYS AND SCROLLBACK GUI

#define __USE_SYSBASE

#ifdef __VBCC__
    #pragma dontwarn 306
#endif
#include <proto/exec.h>               // AllocMem(), AddTail(), FreeMem(), WaitPort(), Remove()
#include <proto/dos.h>                // Open(), Close(), FRead(), FWrite()
#include <proto/intuition.h>          // OpenWindow(),CloseWindow(), NewObjectA() but no NewObject()
#include <proto/graphics.h>           // Move(), SetAPen(), Text(), SetFont(), Draw()
#include <proto/gadtools.h>           // LISTVIEW_KIND, BUTTON_KIND, GTLV_Labels...
#include <proto/icon.h>               // GetDiskObjectNew(), FreeDiskObject()
#include <proto/layers.h>             // InstallLayerHook() (dialog base)
#include <intuition/screens.h>          // BACKGROUNDPEN (dialog base paint)
#ifdef __VBCC__
    #pragma popwarn
#endif
#include <string.h>                   // memcpy(), strlen()
#include <ctype.h>                    // tolower(), toupper()
#include "abook.h"                    // required
#include "edit.h"                     // required
#include "guis.h"
#include "DCTelnet.h"
#include "requesters.h"
#include "utils.h"
#include "site_prefs.h"

/* TEMPORARY DIAGNOSTIC: state snapshot at dialog open (PROGDIR:Dlg.txt).
 * Answers whether broken dialogs see a stale screen/pens/VisualInfo:
 * "name entryid wb depth private vi di ngadgets". */
void DlgDump(const char *which, int ngadgets)
{
    static char buf[128];
    char *p = buf;
    BPTR fh;
    ULONG v;
    int n;

    while (*which)
        *p++ = *which++;
    *p++ = ' ';
    v = (ULONG)sessionSettingsId;
    for (n = 7; n >= 0; n--)
    {
        UBYTE nib = (UBYTE)((v >> (n * 4)) & 0xF);
        *p++ = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
    *p++ = ' ';
    *p++ = (char)('0' + (isRunningOnWB ? 1 : 0));
    *p++ = (char)('0' + (prefs.DisplayDepth / 100));
    *p++ = (char)('0' + (prefs.DisplayDepth / 10) % 10);
    *p++ = (char)('0' + prefs.DisplayDepth % 10);
    *p++ = ' ';
    *p++ = (char)('0' + (UsePrivateUiPens() ? 1 : 0));
    *p++ = (char)('0' + (visualInfos ? 1 : 0));
    *p++ = (char)('0' + (drawInfo ? 1 : 0));
    *p++ = ' ';
    *p++ = (char)('0' + ngadgets / 10);
    *p++ = (char)('0' + ngadgets % 10);
    *p++ = '\r';
    *p++ = '\n';

    fh = Open("PROGDIR:Dlg.txt", MODE_NEWFILE);
    if (fh)
    {
        Write(fh, buf, (LONG)(p - buf));
        Close(fh);
    }
}

/* Gray dialog base on 256-colour screens: a BackFill hook, which paints
 * only damaged areas and can never cover gadgets (a plain RectFill after
 * OpenWindow demonstrably hid every GadTools frame on fullscreen while
 * Workbench -- no fill -- stayed fine). Black TEXTPEN labels stay readable.
 * Fixed-size SMART_REFRESH dialogs keep the fill in their bitmap. */
static ULONG DialogBackFillFunc(HOOK_A0 struct Hook *hook,
                                HOOK_A2 APTR object,
                                HOOK_A1 APTR message)
{
    struct RastPort *rp = (struct RastPort *)object;
    struct Rectangle *bounds = (struct Rectangle *)((UBYTE *)message + sizeof(APTR));
    ULONG saveFg;
    ULONG saveBg;
    UBYTE saveMd;

    if (UsePrivateUiPens() && drawInfo != NULL)
    {
        /* Save/restore: GadTools renders after us inherit RPort pens, and
         * a leftover gray FgPen camouflages every default-pen element. */
        saveFg = rp->FgPen;
        saveBg = rp->BgPen;
        saveMd = rp->DrawMode;
        SetAPen(rp, drawInfo->dri_Pens[BACKGROUNDPEN]);
        RectFill(rp, bounds->MinX, bounds->MinY, bounds->MaxX, bounds->MaxY);
        SetAPen(rp, saveFg);
        SetBPen(rp, saveBg);
        SetDrMd(rp, saveMd);
    }
    return 0;
}

static struct Hook dialogBackFillHook =
    { { NULL, NULL }, (HOOKFUNC)DialogBackFillFunc, NULL, NULL };

void PaintDialogBackground(struct Window *wnd)
{
    ULONG saveFg;
    ULONG saveBg;
    UBYTE saveMd;

    if (wnd == NULL || wnd->WLayer == NULL || !UsePrivateUiPens() || drawInfo == NULL)
        return;
    /* Initial base now (the hook below only fires on future damage).
     * Save/restore RPort pens (see DialogBackFillFunc). */
    saveFg = wnd->RPort->FgPen;
    saveBg = wnd->RPort->BgPen;
    saveMd = wnd->RPort->DrawMode;
    SetAPen(wnd->RPort, drawInfo->dri_Pens[BACKGROUNDPEN]);
    RectFill(wnd->RPort, wnd->BorderLeft, wnd->BorderTop,
             wnd->Width - wnd->BorderRight - 1,
             wnd->Height - wnd->BorderBottom - 1);
    SetAPen(wnd->RPort, saveFg);
    SetBPen(wnd->RPort, saveBg);
    SetDrMd(wnd->RPort, saveMd);
    InstallLayerHook(wnd->WLayer, &dialogBackFillHook);
}

struct BookStruct
{
    char    name[32];
    char    host[52];
    UWORD    port;
    ULONG   lastConnect;
    char    username[42];
    char    password[42];
    /* Per-entry settings (upstream issue #10): 0 = this entry uses the
     * global settings, otherwise the id of its PROGDIR:Sites/<id>.prefs
     * sidecar file. Old DCTelnet versions round-trip these bytes untouched,
     * so a book saved here still loads there (entries use global settings).
     * Shipped books have these bytes zeroed, i.e. settingsId == 0. */
    ULONG   settingsId;
    char    res[78];
};

/* The on-disk record is a raw dump with an implicit 256-byte stride and no
 * version: a size change silently misreads every existing DCTelnet.Book. */
typedef char BookStruct_size_check[sizeof(struct BookStruct) == 256 ? 1 : -1];

static BOOL EditProfile(struct BookStruct *book, struct List *bookList);


static struct Window         *aBookWnd;           // "Address Book" window
static struct Gadget         *aBookGList;         // "Address Book" window GList
static struct Gadget         *aBookGadgets[6];    // "Address Book" window Gadgets
#define aBookWidth 420
#define aBookHeight 132
static struct TextAttr        Attr;
static UWORD                  FontX, FontY;
UWORD                  OffX, OffY;

static ULONG lastsec, lasttic;

static UBYTE *SORT0Labels[] = {
    (UBYTE *)"Recent Connect (first)",
    (UBYTE *)"Oldest Connect (first)",
    (UBYTE *)"Name (A-Z)",
    (UBYTE *)"Name (Z-A)",
    NULL
};


static UBYTE aBookGTypes[] = {
    LISTVIEW_KIND,
    BUTTON_KIND,
    BUTTON_KIND,
    BUTTON_KIND,
    CYCLE_KIND,
    BUTTON_KIND
};


static struct MyNewGadget aBookNGad[] = {
    10, 5, 401, 72, NULL,
    19, 84, 93, 13, (UBYTE *)"_Connect",
    115, 84, 93, 13, (UBYTE *)"_Edit",
    212, 84, 93, 13, (UBYTE *)"_Add",
    160, 103, 203, 13, (UBYTE *)"List Sorted By:",
    308, 84, 93, 13, (UBYTE *)"_Delete",
};

static ULONG aBookGTags[] = {
    GTLV_Labels, 0, (GTLV_ShowSelected), (ULONG)NULL, GTLV_Selected, 0, (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GTCY_Labels), (ULONG)&SORT0Labels[ 0 ], (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE)
};

UWORD ComputeX( UWORD value )
{
    return(( UWORD )((( FontX * value ) + 4 ) / 8 ));
}

UWORD ComputeY( UWORD value )
{
    return(( UWORD )((( FontY * value ) + 4 ) / 8 ));
}

char MakeGadgets(struct MyNewGadget ProjectNGad[], struct Gadget *ProjectGadgets[], ULONG ProjectGTags[], struct Gadget *g, UBYTE ProjectGTypes[], UWORD Count)
{
    UWORD lc, tc;

    newGadget.ng_VisualInfo = visualInfos;
    newGadget.ng_TextAttr   = &Attr;

    for( lc = 0, tc = 0; lc < Count; lc++ )
    {
        //CopyMem((char * )&ProjectNGad[ lc ], (char * )&ng, (long)sizeof( struct MyNewGadget ));

        memcpy((char * )&newGadget, (char * )&ProjectNGad[ lc ], sizeof( struct MyNewGadget ));

        newGadget.ng_GadgetID     = lc;
        newGadget.ng_LeftEdge   = OffX + ComputeX( newGadget.ng_LeftEdge );
        newGadget.ng_TopEdge    = OffY + ComputeY( newGadget.ng_TopEdge );
        newGadget.ng_Width      = ComputeX( newGadget.ng_Width );
        newGadget.ng_Height     = ComputeY( newGadget.ng_Height);

        ProjectGadgets[ lc ] = g = CreateGadgetA((ULONG)ProjectGTypes[ lc ], g, &newGadget, ( struct TagItem * )&ProjectGTags[ tc ] );

        while( ProjectGTags[ tc ] ) tc += 2;
        tc++;

        if ( NOT g ) return( 2 );
    }
    return(0);
}


void ComputeFont( UWORD width, UWORD height )
{
    //Font = &Attr;
    Attr.ta_Name = (STRPTR)scr->RastPort.Font->tf_Message.mn_Node.ln_Name;
    Attr.ta_YSize = FontY = scr->RastPort.Font->tf_YSize;
    FontX = scr->RastPort.Font->tf_XSize;

    OffX = scr->WBorLeft;
    OffY = scr->RastPort.TxHeight + scr->WBorTop + 1;

    if (( ComputeX( width ) + OffX + scr->WBorRight ) > scr->Width )
        goto UseTopaz;
    if (( ComputeY( height ) + OffY + scr->WBorBottom ) > scr->Height )
        goto UseTopaz;
    return;

UseTopaz:
    Attr.ta_Name = "topaz.font";
    FontX = FontY = Attr.ta_YSize = 8;
}

static int OpenABookWindow( void )
{
    struct Gadget    *g;
    UWORD        ww, wh;
    long    x,y;

    ComputeFont( aBookWidth, aBookHeight );

    ww = ComputeX( aBookWidth );
    wh = ComputeY( aBookHeight );

    if ( ! ( g = CreateContext( &aBookGList )))
        return( 1L );

    if(MakeGadgets(aBookNGad, aBookGadgets, aBookGTags, g, aBookGTypes, aBook_CNT) != 0) return( 2L );

    x = ww + OffX + scr->WBorRight;
    y = wh + OffY + scr->WBorBottom;

    newWin.LeftEdge = (scr->Width - x) / 2;
    newWin.TopEdge = (scr->Height - y) / 2;
    newWin.Width = x;
    newWin.Height = y;
    newWin.IDCMPFlags = LISTVIEWIDCMP|BUTTONIDCMP|CYCLEIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY;
    newWin.Flags = WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP;
    newWin.FirstGadget = aBookGList;
    newWin.Title = "DCTelnet: Address Book";

    aBookWnd = OpenWindow(&newWin);
    if(!aBookWnd) return( 4L );
    PaintDialogBackground(aBookWnd);
    DlgDump("abook", 6);

/*    if ( ! ( aBookWnd = OpenWindowTags( NULL,
                WA_Left,    (scr->Width - x) / 2,
                WA_Top,        (scr->Height - y) / 2,
                WA_Width,    x,
                WA_Height,    y,
                WA_IDCMP,    LISTVIEWIDCMP|BUTTONIDCMP|CYCLEIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY,
                WA_Flags,    WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP,
                WA_Gadgets,    aBookGList,
                WA_Title,    "DCTelnet: Address Book",
                WA_CustomScreen,scr,
                TAG_DONE )))
    return( 4L );*/

    GT_RefreshWindow( aBookWnd, NULL );
    /* Unconditional: GT_RefreshWindow is damage-driven and will not repaint
     * what our base fill covered behind layers' back. */
    RefreshGadgets( aBookGList, aBookWnd, NULL );

    ComputeFont( aBookWidth, aBookHeight );

    DrawBevelBox( aBookWnd->RPort, OffX + ComputeX( 11 ),
                    OffY + ComputeY( 80 ),
                    ComputeX( 399 ),
                    ComputeY( 43 ),
                    GT_VisualInfo, visualInfos, GTBB_Recessed, TRUE, TAG_DONE );
    DrawBevelBox( aBookWnd->RPort, OffX + ComputeX( 3 ),
                    OffY + ComputeY( 2 ),
                    ComputeX( 415 ),
                    ComputeY( 128 ),
                    GT_VisualInfo, visualInfos, TAG_DONE );

    return( 0L );
}


static struct Node *FindNode(struct List *listviewlist, UWORD lastcode)
{
    struct Node *worknode;
    UWORD i = 0;

    // BF: Why this test? lastcode is UWORD, how could it be negative?
    if(/*lastcode != -1  &&*/  listviewlist->lh_TailPred != (struct Node *)listviewlist)
    {
        worknode = listviewlist->lh_Head;
        while(i < lastcode  &&  worknode)
        {
            i++;
            worknode = worknode->ln_Succ;
        }
        return(worknode);
    }
    return(0);
}


/**
 * @brief Sorts the bookmark list.
 *
 * Sorts the specified bookmark list according to the selected sort mode.
 *
 * @param list      Pointer to the bookmark list to sort.
 * @param sortMode  Sort mode (0..3) :  index in the SORT0Labels[] array
 */
static void SortABook(struct List *list, UWORD sortMode)
{
    register char *temp;
    struct Node *worknode, *nextnode;
    struct Node *inworknode, *innextnode;
    BOOL swap;
    const BOOL sortByLastConnect = (sortMode <= 1);
    const BOOL reverseSort = (sortMode == 1 || sortMode == 3);

    worknode = list->lh_Head;
    while (worknode)
    {
        nextnode = worknode->ln_Succ;
        if (! nextnode)
            break;

        inworknode = list->lh_Head;
        while (inworknode)
        {
            innextnode = inworknode->ln_Succ;
            if (! innextnode)
                break;

            swap = FALSE;

            if (sortByLastConnect)
            {
                ULONG a = ((struct BookStruct *)inworknode->ln_Name)->lastConnect;
                ULONG b = ((struct BookStruct *)worknode->ln_Name)->lastConnect;

                if (reverseSort)  // Oldest first
                {
                    if(a > b)  swap = TRUE;
                }
                else              // Most recent first
                {
                    if(a < b)  swap = TRUE;
                }
            }
            else
            {
                int cmp = stricmp(inworknode->ln_Name, worknode->ln_Name);

                if (reverseSort)  // Z -> A
                {
                    if (cmp < 0)  swap = TRUE;
                }
                else              // A -> Z
                {
                    if (cmp > 0)  swap = TRUE;
                }
            }

            if (swap)
            {
                temp = inworknode->ln_Name;
                inworknode->ln_Name = worknode->ln_Name;
                worknode->ln_Name = temp;
            }

            inworknode = innextnode;
        }

        worknode = nextnode;
    }
}

/**
 * Opens and manages the Address Book window.
 *
 * This function:
 * - Loads the address book entries from disk
 * - Displays them in a listview
 * - Allows the user to connect, add, edit, delete and sort entries
 * - Optionally initiates a connection to the selected host
 * - Saves modifications back to disk before exiting
 *
 * The function is modal and returns only when the user
 * closes the Address Book window or initiates a connection.
 */

/* -- Per-entry settings sidecar files (upstream issue #10) --
 * Load/Save/Alloc are unused until the connect path (phase 3) and the edit
 * UI (phase 4) call them; DeleteEntrySettings is wired below already. */

#define SITES_DIR "PROGDIR:Sites"

/* Highest settingsId in use + 1, so a new id never reuses a live file's.
 * Returns 0 when the id space is exhausted (caller reports failure). */
ULONG AllocEntrySettingsId(struct List *bookList)
{
    struct Node *worknode, *nextnode;
    ULONG max = 0, id;

    worknode = bookList->lh_Head;
    while (1)
    {
        nextnode = worknode->ln_Succ;
        if (!nextnode) break;

        id = ((struct BookStruct *)worknode->ln_Name)->settingsId;
        if (id > max)
            max = id;

        worknode = nextnode;
    }

    if (max == (ULONG)~0UL)
        return 0;
    return max + 1;
}

/* Reads PROGDIR:Sites/<settingsId>.prefs. FALSE = no settings (missing file,
 * bad magic, short read): the entry uses the global settings. */
BOOL LoadEntrySettings(ULONG settingsId, struct PrefsStruct *settings)
{
    char path[SITE_PREFS_PATH_LEN];
    UBYTE filebuf[4 + sizeof(struct PrefsStruct)];
    BPTR fh;
    LONG got;

    if (settingsId == 0 || settings == NULL)
        return FALSE;
    if (SitePrefs_FileName(settingsId, path, sizeof(path)) == NULL)
        return FALSE;

    fh = Open(path, MODE_OLDFILE);
    if (!fh)
        return FALSE;
    got = Read(fh, filebuf, sizeof(filebuf));
    Close(fh);

    if (got < 0)
        return FALSE;
    return SitePrefs_Decode(filebuf, (size_t)got, settings);
}

/* Writes PROGDIR:Sites/<settingsId>.prefs, creating PROGDIR:Sites on demand. */
BOOL SaveEntrySettings(ULONG settingsId, const struct PrefsStruct *settings)
{
    char path[SITE_PREFS_PATH_LEN];
    UBYTE filebuf[4 + sizeof(struct PrefsStruct)];
    BPTR fh, lock;

    if (settingsId == 0 || settings == NULL)
        return FALSE;
    if (SitePrefs_FileName(settingsId, path, sizeof(path)) == NULL)
        return FALSE;
    if (SitePrefs_Encode(settings, filebuf, sizeof(filebuf)) == 0)
        return FALSE;

    fh = Open(path, MODE_NEWFILE);
    if (!fh)
    {
        lock = CreateDir(SITES_DIR);
        if (lock)
            UnLock(lock);
        fh = Open(path, MODE_NEWFILE);
    }
    if (!fh)
        return FALSE;

    if (Write(fh, filebuf, sizeof(filebuf)) != (LONG)sizeof(filebuf))
    {
        Close(fh);
        return FALSE;
    }
    Close(fh);
    return TRUE;
}

/* Deletes PROGDIR:Sites/<settingsId>.prefs; 0 and missing files are no-ops. */
void DeleteEntrySettings(ULONG settingsId)
{
    char path[SITE_PREFS_PATH_LEN];

    if (settingsId == 0)
        return;
    if (SitePrefs_FileName(settingsId, path, sizeof(path)) == NULL)
        return;
    DeleteFile(path);
}

void AddressBook(void)
{
    struct IntuiMessage *message;
    struct Gadget *gad;
    struct BookStruct *book, *conbook=NULL;
    struct List *listviewlist;
    struct Node *worknode, *nextnode;
    UWORD lastcode = 0;
    UWORD code;
    ULONG class;
    ULONG sec, tic;
    BPTR fh;
    char readfin = FALSE;
    char ret = FALSE;
    char save = FALSE;
    char subdone = FALSE;
    struct PrefsStruct entrySettings;
    BOOL haveEntrySettings = FALSE;

    listviewlist = AllocMem(sizeof(struct List), MEMF_PUBLIC|MEMF_CLEAR);
    if(!listviewlist) return;
    listviewlist->lh_TailPred = (struct Node *)listviewlist;
    listviewlist->lh_Head = (struct Node *)&listviewlist->lh_Tail;

    // Load existing address book entries from disk
    fh = Open(bookFilename, MODE_OLDFILE);
    if(fh)
    {
        while(!readfin)
        {
            book = AllocMem(sizeof(struct BookStruct), MEMF_PUBLIC);
            if(book)
            {
                // Read one address book entry
                if(FRead(fh, book, sizeof(struct BookStruct), 1))
                {
                    // Create a list node pointing to this entry
                    worknode = AllocMem(sizeof(struct Node), MEMF_PUBLIC|MEMF_CLEAR);
                    if(worknode)
                    {
                        worknode->ln_Name = book->name;
                        AddTail(listviewlist, worknode);
                    } else {
                        FreeMem(book, sizeof(struct BookStruct));
                        readfin = TRUE;
                    }
                } else {
                    FreeMem(book, sizeof(struct BookStruct));
                    readfin = TRUE;
                }
            } else {
                readfin = TRUE;
            }
        }
        Close(fh);
    }

    // Sort using the default Address Book sort order
    SortABook(listviewlist, 0);

    // Attach the list to the listview gadget
    aBookGTags[1] = (unsigned long)listviewlist;

    // Open the Address Book window
    if(OpenABookWindow() == RETURN_OK)
    {
        //GT_SetGadgetAttrs(aBookGadgets[GD_LIST],aBookWnd,0,GTLV_Labels,listviewlist,TAG_DONE);
        // Main event loop
        while(!subdone)
        {
            WaitPort(aBookWnd->UserPort);
            while (message = GT_GetIMsg(aBookWnd->UserPort))
            {
                    gad   = (struct Gadget *)message->IAddress;
                class = message->Class;
                code  = message->Code;
                GT_ReplyIMsg(message);
                switch (class)
                {
                case IDCMP_VANILLAKEY:  // Keyboard shortcuts
                    switch(toupper(code))
                    {
                        // Directly jump to the part that manage the required button
                        case 'C':
                            goto connect;
                        case 'E':
                            goto edit;
                        case 'A':
                            goto add;
                        case 'D':
                            goto delete;
                    }
                    break;

                case IDCMP_CLOSEWINDOW:     // User clicked the close gadget
                    subdone = TRUE;
                    break;

                // A action button has been pressed in the Address Book:
                case IDCMP_GADGETUP:
                    switch(gad->GadgetID)
                    {
                    case GD_SORT:       //  Sorting mode changed
                        SortABook(listviewlist, code);

                        // Refresh the ListView to reflect the new sort order while preserving the
                        // current selection index.
                        GT_SetGadgetAttrs(aBookGadgets[GD_LIST], aBookWnd, NULL,
                                          GTLV_Labels,   listviewlist,
                                          GTLV_Selected, lastcode,
                                          TAG_DONE);
                        break;

                    case GD_LIST:       // Item selected in listview or double-clicked
                        CurrentTime(&sec, &tic);
                        if(DoubleClick(lastsec, lasttic, sec, tic) && lastcode == code)
                        {
                            lastcode = code;
                            goto connect;
                        }
                        lastsec = sec;
                        lasttic = tic;
                        lastcode = code;
                        break;

                    case GD_CONNECT:     // Connect to selected entry
connect:
                        if(worknode = FindNode(listviewlist, lastcode))
                        {
                            conbook = (struct BookStruct *)worknode->ln_Name;
                            subdone = TRUE;
                            ret = TRUE;
                            save = TRUE;
                        }
                        break;

                    case GD_DELETE:      // Delete selected entry
delete:
                        if(worknode = FindNode(listviewlist, lastcode))
                        {
                            if (ConfirmRequester(isRunningOnWB ? NULL : win,"Delete|Cancel",
                                                         "Delete \"%s\"?", worknode->ln_Name))
                            //mysprintf(buf, "Delete \042%s\042?", worknode->ln_Name);
                            //if(rtEZRequestA(buf, "Delete|Cancel", NULL, NULL, (struct TagItem *)&reqtoolsTags))
                            {
                                Remove(worknode);
                                DeleteEntrySettings(((struct BookStruct *)worknode->ln_Name)->settingsId);
                                FreeMem(worknode->ln_Name, sizeof(struct BookStruct));
                                FreeMem(worknode, sizeof(struct Node));
                                lastcode--;
                                // BF: Why this test? lastcode is UWORD, how could it be negative?
                                //if(lastcode < 0) lastcode = 0;
                                GT_SetGadgetAttrs(aBookGadgets[GD_LIST],aBookWnd,0,GTLV_Labels,listviewlist,GTLV_Selected,lastcode,TAG_DONE);
                                save = TRUE;
                            }
                        }
                        break;

                    case GD_EDIT:        // Edit selected entry
edit:
                        if(worknode = FindNode(listviewlist, lastcode))
                        {
                            if(EditProfile((struct BookStruct *)worknode->ln_Name, listviewlist))
                            {
                                GT_SetGadgetAttrs(aBookGadgets[GD_LIST],aBookWnd,0,GTLV_Labels,listviewlist,GTLV_Selected,lastcode,TAG_DONE);
                                save = TRUE;
                            }
                        }
                        break;

                    case GD_NEW:        // Add a new entry
add:
                        book = AllocMem(sizeof(struct BookStruct), MEMF_PUBLIC|MEMF_CLEAR);
                        if(book)
                        {
                            strlcpy(book->name, "*new site*", sizeof(book->name));
                            strlcpy(book->host, "*ip/host here*", sizeof(book->host));
                            book->port = 23;
                            if(EditProfile(book, listviewlist))
                            {
                                worknode = AllocMem(sizeof(struct Node), MEMF_PUBLIC|MEMF_CLEAR);
                                if(worknode)
                                {
                                    worknode->ln_Name = book->name;
                                    AddTail(listviewlist, worknode);
                                    GT_SetGadgetAttrs(aBookGadgets[GD_LIST],aBookWnd,0,GTLV_Labels,listviewlist,GTLV_Selected,lastcode,TAG_DONE);
                                    save = TRUE;
                                } else
                                    FreeMem(book, sizeof(struct BookStruct));
                            } else
                                FreeMem(book, sizeof(struct BookStruct));
                        }
                        break;
                    }
                }
            }
        }
    }

    if ( aBookWnd ) CloseWindow( aBookWnd );

    if ( aBookGList ) { FreeGadgets( aBookGList ); aBookGList = NULL; }

    // Initiate connection if requested
    if(ret)
    {
        /* Per-entry settings (issue #10): end any live session first (this
         * restores the globals), then install this entry's settings and
         * reopen the display when they need it -- all BEFORE connecting. */
        haveEntrySettings = conbook->settingsId != 0
            && LoadEntrySettings(conbook->settingsId, &entrySettings);

        DisconnectBeforeEntryConnect();
        ApplyEntrySettings(haveEntrySettings ? &entrySettings : NULL,
                           haveEntrySettings ? conbook->settingsId : 0);

        tcpPort = conbook->port;
        if(BeginServerConnection(conbook->host, conbook->port) == RETURN_OK)
        {
            conbook->lastConnect = mytime();
            strlcpy(username, conbook->username, sizeof(username));
            strlcpy(password, conbook->password, sizeof(password));
        }
    }

    // Save address book back to disk if modified
    if(save) fh = Open(bookFilename, MODE_NEWFILE); else fh = 0;

    worknode = listviewlist -> lh_Head;
    while(1)
    {
        nextnode = worknode -> ln_Succ;
        if(!nextnode) break;

        if(fh) FWrite(fh, worknode->ln_Name, sizeof(struct BookStruct), 1);

        FreeMem(worknode->ln_Name, sizeof(struct BookStruct));
        FreeMem(worknode, sizeof(struct Node));
        worknode = nextnode;
    }
    FreeMem(listviewlist, sizeof(struct List));
    if(fh) Close(fh);
}


static struct Window         *editProfileWnd;           // "Edit Address Book Profile" window
static struct Gadget         *editProfileGList;         // "Edit Address Book Profile" window GList
static struct Gadget         *editProfileGadgets[20];   // "Edit Address Book Profile" window gadgets
#define editProfileWidth 450
#define editProfileHeight 173

static UBYTE editProfileGTypes[] = {
    STRING_KIND,
    STRING_KIND,
    TEXT_KIND,
    BUTTON_KIND,
    BUTTON_KIND,
    INTEGER_KIND,
    STRING_KIND,
    STRING_KIND,
    TEXT_KIND,
    BUTTON_KIND,
    BUTTON_KIND,
    CHECKBOX_KIND,
    TEXT_KIND,
    BUTTON_KIND,
    BUTTON_KIND,
    TEXT_KIND,
    CHECKBOX_KIND,
    CHECKBOX_KIND,
    CHECKBOX_KIND,
    CHECKBOX_KIND
};

static struct MyNewGadget editProfileNGad[] = {
    120, 5, 317, 13, (UBYTE *)"_Site Name:",
    120, 21, 317, 13, (UBYTE *)"_Address:",
    121, 37, 177, 13, (UBYTE *)"Last Called:",
    3, 158, 101, 13, (UBYTE *)"_Ok",
    345, 158, 101, 13, (UBYTE *)"_Cancel",
    365, 37, 72, 13, (UBYTE *)"_Port:",
    120, 53, 317, 13, (UBYTE *)"_Username:",
    120, 68, 317, 13, (UBYTE *)"Pass_word:",
    120, 83, 177, 13, (UBYTE *)"Settings:",
    3, 98, 175, 13, (UBYTE *)"PETSCII _Mode",
    185, 98, 100, 13, (UBYTE *)"_Font...",
    292, 98, 145, 13, (UBYTE *)"",
    3, 113, 150, 13, (UBYTE *)"Screen Mo_de...",
    160, 113, 277, 13, (UBYTE *)"",
    3, 128, 100, 13, (UBYTE *)"_Echo",
    108, 128, 100, 13, (UBYTE *)"_Raw",
    213, 128, 110, 13, (UBYTE *)"_BS/DEL",
    328, 128, 109, 13, (UBYTE *)"CR+_LF",
    3, 143, 180, 13, (UBYTE *)"Use Curren_t Settings",
    267, 143, 180, 13, (UBYTE *)"Use _Global Settings",
};

static ULONG editProfileGTags[] = {
    GTST_String, 0, (GTST_MaxChars), 31, (GT_Underscore), '_', (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, 0, (GTST_MaxChars), 51, (GT_Underscore), '_', (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTTX_Text, 0, (GTTX_Border), TRUE, (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GTIN_Number), 0, (GTIN_MaxChars), 9, (GT_Underscore), '_', (GTIN_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, 0, (GTST_MaxChars), 41, (GT_Underscore), '_', (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, 0, (GTST_MaxChars), 41, (GT_Underscore), '_', (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTTX_Text, 0, (GTTX_Border), TRUE, (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (GTCB_Checked), FALSE, (TAG_DONE),
    GTTX_Text, 0, (GTTX_Border), TRUE, (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    GTTX_Text, 0, (GTTX_Border), TRUE, (TAG_DONE),
    (GT_Underscore), '_', (GTCB_Checked), FALSE, (TAG_DONE),
    (GT_Underscore), '_', (GTCB_Checked), FALSE, (TAG_DONE),
    (GT_Underscore), '_', (GTCB_Checked), FALSE, (TAG_DONE),
    (GT_Underscore), '_', (GTCB_Checked), FALSE, (TAG_DONE)
};

/* Initial-value slots in editProfileGTags (must match the table above;
 * new gadgets go at the end so these never shift). */
#define ETAG_SITE 1
#define ETAG_ADDRESS 10
#define ETAG_LAST 19
#define ETAG_PORT 30
#define ETAG_USERNAME 39
#define ETAG_PASSWORD 48
#define ETAG_SETTINGS 57
#define ETAG_PETSCII 69
#define ETAG_FONTNAME 73
#define ETAG_SCREEN 84
#define ETAG_ECHO 90
#define ETAG_RAW 95
#define ETAG_BSDEL 100
#define ETAG_CRLF 105

// Draw the Edit Address Book Profile window
static int OpenEditProfileWindow( void )
{
    struct Gadget    *g;
    UWORD        ww, wh;
    long x,y;

    ComputeFont( editProfileWidth, editProfileHeight );

    ww = ComputeX( editProfileWidth );
    wh = ComputeY( editProfileHeight );

    if ( ! ( g = CreateContext( &editProfileGList )))
        return( 1L );

    if(MakeGadgets(editProfileNGad, editProfileGadgets, editProfileGTags, g, editProfileGTypes, editProfile_CNT) != 0) return( 2L );

    x = ww + OffX + scr->WBorRight;
    y = wh + OffY + scr->WBorBottom;

    newWin.LeftEdge = (scr->Width - x) / 2;
    newWin.TopEdge = (scr->Height - y) / 2;
    newWin.Width = x;
    newWin.Height = y;
    newWin.IDCMPFlags = STRINGIDCMP|TEXTIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY;
    newWin.Flags = WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP;
    newWin.FirstGadget = editProfileGList;
    newWin.Title = "Edit Address Book Profile";

    editProfileWnd = OpenWindow(&newWin);
    if(!editProfileWnd) return( 4L );
    /* DIAGNOSTIC 2026-09-25: base fill disabled here only -- do the field
     * frames and buttons come back? (Address Book keeps its fill.) */
    PaintDialogBackground(editProfileWnd);
    DlgDump("edit", editProfile_CNT);

    /*if ( ! ( editProfileWnd = OpenWindowTags( NULL,
                WA_Left,    (scr->Width - x) / 2,
                WA_Top,        (scr->Height - y) / 2,
                WA_Width,    x,
                WA_Height,    y,
                WA_IDCMP,    STRINGIDCMP|TEXTIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY,
                WA_Flags,    WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP,
                WA_Gadgets,    editProfileGList,
                WA_Title,    "Edit Address Book Profile",
                WA_CustomScreen,    scr,
                TAG_DONE )))
    return( 4L );*/

    GT_RefreshWindow( editProfileWnd, NULL );
    RefreshGadgets( editProfileGList, editProfileWnd, NULL );

    ComputeFont( editProfileWidth, editProfileHeight );

    DrawBevelBox( editProfileWnd->RPort, OffX + ComputeX( 3 ),
                    OffY + ComputeY( 1 ),
                    ComputeX( 444 ),
                    ComputeY( 141 ),
                    GT_VisualInfo, visualInfos, TAG_DONE );
    return( 0L );
}


/* The entry's staged content, made visible: "Global" or e.g.
 * "Own: PETSCII, WB". Describes the STAGED prefs (file content at dialog
 * open, live edits after) -- never the file, so uncommitted checkbox/font
 * changes show immediately. */
static void EditSettingsSummary(const struct PrefsStruct *staged,
                                ULONG settingsId, char *labelBuf, size_t labelLen)
{
    if (settingsId == 0)
    {
        strlcpy(labelBuf, "Global", labelLen);
    }
    else
    {
        mysprintf(labelBuf, "Own: %s%s", (staged->flags & FLAG_PETSCII_MODE) ? "PETSCII" : "ANSI",
                  (staged->flags & FLAG_USE_WORKBENCH) ? ", WB" : "");
    }
}

/* Erase a TEXT gadget's box: redisplay draws transparently, so a shorter
 * value would ghost over a longer previous one. */
static void EditEraseTextGadget(UWORD gadgetID)
{
    struct Gadget *gad = editProfileGadgets[gadgetID];
    ULONG saveFg = editProfileWnd->RPort->FgPen;

    if (UsePrivateUiPens() && drawInfo != NULL)
        SetAPen(editProfileWnd->RPort, drawInfo->dri_Pens[BACKGROUNDPEN]);
    else
        SetAPen(editProfileWnd->RPort, 0);
    RectFill(editProfileWnd->RPort, gad->LeftEdge, gad->TopEdge,
             gad->LeftEdge + gad->Width - 1, gad->TopEdge + gad->Height - 1);
    SetAPen(editProfileWnd->RPort, saveFg);
}

/* Same, pushed into the live label (window must be open). */
static void EditSettingsRefresh(const struct PrefsStruct *staged,
                                char *labelBuf, size_t labelLen, ULONG settingsId)
{
    EditSettingsSummary(staged, settingsId, labelBuf, labelLen);
    EditEraseTextGadget(GD_SETTINGS_LABEL);
    GT_SetGadgetAttrs(editProfileGadgets[GD_SETTINGS_LABEL], editProfileWnd, 0,
                      GTTX_Text, labelBuf, TAG_DONE);
}

/* Push the staged PETSCII/font/screen/toggles into the live gadgets
 * (window must be open). */
static void EditStagedRefresh(struct PrefsStruct *staged,
                              char *labelBuf, size_t labelLen, ULONG settingsId,
                              char *screenBuf)
{
    mysprintf(screenBuf, "%lux%lux%lu", (ULONG)staged->DisplayWidth,
              (ULONG)staged->DisplayHeight, (ULONG)staged->DisplayDepth);
    GT_SetGadgetAttrs(editProfileGadgets[GD_PETSCII], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & FLAG_PETSCII_MODE) ? TRUE : FALSE,
                      TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_FONT_TEXT], editProfileWnd, 0,
                      GTTX_Text, staged->fontname, TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_SCREEN_TEXT], editProfileWnd, 0,
                      GTTX_Text, screenBuf, TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_ECHO], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & FLAG_LOCAL_ECHO) ? TRUE : FALSE,
                      TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_RAW], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & FLAG_RAW_CONNECTION) ? TRUE : FALSE,
                      TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_BSDEL], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & FLAG_BS_DEL_SWAP) ? TRUE : FALSE,
                      TAG_DONE);
    GT_SetGadgetAttrs(editProfileGadgets[GD_CRLF], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & FLAG_RETURN_CRLF) ? TRUE : FALSE,
                      TAG_DONE);
    EditSettingsRefresh(staged, labelBuf, labelLen, settingsId);
}

/* "Use Current Settings": the entry gets its own settings on OK (a fresh
 * id when it has none). Snapshots the live effective prefs into the
 * staged copy, which the checkbox/font/screen buttons may then adjust. */
static void EditUseCurrentSettings(struct List *bookList, ULONG *newSettingsId,
                                   struct PrefsStruct *staged,
                                   char *labelBuf, size_t labelLen,
                                   char *screenBuf)
{
    if (*newSettingsId == 0)
    {
        *newSettingsId = AllocEntrySettingsId(bookList);
        if (*newSettingsId == 0)
        {
            SimpleReq("Could not allocate a settings id.");
            return;
        }
    }
    *staged = prefs;
    EditStagedRefresh(staged, labelBuf, labelLen, *newSettingsId, screenBuf);
}

/* "Use Global Settings": drop the entry's settings on OK (file deleted). */
static void EditUseGlobalSettings(ULONG *newSettingsId,
                                  struct PrefsStruct *staged,
                                  char *labelBuf, size_t labelLen,
                                  char *screenBuf)
{
    *newSettingsId = 0;
    *staged = globalPrefs;
    EditStagedRefresh(staged, labelBuf, labelLen, 0, screenBuf);
}

/* Staged flag checkbox (button or key): toggle and force the gadget to
 * match, so both stay in sync however GadTools toggles. */
static void EditToggleFlag(struct PrefsStruct *staged, ULONG flag, UWORD gadgetID,
                           char *labelBuf, size_t labelLen, ULONG settingsId)
{
    if (staged->flags & flag)
        staged->flags &= ~flag;
    else
        staged->flags |= flag;
    GT_SetGadgetAttrs(editProfileGadgets[gadgetID], editProfileWnd, 0,
                      GTCB_Checked, (staged->flags & flag) ? TRUE : FALSE,
                      TAG_DONE);
    EditSettingsRefresh(staged, labelBuf, labelLen, settingsId);
}

/* Screen button: pick straight into the staged copy. */
static void EditPickScreen(struct PrefsStruct *staged, char *screenBuf)
{
    ULONG id = staged->DisplayID;
    UWORD w = staged->DisplayWidth, h = staged->DisplayHeight, d = staged->DisplayDepth;

    if (ScreenModeRequester(editProfileWnd, &id, &w, &h, &d, ScreenMaxDepth()))
    {
        staged->DisplayID = id;
        staged->DisplayWidth = w;
        staged->DisplayHeight = h;
        staged->DisplayDepth = d;
        mysprintf(screenBuf, "%lux%lux%lu", (ULONG)w, (ULONG)h, (ULONG)d);
        EditEraseTextGadget(GD_SCREEN_TEXT);
        GT_SetGadgetAttrs(editProfileGadgets[GD_SCREEN_TEXT], editProfileWnd, 0,
                          GTTX_Text, screenBuf, TAG_DONE);
    }
}

/* PETSCII checkbox (button or 'M' key) is handled by EditToggleFlag. */

/* Font button (or 'F' key): pick straight into the staged copy. */
static void EditPickFont(struct PrefsStruct *staged)
{
    if (FontRequester(editProfileWnd,
                      staged->fontname, sizeof(staged->fontname),
                      &staged->fontsize))
    {
        EditEraseTextGadget(GD_FONT_TEXT);
        GT_SetGadgetAttrs(editProfileGadgets[GD_FONT_TEXT], editProfileWnd, 0,
                          GTTX_Text, staged->fontname, TAG_DONE);
    }
}

/*
Opens the Edit Address Book Profile dialog.

Updates the book structure only if the user validates the changes.

return TRUE  if the user validated the changes (OK)
       FALSE if the user cancelled or closed the window
 */
static BOOL EditProfile(struct BookStruct *book, struct List *bookList)
{
    char strLastTime[2 * LEN_DATSTRING];
    char strSettings[24];
    char strScreen[32];
    ULONG newSettingsId;
    struct PrefsStruct stagedSettings;
    struct IntuiMessage *message;
    struct Gadget *gad;
    ULONG class;
    UWORD code;
    char subdone = FALSE;
    BOOL ret = FALSE;

    // The entry's settings id, staged until OK (Cancel changes nothing).
    // stagedSettings starts as the sidecar content (or the globals) and
    // takes the checkbox/font/button edits; OK writes it to the sidecar.
    newSettingsId = book->settingsId;
    stagedSettings = globalPrefs;
    if (newSettingsId != 0)
        LoadEntrySettings(newSettingsId, &stagedSettings);  /* stale id keeps globals */

    // Initialize gadget fields with current book data
    editProfileGTags[ETAG_SITE] = (unsigned long)book->name;
    editProfileGTags[ETAG_ADDRESS] = (unsigned long)book->host;
    myctime(book->lastConnect, strLastTime, sizeof(strLastTime));
    editProfileGTags[ETAG_LAST] = (unsigned long)strLastTime;
    editProfileGTags[ETAG_PORT] = (unsigned long)book->port;
    editProfileGTags[ETAG_USERNAME] = (unsigned long)book->username;
    editProfileGTags[ETAG_PASSWORD] = (unsigned long)book->password;
    EditSettingsSummary(&stagedSettings, newSettingsId, strSettings, sizeof(strSettings));
    editProfileGTags[ETAG_SETTINGS] = (unsigned long)strSettings;
    mysprintf(strScreen, "%lux%lux%lu", (ULONG)stagedSettings.DisplayWidth,
              (ULONG)stagedSettings.DisplayHeight, (ULONG)stagedSettings.DisplayDepth);
    editProfileGTags[ETAG_SCREEN] = (unsigned long)strScreen;
    editProfileGTags[ETAG_PETSCII] =
        (stagedSettings.flags & FLAG_PETSCII_MODE) ? TRUE : FALSE;
    editProfileGTags[ETAG_FONTNAME] = (unsigned long)stagedSettings.fontname;
    editProfileGTags[ETAG_ECHO] =
        (stagedSettings.flags & FLAG_LOCAL_ECHO) ? TRUE : FALSE;
    editProfileGTags[ETAG_RAW] =
        (stagedSettings.flags & FLAG_RAW_CONNECTION) ? TRUE : FALSE;
    editProfileGTags[ETAG_BSDEL] =
        (stagedSettings.flags & FLAG_BS_DEL_SWAP) ? TRUE : FALSE;
    editProfileGTags[ETAG_CRLF] =
        (stagedSettings.flags & FLAG_RETURN_CRLF) ? TRUE : FALSE;

    // Open the Edit Profile window
    if(OpenEditProfileWindow() == RETURN_OK)
    {
        SetWaitPointer(aBookWnd);  // Set "wait" mouse pointer to indicate modal operation

        // Activate the first gadget (Site Name) to receive keyboard input
        ActivateGadget(editProfileGadgets[GD_SITE], editProfileWnd, 0);

        while(!subdone)
        {
            register struct Gadget *vgad = NULL;

            WaitPort(editProfileWnd->UserPort); // Wait for input events

            while (message = GT_GetIMsg(editProfileWnd->UserPort))
            {
                gad   = (struct Gadget *)message->IAddress; // Gadget associated with the message
                class = message->Class;
                code  = message->Code;                      // Key code for keyboard events
                GT_ReplyIMsg(message);                      // Acknowledge message

                switch (class)
                {
                case IDCMP_CLOSEWINDOW:      // User clicked the close gadget
                    subdone = TRUE;
                    ret = FALSE;
                    break;

                case IDCMP_VANILLAKEY:       // Keyboard input (letters, Enter, etc.)
                    switch(toupper(code))
                    {
                        case 'O':   // OK
                            subdone = TRUE;
                            ret = TRUE;
                            break;
                        case 'C':   // Cancel
                            subdone = TRUE;
                            ret = FALSE;
                            break;
                        // Keyboard shortcuts to jump to a specific gadget:
                        case 'S':  vgad = editProfileGadgets[GD_SITE];        break;
                        case 'A':  vgad = editProfileGadgets[GD_ADDRESS];     break;
                        case 'P':  vgad = editProfileGadgets[GD_PORT];        break;
                        case 'U':  vgad = editProfileGadgets[GD_USERNAME];    break;
                        case 'W':  vgad = editProfileGadgets[GD_PASSWORD];    break;
                        case 'T':  EditUseCurrentSettings(bookList, &newSettingsId,
                                                          &stagedSettings,
                                                          strSettings, sizeof(strSettings),
                                                          strScreen);
                                   break;
                        case 'G':  EditUseGlobalSettings(&newSettingsId,
                                                         &stagedSettings,
                                                         strSettings, sizeof(strSettings),
                                                         strScreen);
                                   break;
                        case 'M':  EditToggleFlag(&stagedSettings, FLAG_PETSCII_MODE,
                                                  GD_PETSCII,
                                                  strSettings, sizeof(strSettings),
                                                  newSettingsId);
                                   break;
                        case 'F':  EditPickFont(&stagedSettings);
                                   break;
                        case 'E':  EditToggleFlag(&stagedSettings, FLAG_LOCAL_ECHO,
                                                  GD_ECHO,
                                                  strSettings, sizeof(strSettings),
                                                  newSettingsId);
                                   break;
                        case 'R':  EditToggleFlag(&stagedSettings, FLAG_RAW_CONNECTION,
                                                  GD_RAW,
                                                  strSettings, sizeof(strSettings),
                                                  newSettingsId);
                                   break;
                        case 'B':  EditToggleFlag(&stagedSettings, FLAG_BS_DEL_SWAP,
                                                  GD_BSDEL,
                                                  strSettings, sizeof(strSettings),
                                                  newSettingsId);
                                   break;
                        case 'L':  EditToggleFlag(&stagedSettings, FLAG_RETURN_CRLF,
                                                  GD_CRLF,
                                                  strSettings, sizeof(strSettings),
                                                  newSettingsId);
                                   break;
                        case 'D':  EditPickScreen(&stagedSettings, strScreen);
                                   break;
                    }
                    if(vgad) ActivateGadget(vgad, editProfileWnd, 0); // Focus gadget
                    break;

                case IDCMP_GADGETUP:         // Mouse released over a gadget
                    switch(gad->GadgetID)
                    {
                    case GD_OK:
                        subdone = TRUE;
                        ret = TRUE;
                        break;
                    case GD_CANCEL:
                        subdone = TRUE;
                        ret = FALSE;
                        break;
                    case GD_USE_CURRENT:
                        EditUseCurrentSettings(bookList, &newSettingsId,
                                               &stagedSettings,
                                               strSettings, sizeof(strSettings),
                                               strScreen);
                        break;
                    case GD_USE_GLOBAL:
                        EditUseGlobalSettings(&newSettingsId,
                                              &stagedSettings,
                                              strSettings, sizeof(strSettings),
                                              strScreen);
                        break;
                    case GD_PETSCII:
                        EditToggleFlag(&stagedSettings, FLAG_PETSCII_MODE,
                                       GD_PETSCII,
                                       strSettings, sizeof(strSettings),
                                       newSettingsId);
                        break;
                    case GD_FONT_BUTTON:
                        EditPickFont(&stagedSettings);
                        break;
                    case GD_SCREEN_BUTTON:
                        EditPickScreen(&stagedSettings, strScreen);
                        break;
                    case GD_ECHO:
                        EditToggleFlag(&stagedSettings, FLAG_LOCAL_ECHO,
                                       GD_ECHO,
                                       strSettings, sizeof(strSettings),
                                       newSettingsId);
                        break;
                    case GD_RAW:
                        EditToggleFlag(&stagedSettings, FLAG_RAW_CONNECTION,
                                       GD_RAW,
                                       strSettings, sizeof(strSettings),
                                       newSettingsId);
                        break;
                    case GD_BSDEL:
                        EditToggleFlag(&stagedSettings, FLAG_BS_DEL_SWAP,
                                       GD_BSDEL,
                                       strSettings, sizeof(strSettings),
                                       newSettingsId);
                        break;
                    case GD_CRLF:
                        EditToggleFlag(&stagedSettings, FLAG_RETURN_CRLF,
                                       GD_CRLF,
                                       strSettings, sizeof(strSettings),
                                       newSettingsId);
                        break;
                    }
                    break;
                }
            }
        }

        // Copy gadget values to the BookStruct if user pressed OK:
        if (ret)
        {
            strlcpy(book->name,
                    ((struct StringInfo *)editProfileGadgets[GD_SITE]->SpecialInfo)->Buffer,
                    sizeof(book->name));
            strlcpy(book->host,
                    ((struct StringInfo *)editProfileGadgets[GD_ADDRESS]->SpecialInfo)->Buffer,
                    sizeof(book->host));
            book->port = ((struct StringInfo *)editProfileGadgets[GD_PORT]->SpecialInfo)->LongInt;
            strlcpy(book->username,
                    ((struct StringInfo *)editProfileGadgets[GD_USERNAME]->SpecialInfo)->Buffer,
                    sizeof(book->username));
            strlcpy(book->password,
                    ((struct StringInfo *)editProfileGadgets[GD_PASSWORD]->SpecialInfo)->Buffer,
                    sizeof(book->password));

            /* Per-entry settings (issue #10), staged in this dialog:
             * a dropped id deletes its sidecar, a kept or new id writes
             * the staged copy (checkbox/font/buttons). Cancel above
             * skipped all of this. */
            if (newSettingsId != book->settingsId)
            {
                if (book->settingsId != 0)
                    DeleteEntrySettings(book->settingsId);
                book->settingsId = newSettingsId;
            }
            if (newSettingsId != 0
                && !SaveEntrySettings(newSettingsId, &stagedSettings))
                SimpleReq("Could not save the entry settings (PROGDIR:Sites).");
        }

        ClearPointer(aBookWnd); // Restore normal pointer
    }

    // Close window and free gadgets
    if ( editProfileWnd ) CloseWindow( editProfileWnd );
    if ( editProfileGList ) { FreeGadgets( editProfileGList ); editProfileGList = NULL; }

    return ret;
}


#include <intuition/imageclass.h>
#include <intuition/icclass.h>


enum    {    GAD_SCROLLER,
        GAD_UP,
        GAD_DOWN
    };

static APTR UpImage, DownImage;
static APTR UpArrow, DownArrow;
APTR Scroller;

void CloseScrollBack(void)
{
    if(scrollbackWin)
    {
        ClearMenuStrip(scrollbackWin);
        CloseWindow(scrollbackWin);    scrollbackWin = NULL;
        DisposeObject(Scroller);       Scroller=NULL;
        DisposeObject(UpArrow);        UpArrow = NULL;
        DisposeObject(DownArrow);      DownArrow = NULL;
        DisposeObject(UpImage);        UpImage = NULL;
        DisposeObject(DownImage);      DownImage = NULL;
    }
}

void RefreshListView(UWORD top)
{
    register struct RastPort *rp = scrollbackWin->RPort;
    struct Node *node = FindNode(scrollbackList, top);
    UWORD WWinTop = rp->Font->tf_YSize + scr->WBorTop + 2;
    UWORD y, i = 0;
    char print = TRUE;

    Move(rp, 5, WWinTop);
    WWinTop += rp->Font->tf_YSize;
    SetAPen(rp, drawInfo->dri_Pens[TEXTPEN]);
    while(1)
    {
        UWORD chars = (scrollbackWin->Width - 28) / rp->Font->tf_XSize;
        UWORD len;

        if(print)
        {
            if(!node->ln_Succ)
                print = FALSE;
            else {
                len = strlen(node->ln_Name);
                if(chars > len) chars = len;
            }
        }

        y = WWinTop + (i * rp->Font->tf_YSize);

        if(y > (scrollbackWin->Height-5)) break;

        Move(rp, 5, y);
        if(print) Text(rp, node->ln_Name, chars);
        EraseRect(rp, rp->cp_x, (rp->cp_y-rp->Font->tf_YSize)+2, scrollbackWin->Width - 24, rp->cp_y+1);

        if(print) node = node->ln_Succ;
        i++;
    }
}

void OpenScrollBack(UWORD sel)
{
    STATIC struct TagItem ArrowMappings[] =
    {
        GA_ID,    GA_ID,
        TAG_END
    };

    ULONG ArrowHeight;
    LONG SizeType;
    Object *SizeImage;

    if(prefs.sb_width > scr->Width) prefs.sb_width = scr->Width;
    if(prefs.sb_height > scr->Height) prefs.sb_height = scr->Height;

    if(scr->Flags & SCREENHIRES)
        SizeType = SYSISIZE_MEDRES;
    else
        SizeType = SYSISIZE_LOWRES;

    /*
     NewObject() allows an arbitrary number of tags. It is a varargs stub for NewObjectA().
     You specify a class either as a pointer (for a private class) or by its ID string (for public
     classes).  If the class pointer is NULL, then the classID is used.
    */
    if(SizeImage = NewObject(NULL,SYSICLASS,  // class
        SYSIA_Size,    SizeType,                 // 1st  tag (= key/value pair = property)
        SYSIA_Which,    SIZEIMAGE,            // 2nd  tag
        SYSIA_DrawInfo,    drawInfo,             // ...
    TAG_DONE))                                // terminator tag
    {
        ULONG SizeWidth, SizeHeight;

        GetAttr(IA_Width, SizeImage, &SizeWidth);
        GetAttr(IA_Height, SizeImage, &SizeHeight);

        DisposeObject(SizeImage);

        if(UpImage = NewObject(NULL, SYSICLASS,
            SYSIA_Size,    SizeType,
            SYSIA_Which,    UPIMAGE,
            SYSIA_DrawInfo,    drawInfo,
        TAG_DONE))
        {
            GetAttr(IA_Height, UpImage, &ArrowHeight);

            if(DownImage = NewObject(NULL, SYSICLASS,
                SYSIA_Size,    SizeType,
                SYSIA_Which,    DOWNIMAGE,
                SYSIA_DrawInfo,    drawInfo,
            TAG_DONE))
            {
                if(Scroller = NewObject(NULL, PROPGCLASS,
                    GA_ID,        GAD_SCROLLER,
                    GA_Top,        scr->WBorTop + scr->Font->ta_YSize + 2,
                    GA_RelHeight,    -(scr->WBorTop + scr->Font->ta_YSize + 2 + SizeHeight + 1 + 2 * ArrowHeight),
                    GA_Width,    SizeWidth - 8,
                    GA_RelRight,    -(SizeWidth - 5),
                    GA_Immediate,    TRUE,
                    GA_FollowMouse,    TRUE,
                    GA_RelVerify,    TRUE,
                    GA_RightBorder,    TRUE,
                    PGA_Freedom,    FREEVERT,
                    PGA_NewLook,    TRUE,
                    PGA_Borderless,    TRUE,
                    PGA_Top,    sel,
                    PGA_Visible,    (prefs.sb_height - (prefs.fontsize + scr->WBorTop + 2)) / prefs.fontsize,
                    PGA_Total,    nScrollbackLines,
                TAG_DONE))
                {
                    if(UpArrow = NewObject(NULL, BUTTONGCLASS,
                        GA_ID,        GAD_UP,
                        GA_Image,    UpImage,
                        GA_RelRight,    -(SizeWidth - 1),
                        GA_RelBottom,    -(SizeHeight - 1 + 2 * ArrowHeight),
                        GA_Height,    ArrowHeight,
                        GA_Width,    SizeWidth,
                        GA_Previous,    Scroller,
                        GA_RightBorder,    TRUE,
                        ICA_TARGET,    ICTARGET_IDCMP,
                        ICA_MAP,    ArrowMappings,
                    TAG_DONE))
                    {
                        if(DownArrow = NewObject(NULL, BUTTONGCLASS,
                            GA_ID,        GAD_DOWN,
                            GA_Image,    DownImage,
                            GA_RelRight,    -(SizeWidth - 1),
                            GA_RelBottom,    -(SizeHeight - 1 + ArrowHeight),
                            GA_Height,    ArrowHeight,
                            GA_Width,    SizeWidth,
                            GA_Previous,    UpArrow,
                            GA_RightBorder,    TRUE,
                            ICA_TARGET,    ICTARGET_IDCMP,
                            ICA_MAP,    ArrowMappings,
                        TAG_DONE))
                        {
                            newWin.LeftEdge   = prefs.sb_left;
                            newWin.TopEdge    = prefs.sb_top;
                            newWin.Width      = prefs.sb_width;
                            newWin.Height     = prefs.sb_height;
                            newWin.IDCMPFlags = IDCMP_IDCMPUPDATE | LISTVIEWIDCMP | IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | BUTTONIDCMP | IDCMP_RAWKEY;
                            newWin.Flags = WFLG_NOCAREREFRESH | WFLG_ACTIVATE|WFLG_CLOSEGADGET|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_SIZEGADGET;
                            newWin.FirstGadget = Scroller;
                            newWin.Title = "Scroll Back:  F1 - Clear  F3 - Print  F5 - Save";
                            newWin.MinWidth = 180;
                            newWin.MinHeight = 50;
                            newWin.MaxWidth = 1600;
                            newWin.MaxHeight = 1200;
                            CheckDimensions(&newWin);
                            scrollbackWin = OpenWindow(&newWin);
                            /*scrollbackWin = OpenWindowTags(NULL,
                                WA_Title,        "Scroll Back:  F1 - Clear  F3 - Print  F5 - Save",
                                WA_Left,        prefs.sb_left,
                                WA_Top,            prefs.sb_top,
                                WA_Width,        prefs.sb_width,
                                WA_Height,        prefs.sb_height,
                                WA_MinHeight,        50,
                                WA_MinWidth,        200,
                                WA_MaxHeight,        1200,
                                WA_MaxWidth,        1600,
                                WA_CustomScreen,    scr,
                                WA_Gadgets,        Scroller,
                                WA_IDCMP,        IDCMP_IDCMPUPDATE | LISTVIEWIDCMP | IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW | BUTTONIDCMP | IDCMP_RAWKEY,
                                WA_Flags,        WFLG_NOCAREREFRESH | WFLG_ACTIVATE|WFLG_CLOSEGADGET|WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_SIZEGADGET,
                                TAG_END);*/
                            if(scrollbackWin)
                            {
                                //GT_RefreshWindow(scrollbackWin, NULL);
                                RefreshListView(sel);
                                ResetMenuStrip(scrollbackWin, mainMenuStrip);
                            }
                        }
                    }
                }
            }
        }
    }
}


#include "fkey.h"

static struct Window         *fKeysWnd;           // "Function Keys" settings window
static struct Gadget         *fKeysGList;         // "Function Keys" settings window GList
static struct Gadget         *fKeysGadgets[13];   // "Function Keys" settings window gadgets
#define fKeysWidth 503
#define fKeysHeight 199


static UBYTE *MOD0Labels[] = {
    (UBYTE *)"None",
    (UBYTE *)"Shift",
    NULL };

static UBYTE fKeysGTypes[] = {
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    STRING_KIND,
    CYCLE_KIND,
    BUTTON_KIND,
    BUTTON_KIND
};


static struct MyNewGadget fKeysNGad[] = {
    43, 21, 443, 15, (UBYTE *)"F1:",
    43, 37, 443, 15, (UBYTE *)"F2:",
    43, 53, 443, 15, (UBYTE *)"F3:",
    43, 69, 443, 15, (UBYTE *)"F4:",
    43, 85, 443, 15, (UBYTE *)"F5:",
    43, 101, 443, 15, (UBYTE *)"F6:",
    43, 117, 443, 15, (UBYTE *)"F7:",
    43, 133, 443, 15, (UBYTE *)"F8:",
    43, 149, 443, 15, (UBYTE *)"F9:",
    43, 165, 443, 15, (UBYTE *)"F0:",
    171, 4, 187, 14, (UBYTE *)"Modifier:",
    7, 183, 101, 14, (UBYTE *)"_Save",
    394, 183, 101, 14, (UBYTE *)"_Cancel",
};

static ULONG fKeysGTags[] = {
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    GTST_String, (ULONG) NULL, (GTST_MaxChars), F_KEY_SIZE-1, (GTST_EditHook), (ULONG)&selectAllHook, (TAG_DONE),
    (GTCY_Labels), (ULONG)&MOD0Labels[ 0 ], (GA_Disabled), TRUE, (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE),
    (GT_Underscore), '_', (TAG_DONE)
};

static int OpenFKeysWindow( void )
{
    struct Gadget    *g;
    UWORD        ww, wh;
    long x, y;

    ComputeFont( fKeysWidth, fKeysHeight );

    ww = ComputeX( fKeysWidth );
    wh = ComputeY( fKeysHeight );

    if ( ! ( g = CreateContext( &fKeysGList )))
        return( 1L );

    if(MakeGadgets(fKeysNGad, fKeysGadgets, fKeysGTags, g, fKeysGTypes, fKeys_CNT) != 0) return( 2L );

    x = ww + OffX + scr->WBorRight;
    y = wh + OffY + scr->WBorBottom;

    /*newWin.LeftEdge = (scr->Width - x) / 2;
    newWin.TopEdge = (scr->Height - y) / 2;
    newWin.Width = x;
    newWin.Height = y;
    newWin.IDCMPFlags = CYCLEIDCMP|STRINGIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY;
    newWin.Flags = WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP;
    newWin.FirstGadget = fKeysGList;
    newWin.Title = "Function Keys";

    CheckDimensions(&newWin);

    fKeysWnd = OpenWindow(&newWin);
    if(!fKeysWnd) return( 4L );*/

    if ( ! ( fKeysWnd = OpenWindowTags( NULL,
                WA_Left,    (scr->Width - x) / 2,
                WA_Top,        (scr->Height - y) / 2,
                WA_Width,    x,
                WA_Height,    y,
                WA_IDCMP,    CYCLEIDCMP|STRINGIDCMP|BUTTONIDCMP|IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|IDCMP_VANILLAKEY,
                WA_Flags,    WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET|WFLG_SMART_REFRESH|WFLG_ACTIVATE|WFLG_RMBTRAP,
                WA_Gadgets,    fKeysGList,
                WA_Title,    "Function Keys",
                WA_CustomScreen,scr,
                TAG_DONE )))
    return( 4L );

    PaintDialogBackground(fKeysWnd);
    DlgDump("fkeys", 13);
    GT_RefreshWindow( fKeysWnd, NULL );
    RefreshGadgets( fKeysGList, fKeysWnd, NULL );

    return( 0L );
}


/**
 * @brief Display the Function Keys configuration dialog.
 *
 * Opens a modal GadTools-based dialog that allows the user to edit the strings associated with the
 * function keys (F1-F10). Each string defines the text that will be sent to the remote Telnet
 * server when the corresponding function key is pressed.
 *
 * If the user confirms the changes, the updated strings are copied to the global function key table
 * and written to the function key preferences file. If the dialog is cancelled, all modifications
 * are discarded.
 *
 * @note The dialog is implemented entirely with GadTools gadgets and processes Intuition messages
 *       until the user closes it.
 *
 * @note The function updates the in-memory function key table before writing it to disk.
 */
void FunctionKeys(void)
{
    struct IntuiMessage *message;
    struct Gadget *gad;
    ULONG class;
    UWORD code;
    BOOL subdone = FALSE;
    BOOL save = FALSE;
    int i;

    // Initialize gadget fields with current settings:
    for (i = 0; i < F_KEY_COUNT; i++)
        fKeysGTags[1 + i * 7] = (ULONG)&fKeys[i * F_KEY_SIZE];

    // Open the Functions Keys window
    if(OpenFKeysWindow() == RETURN_OK)
    {
        while(!subdone)
        {
            WaitPort(fKeysWnd->UserPort);
            while (message = GT_GetIMsg(fKeysWnd->UserPort))
            {
                gad   = (struct Gadget *)message->IAddress;
                class = message->Class;
                code  = message->Code;
                GT_ReplyIMsg(message);

                switch (class)
                {
                case IDCMP_CLOSEWINDOW:
                    subdone = TRUE;
                    break;

                case IDCMP_VANILLAKEY:
                    switch(toupper(code))
                    {
                        case 'S':            // Save
                            save = TRUE;
                            /* fall through */
                        case 'C':
                            subdone = TRUE;  // Cancel
                    }
                    break;

                case IDCMP_GADGETUP:
                    switch(gad->GadgetID)
                    {
                    case GD_SAVEE:
                        save = TRUE;
                        /* fall through */
                    case GD_CANCELL:
                        subdone = TRUE;
                        break;
                    }
                }
            }
        }
    }

    // Save gadget values to the Prefs file if user pressed OK:
    if(save)
    {
        register BPTR fh;
        LONG l;

        for (i=0 ; i < F_KEY_COUNT ; i++)
        {
            // The gadget buffer is expected to be limited by "(GTST_MaxChars), F_KEY_SIZE-1",
            // but this provides additional protection against unexpected gadget behavior.
            strlcpy(&fKeys[i * F_KEY_SIZE],
                    ((struct StringInfo *)fKeysGadgets[i]->SpecialInfo)->Buffer,
                    F_KEY_SIZE);
        }

        fh = Open(keysFilename, MODE_NEWFILE);
        l = 0;
        if(fh)
        {
            l = Write(fh, fKeys, F_KEY_COUNT * F_KEY_SIZE);
            Close(fh);
        }

        if (!fh || l != (F_KEY_COUNT * F_KEY_SIZE))
        {
            SimpleReq("ERROR: Failed to save Function keys settings!");
        }
    }

    if (fKeysWnd)   { CloseWindow(fKeysWnd);   fKeysWnd   = NULL; }
    if (fKeysGList) { FreeGadgets(fKeysGList); fKeysGList = NULL; }
}


static char *icons[BUTTON_COUNT] =
{
    "Connect",
    "Disconnect",
    "AddressBook",
    "Information",
    "Upload",
    "Download",
    "Quit"
};

static struct DiskObject *dob[BUTTON_COUNT];


void CheckDimensions(struct NewWindow *newwin)
{
    if(newwin->Width > scr->Width) newwin->Width = scr->Width;
    if(newwin->Height > scr->Height) newwin->Height = scr->Height;

    if(newwin->LeftEdge + newwin->Width > scr->Width) newwin->LeftEdge = 0;
    if(newwin->TopEdge + newwin->Height > scr->Height) newwin->TopEdge = 0;
}

void CloseToolBarWindow(void)
{
    if (toolBarWin)
    {
        register struct MenuItem *item;
         register UWORD i;

        ClearMenuStrip(toolBarWin);
        CloseWindow(toolBarWin);
        toolBarWin = NULL;

        for(i=0; i<BUTTON_COUNT; i++)
        {
            if(dob[i]) { FreeDiskObject(dob[i]);  dob[i]= NULL; }
        }

        item = GetMenuItemFromID(MENU_TOOLBAR);
        if (item != NULL)
            item->Flags &= ~CHECKED;
    }
}

void OpenToolBarWindow(char setmenus)
{
    if(!toolBarWin)
    {
        register struct Gadget *firstgad = 0;
        register struct Gadget *gad = 0;
        UWORD nextleft = scr->WBorLeft + 1, maxheight = 0, i = 0;
        WORD wintop, spacing = 5;

        if (isRunningOnWB)
            wintop = winTop;
        else
            wintop = 0;

        do
        {
            strlcpy(buf, isRunningOnWB ? "PROGDIR:WBIcons/" : "PROGDIR:SCIcons/", sizeof(buf));
            strlcat(buf, icons[i], sizeof(buf));
            dob[i] = GetDiskObjectNew(buf);
            if(dob[i])
            {
                if(gad) gad->NextGadget = &dob[i]->do_Gadget;
                gad = &dob[i]->do_Gadget;
                gad->NextGadget = 0;
                gad->LeftEdge = nextleft;
                gad->TopEdge = wintop + 1;
                if(gad->SelectRender == 0)
                    gad->Flags = GFLG_GADGIMAGE | GFLG_GADGHCOMP;
                else
                    gad->Flags = GFLG_GADGIMAGE | GFLG_GADGHIMAGE;
                gad->Activation = GACT_RELVERIFY;
                gad->GadgetType = GTYP_BOOLGADGET;
                //gad->GadgetText = 0;
                gad->GadgetID = i;
                gad->UserData = (APTR)dob[i]->do_ToolTypes[0];
                nextleft = gad->LeftEdge + gad->Width + spacing;
                if(!firstgad) firstgad = gad;
                if(gad->Height > maxheight) maxheight = gad->Height;
            }
            i++;
        }
        while(i != BUTTON_COUNT);

        if(!firstgad)
        {
            SimpleReq("No icons available.");
            prefs.flags &= ~FLAG_TOOL_BAR;
            return;
        }

        if (isRunningOnWB)
        {
            newWin.LeftEdge = prefs.toolBarWin_left;
            newWin.TopEdge  = prefs.toolBarWin_top;

            newWin.Width = gad->LeftEdge + gad->Width + scr->WBorRight + 1;
            newWin.Height = maxheight + wintop + scr->WBorBottom + 3 + scr->RastPort.Font->tf_YSize;
            newWin.Flags = WFLG_NOCAREREFRESH|WFLG_NEWLOOKMENUS|WFLG_CLOSEGADGET|WFLG_DRAGBAR|WFLG_DEPTHGADGET;
            newWin.IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_GADGETUP;
            newWin.Title = "Tool Bar";
        } else {

            spacing = scr->Width / i;
            nextleft = (scr->Width - ((scr->Width - spacing) + gad->Width)) / 2;
            i = 0;
            gad = firstgad;
            while(gad)
            {
                gad->LeftEdge = nextleft + (spacing * i);
                gad = gad->NextGadget;
                i++;
            }

            newWin.LeftEdge = 0;
            if(prefs.flags & FLAG_HIDE_TITLEBAR)
                newWin.TopEdge = 0;
            else
                newWin.TopEdge = prefs.fontsize + 3;

            newWin.Width = scr->Width;
            newWin.Height = maxheight + scr->RastPort.Font->tf_YSize + 4;
            newWin.Flags = WFLG_NOCAREREFRESH|WFLG_NEWLOOKMENUS|WFLG_BACKDROP|WFLG_BORDERLESS;
            newWin.IDCMPFlags = IDCMP_MENUPICK | IDCMP_GADGETUP | IDCMP_RAWKEY;
            newWin.Title = 0;
        }
        newWin.FirstGadget = firstgad;

        CheckDimensions(&newWin);

        toolBarWin = OpenWindow(&newWin);
        if (toolBarWin)
        {
            if(setmenus) ResetMenuStrip(toolBarWin, mainMenuStrip);
            SetFont(toolBarWin->RPort, scr->RastPort.Font);
            SetAPen(toolBarWin->RPort, drawInfo->dri_Pens[TEXTPEN]);
            gad = firstgad;
            while(gad)
            {
                register UWORD len = strlen((char *)gad->UserData);

                Move(toolBarWin->RPort, gad->LeftEdge + ((gad->Width - (len*scr->RastPort.Font->tf_XSize)) / 2), wintop + maxheight + scr->RastPort.Font->tf_YSize - 1);
                Text(toolBarWin->RPort, (char *)gad->UserData, len);

                gad = gad->NextGadget;
            }
            if(!isRunningOnWB)
            {
                SetAPen(toolBarWin->RPort, drawInfo->dri_Pens[SHINEPEN]);
                Move(toolBarWin->RPort, 0, toolBarWin->Height-2);
                Draw(toolBarWin->RPort, toolBarWin->Width, toolBarWin->Height-2);
                SetAPen(toolBarWin->RPort, drawInfo->dri_Pens[FILLPEN]);
                Move(toolBarWin->RPort, 0, toolBarWin->Height-1);
                Draw(toolBarWin->RPort, toolBarWin->Width, toolBarWin->Height-1);
            }
        }
    }
}
