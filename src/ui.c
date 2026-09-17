#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ncurses.h>
#include "ui.h"
#include "timefmt.h"

#define TAB_ROW     0
#define RULE_ROW    1
#define BODY_TOP    2

int ui_init(void) {
    if (!initscr()) return -1;
    cbreak();
    noecho();
    nonl();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Wake roughly twice a second so the idle refresh and badge timers
     * can run without a second thread. */
    timeout(500);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_TAB_ACTIVE, COLOR_BLACK,   COLOR_CYAN);
        init_pair(CP_AUTHOR,     COLOR_CYAN,    -1);
        init_pair(CP_LIKED,      COLOR_GREEN,   -1);
        init_pair(CP_ERROR,      COLOR_RED,     -1);
        init_pair(CP_BADGE,      COLOR_YELLOW,  -1);
        init_pair(CP_HINT,       COLOR_BLUE,    -1);
    }
    return 0;
}

void ui_teardown(void) {
    curs_set(1);
    endwin();
}

int ui_body_height(void) {
    int h = LINES - BODY_TOP - 1;  /* minus the status row */
    return h > 0 ? h : 0;
}

int ui_utf8_width(const char *s) {
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    int cols = 0;
    const char *p = s;
    size_t left = strlen(s);

    while (left > 0) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, left, &st);
        if (n == (size_t)-1 || n == (size_t)-2) { cols += 1; p += 1; left -= 1;
            memset(&st, 0, sizeof(st)); continue; }
        if (n == 0) break;
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        cols += w;
        p += n;
        left -= n;
    }
    return cols;
}

void ui_utf8_take(char *dst, size_t dstsz, const char *src, int max_cols, int ellipsis) {
    if (dstsz == 0) return;
    dst[0] = '\0';
    if (!src || max_cols <= 0) return;

    /* Reserve a column for the ellipsis only if we actually overflow. */
    int budget = max_cols;
    if (ellipsis && ui_utf8_width(src) > max_cols && budget > 1) budget = max_cols - 1;

    mbstate_t st;
    memset(&st, 0, sizeof(st));
    const char *p = src;
    size_t left = strlen(src);
    size_t used = 0;
    int cols = 0;
    int cut = 0;

    while (left > 0) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, left, &st);
        if (n == (size_t)-1 || n == (size_t)-2) {
            /* Invalid byte: skip it rather than emit half a character. */
            p += 1; left -= 1;
            memset(&st, 0, sizeof(st));
            continue;
        }
        if (n == 0) break;

        int w = wcwidth(wc);
        if (w < 0) w = 0;                 /* control char: drop */
        if (cols + w > budget) { cut = 1; break; }
        if (used + n + 1 > dstsz) { cut = 1; break; }

        memcpy(dst + used, p, n);
        used += n;
        cols += w;
        p += n;
        left -= n;
    }
    dst[used] = '\0';

    if (cut && ellipsis && used + 4 < dstsz) {
        memcpy(dst + used, "\xe2\x80\xa6", 3);   /* U+2026 */
        dst[used + 3] = '\0';
    }
}

/* ---------- chrome ---------- */

static void draw_tabs(app_t *app) {
    move(TAB_ROW, 0);
    clrtoeol();

    int x = 0;
    for (int t = 0; t < TAB_COUNT; t++) {
        char label[64];
        if (t == TAB_NOTIFS && app->unseen > 0)
            snprintf(label, sizeof(label), " %s(%d) ", app_tab_name(t), app->unseen);
        else
            snprintf(label, sizeof(label), " %s ", app_tab_name(t));

        int w = ui_utf8_width(label);
        if (x + w >= COLS) break;

        int active = (t == (int)app->tab);
        if (active) attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else if (t == TAB_NOTIFS && app->unseen > 0) attron(COLOR_PAIR(CP_BADGE));
        mvaddstr(TAB_ROW, x, label);
        if (active) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else if (t == TAB_NOTIFS && app->unseen > 0) attroff(COLOR_PAIR(CP_BADGE));

        x += w;
        if (x < COLS) { attron(A_DIM); mvaddstr(TAB_ROW, x, "|"); attroff(A_DIM); x += 1; }
    }

    /* Who you are, right-aligned, if it fits. */
    if (app->state.user.email[0]) {
        char who[64];
        ui_utf8_take(who, sizeof(who), app->state.user.email, 28, 1);
        int w = ui_utf8_width(who);
        if (COLS - w - 1 > x + 2) {
            attron(A_DIM);
            mvaddstr(TAB_ROW, COLS - w - 1, who);
            attroff(A_DIM);
        }
    }

    attron(A_DIM);
    mvhline(RULE_ROW, 0, ACS_HLINE, COLS);
    attroff(A_DIM);
}

