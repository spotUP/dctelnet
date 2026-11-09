/* src/vtdownload.h -- where a file the terminal engine saves goes (iTerm2 inline=0).
 * The engine hands over a sanitised host and file name (vt_set_download_sink); this
 * builds the path under the Download Path preference and writes the bytes. */
#ifndef VTDOWNLOAD_H
#define VTDOWNLOAD_H

#include <exec/types.h>

/* <prefs.DownloadPath>/<host>/<name>: the host drawer is made when missing.
 * Returns 1 when the file was saved, 0 when not (no Download Path, drawer or
 * file cannot be made, short write). Matches vt_dl_sink. */
int VtDownload_Sink(const char *host, const char *name, const unsigned char *data, long len);

#endif /* VTDOWNLOAD_H */
