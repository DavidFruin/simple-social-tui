#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <ncurses.h>
#include "ui.h"
#include "timefmt.h"
#include "detail.h"
#include "settings.h"
#include "auth.h"

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
        /* Forces a black background everywhere, including plain unpaired
         * text (pair 0), instead of use_default_colors()'s transparency --
         * the app should look the same regardless of the terminal's own
         * scheme, the way the website looks the same in every browser. */
        assume_default_colors(COLOR_WHITE, COLOR_BLACK);
        init_pair(CP_TAB_ACTIVE, COLOR_BLACK,   COLOR_BLUE);
        init_pair(CP_AUTHOR,     COLOR_CYAN,    COLOR_BLACK);
        init_pair(CP_LIKED,      COLOR_GREEN,   COLOR_BLACK);
        init_pair(CP_ERROR,      COLOR_RED,     COLOR_BLACK);
        init_pair(CP_BADGE,      COLOR_YELLOW,  COLOR_BLACK);
        init_pair(CP_PRIMARY,    COLOR_BLUE,    COLOR_BLACK);
        init_pair(CP_MENTION,    COLOR_MAGENTA, COLOR_BLACK);
    }
    return 0;
}

void ui_teardown(void) {
    curs_set(1);
    endwin();
}

int ui_body_height(void) {
    int h = LINES - BODY_TOP - 2;  /* minus the footer's rule + status rows */
    return h > 0 ? h : 0;
}

/* Draws an h x w box (ACS line-drawing, so it degrades on limited
 * terminals) with its top-left corner at (y, x) on stdscr. emphasize
 * draws a bold border instead of a dim one -- used to mark the selected
 * card among a list of collapsed ones. */
void ui_draw_box(int y, int x, int h, int w, int emphasize) {
    if (h < 2 || w < 2) return;

    attr_t a = emphasize ? A_BOLD : A_DIM;
    attron(COLOR_PAIR(CP_PRIMARY) | a);
    mvaddch(y, x, ACS_ULCORNER);
    mvhline(y, x + 1, ACS_HLINE, w - 2);
    mvaddch(y, x + w - 1, ACS_URCORNER);

    for (int row = y + 1; row < y + h - 1; row++) {
        mvaddch(row, x, ACS_VLINE);
        mvaddch(row, x + w - 1, ACS_VLINE);
    }

    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    attroff(COLOR_PAIR(CP_PRIMARY) | a);
}

/* Draws one already-wrapped line of post/comment text, highlighting
 * "@[id]" mention tokens in magenta. The vendored API client doesn't
 * resolve mention ids to emails (the JSON parser never captures the
 * `mentions` field the API attaches), so the id is dropped in favour of
 * a generic "@user" rather than showing the raw number. */
