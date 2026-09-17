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

/* Same for comments (~1.3 MB). */
static api_comments_result_t *cpage_buf(void) {
    static api_comments_result_t *buf = NULL;
    if (!buf) buf = calloc(1, sizeof(api_comments_result_t));
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

/* ---------- post detail ---------- */

static int fetch_comments(app_t *app, int offset, int append) {
    api_comments_result_t *res = cpage_buf();
    if (!res) { app_set_error(app, "out of memory"); return -1; }

    announce(app, append ? "Loading more comments..." : "Loading comments...");

    memset(res, 0, sizeof(*res));
    if (api_get_post_comments(app->detail.id, offset, app->cfg.page_size, res) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (!append) cstore_clear(&app->comments);
    if (cstore_append(&app->comments, res->comments, res->count) != 0) {
        app_set_error(app, "out of memory");
        return -1;
    }

    app->comments.has_more = res->has_more;
    app->comments.total_count = res->total_count;
    return 0;
}

int net_open_post(app_t *app, const char *post_id, int feed_src) {
    api_post_t post;

    announce(app, "Loading post...");

    memset(&post, 0, sizeof(post));
    if (api_get_post_by_id(post_id, &post) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    app->detail = post;
    app->detail_src = feed_src;
    app->detail_scroll = 0;
    app->comment_sel = 0;
    app->detail_on_more = 0;

    if (fetch_comments(app, 0, 0) != 0) return -1;

    app->view = VIEW_POST;
    if (app->comments.total_count > 0)
        app_set_status(app, "%d comments.", app->comments.total_count);
    else
        app_set_status(app, "No comments yet.");
    return 0;
}

int net_load_more_comments(app_t *app) {
    if (!app->comments.has_more) {
        app_set_status(app, "No more comments.");
        return 0;
    }
    int before = app->comments.count;
    if (fetch_comments(app, app->comments.count, 1) != 0) return -1;
    app_set_status(app, "Loaded %d more (%d of %d).",
                   app->comments.count - before,
                   app->comments.count, app->comments.total_count);
    return 0;
}

/* Toggled locally rather than re-fetching the post: it is one field, and
 * the next refresh reconciles with the server anyway. */
int net_toggle_like(app_t *app) {
    api_post_t *p = &app->detail;
    int liking = !p->is_liked;

    announce(app, liking ? "Liking..." : "Unliking...");

    int rc = liking ? api_like_post(p->id) : api_unlike_post(p->id);
    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    p->is_liked = liking;
    p->like_count += liking ? 1 : -1;
    if (p->like_count < 0) p->like_count = 0;

    /* Keep the feed row behind this view in step. */
    if (app->detail_src >= 0 && app->detail_src < app->feed.count &&
        strcmp(app->feed.posts[app->detail_src].id, p->id) == 0) {
        app->feed.posts[app->detail_src].is_liked = p->is_liked;
        app->feed.posts[app->detail_src].like_count = p->like_count;
    }

    app_set_status(app, liking ? "Liked." : "Unliked.");
    return 0;
}

/* ---------- writes ---------- */

int net_create_post(app_t *app, const char *text, const char *media_path) {
    char post_id[64] = {0};
    char media_url[512] = {0};
    int  media_id = 0;

    if (media_path && media_path[0]) {
        const char *base = strrchr(media_path, '/');
        base = base ? base + 1 : media_path;

        /* Uploads get a 120s timeout in the library, so this is the
         * longest the UI ever freezes. Say which file, so a long pause
         * is explicable. */
        char msg[256];
        snprintf(msg, sizeof(msg), "Uploading %s...", base);
        announce(app, msg);

        if (api_upload_media_with_id(media_path, media_url, sizeof(media_url),
                                     &media_id) != 0) {
            app_set_error(app, "upload failed: %s", api_get_last_error());
            return -1;
        }
    }

    announce(app, "Posting...");

    if (api_create_post(text, media_url[0] ? media_url : NULL,
                        post_id, sizeof(post_id)) != 0) {
        app_set_error(app, "%s", api_get_last_error());

        /* The upload succeeded but the post did not, so the media would
         * be orphaned on the server. Roll it back, the way the CLI does. */
        if (media_id > 0) api_delete_media(media_id);
        return -1;
    }

    /* Show it straight away rather than waiting for the idle timer. */
    if (net_refresh_feed(app) == 0)
        app_set_status(app, media_url[0] ? "Posted with media." : "Posted.");
    return 0;
}

int net_create_comment(app_t *app, const char *text) {
    int comment_id = 0;

    announce(app, "Commenting...");

    if (api_create_comment(app->detail.id, text, &comment_id) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (fetch_comments(app, 0, 0) != 0) return -1;
    app->comment_sel = 0;
    app->detail_scroll = 0;
    app->detail_on_more = 0;
    app_set_status(app, "Comment added.");
    return 0;
}

int net_delete_post(app_t *app, const char *post_id) {
    announce(app, "Deleting post...");

    if (api_delete_post(post_id) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (net_refresh_feed(app) == 0) app_set_status(app, "Post deleted.");
    return 0;
}

int net_delete_comment(app_t *app, int comment_id) {
    announce(app, "Deleting comment...");

    if (api_delete_comment(comment_id) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (fetch_comments(app, 0, 0) != 0) return -1;
    if (app->comment_sel >= app->comments.count)
        app->comment_sel = app->comments.count ? app->comments.count - 1 : 0;
    app->detail_on_more = 0;
    app_set_status(app, "Comment deleted.");
    return 0;
}

int net_reload_comments(app_t *app) {
    return fetch_comments(app, 0, 0);
}
