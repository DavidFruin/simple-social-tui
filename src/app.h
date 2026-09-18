#ifndef TUI_APP_H
#define TUI_APP_H

#include <time.h>
#include "ss_state.h"
#include "cfg.h"
#include "store.h"

typedef enum {
    TAB_FEED = 0,
    TAB_NOTIFS,
    TAB_USERS,
    TAB_ME,
    TAB_SETTINGS,
    TAB_COUNT
} tab_t;

typedef enum {
    VIEW_TABS = 0,
    VIEW_POST,
    VIEW_PROFILE,
    VIEW_USERLIST
} view_t;

/* Which set of users a pushed list is showing. */
typedef enum {
    LIST_FOLLOWS = 0,
    LIST_FOLLOWERS
} list_kind_t;

/* Screens stack. Each frame carries enough to rebuild the screen it
 * returns to, since the state for one profile is overwritten when
 * another is opened from a follows list. */
#define VIEW_STACK_MAX 6

typedef struct {
    view_t      view;
    int         profile_id;   /* VIEW_PROFILE */
    list_kind_t list_kind;    /* VIEW_USERLIST */
    int         list_owner;
} frame_t;

/* A scrollable list of posts plus where the cursor is in it. The feed and
 * a profile's posts are the same thing over different data, so they share
 * this and the drawing that goes with it. */
typedef struct {
    post_store_t store;
    int sel;
    int top;
    int on_more;       /* cursor parked on the load-more row */
    time_t fetched;    /* 0 = never */
} post_list_t;

typedef struct {
    ss_state_t state;
    tui_cfg_t  cfg;

    view_t view;
    tab_t tab;

    post_list_t feed;

    frame_t stack[VIEW_STACK_MAX];
    int     depth;

    /* Users tab: everyone, plus who I follow so the list can say so
     * without a call per row. */
    api_users_result_t *users;
    api_users_result_t *my_follows;
    int    users_sel;
    int    users_top;
    time_t users_fetched;

    /* A pushed follows/followers list. */
    api_users_result_t *list_users;
    int         list_sel;
    int         list_top;
    list_kind_t list_kind;
    int         list_owner;
    char        list_title[96];

    /* Profile being viewed. */
    int    profile_id;
    char   profile_email[256];
    char   profile_created[32];
    int    profile_is_following;
    int    profile_follows;
    int    profile_followers;
    post_list_t profile_posts;

    /* Post detail. The post is a copy; feed_src is the feed index it came
     * from, so a like applied here stays in step with the list behind it. */
    api_post_t     detail;
    int            detail_src;
    comment_store_t comments;
    int            comment_sel;
    int            detail_scroll;
    int            detail_on_more;

    /* Notifications */
    notif_store_t notifs;
    int    notif_sel;
    int    notif_top;
    int    notif_on_more;
    int    notif_new;        /* how many were unseen when the list loaded */
    time_t notifs_fetched;

    int    unseen;           /* notification badge count */
    time_t unseen_fetched;

    char status[256];
    int  status_is_error;

    /* Set while a composer or prompt owns the keyboard, so the idle
     * refresh timer never fires underneath someone's typing. */
    int  input_active;

    int settings_sel;

    int in_auth;           /* the login screen owns the display */
    int auth_sel;

    int logged_out;        /* set by logout: fall back to the login screen */
    int account_deleted;   /* set by delete: say so on the way out */

    int quit;
} app_t;

int  app_init(app_t *app);
void app_free(app_t *app);
void app_run(app_t *app);

/* Drops everything fetched for the previous session. */
void app_reset_data(app_t *app);

void app_set_status(app_t *app, const char *fmt, ...);
void app_set_error(app_t *app, const char *fmt, ...);

const char *app_tab_name(tab_t t);

/* Screen stack */
void app_push(app_t *app, view_t view);
void app_pop(app_t *app);

/* True when `id` is someone I follow, from the cached list. */
int  app_follows(app_t *app, int id);

#endif
