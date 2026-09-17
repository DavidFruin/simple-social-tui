#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ncurses.h>
#include "app.h"
#include "ui.h"
#include "net.h"
#include "ss_api.h"
#include "detail.h"

static const char *TAB_NAMES[TAB_COUNT] = {
    "Feed", "Notifs", "Users", "Me", "Settings"
};

const char *app_tab_name(tab_t t) {
    if (t < 0 || t >= TAB_COUNT) return "?";
    return TAB_NAMES[t];
}

int app_init(app_t *app) {
    memset(app, 0, sizeof(*app));
    ss_state_init(&app->state);
    tui_cfg_load(&app->cfg);
    store_init(&app->feed);
    cstore_init(&app->comments);
    app->tab = TAB_FEED;
    app->view = VIEW_TABS;
    app->detail_src = -1;
    return 0;
}

void app_free(app_t *app) {
    store_free(&app->feed);
    cstore_free(&app->comments);
}

void app_set_status(app_t *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_is_error = 0;
}

void app_set_error(app_t *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_is_error = 1;
}

/* Failures get a modal so they cannot scroll past unnoticed; transient
 * messages stay in the status line. */
static void report(app_t *app, int rc) {
    if (rc != 0 && app->status_is_error) ui_modal_error(app, app->status);
}

static void feed_move(app_t *app, int delta) {
    if (app->feed.count == 0) return;

    if (app->feed_on_more) {
        if (delta < 0) { app->feed_on_more = 0; app->feed_sel = app->feed.count - 1; }
        return;
    }

    int next = app->feed_sel + delta;

    if (next >= app->feed.count) {
        if (app->feed.has_more) { app->feed_on_more = 1; return; }
        next = app->feed.count - 1;
    }
    if (next < 0) next = 0;
    app->feed_sel = next;
}

static void feed_page(app_t *app, int dir) {
    int step = ui_body_height() / 2;
    if (step < 1) step = 1;
    feed_move(app, dir * step);
}

/* Liking from the feed reuses net_toggle_like by pointing the detail slot
 * at the selected row first, so both copies stay in step. */
static void feed_like(app_t *app) {
    if (app->feed.count == 0 || app->feed_on_more) return;
    api_post_t saved = app->detail;
    int saved_src = app->detail_src;

    app->detail = app->feed.posts[app->feed_sel];
    app->detail_src = app->feed_sel;

    int rc = net_toggle_like(app);

    if (rc == 0) app->feed.posts[app->feed_sel] = app->detail;

    /* Restore whatever the detail view was holding. */
    app->detail = saved;
    app->detail_src = saved_src;

    if (rc != 0 && app->status_is_error) ui_modal_error(app, app->status);
}

static void open_selected_post(app_t *app) {
    if (app->feed.count == 0 || app->feed_on_more) return;
    int rc = net_open_post(app, app->feed.posts[app->feed_sel].id, app->feed_sel);
    if (rc != 0 && app->status_is_error) ui_modal_error(app, app->status);
}

static void close_post(app_t *app) {
    app->view = VIEW_TABS;
    app->status[0] = '\0';
}

static void switch_tab(app_t *app, tab_t t) {
    if (t == app->tab) return;
    app->tab = t;
    app->status[0] = '\0';

    /* Re-fetch on arrival when what we hold is stale. */
    if (t == TAB_FEED) {
        int stale = !app->feed_fetched ||
            (app->cfg.feed_refresh_secs > 0 &&
             difftime(time(NULL), app->feed_fetched) >= app->cfg.feed_refresh_secs);
        if (stale) report(app, net_refresh_feed(app));
    }
}

/* Idle timers. Suppressed while a composer or prompt owns the keyboard,
 * so a refresh can never yank the screen out from under someone typing. */
static void run_timers(app_t *app) {
    if (app->input_active) return;

    time_t now = time(NULL);

    if (app->cfg.badge_refresh_secs > 0 &&
        difftime(now, app->unseen_fetched) >= app->cfg.badge_refresh_secs) {
        net_refresh_badge(app);   /* silent: a failed badge poll is not worth a modal */
    }

    if (app->tab == TAB_FEED && app->cfg.feed_refresh_secs > 0 && app->feed_fetched &&
        difftime(now, app->feed_fetched) >= app->cfg.feed_refresh_secs) {
        /* Hold position across an idle refresh rather than snapping to top. */
        int sel = app->feed_sel, top = app->feed_top;
        if (net_refresh_feed(app) == 0) {
            if (sel < app->feed.count) { app->feed_sel = sel; app->feed_top = top; }
        }
    }
}