void draw_text_line(int row, int col, const char *line, int plain) {
    const char *p = line;
    while (*p) {
        const char *at = strstr(p, "@[");
        if (!at) { mvaddstr(row, col, p); return; }

        if (at > p) {
            char pre[512];
            size_t n = (size_t)(at - p);
            if (n >= sizeof(pre)) n = sizeof(pre) - 1;
            memcpy(pre, p, n);
            pre[n] = '\0';
            mvaddstr(row, col, pre);
            col += ui_utf8_width(pre);
        }

        const char *digits = at + 2;
        const char *q = digits;
        while (*q >= '0' && *q <= '9') q++;

        if (q > digits && *q == ']') {
            /* plain: the caller already owns the background (e.g. a
             * selected card's blue fill) -- magenta would clash or be
             * unreadable there, so just leave "@user" in the current
             * attributes instead of layering the mention color on top. */
            if (!plain) attron(COLOR_PAIR(CP_MENTION) | A_BOLD);
            mvaddstr(row, col, "@user");
            if (!plain) attroff(COLOR_PAIR(CP_MENTION) | A_BOLD);
            col += 5;
            p = q + 1;
        } else {
            /* "@[" that wasn't actually a mention token -- print it
             * literally (this also covers a token truncated mid-way by
             * the caller's column budget) and move past it. */
            mvaddstr(row, col, "@[");
            col += 2;
            p = at + 2;
        }
    }
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

    /* "Simple Social" brand, mirroring the web header's logo link --
     * dropped on narrow terminals so the tabs keep their room. */
    if (COLS >= 60) {
        const char *logo = " Simple Social ";
        attron(COLOR_PAIR(CP_PRIMARY) | A_BOLD);
        mvaddstr(TAB_ROW, x, logo);
        attroff(COLOR_PAIR(CP_PRIMARY) | A_BOLD);
        x += ui_utf8_width(logo);

        attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
        mvaddch(TAB_ROW, x, ACS_VLINE);
        attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);
        x += 1;
    }

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
        else if (t == TAB_NOTIFS && app->unseen > 0) attron(COLOR_PAIR(CP_BADGE) | A_BOLD);
        mvaddstr(TAB_ROW, x, label);
        if (active) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else if (t == TAB_NOTIFS && app->unseen > 0) attroff(COLOR_PAIR(CP_BADGE) | A_BOLD);

        x += w;
        if (x < COLS) {
            attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
            mvaddch(TAB_ROW, x, ACS_VLINE);
            attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);
            x += 1;
        }
    }

    /* Who you are, right-aligned, if it fits. */
    if (app->state.user.email[0]) {
        char who[64];
        ui_utf8_take(who, sizeof(who), app->state.user.email, 28, 1);
        int w = ui_utf8_width(who);
        if (COLS - w - 1 > x + 2) {
            attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
            mvaddstr(TAB_ROW, COLS - w - 1, who);
            attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);
        }
    }

    attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
    mvhline(RULE_ROW, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);
}

static const char *hints_for(app_t *app) {
    if (app->view == VIEW_POST)
        return "j/k:comments  c:comment  l:like  o:media  d/D:del  tab:tabs  esc:back  ?:help";

    if (app->view == VIEW_PROFILE)
        return "j/k:posts  enter:open  f:follow  w/W:follows  esc:back  ?:help";

    if (app->view == VIEW_USERLIST)
        return "j/k:move  enter:profile  esc:back  ?:help  q:quit";

    switch (app->tab) {
        case TAB_FEED:
            return "j/k:move  enter:open  p:post  l:like  r:refresh  ?:help  q:quit";
        case TAB_NOTIFS:
            return "j/k:move  enter:open post  r:refresh  1-5:tabs  ?:help  q:quit";
        case TAB_USERS:
            return "j/k:move  enter:profile  r:refresh  1-5:tabs  ?:help  q:quit";
        case TAB_ME:
            return "j/k:posts  enter:open  w/W:follows  r:refresh  ?:help  q:quit";
        case TAB_SETTINGS:
            return "j/k:move  enter:choose  1-5:tabs  ?:help  q:quit";
        default:
            return "1-5:tabs  r:refresh  ?:help  q:quit";
    }
}

