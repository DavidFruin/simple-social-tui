#define _XOPEN_SOURCE_EXTENDED 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ncurses.h>
#include "editor.h"
#include "ui.h"

#define MAX_ROWS 512

/* One wrapped display row: a byte range of the buffer. */
typedef struct {
    size_t start;
    size_t end;
} erow_t;

/* ---------- buffer ---------- */

static int ensure_cap(editor_t *ed, size_t need) {
    if (need + 1 <= ed->cap) return 0;
    size_t cap = ed->cap ? ed->cap : 256;
    while (cap < need + 1) cap *= 2;
    char *p = realloc(ed->buf, cap);
    if (!p) return -1;
    ed->buf = p;
    ed->cap = cap;
    return 0;
}

int editor_init(editor_t *ed, const char *initial, int max_chars) {
    memset(ed, 0, sizeof(*ed));
    ed->max_chars = max_chars;
    size_t n = initial ? strlen(initial) : 0;
    if (ensure_cap(ed, n) != 0) return -1;
    if (n) memcpy(ed->buf, initial, n);
    ed->len = n;
    ed->buf[n] = '\0';
    ed->cursor = n;
    return 0;
}

void editor_free(editor_t *ed) {
    free(ed->buf);
    memset(ed, 0, sizeof(*ed));
}

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

int editor_char_count(const editor_t *ed) {
    int n = 0;
    for (size_t i = 0; i < ed->len; i++)
        if (!is_cont((unsigned char)ed->buf[i])) n++;
    return n;
}

static size_t prev_boundary(const editor_t *ed, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && is_cont((unsigned char)ed->buf[pos])) pos--;
    return pos;
}

static size_t next_boundary(const editor_t *ed, size_t pos) {
    if (pos >= ed->len) return ed->len;
    pos++;
    while (pos < ed->len && is_cont((unsigned char)ed->buf[pos])) pos++;
    return pos;
}

static int insert_bytes(editor_t *ed, const char *bytes, size_t n) {
    if (ensure_cap(ed, ed->len + n) != 0) return -1;
    memmove(ed->buf + ed->cursor + n, ed->buf + ed->cursor, ed->len - ed->cursor);
    memcpy(ed->buf + ed->cursor, bytes, n);
    ed->len += n;
    ed->cursor += n;
    ed->buf[ed->len] = '\0';
    return 0;
}

static void delete_range(editor_t *ed, size_t from, size_t to) {
    if (to <= from) return;
    memmove(ed->buf + from, ed->buf + to, ed->len - to);
    ed->len -= (to - from);
    ed->buf[ed->len] = '\0';
    ed->cursor = from;
}

static int insert_wch(editor_t *ed, wchar_t wc) {
    if (ed->max_chars > 0 && editor_char_count(ed) >= ed->max_chars) return -1;

    char mb[MB_CUR_MAX + 1];
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    size_t n = wcrtomb(mb, wc, &st);
    if (n == (size_t)-1) return -1;
    return insert_bytes(ed, mb, n);
}

/* ---------- layout ---------- */

/* Wraps the buffer to `width` columns, honouring embedded newlines.
 * Always emits at least one row, and the rows cover [0, len] so the
 * cursor can be placed even at the very end. */
static int layout(const editor_t *ed, int width, erow_t *rows, int max_rows) {
    if (width < 1) width = 1;

    int n = 0;
    size_t i = 0;

    while (n < max_rows) {
        size_t start = i;
        int cols = 0;
        size_t last_space = (size_t)-1;

        while (i < ed->len) {
            unsigned char c = (unsigned char)ed->buf[i];

            if (c == '\n') break;

            size_t nx = next_boundary(ed, i);

            /* Width of this one character. */
            wchar_t wc;
            mbstate_t st;
            memset(&st, 0, sizeof(st));
            int w = 1;
            if (mbrtowc(&wc, ed->buf + i, nx - i, &st) <= (size_t)(nx - i)) {
                int ww = wcwidth(wc);
                w = ww < 0 ? 0 : ww;
            }

            if (cols + w > width) break;

            if (c == ' ') last_space = i;
            cols += w;
            i = nx;
        }

        size_t end = i;

        /* Soft wrap: prefer breaking after the last space on the row. */
        if (i < ed->len && ed->buf[i] != '\n' && last_space != (size_t)-1 && last_space > start) {
            end = last_space + 1;
            i = end;
        }

        rows[n].start = start;
        rows[n].end = end;
        n++;

        if (i < ed->len && ed->buf[i] == '\n') {
            i++;                       /* the newline belongs to no row */
            if (i == ed->len) {        /* trailing newline: empty last row */
                if (n < max_rows) { rows[n].start = i; rows[n].end = i; n++; }
                break;
            }
            continue;
        }
        if (i >= ed->len) break;
    }

    if (n == 0) { rows[0].start = 0; rows[0].end = 0; n = 1; }
    return n;
}