static const char *hints_for(app_t *app) {
    switch (app->tab) {
        case TAB_FEED:
            return "j/k:move  enter:more  r:refresh  1-5:tabs  ?:help  q:quit";
        default:
            return "1-5:tabs  r:refresh  ?:help  q:quit";
    }
}

static void draw_status(app_t *app) {
    int row = LINES - 1;
    move(row, 0);
    clrtoeol();

    if (app->status[0]) {
        char buf[256];
        ui_utf8_take(buf, sizeof(buf), app->status, COLS - 1, 1);
        if (app->status_is_error) attron(COLOR_PAIR(CP_ERROR) | A_BOLD);
        mvaddstr(row, 0, buf);
        if (app->status_is_error) attroff(COLOR_PAIR(CP_ERROR) | A_BOLD);
        return;
    }

    char buf[256];
    ui_utf8_take(buf, sizeof(buf), hints_for(app), COLS - 1, 1);
    attron(COLOR_PAIR(CP_HINT) | A_DIM);
    mvaddstr(row, 0, buf);
    attroff(COLOR_PAIR(CP_HINT) | A_DIM);
}

/* ---------- feed ---------- */

/* Wraps text into the body width, returning how many lines it needs and
 * filling lines[] when it is non-NULL. */
static int wrap_text(const char *text, int width, char lines[][512], int max_lines) {
    int n = 0;
    const char *p = text;

    while (*p && n < max_lines) {
        /* Honour explicit newlines in the post. */
        const char *nl = strchr(p, '\n');
        const char *seg_end = nl ? nl : p + strlen(p);

        char seg[5100];
        size_t seglen = (size_t)(seg_end - p);
        if (seglen >= sizeof(seg)) seglen = sizeof(seg) - 1;
        memcpy(seg, p, seglen);
        seg[seglen] = '\0';

        if (seg[0] == '\0') {
            if (lines) lines[n][0] = '\0';
            n++;
        } else {
            const char *q = seg;
            while (*q && n < max_lines) {
                char chunk[512];
                ui_utf8_take(chunk, sizeof(chunk), q, width, 0);
                if (chunk[0] == '\0') break;

                size_t taken = strlen(chunk);

                /* Prefer breaking on the last space in the chunk. */
                if (q[taken] != '\0' && q[taken] != ' ') {
                    char *sp = strrchr(chunk, ' ');
                    if (sp && sp != chunk) {
                        *sp = '\0';
                        taken = strlen(chunk);
                    }
                }

                if (lines) snprintf(lines[n], 512, "%s", chunk);
                n++;

                q += taken;
                while (*q == ' ') q++;
            }
        }

        if (!nl) break;
        p = nl + 1;
    }
    return n > 0 ? n : 1;
}

#define MAX_WRAP 64

static int post_height(app_t *app, int idx, int width) {
    if (idx != app->feed_sel) return 1;
    int body_w = width - 2;
    if (body_w < 8) body_w = 8;
    int n = wrap_text(app->feed.posts[idx].text, body_w, NULL, MAX_WRAP);
    return 1 /*header*/ + n + 1 /*meta*/ + 1 /*spacer*/;
}

/* Scroll feed_top just far enough that the cursor is fully on screen.
 * When the cursor is parked on the load-more row, that row has to be
 * visible too -- otherwise moving onto it looks like nothing happened. */
