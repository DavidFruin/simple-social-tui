#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include "net.h"
#include "ui.h"
#include "ss_api.h"

/* api_posts_result_t is ~1.4 MB. One heap buffer, reused, never a local. */
static api_posts_result_t *page_buf(void) {
    static api_posts_result_t *buf = NULL;
    if (!buf) buf = calloc(1, sizeof(api_posts_result_t));
    return buf;
}

/* Paint the pending message and force it out before we block. */
static void announce(app_t *app, const char *msg) {
    app_set_status(app, "%s", msg);
    ui_draw(app);
    refresh();
}

static int fetch_page(app_t *app, int offset, int append) {
    api_posts_result_t *res = page_buf();
    if (!res) {
        app_set_error(app, "out of memory");
        return -1;
    }

    announce(app, append ? "Loading more..." : "Loading feed...");

    memset(res, 0, sizeof(*res));
    if (api_fetch_followed_posts(offset, app->cfg.page_size, res) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (!append) store_clear(&app->feed);
    if (store_append(&app->feed, res->posts, res->count) != 0) {
        app_set_error(app, "out of memory");
        return -1;
    }

    app->feed.has_more = res->has_more;
    app->feed.total_count = res->total_count;
    app->feed_fetched = time(NULL);

    if (app->feed.count == 0) {
        app_set_status(app, "Nothing in your feed yet.");
    } else if (append) {
        app_set_status(app, "Loaded %d more (%d shown).", res->count, app->feed.count);
    } else {
        app_set_status(app, "%d posts.", app->feed.count);
    }
    return 0;
}

int net_refresh_feed(app_t *app) {
    int rc = fetch_page(app, 0, 0);
    if (rc == 0) {
        app->feed_sel = 0;
        app->feed_top = 0;
        app->feed_on_more = 0;
    }
    return rc;
}

int net_load_more_feed(app_t *app) {
    if (!app->feed.has_more) {
        app_set_status(app, "No more posts.");
        return 0;
    }
    return fetch_page(app, app->feed.count, 1);
}

int net_refresh_badge(app_t *app) {
    int count = 0;
    /* No announce(): this runs on a timer, and a flashing status line on
     * every poll would be noise. It still blocks, just briefly. */
    if (api_get_unseen_notification_count(&count) != 0) return -1;
    app->unseen = count;
    app->unseen_fetched = time(NULL);
    return 0;
}