/* Column offset of the cursor within its row. */
static int cursor_col(const editor_t *ed, size_t start) {
    char tmp[1024];
    size_t n = ed->cursor - start;
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, ed->buf + start, n);
    tmp[n] = '\0';
    return ui_utf8_width(tmp);
}

/* The row the cursor sits on: the last row starting at or before it. */
static int cursor_row(const erow_t *rows, int nrows, size_t cursor) {
    int r = 0;
    for (int i = 0; i < nrows; i++)
        if (rows[i].start <= cursor) r = i;
    return r;
}

/* ---------- drawing ---------- */

typedef struct {
    int top, left, w, h;   /* outer box */
    int text_w, text_h;    /* inner text area */
} box_t;

static box_t compute_box(void) {
    box_t b;
    b.w = COLS - 8;
    if (b.w > 76) b.w = 76;
    if (b.w < 24) b.w = COLS > 24 ? 24 : COLS;

    b.h = LINES - 6;
    if (b.h > 16) b.h = 16;
    if (b.h < 7) b.h = LINES > 7 ? 7 : LINES;

    b.left = (COLS - b.w) / 2;
    b.top = (LINES - b.h) / 2;
    if (b.left < 0) b.left = 0;
    if (b.top < 0) b.top = 0;

    b.text_w = b.w - 4;
    b.text_h = b.h - 4;       /* borders + counter row */
    if (b.text_w < 8) b.text_w = 8;
    if (b.text_h < 1) b.text_h = 1;
    return b;
}