static void ensure_visible(app_t *app, int body_h) {
    if (app->feed.count == 0) { app->feed_top = 0; return; }

    int target = app->feed_on_more ? app->feed.count - 1 : app->feed_sel;
    int extra  = app->feed_on_more ? 1 : 0;

    if (target < app->feed_top) app->feed_top = target;

    for (;;) {
        int used = extra;
        for (int i = app->feed_top; i <= target && i < app->feed.count; i++)
            used += post_height(app, i, COLS);
        if (used <= body_h || app->feed_top >= target) break;
        app->feed_top++;
    }
}

static void draw_collapsed(app_t *app, int row, int idx) {
    api_post_t *p = &app->feed.posts[idx];

    char when[32];
    timefmt_short(p->timestamp, when, sizeof(when));
    int when_w = ui_utf8_width(when);

    char likes[16];
    snprintf(likes, sizeof(likes), "%s%d", "\xe2\x99\xa5", p->like_count);  /* ♥ */
    int likes_w = ui_utf8_width(likes);

    int author_w = 22;
    if (COLS < 60) author_w = 14;

    char author[128];
    ui_utf8_take(author, sizeof(author), p->user_email, author_w, 1);

    int text_x = 2 + author_w + 1;
    int right_w = likes_w + 2 + when_w + 1;   /* +2 keeps a gap after the text */
    int text_w = COLS - text_x - right_w;
    if (text_w < 4) text_w = 4;

    char text[1024];
    ui_utf8_take(text, sizeof(text), p->text, text_w, 1);

    mvaddstr(row, 0, p->media_url[0] ? "\xe2\x97\x8f" : " ");  /* ● marks media */

    attron(COLOR_PAIR(CP_AUTHOR));
    mvaddstr(row, 2, author);
    attroff(COLOR_PAIR(CP_AUTHOR));

    mvaddstr(row, text_x, text);

    if (p->is_liked) attron(COLOR_PAIR(CP_LIKED) | A_BOLD);
    mvaddstr(row, COLS - right_w, likes);
    if (p->is_liked) attroff(COLOR_PAIR(CP_LIKED) | A_BOLD);

    attron(A_DIM);
    mvaddstr(row, COLS - when_w - 1, when);
    attroff(A_DIM);
}

static int draw_expanded(app_t *app, int row, int idx, int bottom) {
    api_post_t *p = &app->feed.posts[idx];
    int body_w = COLS - 2;
    if (body_w < 8) body_w = 8;

    char when[32];
    timefmt_full(p->timestamp, when, sizeof(when));
    int when_w = ui_utf8_width(when);

    char author[128];
    ui_utf8_take(author, sizeof(author), p->user_email, COLS - when_w - 6, 1);

    /* Header */
    if (row <= bottom) {
        attron(A_REVERSE);
        mvhline(row, 0, ' ', COLS);
        mvaddstr(row, 0, "\xe2\x96\xbc ");   /* ▼ */
        attron(A_BOLD);
        mvaddstr(row, 2, author);
        attroff(A_BOLD);
        if (COLS - when_w - 1 > 2 + ui_utf8_width(author))
            mvaddstr(row, COLS - when_w - 1, when);
        attroff(A_REVERSE);
    }
    row++;

    /* Body */
    static char lines[MAX_WRAP][512];
    int n = wrap_text(p->text, body_w, lines, MAX_WRAP);
    for (int i = 0; i < n && row <= bottom; i++, row++)
        mvaddstr(row, 2, lines[i]);

    /* Meta */
    if (row <= bottom) {
        char meta[256];
        snprintf(meta, sizeof(meta), "%s %d", "\xe2\x99\xa5", p->like_count);
        if (p->is_liked) attron(COLOR_PAIR(CP_LIKED) | A_BOLD);
        mvaddstr(row, 2, meta);
        if (p->is_liked) attroff(COLOR_PAIR(CP_LIKED) | A_BOLD);

        if (p->media_url[0]) {
            int x = 2 + ui_utf8_width(meta) + 3;
            int budget = COLS - x - 7 - 1;   /* 7 = "media: " */
            if (budget > 4) {
                char murl[512];
                ui_utf8_take(murl, sizeof(murl), p->media_url, budget, 1);
                attron(A_DIM);
                mvprintw(row, x, "media: %s", murl);
                attroff(A_DIM);
            }
        }
        row++;
    }

    if (row <= bottom) row++;  /* spacer */
    return row;
}

