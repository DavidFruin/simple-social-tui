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

/* Screens stack one level deep: a tab, or a post opened from it. */
typedef enum {
    VIEW_TABS = 0,
    VIEW_POST
} view_t;

typedef struct {
    ss_state_t state;
    tui_cfg_t  cfg;

    view_t view;
    tab_t tab;

    post_store_t feed;
    int   feed_sel;          /* selected post index */
    int   feed_top;          /* first post index drawn */
    int   feed_on_more;      /* cursor is parked on the "load more" row */
    time_t feed_fetched;     /* 0 = never */

    /* Post detail. The post is a copy; feed_src is the feed index it came
     * from, so a like applied here stays in step with the list behind it. */
    api_post_t     detail;
    int            detail_src;
    comment_store_t comments;
    int            comment_sel;
    int            detail_scroll;
    int            detail_on_more;

    int    unseen;           /* notification badge count */
    time_t unseen_fetched;

    char status[256];
    int  status_is_error;

    /* Set while a composer or prompt owns the keyboard, so the idle
     * refresh timer never fires underneath someone's typing. */
    int  input_active;

    int quit;
} app_t;

int  app_init(app_t *app);
void app_free(app_t *app);
void app_run(app_t *app);

void app_set_status(app_t *app, const char *fmt, ...);
void app_set_error(app_t *app, const char *fmt, ...);

const char *app_tab_name(tab_t t);

#endif