static void draw_status(app_t *app) {
    /* Rule above the footer text, mirroring the web footer's border-top. */
    attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
    mvhline(LINES - 2, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);

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
    attron(COLOR_PAIR(CP_PRIMARY) | A_DIM);
    mvaddstr(row, 0, buf);
    attroff(COLOR_PAIR(CP_PRIMARY) | A_DIM);
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

static int post_height(post_list_t *pl, int idx, int width) {
    int body_w = width - 4;
    if (body_w < 8) body_w = 8;
    int n = wrap_text(pl->store.posts[idx].text, body_w, NULL, MAX_WRAP);
    return 4 /*top border + header + meta + bottom border*/ + n;
}

/* Scroll feed_top just far enough that the cursor is fully on screen.
 * When the cursor is parked on the load-more row, that row has to be
 * visible too -- otherwise moving onto it looks like nothing happened. */
static void ensure_visible(post_list_t *pl, int body_h) {
    if (pl->store.count == 0) { pl->top = 0; return; }

    int target = pl->on_more ? pl->store.count - 1 : pl->sel;
    int extra  = pl->on_more ? 1 : 0;

    if (target < pl->top) pl->top = target;

    for (;;) {
        int used = extra;
        for (int i = pl->top; i <= target && i < pl->store.count; i++)
            used += post_height(pl, i, COLS);
        if (used <= body_h || pl->top >= target) break;
        pl->top++;
    }
}

/* Every post is drawn in full -- wrapped body, meta row, the works --
 * with only the selected one getting a bold border instead of a dim one
 * to mark it as the selection. Clamped to `bottom`: near the end of the
 * screen it just shows fewer interior rows, always closing with its own
 * bottom border rather than running past `bottom`. */
static int draw_post_card(post_list_t *pl, int row, int idx, int bottom) {
    api_post_t *p = &pl->store.posts[idx];
    int selected = (idx == pl->sel);
    int body_w = COLS - 4;
    if (body_w < 8) body_w = 8;

    static char lines[MAX_WRAP][512];
    int n = wrap_text(p->text, body_w, lines, MAX_WRAP);

    int h = 4 + n;   /* top border, header, n body lines, meta, bottom border */
    if (row + h - 1 > bottom) h = bottom - row + 1;
    if (h < 2) return bottom + 1;   /* no room left to draw anything */

    ui_draw_box(row, 0, h, COLS, selected);
    int last = row + h - 1;   /* the bottom border's row -- never draw on it */

    /* Selection needs to be unmissable, not just a bolder border -- fill
     * the whole card's interior with the same blue used for a selected
     * row everywhere else (Notifications/Users/Settings). That means the
     * per-kind semantic colors below (author cyan, likes green, mentions
     * magenta, dim timestamps) are skipped while selected: they're tuned
     * for a black background and would be unreadable or clash on blue,
     * so the selected card trades that nuance for being impossible to
     * miss, matching the rest of the app's selection language. */
    if (selected) {
        attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        for (int fr = row + 1; fr < last; fr++) mvhline(fr, 1, ' ', COLS - 2);
    }

    char when[32];
    timefmt_full(p->timestamp, when, sizeof(when));
    int when_w = ui_utf8_width(when);

    char author[128];
    ui_utf8_take(author, sizeof(author), p->user_email, COLS - when_w - 8, 1);

    int r = row + 1;

    /* Header */
    if (r < last) {
        if (!selected) attron(COLOR_PAIR(CP_AUTHOR) | A_BOLD);
        mvaddstr(r, 3, author);
        if (!selected) attroff(COLOR_PAIR(CP_AUTHOR) | A_BOLD);
        if (COLS - when_w - 2 > 3 + ui_utf8_width(author)) {
            if (!selected) attron(A_DIM);
            mvaddstr(r, COLS - when_w - 2, when);
            if (!selected) attroff(A_DIM);
        }
        r++;
    }

    /* Body, with @[id] mentions highlighted -- except on the fill, where
     * every foreground color but the fill's own would be unreadable. */
    for (int i = 0; i < n && r < last; i++, r++)
        draw_text_line(r, 3, lines[i], selected);

    /* Meta */
    if (r < last) {
        char meta[256];
        snprintf(meta, sizeof(meta), "%s %d", "\xe2\x99\xa5", p->like_count);
        if (p->is_liked && !selected) attron(COLOR_PAIR(CP_LIKED) | A_BOLD);
        mvaddstr(r, 3, meta);
        if (p->is_liked && !selected) attroff(COLOR_PAIR(CP_LIKED) | A_BOLD);

        if (p->media_url[0]) {
            int x = 3 + ui_utf8_width(meta) + 3;
            int budget = (COLS - 2) - x - 7;   /* 7 = "media: " */
            if (budget > 4) {
                char murl[512];
                ui_utf8_take(murl, sizeof(murl), p->media_url, budget, 1);
                if (!selected) attron(A_DIM);
                mvprintw(r, x, "media: %s", murl);
                if (!selected) attroff(A_DIM);
            }
        }
    }

    if (selected) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
    return row + h;
}

void ui_draw_post_list(post_list_t *pl, int body_top, int bottom, const char *empty_msg,
                       const char *end_label) {
    if (pl->store.count == 0) {
        attron(A_DIM);
        mvaddstr(body_top + 1, 2, empty_msg);
        attroff(A_DIM);
        return;
    }

    ensure_visible(pl, bottom - body_top + 1);

    int row = body_top;
    for (int i = pl->top; i < pl->store.count && row <= bottom; i++)
        row = draw_post_card(pl, row, i, bottom);

    if (row <= bottom) {
        char more[128];
        if (pl->store.has_more)
            snprintf(more, sizeof(more), "-- load more (%d of %d) --",
                     pl->store.count, pl->store.total_count);
        else
            snprintf(more, sizeof(more), "-- %s (%d) --", end_label, pl->store.count);

        int w = ui_utf8_width(more);
        int x = (COLS - w) / 2;
        if (x < 0) x = 0;

        if (pl->on_more && pl->store.has_more) attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else attron(A_DIM);
        mvaddstr(row, x, more);
        if (pl->on_more && pl->store.has_more) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else attroff(A_DIM);
    }
}

static void draw_feed(app_t *app) {
    /* LINES - 3: leaves room for the footer's rule + status rows. */
    ui_draw_post_list(&app->feed, BODY_TOP, LINES - 3,
                      app->feed.fetched ? "Feed is empty." : "Press r to load your feed.",
                      "end of feed");
}

/* The five types api.php writes. */
static const char *notif_phrase(const char *type) {
    if (strcmp(type, "like") == 0)     return "liked your post";
    if (strcmp(type, "unlike") == 0)   return "unliked your post";
    if (strcmp(type, "comment") == 0)  return "commented on your post";
    if (strcmp(type, "follow") == 0)   return "followed you";
    if (strcmp(type, "unfollow") == 0) return "unfollowed you";
    return type;
}

static void draw_notifs(app_t *app) {
    int bottom = LINES - 3;  /* leaves room for the footer's rule + status rows */

    if (app->notifs.count == 0) {
        attron(A_DIM);
        mvaddstr(BODY_TOP + 1, 2,
                 app->notifs_fetched ? "Nothing here yet."
                                     : "Press r to load your notifications.");
        attroff(A_DIM);
        return;
    }

    int rows = bottom - BODY_TOP + 1;
    if (rows < 1) rows = 1;

    /* Parked on the load-more row: keep a line for it, or moving onto it
     * looks like nothing happened. */
    int reserve = app->notif_on_more ? 1 : 0;
    int avail = rows - reserve;
    if (avail < 1) avail = 1;

    int target = app->notif_on_more ? app->notifs.count - 1 : app->notif_sel;
    if (target < app->notif_top) app->notif_top = target;
    if (target >= app->notif_top + avail) app->notif_top = target - avail + 1;
    if (app->notif_top < 0) app->notif_top = 0;

    int row = BODY_TOP;
    for (int i = app->notif_top; i < app->notifs.count && row <= bottom - reserve; i++, row++) {
        api_notification_t *n = &app->notifs.notifs[i];

        char when[32];
        timefmt_short(n->created_at, when, sizeof(when));
        int when_w = ui_utf8_width(when);

        /* Entries that arrived since the last visit. */
        int is_new = (i < app->notif_new);
        int selected = (i == app->notif_sel && !app->notif_on_more);

        int actor_w = COLS < 60 ? 16 : 26;
        char actor[128];
        ui_utf8_take(actor, sizeof(actor), n->actor_email, actor_w, 1);

        const char *phrase = notif_phrase(n->type);
        int phrase_x = 2 + actor_w + 1;
        int phrase_w = COLS - phrase_x - when_w - 2;
        if (phrase_w < 4) phrase_w = 4;

        char what[128];
        ui_utf8_take(what, sizeof(what), phrase, phrase_w, 1);

        if (selected) { attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD); mvhline(row, 0, ' ', COLS); }

        if (is_new) {
            attron(COLOR_PAIR(CP_BADGE) | A_BOLD);
            mvaddstr(row, 0, "\xe2\x97\x8f");     /* new marker */
            attroff(COLOR_PAIR(CP_BADGE) | A_BOLD);
            /* attroff cleared the color pair entirely (not just the badge's
             * own), so a selected+new row needs its fill pair reinstated. */
            if (selected) attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        }

        if (!selected) attron(COLOR_PAIR(CP_AUTHOR));
        mvaddstr(row, 2, actor);
        if (!selected) attroff(COLOR_PAIR(CP_AUTHOR));

        if (is_new && !selected) attron(A_BOLD);
        mvaddstr(row, phrase_x, what);
        if (is_new && !selected) attroff(A_BOLD);

        if (!selected) attron(A_DIM);
        mvaddstr(row, COLS - when_w - 1, when);
        if (!selected) attroff(A_DIM);

        if (selected) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
    }

    if (row <= bottom) {
        char more[128];
        if (app->notifs.has_more)
            snprintf(more, sizeof(more), "-- load more (%d shown) --", app->notifs.count);
        else
            snprintf(more, sizeof(more), "-- that is all (%d) --", app->notifs.count);

        int w = ui_utf8_width(more);
        int x = (COLS - w) / 2;
        if (x < 0) x = 0;

        if (app->notif_on_more && app->notifs.has_more) attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else attron(A_DIM);
        mvaddstr(row, x, more);
        if (app->notif_on_more && app->notifs.has_more) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
        else attroff(A_DIM);
    }
}

