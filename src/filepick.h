#ifndef TUI_FILEPICK_H
#define TUI_FILEPICK_H

#include <stddef.h>
#include "app.h"

/* Picks a media file to attach.
 *
 * Two ways in, per preference: a browsable directory listing, and a typed
 * path with Tab completion (press `/` to switch to it). Returns 1 when
 * `out` holds a chosen path, 0 when cancelled. */
int filepick_run(app_t *app, char *out, size_t outsz);

/* What the server will accept, checked locally so an oversized file is
 * refused before a 120-second upload rather than after. */
int  filepick_is_media(const char *name);
long filepick_max_bytes(const char *name);
const char *filepick_kind(const char *name);

#endif
