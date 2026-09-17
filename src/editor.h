#ifndef TUI_EDITOR_H
#define TUI_EDITOR_H

#include <stddef.h>
#include "app.h"

/* Inline multi-line text editor, drawn in a box over the current screen.
 *
 * The buffer is UTF-8. The cursor is a byte offset but only ever lands on
 * a character boundary, and every movement and deletion works a whole
 * codepoint at a time, so editing non-ASCII text behaves.
 *
 * Input comes through get_wch() rather than getch(): getch() hands back
 * one byte at a time, which would insert half a character. */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;      /* bytes, excluding the NUL */
    size_t cursor;   /* byte offset, always a character boundary */
    int    max_chars;
} editor_t;

#define EDITOR_SUBMIT 1
#define EDITOR_CANCEL 0

int  editor_init(editor_t *ed, const char *initial, int max_chars);
void editor_free(editor_t *ed);

/* Runs the editor modally. Returns EDITOR_SUBMIT or EDITOR_CANCEL.
 *
 * When `attach` is non-NULL, ^O opens the media picker and the chosen
 * path is written there; the box shows what is attached. Pass NULL where
 * media makes no sense -- the API takes no media on comments. */
int  editor_run(app_t *app, editor_t *ed, const char *title, const char *send_label,
                char *attach, size_t attach_sz);

/* Characters (not bytes) currently in the buffer. */
int  editor_char_count(const editor_t *ed);

#endif
