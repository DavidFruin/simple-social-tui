#ifndef TUI_SHELL_H
#define TUI_SHELL_H

#include <stddef.h>

/* Handing the terminal to another program and taking it back.
 *
 * Used now for opening media in the system viewer, and later for the
 * optional $EDITOR composer -- same suspend/restore dance either way. */

/* Turns a site-relative media path ("/media/26/image/x.webp") into an
 * absolute URL, using the origin of the library's configured base_url. */
int shell_media_url(const char *media_path, char *out, size_t n);

/* Opens a URL in the system viewer. Returns 0 if the opener was launched.
 * The URL comes from the server, so it is passed as an argv element to
 * execvp and never through a shell. */
int shell_open_url(const char *url);

#endif