int editor_run(app_t *app, editor_t *ed, const char *title, const char *send_label) {
    static erow_t rows[MAX_ROWS];

    int scroll = 0;
    int result = EDITOR_CANCEL;
    int over_limit = 0;

    app->input_active = 1;      /* keeps the idle refresh timer off our back */
    curs_set(1);
    timeout(-1);                /* block for input: no timers while typing */
    keypad(stdscr, TRUE);

    for (;;) {
        box_t b = compute_box();
        int nrows = layout(ed, b.text_w, rows, MAX_ROWS);
        int crow = cursor_row(rows, nrows, ed->cursor);

        if (crow < scroll) scroll = crow;
        if (crow >= scroll + b.text_h) scroll = crow - b.text_h + 1;
        if (scroll > nrows - 1) scroll = nrows - 1;
        if (scroll < 0) scroll = 0;

        /* Repaint what is underneath, then the box on top. */
        ui_draw(app);

        WINDOW *win = newwin(b.h, b.w, b.top, b.left);
        if (!win) break;

        box(win, 0, 0);
        wattron(win, A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title);
        wattroff(win, A_BOLD);

        for (int i = 0; i < b.text_h; i++) {
            int ri = scroll + i;
            if (ri >= nrows) break;
            size_t s = rows[ri].start, e = rows[ri].end;

            char line[1024];
            size_t n = e - s;
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, ed->buf + s, n);
            line[n] = '\0';

            mvwaddstr(win, 1 + i, 2, line);
        }

        /* Counter and key hints. */
        int count = editor_char_count(ed);
        over_limit = (ed->max_chars > 0 && count >= ed->max_chars);

        char meta[256];
        snprintf(meta, sizeof(meta), "%d/%d", count, ed->max_chars);
        if (over_limit) wattron(win, COLOR_PAIR(CP_ERROR) | A_BOLD);
        else wattron(win, A_DIM);
        mvwaddstr(win, b.h - 2, 2, meta);
        if (over_limit) wattroff(win, COLOR_PAIR(CP_ERROR) | A_BOLD);
        else wattroff(win, A_DIM);

        char hint[128];
        snprintf(hint, sizeof(hint), "^D %s   esc cancel", send_label);
        int hw = ui_utf8_width(hint);
        if (b.w - 2 - hw > (int)strlen(meta) + 3) {
            wattron(win, A_DIM);
            mvwaddstr(win, b.h - 2, b.w - hw - 2, hint);
            wattroff(win, A_DIM);
        }

        /* Cursor, in window coordinates. */
        int cy = 1 + (crow - scroll);
        int cx = 2 + cursor_col(ed, rows[crow].start);
        if (cx > b.w - 2) cx = b.w - 2;
        wmove(win, cy, cx);

        wrefresh(win);

        wint_t wch;
        int kind = wget_wch(win, &wch);

        delwin(win);

        if (kind == ERR) continue;

        if (kind == KEY_CODE_YES) {
            switch (wch) {
                case KEY_LEFT:  ed->cursor = prev_boundary(ed, ed->cursor); break;
                case KEY_RIGHT: ed->cursor = next_boundary(ed, ed->cursor); break;

                case KEY_UP:
                    if (crow > 0) {
                        int want = cursor_col(ed, rows[crow].start);
                        size_t s = rows[crow - 1].start, e = rows[crow - 1].end;
                        size_t p = s;
                        int col = 0;
                        while (p < e && col < want) { p = next_boundary(ed, p); col++; }
                        ed->cursor = p;
                    }
                    break;

                case KEY_DOWN:
                    if (crow < nrows - 1) {
                        int want = cursor_col(ed, rows[crow].start);
                        size_t s = rows[crow + 1].start, e = rows[crow + 1].end;
                        size_t p = s;
                        int col = 0;
                        while (p < e && col < want) { p = next_boundary(ed, p); col++; }
                        ed->cursor = p;
                    }
                    break;

                case KEY_HOME: ed->cursor = rows[crow].start; break;
                case KEY_END:  ed->cursor = rows[crow].end;   break;

                case KEY_BACKSPACE:
                    if (ed->cursor > 0)
                        delete_range(ed, prev_boundary(ed, ed->cursor), ed->cursor);
                    break;

                case KEY_DC:
                    if (ed->cursor < ed->len)
                        delete_range(ed, ed->cursor, next_boundary(ed, ed->cursor));
                    break;

                case KEY_RESIZE:
                    break;

                default:
                    break;
            }
            continue;
        }

        /* kind == OK: a character, possibly a control character. */
        switch (wch) {
            case 4:                     /* ^D: send */
                result = EDITOR_SUBMIT;
                goto done;

            case 27:                    /* esc: cancel, confirming if there is text */
                if (ed->len > 0) {
                    if (ui_confirm(app, "Discard what you have written?")) {
                        result = EDITOR_CANCEL;
                        goto done;
                    }
                    break;              /* keep editing */
                }
                result = EDITOR_CANCEL;
                goto done;

            case 1:  ed->cursor = rows[crow].start; break;   /* ^A */
            case 5:  ed->cursor = rows[crow].end;   break;   /* ^E */

            case 8: case 127:                                /* ^H / DEL */
                if (ed->cursor > 0)
                    delete_range(ed, prev_boundary(ed, ed->cursor), ed->cursor);
                break;

            case '\r': case '\n':
                insert_wch(ed, L'\n');
                break;

            case '\t':
                insert_wch(ed, L' ');
                insert_wch(ed, L' ');
                break;

            default:
                if (wch >= 32 || wch == '\n') {
                    if (insert_wch(ed, (wchar_t)wch) != 0 && over_limit)
                        beep();
                }
                break;
        }
    }

done:
    curs_set(0);
    timeout(500);
    app->input_active = 0;
    ui_draw(app);
    return result;
}