/* ---------- users ---------- */

static void draw_user_row(int row, const api_user_t *u, int selected, int followed,
                          int is_me) {
    char when[32];
    timefmt_short(u->created_at, when, sizeof(when));
    int when_w = ui_utf8_width(when);

    const char *tag = is_me ? "you" : (followed ? "following" : "");
    int tag_w = ui_utf8_width(tag);

    int email_w = COLS - 4 - tag_w - when_w - 3;
    if (email_w < 8) email_w = 8;

    char email[288];
    ui_utf8_take(email, sizeof(email), u->email, email_w, 1);

    if (selected) { attron(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD); mvhline(row, 0, ' ', COLS); }

    if (!selected) attron(COLOR_PAIR(CP_AUTHOR));
    mvaddstr(row, 2, email);
    if (!selected) attroff(COLOR_PAIR(CP_AUTHOR));

    if (tag[0]) {
        int x = COLS - when_w - tag_w - 3;
        if (x > 2 + ui_utf8_width(email)) {
            if (!selected) attron(COLOR_PAIR(CP_LIKED));
            mvaddstr(row, x, tag);
            if (!selected) attroff(COLOR_PAIR(CP_LIKED));
        }
    }

    if (!selected) attron(A_DIM);
    mvaddstr(row, COLS - when_w - 1, when);
    if (!selected) attroff(A_DIM);

    if (selected) attroff(COLOR_PAIR(CP_TAB_ACTIVE) | A_BOLD);
}

