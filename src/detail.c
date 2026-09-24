#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include "detail.h"
#include "ui.h"
#include "timefmt.h"
#include "shell.h"

/* The detail view is rendered into a flat list of lines, then scrolled by
 * line. Post text and comments both wrap to arbitrary heights, and
 * scrolling a flat list is far easier to get right than walking
 * variable-height items. Each line remembers which comment it belongs to
 * (-1 for the post itself) so selection can highlight a whole comment. */

typedef enum {
    DL_PLAIN = 0,
    DL_AUTHOR,
    DL_META,
    DL_MEDIA,
    DL_RULE,
    DL_SECTION,
    DL_CAUTHOR,
    DL_MORE
} dl_kind_t;

typedef struct {
    char text[512];
    dl_kind_t kind;
    int cidx;          /* comment index, or -1 */
} dline_t;

static dline_t *g_lines = NULL;
static int g_count = 0;
static int g_cap = 0;

/* First rendered line of each comment, for scroll-to-selection. */
static int *g_cfirst = NULL;
static int g_cfirst_cap = 0;

static void lines_reset(void) { g_count = 0; }

static dline_t *line_push(dl_kind_t kind, int cidx) {
    if (g_count == g_cap) {
        int cap = g_cap ? g_cap * 2 : 256;
        dline_t *p = realloc(g_lines, (size_t)cap * sizeof(dline_t));
        if (!p) return NULL;
        g_lines = p;
        g_cap = cap;
    }
    dline_t *l = &g_lines[g_count++];
    l->text[0] = '\0';
    l->kind = kind;
    l->cidx = cidx;
    return l;
}

static void line_add(dl_kind_t kind, int cidx, const char *fmt, ...) {
    dline_t *l = line_push(kind, cidx);
    if (!l) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l->text, sizeof(l->text), fmt, ap);
    va_end(ap);
}

