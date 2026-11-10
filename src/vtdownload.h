/* src/vtdownload.h -- where a file the terminal engine saves goes (iTerm2 inline=0),
 * and where a file it asks for (RIP 1I icons) comes from. The engine hands over a
 * sanitised host and file name (vt_set_download_sink, vt_download_host); this builds
 * the path under the Download Path preference and writes or reads the bytes. */
#ifndef VTDOWNLOAD_H
#define VTDOWNLOAD_H

#include <exec/types.h>

/* <prefs.DownloadPath>/<host>/<name>: the host drawer is made when missing.
 * Returns 1 when the file was saved, 0 when not (no Download Path, drawer or
 * file cannot be made, short write). Matches vt_dl_sink. */
int VtDownload_Sink(const char *host, const char *name, const unsigned char *data, long len);

/* The file the engine asks for by its bare name (RIP 1I icon, vt_file_reader with
 * the board's host folder added): <prefs.DownloadPath>/<host>/<name> first, so an icon
 * the board sent wins, then PROGDIR:Icons/<name>. A name with '/' or ':' is refused,
 * so a kitty t=f path never reaches the disk. Reads <size> bytes from <offset> (size 0:
 * to the end; at most VTDOWNLOAD_READ_MAX) into a malloc() buffer the caller frees
 * through VtDownload_Release. Returns 1 with *buf and *n set, 0 when no file. */
#define VTDOWNLOAD_READ_MAX (4L * 1024 * 1024)
int VtDownload_Read(const char *host, const char *name, long offset, long size, unsigned char **buf, long *n);
/* Frees a VtDownload_Read buffer (the engine hands it back through vt_image_release). */
void VtDownload_Release(void *buf);

#endif /* VTDOWNLOAD_H */
