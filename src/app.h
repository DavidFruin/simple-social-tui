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

typedef struct {
    ss_state_t state;
    tui_cfg_t  cfg;

    tab_t tab;

    post_store_t feed;
    int   feed_sel;          /* selected post index */
    int   feed_top;          /* first post index drawn */
    int   feed_on_more;      /* cursor is parked on the "load more" row */
    time_t feed_fetched;     /* 0 = never */

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