/* Wrap src into successive lines of the given kind. */
static void line_add_wrapped(dl_kind_t kind, int cidx, const char *src, int width, int indent) {
    char pad[32];
    int n = indent < 31 ? indent : 31;
    memset(pad, ' ', (size_t)n);
    pad[n] = '\0';

    int avail = width - indent;
    if (avail < 8) avail = 8;

    const char *p = src;
    if (!p || !*p) { line_add(kind, cidx, "%s", pad); return; }

    while (*p) {
        const char *nl = strchr(p, '\n');
        char seg[5100];
        size_t seglen = nl ? (size_t)(nl - p) : strlen(p);
        if (seglen >= sizeof(seg)) seglen = sizeof(seg) - 1;
        memcpy(seg, p, seglen);
        seg[seglen] = '\0';

        if (seg[0] == '\0') {
            line_add(kind, cidx, "%s", pad);
        } else {
            const char *q = seg;
            while (*q) {
                char chunk[512];
                ui_utf8_take(chunk, sizeof(chunk), q, avail, 0);
                if (chunk[0] == '\0') break;
                size_t taken = strlen(chunk);

                if (q[taken] != '\0' && q[taken] != ' ') {
                    char *sp = strrchr(chunk, ' ');
                    if (sp && sp != chunk) { *sp = '\0'; taken = strlen(chunk); }
                }

                line_add(kind, cidx, "%s%s", pad, chunk);
                q += taken;
                while (*q == ' ') q++;
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
}

static void remember_cfirst(app_t *app, int cidx, int line) {
    if (cidx >= g_cfirst_cap) {
        int cap = g_cfirst_cap ? g_cfirst_cap * 2 : 64;
        while (cap <= cidx) cap *= 2;
        int *p = realloc(g_cfirst, (size_t)cap * sizeof(int));
        if (!p) return;
        g_cfirst = p;
        g_cfirst_cap = cap;
    }
    g_cfirst[cidx] = line;
    (void)app;
}

static void build_lines(app_t *app) {
    lines_reset();

    int width = COLS - 2;
    if (width < 16) width = 16;

    api_post_t *p = &app->detail;

    char when[64];
    timefmt_full(p->timestamp, when, sizeof(when));
    line_add(DL_AUTHOR, -1, "%s", p->user_email);
    line_add(DL_META,   -1, "%s", when);
    line_add(DL_PLAIN,  -1, "");

    line_add_wrapped(DL_PLAIN, -1, p->text, width, 0);
    line_add(DL_PLAIN, -1, "");

    line_add(DL_META, -1, "%s %d   comments %d",
             "\xe2\x99\xa5", p->like_count, app->comments.total_count);

    if (p->media_url[0]) {
        char url[1024];
        if (shell_media_url(p->media_url, url, sizeof(url)) == 0)
            line_add(DL_MEDIA, -1, "media: %s", url);
        else
            line_add(DL_MEDIA, -1, "media: %s", p->media_url);
    }

    line_add(DL_RULE, -1, "");

    if (app->comments.count == 0) {
        line_add(DL_SECTION, -1, "No comments yet.");
    } else {
        line_add(DL_SECTION, -1, "Comments (%d of %d)",
                 app->comments.count, app->comments.total_count);
        line_add(DL_PLAIN, -1, "");

        for (int i = 0; i < app->comments.count; i++) {
            api_comment_t *c = &app->comments.comments[i];
            remember_cfirst(app, i, g_count);

            char cwhen[64];
            timefmt_short(c->created_at, cwhen, sizeof(cwhen));
            line_add(DL_CAUTHOR, i, "%s  %s", c->user_email, cwhen);
            line_add_wrapped(DL_PLAIN, i, c->text, width, 2);
            line_add(DL_PLAIN, i, "");
        }

        if (app->comments.has_more)
            line_add(DL_MORE, -1, "-- load more comments (%d of %d) --",
                     app->comments.count, app->comments.total_count);
    }
}

static void clamp_scroll(app_t *app, int body_h) {
    int max = g_count - body_h;
    if (max < 0) max = 0;
    if (app->detail_scroll > max) app->detail_scroll = max;
    if (app->detail_scroll < 0) app->detail_scroll = 0;
}

/* Scroll so the selected comment's first line is on screen. */
static void follow_selection(app_t *app, int body_h) {
    if (app->comments.count == 0) return;
    if (app->comment_sel < 0 || app->comment_sel >= g_cfirst_cap) return;

    int first = g_cfirst[app->comment_sel];
    if (first < app->detail_scroll) app->detail_scroll = first;
    else if (first >= app->detail_scroll + body_h)
        app->detail_scroll = first - body_h + 1;
}

void detail_draw(app_t *app, int body_top, int body_h) {
    build_lines(app);
    follow_selection(app, body_h);
    clamp_scroll(app, body_h);

    for (int i = 0; i < body_h; i++) {
        int li = app->detail_scroll + i;
        if (li >= g_count) break;
        dline_t *l = &g_lines[li];
        int row = body_top + i;

        int selected = (l->cidx >= 0 && l->cidx == app->comment_sel &&
                        app->comments.count > 0);

        if (l->kind == DL_RULE) {
            attron(A_DIM);
            mvhline(row, 0, ACS_HLINE, COLS);
            attroff(A_DIM);
            continue;
        }

        char buf[512];
        ui_utf8_take(buf, sizeof(buf), l->text, COLS - 2, 1);

        if (selected) attron(A_REVERSE);

        switch (l->kind) {
            case DL_AUTHOR:  attron(COLOR_PAIR(CP_AUTHOR) | A_BOLD); break;
            case DL_CAUTHOR: attron(COLOR_PAIR(CP_AUTHOR)); break;
            case DL_META:    attron(A_DIM); break;
            case DL_MEDIA:   attron(A_DIM); break;
            case DL_SECTION: attron(A_BOLD); break;
            case DL_MORE:    attron(app->detail_on_more ? (COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD) : A_DIM); break;
            default: break;
        }

        if (app->detail.is_liked && l->kind == DL_META && strstr(l->text, "\xe2\x99\xa5"))
            attron(COLOR_PAIR(CP_LIKED) | A_BOLD);

        mvaddstr(row, 1, buf);

        if (app->detail.is_liked && l->kind == DL_META && strstr(l->text, "\xe2\x99\xa5"))
            attroff(COLOR_PAIR(CP_LIKED) | A_BOLD);

        switch (l->kind) {
            case DL_AUTHOR:  attroff(COLOR_PAIR(CP_AUTHOR) | A_BOLD); break;
            case DL_CAUTHOR: attroff(COLOR_PAIR(CP_AUTHOR)); break;
            case DL_META:    attroff(A_DIM); break;
            case DL_MEDIA:   attroff(A_DIM); break;
            case DL_SECTION: attroff(A_BOLD); break;
            case DL_MORE:    attroff(app->detail_on_more ? (COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD) : A_DIM); break;
            default: break;
        }

        if (selected) attroff(A_REVERSE);
    }
}

void detail_scroll_by(app_t *app, int delta) {
    app->detail_scroll += delta;
    if (app->detail_scroll < 0) app->detail_scroll = 0;
    if (g_count && app->detail_scroll >= g_count) app->detail_scroll = g_count - 1;
}

void detail_move_selection(app_t *app, int delta) {
    if (app->comments.count == 0) { detail_scroll_by(app, delta); return; }

    if (app->detail_on_more) {
        if (delta < 0) {
            app->detail_on_more = 0;
            app->comment_sel = app->comments.count - 1;
        }
        return;
    }

    int next = app->comment_sel + delta;
    if (next >= app->comments.count) {
        if (app->comments.has_more) { app->detail_on_more = 1; return; }
        next = app->comments.count - 1;
    }
    if (next < 0) {
        /* Above the first comment, scroll the post body instead. */
        if (app->detail_scroll > 0) { detail_scroll_by(app, delta); return; }
        next = 0;
    }
    app->comment_sel = next;
}

int detail_open_media(app_t *app) {
    if (!app->detail.media_url[0]) {
        app_set_status(app, "This post has no media.");
        return 0;
    }

    char url[1024];
    if (shell_media_url(app->detail.media_url, url, sizeof(url)) != 0) {
        app_set_error(app, "could not build a URL for %s", app->detail.media_url);
        return -1;
    }

    if (shell_open_url(url) != 0) {
        app_set_error(app, "could not launch xdg-open");
        return -1;
    }

    app_set_status(app, "Opened %s", url);
    return 0;
}