/* Shared scroll + draw for the users tab and a pushed follows list. */
static void draw_user_list(app_t *app, api_users_result_t *lst, int sel, int *top,
                           const char *empty_msg, int body_top) {
    int bottom = LINES - 3;  /* leaves room for the footer's rule + status rows */

    if (!lst || lst->count == 0) {
        attron(A_DIM);
        mvaddstr(body_top + 1, 2, empty_msg);
        attroff(A_DIM);
        return;
    }

    int rows = bottom - body_top + 1;
    if (rows < 1) rows = 1;

    if (sel < *top) *top = sel;
    if (sel >= *top + rows) *top = sel - rows + 1;
    if (*top < 0) *top = 0;

    int row = body_top;
    for (int i = *top; i < lst->count && row <= bottom; i++, row++) {
        api_user_t *u = &lst->users[i];
        draw_user_row(row, u, i == sel,
                      app_follows(app, u->id),
                      u->id == app->state.user.user_id);
    }
}

static void draw_users(app_t *app) {
    draw_user_list(app, app->users, app->users_sel, &app->users_top,
                   app->users_fetched ? "No users." : "Press r to load the user list.",
                   BODY_TOP);
}

/* ---------- profile ---------- */

static void draw_profile(app_t *app) {
    int row = BODY_TOP;

    char email[288];
    ui_utf8_take(email, sizeof(email), app->profile_email, COLS - 4, 1);
    attron(COLOR_PAIR(CP_AUTHOR) | A_BOLD);
    mvaddstr(row, 1, email);
    attroff(COLOR_PAIR(CP_AUTHOR) | A_BOLD);
    row++;

    char joined[64];
    timefmt_full(app->profile_created, joined, sizeof(joined));

    attron(A_DIM);
    mvprintw(row, 1, "joined %s", joined);
    attroff(A_DIM);

    if (app->profile_id == app->state.user.user_id) {
        attron(A_DIM);
        mvaddstr(row, COLS - 4, "you");
        attroff(A_DIM);
    } else if (app->profile_is_following) {
        attron(COLOR_PAIR(CP_LIKED) | A_BOLD);
        mvaddstr(row, COLS - 11, "following");
        attroff(COLOR_PAIR(CP_LIKED) | A_BOLD);
    }
    row++;

    int nposts = app->profile_posts.store.total_count;
    attron(A_DIM);
    mvprintw(row, 1, "%d following   %d %s   %d %s",
             app->profile_follows,
             app->profile_followers, app->profile_followers == 1 ? "follower" : "followers",
             nposts, nposts == 1 ? "post" : "posts");
    attroff(A_DIM);
    row++;

    attron(A_DIM);
    mvhline(row, 0, ACS_HLINE, COLS);
    attroff(A_DIM);
    row++;

    /* LINES - 3: leaves room for the footer's rule + status rows. */
    ui_draw_post_list(&app->profile_posts, row, LINES - 3,
                      "No posts yet.", "end");
}


