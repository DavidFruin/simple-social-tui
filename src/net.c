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

static api_notifications_result_t *npage_buf(void) {
    static api_notifications_result_t *buf = NULL;
    if (!buf) buf = calloc(1, sizeof(api_notifications_result_t));
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

    if (!append) store_clear(&app->feed.store);
    if (store_append(&app->feed.store, res->posts, res->count) != 0) {
        app_set_error(app, "out of memory");
        return -1;
    }

    app->feed.store.has_more = res->has_more;
    app->feed.store.total_count = res->total_count;
    app->feed.fetched = time(NULL);

    if (app->feed.store.count == 0) {
        app_set_status(app, "Nothing in your feed yet.");
    } else if (append) {
        app_set_status(app, "Loaded %d more (%d shown).", res->count, app->feed.store.count);
    } else {
        app_set_status(app, "%d posts.", app->feed.store.count);
    }
    return 0;
}

int net_refresh_feed(app_t *app) {
    int rc = fetch_page(app, 0, 0);
    if (rc == 0) {
        app->feed.sel = 0;
        app->feed.top = 0;
        app->feed.on_more = 0;
    }
    return rc;
}

int net_load_more_feed(app_t *app) {
    if (!app->feed.store.has_more) {
        app_set_status(app, "No more posts.");
        return 0;
    }
    return fetch_page(app, app->feed.store.count, 1);
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
    if (app->detail_src >= 0 && app->detail_src < app->feed.store.count &&
        strcmp(app->feed.store.posts[app->detail_src].id, p->id) == 0) {
        app->feed.store.posts[app->detail_src].is_liked = p->is_liked;
        app->feed.store.posts[app->detail_src].like_count = p->like_count;
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

/* ---------- notifications ---------- */

/* api.php serves getNotifications in fixed pages of this size. */
#define NOTIF_PAGE 25

static int fetch_notifs(app_t *app, int offset, int append) {
    api_notifications_result_t *res = npage_buf();
    if (!res) { app_set_error(app, "out of memory"); return -1; }

    announce(app, append ? "Loading more..." : "Loading notifications...");

    memset(res, 0, sizeof(*res));
    if (api_get_notifications(offset, res) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (!append) nstore_clear(&app->notifs);
    if (nstore_append(&app->notifs, res->notifications, res->count) != 0) {
        app_set_error(app, "out of memory");
        return -1;
    }

    /* No total comes back, so a full page means there may be another. */
    app->notifs.has_more = (res->count == NOTIF_PAGE);
    app->notifs_fetched = time(NULL);
    return 0;
}

int net_refresh_notifs(app_t *app) {
    /* Capture the unseen count before marking them seen, so the new ones
     * can still be pointed out in the list. */
    int was_unseen = app->unseen;

    if (fetch_notifs(app, 0, 0) != 0) return -1;

    app->notif_sel = 0;
    app->notif_top = 0;
    app->notif_on_more = 0;
    app->notif_new = was_unseen < app->notifs.count ? was_unseen : app->notifs.count;

    /* Opening the tab is what counts as having seen them. */
    if (api_mark_notifications_seen() == 0) {
        app->unseen = 0;
        app->unseen_fetched = time(NULL);
    }

    if (app->notifs.count == 0) app_set_status(app, "No notifications.");
    else if (app->notif_new > 0)
        app_set_status(app, "%d new of %d.", app->notif_new, app->notifs.count);
    else
        app_set_status(app, "%d notifications.", app->notifs.count);
    return 0;
}

int net_load_more_notifs(app_t *app) {
    if (!app->notifs.has_more) {
        app_set_status(app, "No more notifications.");
        return 0;
    }
    int before = app->notifs.count;
    if (fetch_notifs(app, app->notifs.count, 1) != 0) return -1;
    app_set_status(app, "Loaded %d more (%d shown).",
                   app->notifs.count - before, app->notifs.count);
    return 0;
}

/* ---------- users and profiles ---------- */

/* api_users_result_t is a fixed api_user_t[256] -- small enough to keep
 * around, but still heap so it never lands on the stack. */
static api_users_result_t *users_buf(api_users_result_t **slot) {
    if (!*slot) *slot = calloc(1, sizeof(api_users_result_t));
    return *slot;
}

int net_refresh_users(app_t *app) {
    api_users_result_t *all = users_buf(&app->users);
    api_users_result_t *mine = users_buf(&app->my_follows);
    if (!all || !mine) { app_set_error(app, "out of memory"); return -1; }

    announce(app, "Loading users...");

    memset(all, 0, sizeof(*all));
    if (api_get_users(all) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    /* One extra call, so each row can show whether I follow them without
     * an isFollowing call per user. */
    memset(mine, 0, sizeof(*mine));
    api_get_my_follows(app->state.user.user_id, mine);

    app->users_fetched = time(NULL);
    if (app->users_sel >= all->count) app->users_sel = all->count ? all->count - 1 : 0;

    app_set_status(app, "%d users.", all->count);
    return 0;
}

static int fetch_profile_posts(app_t *app, int offset, int append) {
    api_posts_result_t *res = page_buf();
    if (!res) { app_set_error(app, "out of memory"); return -1; }

    announce(app, append ? "Loading more..." : "Loading posts...");

    memset(res, 0, sizeof(*res));

    int rc;
    if (app->profile_id == app->state.user.user_id)
        rc = api_get_my_posts(offset, app->cfg.page_size, res);
    else
        rc = api_get_user_posts(app->profile_id, offset, app->cfg.page_size, res);

    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    if (!append) store_clear(&app->profile_posts.store);
    if (store_append(&app->profile_posts.store, res->posts, res->count) != 0) {
        app_set_error(app, "out of memory");
        return -1;
    }

    app->profile_posts.store.has_more = res->has_more;
    app->profile_posts.store.total_count = res->total_count;
    app->profile_posts.fetched = time(NULL);
    return 0;
}

int net_open_profile(app_t *app, int user_id) {
    announce(app, "Loading profile...");

    char email[256] = {0}, created[32] = {0};

    if (user_id == app->state.user.user_id) {
        snprintf(email, sizeof(email), "%s", app->state.user.email);
        snprintf(created, sizeof(created), "%s", app->state.user.created_at);
    } else if (api_get_user_info(user_id, email, sizeof(email),
                                 created, sizeof(created)) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    app->profile_id = user_id;
    snprintf(app->profile_email, sizeof(app->profile_email), "%s", email);
    snprintf(app->profile_created, sizeof(app->profile_created), "%s", created);

    app->profile_posts.sel = 0;
    app->profile_posts.top = 0;
    app->profile_posts.on_more = 0;

    /* Counts for the header. Failures here are not worth abandoning the
     * profile over, so they just leave a zero. */
    api_users_result_t follows, followers;
    memset(&follows, 0, sizeof(follows));
    memset(&followers, 0, sizeof(followers));

    app->profile_follows = 0;
    app->profile_followers = 0;
    if (api_get_my_follows(user_id, &follows) == 0) app->profile_follows = follows.count;
    if (api_get_my_followers(user_id, &followers) == 0) app->profile_followers = followers.count;

    app->profile_is_following = 0;
    if (user_id != app->state.user.user_id) {
        int f = 0;
        if (api_is_following(user_id, &f) == 0) app->profile_is_following = f;
    }

    if (fetch_profile_posts(app, 0, 0) != 0) return -1;

    app_set_status(app, "%d posts.", app->profile_posts.store.total_count);
    return 0;
}

int net_load_more_profile_posts(app_t *app) {
    if (!app->profile_posts.store.has_more) {
        app_set_status(app, "No more posts.");
        return 0;
    }
    int before = app->profile_posts.store.count;
    if (fetch_profile_posts(app, app->profile_posts.store.count, 1) != 0) return -1;
    app_set_status(app, "Loaded %d more (%d of %d).",
                   app->profile_posts.store.count - before,
                   app->profile_posts.store.count,
                   app->profile_posts.store.total_count);
    return 0;
}

int net_toggle_follow(app_t *app) {
    if (app->profile_id == app->state.user.user_id) {
        app_set_status(app, "You cannot follow yourself.");
        return 0;
    }

    int following = !app->profile_is_following;

    announce(app, following ? "Following..." : "Unfollowing...");

    int rc = following ? api_follow_user(app->profile_id)
                       : api_unfollow_user(app->profile_id);
    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    app->profile_is_following = following;
    app->profile_followers += following ? 1 : -1;
    if (app->profile_followers < 0) app->profile_followers = 0;

    /* The users tab shows follow state, and the feed is followed posts,
     * so both are now out of date. */
    app->users_fetched = 0;
    app->feed.fetched = 0;

    app_set_status(app, following ? "Following %s." : "Unfollowed %s.",
                   app->profile_email);
    return 0;
}

int net_open_user_list(app_t *app, int owner_id, list_kind_t kind) {
    api_users_result_t *lst = users_buf(&app->list_users);
    if (!lst) { app_set_error(app, "out of memory"); return -1; }

    announce(app, kind == LIST_FOLLOWS ? "Loading follows..." : "Loading followers...");

    memset(lst, 0, sizeof(*lst));
    int rc = (kind == LIST_FOLLOWS) ? api_get_my_follows(owner_id, lst)
                                    : api_get_my_followers(owner_id, lst);
    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    app->list_kind = kind;
    app->list_owner = owner_id;
    app->list_sel = 0;
    app->list_top = 0;

    /* LIST_FOLLOWS is who they follow; LIST_FOLLOWERS is who follows them.
     * Spelled out, because "follows" alone reads both ways. */
    int self = (owner_id == app->state.user.user_id);
    if (self)
        snprintf(app->list_title, sizeof(app->list_title), "%s",
                 kind == LIST_FOLLOWS ? "People you follow" : "People following you");
    else
        snprintf(app->list_title, sizeof(app->list_title),
                 kind == LIST_FOLLOWS ? "People %.60s follows" : "People following %.60s",
                 app->profile_email);

    app_set_status(app, "%d %s.", lst->count,
                   kind == LIST_FOLLOWS ? "follows" : "followers");
    return 0;
}
