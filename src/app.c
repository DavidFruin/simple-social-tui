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
#include "editor.h"
#include "shell.h"
#include "filepick.h"

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
    nstore_init(&app->notifs);
    app->tab = TAB_FEED;
    app->view = VIEW_TABS;
    app->detail_src = -1;
    return 0;
}

void app_free(app_t *app) {
    store_free(&app->feed);
    cstore_free(&app->comments);
    nstore_free(&app->notifs);
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

static void notif_move(app_t *app, int delta) {
    if (app->notifs.count == 0) return;

    if (app->notif_on_more) {
        if (delta < 0) { app->notif_on_more = 0; app->notif_sel = app->notifs.count - 1; }
        return;
    }

    int next = app->notif_sel + delta;
    if (next >= app->notifs.count) {
        if (app->notifs.has_more) { app->notif_on_more = 1; return; }
        next = app->notifs.count - 1;
    }
    if (next < 0) next = 0;
    app->notif_sel = next;
}

/* Opens the post a notification refers to. follow/unfollow carry no post,
 * so there is nothing to open until profiles exist. */
static void open_notif_target(app_t *app) {
    if (app->notifs.count == 0) return;

    if (app->notif_on_more) {
        report(app, net_load_more_notifs(app));
        app->notif_on_more = 0;
        return;
    }

    api_notification_t *n = &app->notifs.notifs[app->notif_sel];
    if (!n->post_id[0]) {
        app_set_status(app, "That one is about a follow, not a post.");
        return;
    }

    /* -1: this did not come from the feed, so there is no row to keep in step. */
    report(app, net_open_post(app, n->post_id, -1));
}

static void close_post(app_t *app) {
    app->view = VIEW_TABS;
    app->status[0] = '\0';
}

/* Collects text either inline or from $EDITOR, per the config. Returns 1
 * if there is something to send. */
static int compose_text(app_t *app, const char *title, const char *send_label,
                        char *out, size_t outsz, char *attach, size_t attach_sz) {
    if (app->cfg.compose_mode == CFG_COMPOSE_EXTERNAL) {
        app->input_active = 1;
        int rc = shell_edit_text("", out, outsz);
        app->input_active = 0;

        if (rc < 0) {
            app_set_error(app, "no editor found -- set $EDITOR, or use editor = inline in %s",
                          app->cfg.path);
            ui_modal_error(app, app->status);
            return 0;
        }
        if (rc == 0) { app_set_status(app, "Nothing written, nothing sent."); return 0; }

        /* No editor box to host ^O, so ask afterwards instead. */
        if (attach && ui_confirm(app, "Attach a media file to this post?")) {
            char picked[1024];
            if (filepick_run(app, picked, sizeof(picked)))
                snprintf(attach, attach_sz, "%s", picked);
        }
        return 1;
    }

    editor_t ed;
    if (editor_init(&ed, "", 5000) != 0) {
        app_set_error(app, "out of memory");
        return 0;
    }

    int rc = editor_run(app, &ed, title, send_label, attach, attach_sz);
    if (rc != EDITOR_SUBMIT || ed.len == 0) {
        editor_free(&ed);
        if (rc == EDITOR_SUBMIT) app_set_status(app, "Empty -- nothing sent.");
        return 0;
    }

    snprintf(out, outsz, "%s", ed.buf);
    editor_free(&ed);
    return 1;
}

static void compose_post(app_t *app) {
    char text[5200];
    char attach[1024] = {0};
    if (!compose_text(app, "New post", "post", text, sizeof(text), attach, sizeof(attach)))
        return;
    report(app, net_create_post(app, text, attach));
}

/* Media only attaches to posts: api_create_comment takes no media. */
static void compose_comment(app_t *app) {
    char text[5200];
    if (!compose_text(app, "New comment", "comment", text, sizeof(text), NULL, 0)) return;
    report(app, net_create_comment(app, text));
}

static int is_mine(app_t *app, int user_id) {
    return app->state.user.user_id != 0 && user_id == app->state.user.user_id;
}

static void delete_current_post(app_t *app) {
    if (!is_mine(app, app->detail.user_id)) {
        app_set_status(app, "You can only delete your own posts.");
        return;
    }
    if (!ui_confirm(app, "Delete this post? This cannot be undone.")) {
        app_set_status(app, "Not deleted.");
        return;
    }

    char id[64];
    snprintf(id, sizeof(id), "%s", app->detail.id);

    if (net_delete_post(app, id) == 0) {
        app->view = VIEW_TABS;
    } else if (app->status_is_error) {
        ui_modal_error(app, app->status);
    }
}

static void delete_selected_comment(app_t *app) {
    if (app->comments.count == 0 || app->detail_on_more) return;
    if (app->comment_sel < 0 || app->comment_sel >= app->comments.count) return;

    api_comment_t *c = &app->comments.comments[app->comment_sel];
    if (!is_mine(app, c->user_id)) {
        app_set_status(app, "You can only delete your own comments.");
        return;
    }
    if (!ui_confirm(app, "Delete this comment? This cannot be undone.")) {
        app_set_status(app, "Not deleted.");
        return;
    }

    report(app, net_delete_comment(app, c->id));
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
    } else if (t == TAB_NOTIFS) {
        int stale = !app->notifs_fetched ||
            (app->cfg.feed_refresh_secs > 0 &&
             difftime(time(NULL), app->notifs_fetched) >= app->cfg.feed_refresh_secs);
        if (stale) report(app, net_refresh_notifs(app));
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

    if (app->view != VIEW_TABS) return;

    if (app->tab == TAB_FEED && app->cfg.feed_refresh_secs > 0 && app->feed_fetched &&
        difftime(now, app->feed_fetched) >= app->cfg.feed_refresh_secs) {
        /* Hold position across an idle refresh rather than snapping to top. */
        int sel = app->feed_sel, top = app->feed_top;
        if (net_refresh_feed(app) == 0) {
            if (sel < app->feed.count) { app->feed_sel = sel; app->feed_top = top; }
        }
    }

    if (app->tab == TAB_NOTIFS && app->cfg.feed_refresh_secs > 0 && app->notifs_fetched &&
        difftime(now, app->notifs_fetched) >= app->cfg.feed_refresh_secs) {
        int sel = app->notif_sel, top = app->notif_top;
        if (net_refresh_notifs(app) == 0) {
            if (sel < app->notifs.count) { app->notif_sel = sel; app->notif_top = top; }
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

                case 'c':
                    compose_comment(app);
                    continue;

                case 'd':
                    delete_selected_comment(app);
                    continue;

                case 'D':
                    delete_current_post(app);
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

            case 'j': case KEY_DOWN:
                if (app->tab == TAB_NOTIFS) notif_move(app, 1); else feed_move(app, 1);
                break;
            case 'k': case KEY_UP:
                if (app->tab == TAB_NOTIFS) notif_move(app, -1); else feed_move(app, -1);
                break;
            case KEY_NPAGE: case 4:                                /* ^D */
                if (app->tab == TAB_NOTIFS) notif_move(app, ui_body_height() / 2);
                else feed_page(app, 1);
                break;
            case KEY_PPAGE: case 21:                               /* ^U */
                if (app->tab == TAB_NOTIFS) notif_move(app, -(ui_body_height() / 2));
                else feed_page(app, -1);
                break;

            case 'g': case KEY_HOME:
                if (app->tab == TAB_NOTIFS) {
                    app->notif_sel = 0; app->notif_top = 0; app->notif_on_more = 0;
                } else {
                    app->feed_sel = 0; app->feed_top = 0; app->feed_on_more = 0;
                }
                break;

            case 'G': case KEY_END:
                if (app->tab == TAB_NOTIFS) {
                    if (app->notifs.count) {
                        app->notif_sel = app->notifs.count - 1;
                        app->notif_on_more = 0;
                    }
                } else if (app->feed.count) {
                    app->feed_sel = app->feed.count - 1; app->feed_on_more = 0;
                }
                break;

            case 'r':
                if (app->tab == TAB_FEED) { report(app, net_refresh_feed(app)); net_refresh_badge(app); }
                else if (app->tab == TAB_NOTIFS) report(app, net_refresh_notifs(app));
                else net_refresh_badge(app);
                break;

            case '\r': case '\n': case KEY_ENTER: case ' ':
                if (app->tab == TAB_NOTIFS) { open_notif_target(app); break; }
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

            case 'c':
                if (app->tab == TAB_FEED) compose_post(app);
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