void ui_draw(app_t *app) {
    if (app->in_auth) {
        auth_draw(app);
        return;
    }

    erase();
    draw_tabs(app);

    if (app->view == VIEW_POST) {
        detail_draw(app, BODY_TOP, ui_body_height());
        draw_status(app);
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }

    if (app->view == VIEW_PROFILE) {
        draw_profile(app);
        draw_status(app);
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }

    if (app->view == VIEW_USERLIST) {
        attron(A_BOLD);
        char t[128];
        ui_utf8_take(t, sizeof(t), app->list_title, COLS - 2, 1);
        mvaddstr(BODY_TOP, 1, t);
        attroff(A_BOLD);
        attron(A_DIM);
        mvhline(BODY_TOP + 1, 0, ACS_HLINE, COLS);
        attroff(A_DIM);

        draw_user_list(app, app->list_users, app->list_sel, &app->list_top,
                       "Nobody here.", BODY_TOP + 2);
        draw_status(app);
        wnoutrefresh(stdscr);
        doupdate();
        return;
    }

    switch (app->tab) {
        case TAB_FEED:     draw_feed(app); break;
        case TAB_NOTIFS:   draw_notifs(app); break;
        case TAB_USERS:    draw_users(app); break;
        case TAB_ME:       draw_profile(app); break;
        case TAB_SETTINGS: settings_draw(app, BODY_TOP); break;
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

    if (has_colors()) wattron(win, COLOR_PAIR(CP_ERROR) | A_BOLD);
    box(win, 0, 0);
    mvwaddstr(win, 0, 2, " Error ");
    if (has_colors()) wattroff(win, COLOR_PAIR(CP_ERROR) | A_BOLD);

    for (int i = 0; i < n && i + 1 < h - 2; i++)
        mvwaddstr(win, 1 + i, 2, lines[i]);

    wattron(win, COLOR_PAIR(CP_ERROR) | A_DIM);
    mvwaddstr(win, h - 2, 2, "any key to dismiss");
    wattroff(win, COLOR_PAIR(CP_ERROR) | A_DIM);

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

void ui_help(app_t *app) {
    static const char *rows[] = {
        "Moving",
        "  j / k, down / up      move",
        "  ctrl-d / ctrl-u       half page",
        "  g / G, home / end     first / last",
        "",
        "Feed",
        "  enter                 open the selected post",
        "  enter on load-more    fetch the next page",
        "  p                     write a post",
        "  l                     like / unlike",
        "  r                     refresh",
        "",
        "Post",
        "  j / k                 move between comments",
        "  c                     write a comment",
        "  l                     like / unlike",
        "  o                     open media in system viewer",
        "  d                     delete the selected comment",
        "  D                     delete the post",
        "  tab / shift-tab       next / previous tab (closes the post)",
        "  esc, backspace        back to the feed",
        "",
        "Composer",
        "  type                  insert; enter makes a new line",
        "  ctrl-d                send",
        "  ctrl-o                attach media (posts only)",
        "  esc                   cancel (asks first if you wrote something)",
        "  arrows, home / end    move the cursor",
        "",
        "Notifications",
        "  enter                 open the post it refers to",
        "  r                     refresh",
        "",
        "Users and profiles",
        "  enter                 open a profile / a post",
        "  f                     follow or unfollow",
        "  w / W                 who they follow / who follows them",
        "",
        "Tabs",
        "  1 - 5                 jump to a tab",
        "  tab / shift-tab       next / previous tab",
        "",
        "Settings",
        "  j / k                 move between actions",
        "  enter                 run the selected action",
        "",
        "  ?                     this help",
        "  q                     quit",
        NULL
    };

    int n = 0;
    int widest = 0;
    for (; rows[n]; n++) {
        int w = ui_utf8_width(rows[n]);
        if (w > widest) widest = w;
    }

    int h = n + 4;
    int w = widest + 4;
    if (h > LINES) h = LINES;
    if (w > COLS) w = COLS;

    int top = (LINES - h) / 2;
    int left = (COLS - w) / 2;
    if (top < 0) top = 0;
    if (left < 0) left = 0;

    WINDOW *win = newwin(h, w, top, left);
    if (!win) return;

    wattron(win, COLOR_PAIR(CP_PRIMARY));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(CP_PRIMARY));
    wattron(win, COLOR_PAIR(CP_PRIMARY) | A_BOLD);
    mvwaddstr(win, 0, 2, " Keys ");
    wattroff(win, COLOR_PAIR(CP_PRIMARY) | A_BOLD);

    for (int i = 0; i < n && i + 2 < h - 1; i++) {
        char buf[256];
        ui_utf8_take(buf, sizeof(buf), rows[i], w - 4, 1);
        /* Section headings are the lines with no leading space. */
        int heading = rows[i][0] != ' ' && rows[i][0] != '\0';
        if (heading) wattron(win, A_BOLD);
        else wattron(win, A_DIM);
        mvwaddstr(win, 1 + i, 2, buf);
        if (heading) wattroff(win, A_BOLD);
        else wattroff(win, A_DIM);
    }

    wattron(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);
    mvwaddstr(win, h - 2, 2, "any key to close");
    wattroff(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);

    wrefresh(win);
    timeout(-1);
    getch();
    timeout(500);

    delwin(win);
    ui_draw(app);
}

int ui_confirm(app_t *app, const char *question, int danger) {
    static char lines[MAX_WRAP][512];
    int cp = danger ? CP_ERROR : CP_PRIMARY;

    int w = COLS - 8;
    if (w > 60) w = 60;
    if (w < 24) w = COLS > 24 ? 24 : COLS;

    int n = wrap_text(question, w - 4, lines, MAX_WRAP);
    int h = n + 4;
    if (h > LINES) h = LINES;

    int top = (LINES - h) / 2;
    int left = (COLS - w) / 2;
    if (top < 0) top = 0;
    if (left < 0) left = 0;

    WINDOW *win = newwin(h, w, top, left);
    if (!win) return 0;

    wattron(win, COLOR_PAIR(cp));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(cp));
    wattron(win, COLOR_PAIR(cp) | A_BOLD);
    mvwaddstr(win, 0, 2, danger ? " Confirm (cannot be undone) " : " Confirm ");
    wattroff(win, COLOR_PAIR(cp) | A_BOLD);

    for (int i = 0; i < n && i + 1 < h - 2; i++)
        mvwaddstr(win, 1 + i, 2, lines[i]);

    wattron(win, COLOR_PAIR(cp) | A_BOLD);
    mvwaddstr(win, h - 2, 2, "y");
    wattroff(win, COLOR_PAIR(cp) | A_BOLD);
    wattron(win, COLOR_PAIR(cp) | A_DIM);
    mvwaddstr(win, h - 2, 3, " to confirm, any other key to cancel");
    wattroff(win, COLOR_PAIR(cp) | A_DIM);

    wrefresh(win);

    timeout(-1);
    int ch = getch();
    timeout(500);

    delwin(win);
    ui_draw(app);

    return (ch == 'y' || ch == 'Y');
}