void app_run(app_t *app) {
    report(app, net_refresh_feed(app));
    net_refresh_badge(app);

    while (!app->quit) {
        ui_draw(app);

        int ch = getch();

        if (ch == ERR) {          /* timeout tick */
            run_timers(app);
            continue;
        }

        /* Any keypress clears a stale status message so hints return. */
        if (app->status[0] && !app->status_is_error) app->status[0] = '\0';

        /* The post view owns most keys while it is open. */
        if (app->view == VIEW_POST) {
            switch (ch) {
                case KEY_RESIZE: break;

                case 27:            /* esc */
                case KEY_BACKSPACE:
                case 127: case 8:
                    close_post(app);
                    continue;

                case 'q':
                    app->quit = 1;
                    continue;

                case 'j': case KEY_DOWN:  detail_move_selection(app, 1);  continue;
                case 'k': case KEY_UP:    detail_move_selection(app, -1); continue;

                case KEY_NPAGE: case 4:
                    detail_scroll_by(app, ui_body_height() / 2);  continue;
                case KEY_PPAGE: case 21:
                    detail_scroll_by(app, -(ui_body_height() / 2)); continue;

                case 'g': case KEY_HOME:
                    app->detail_scroll = 0; app->comment_sel = 0;
                    app->detail_on_more = 0; continue;

                case 'l':
                    report(app, net_toggle_like(app));
                    if (app->detail_src >= 0 && app->detail_src < app->feed.count &&
                        strcmp(app->feed.posts[app->detail_src].id, app->detail.id) == 0)
                        app->feed.posts[app->detail_src] = app->detail;
                    continue;

                case 'o':
                    report(app, detail_open_media(app));
                    continue;

                case '\r': case '\n': case KEY_ENTER: case ' ':
                    if (app->detail_on_more) {
                        report(app, net_load_more_comments(app));
                        app->detail_on_more = 0;
                    }
                    continue;

                case '?':
                    ui_help(app);
                    continue;

                default:
                    continue;
            }
        }

        switch (ch) {
            case KEY_RESIZE:
                /* ncurses has already resized; the next draw re-lays out. */
                break;

            case 'q':
                app->quit = 1;
                break;

            case 'j': case KEY_DOWN:  feed_move(app, 1);  break;
            case 'k': case KEY_UP:    feed_move(app, -1); break;
            case KEY_NPAGE: case 4:   feed_page(app, 1);  break;   /* ^D */
            case KEY_PPAGE: case 21:  feed_page(app, -1); break;   /* ^U */

            case 'g': case KEY_HOME:
                app->feed_sel = 0; app->feed_top = 0; app->feed_on_more = 0;
                break;

            case 'G': case KEY_END:
                if (app->feed.count) { app->feed_sel = app->feed.count - 1; app->feed_on_more = 0; }
                break;

            case 'r':
                if (app->tab == TAB_FEED) report(app, net_refresh_feed(app));
                net_refresh_badge(app);
                break;

            case '\r': case '\n': case KEY_ENTER: case ' ':
                if (app->tab != TAB_FEED) break;
                if (app->feed_on_more) {
                    report(app, net_load_more_feed(app));
                    app->feed_on_more = 0;
                    if (app->feed.count) app->feed_sel = app->feed.count - 1;
                } else {
                    open_selected_post(app);
                }
                break;

            case 'l':
                if (app->tab == TAB_FEED) feed_like(app);
                break;

            case '1': switch_tab(app, TAB_FEED);     break;
            case '2': switch_tab(app, TAB_NOTIFS);   break;
            case '3': switch_tab(app, TAB_USERS);    break;
            case '4': switch_tab(app, TAB_ME);       break;
            case '5': switch_tab(app, TAB_SETTINGS); break;

            case '\t':
                switch_tab(app, (app->tab + 1) % TAB_COUNT);
                break;
            case KEY_BTAB:
                switch_tab(app, (app->tab + TAB_COUNT - 1) % TAB_COUNT);
                break;

            case '?':
                ui_help(app);
                break;

            default:
                break;
        }
    }
}