static void draw_feed(app_t *app) {
    int bottom = LINES - 2;

    if (app->feed.count == 0) {
        attron(A_DIM);
        mvaddstr(BODY_TOP + 1, 2,
                 app->feed_fetched ? "Feed is empty." : "Press r to load your feed.");
        attroff(A_DIM);
        return;
    }

    ensure_visible(app, ui_body_height());

    int row = BODY_TOP;
    for (int i = app->feed_top; i < app->feed.count && row <= bottom; i++) {
        if (i == app->feed_sel) {
            row = draw_expanded(app, row, i, bottom);
        } else {
            draw_collapsed(app, row, i);
            row++;
        }
    }

    /* load-more affordance */
    if (row <= bottom) {
        char more[128];
        if (app->feed.has_more)
            snprintf(more, sizeof(more), "-- load more (%d of %d) --",
                     app->feed.count, app->feed.total_count);
        else
            snprintf(more, sizeof(more), "-- end of feed (%d) --", app->feed.count);

        int w = ui_utf8_width(more);
        int x = (COLS - w) / 2;
        if (x < 0) x = 0;

        if (app->feed_on_more && app->feed.has_more) attron(A_REVERSE | A_BOLD);
        else attron(A_DIM);
        mvaddstr(row, x, more);
        if (app->feed_on_more && app->feed.has_more) attroff(A_REVERSE | A_BOLD);
        else attroff(A_DIM);
    }
}

static void draw_placeholder(app_t *app, const char *what) {
    attron(A_DIM);
    mvprintw(BODY_TOP + 1, 2, "%s is not built yet.", what);
    mvaddstr(BODY_TOP + 3, 2, "Coming in the next chunk of work.");
    attroff(A_DIM);
    (void)app;
}

void ui_draw(app_t *app) {
    erase();
    draw_tabs(app);

    switch (app->tab) {
        case TAB_FEED:     draw_feed(app); break;
        case TAB_NOTIFS:   draw_placeholder(app, "Notifications"); break;
        case TAB_USERS:    draw_placeholder(app, "Users"); break;
        case TAB_ME:       draw_placeholder(app, "Your profile"); break;
        case TAB_SETTINGS: draw_placeholder(app, "Settings"); break;
        default: break;
    }

    draw_status(app);
    wnoutrefresh(stdscr);
    doupdate();
}

void ui_modal_error(app_t *app, const char *msg) {
    static char lines[MAX_WRAP][512];

    int w = COLS - 8;
    if (w > 64) w = 64;
    if (w < 20) w = 20;

    int n = wrap_text(msg && *msg ? msg : "Unknown error", w - 4, lines, MAX_WRAP);
    int h = n + 4;
    if (h > LINES - 2) h = LINES - 2;

    int top = (LINES - h) / 2;
    int left = (COLS - w) / 2;
    if (top < 0) top = 0;
    if (left < 0) left = 0;

    WINDOW *win = newwin(h, w, top, left);
    if (!win) return;

    if (has_colors()) wattron(win, COLOR_PAIR(CP_ERROR));
    box(win, 0, 0);
    mvwaddstr(win, 0, 2, " Error ");
    if (has_colors()) wattroff(win, COLOR_PAIR(CP_ERROR));

    for (int i = 0; i < n && i + 1 < h - 2; i++)
        mvwaddstr(win, 1 + i, 2, lines[i]);

    wattron(win, A_DIM);
    mvwaddstr(win, h - 2, 2, "any key to dismiss");
    wattroff(win, A_DIM);

    wrefresh(win);

    /* Blocking wait: an error is worth a keypress. */
    timeout(-1);
    getch();
    timeout(500);

    delwin(win);
    app->status[0] = '\0';
    app->status_is_error = 0;
    ui_draw(app);
}