int ui_prompt(app_t *app, const char *title, const char *label,
              int hidden, char *out, size_t outsz) {
    char buf[1024];
    size_t len = 0;
    buf[0] = '\0';

    int chars = 0;      /* codepoints, for the dot echo and the counter */
    int prev_input = app->input_active;

    app->input_active = 1;   /* no idle refresh while someone is typing */
    curs_set(1);
    timeout(-1);

    int result = 0;

    for (;;) {
        int w = COLS - 8;
        if (w > 64) w = 64;
        if (w < 24) w = COLS > 24 ? 24 : COLS;
        int h = 6;
        if (h > LINES) h = LINES;

        int top = (LINES - h) / 2, left = (COLS - w) / 2;
        if (top < 0) top = 0;
        if (left < 0) left = 0;

        ui_draw(app);

        WINDOW *win = newwin(h, w, top, left);
        if (!win) break;

        wattron(win, COLOR_PAIR(CP_PRIMARY));
        box(win, 0, 0);
        wattroff(win, COLOR_PAIR(CP_PRIMARY));
        wattron(win, COLOR_PAIR(CP_PRIMARY) | A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title);
        wattroff(win, COLOR_PAIR(CP_PRIMARY) | A_BOLD);

        wattron(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);
        char lbl[128];
        ui_utf8_take(lbl, sizeof(lbl), label, w - 4, 1);
        mvwaddstr(win, 1, 2, lbl);
        wattroff(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);

        /* What to show: dots for a secret, the tail of the text otherwise. */
        char shown[1024];
        int avail = w - 4;
        if (hidden) {
            int dots = chars < avail ? chars : avail;
            for (int i = 0; i < dots; i++) shown[i] = '.';
            shown[dots] = '\0';
        } else {
            const char *src = buf;
            if ((int)len > avail) src = buf + (len - avail);
            snprintf(shown, sizeof(shown), "%s", src);
        }
        mvwaddstr(win, 2, 2, shown);

        wattron(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);
        mvwaddstr(win, h - 2, 2, "enter accepts   esc cancels");
        wattroff(win, COLOR_PAIR(CP_PRIMARY) | A_DIM);

        int cx = 2 + ui_utf8_width(shown);
        if (cx > w - 2) cx = w - 2;
        wmove(win, 2, cx);
        wrefresh(win);

        wint_t wch;
        int kind = wget_wch(win, &wch);
        delwin(win);

        if (kind == ERR) continue;

        if (kind == KEY_CODE_YES) {
            if (wch == KEY_BACKSPACE && len > 0) {
                size_t p = len - 1;
                while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80) p--;
                buf[p] = '\0';
                len = p;
                if (chars > 0) chars--;
            }
            continue;
        }

        if (wch == 27) { result = 0; break; }                 /* esc */

        if (wch == '\r' || wch == '\n') {
            result = (len > 0);
            break;
        }

        if (wch == 8 || wch == 127) {                          /* backspace */
            if (len > 0) {
                size_t p = len - 1;
                while (p > 0 && ((unsigned char)buf[p] & 0xC0) == 0x80) p--;
                buf[p] = '\0';
                len = p;
                if (chars > 0) chars--;
            }
            continue;
        }

        if (wch == 21) { len = 0; chars = 0; buf[0] = '\0'; continue; }  /* ^U */

        if (wch >= 32) {
            char mb[MB_CUR_MAX + 1];
            mbstate_t st;
            memset(&st, 0, sizeof(st));
            size_t n = wcrtomb(mb, (wchar_t)wch, &st);
            if (n != (size_t)-1 && len + n + 1 < sizeof(buf)) {
                memcpy(buf + len, mb, n);
                len += n;
                buf[len] = '\0';
                chars++;
            }
        }
    }

    curs_set(0);
    timeout(500);
    app->input_active = prev_input;

    if (result) snprintf(out, outsz, "%s", buf);
    else if (outsz) out[0] = '\0';

    /* Not left in the buffer any longer than needed. */
    memset(buf, 0, sizeof(buf));

    ui_draw(app);
    return result;
}
