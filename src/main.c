#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include "app.h"
#include "ui.h"
#include "auth.h"
#include "ss_api.h"
#include "ss_config.h"
#include "ss_state.h"

/* Restore this tool's own session. Each front end keeps its tokens in its
 * own directory under ~/.simple-social-cli/ and holds an independent
 * server-side session, so signing in here is separate from the CLI and the
 * wizard, and each shows up as its own device.
 *
 * Returns 0 with a live session, 1 with no usable session, -1 on a failure
 * that means we cannot run at all. */
static int auto_login(app_t *app) {
    config_t cfg;
    config_load(&cfg);

    if (api_init() != 0) {
        fprintf(stderr, "error: failed to initialise HTTP client\n");
        return -1;
    }

    /* Loaded before the first request so an access token that merely aged
     * out gets renewed rather than dropping the user back to a login
     * screen. */
    if (ss_state_load_refresh(&app->state) == 0)
        api_set_refresh_token(app->state.refresh);

    if (ss_state_load_jwt(&app->state) != 0) return 1;   /* no token stored */
    api_set_jwt(app->state.jwt);

    int id = 0;
    char email[256] = {0}, created[32] = {0};
    if (api_get_my_info(&id, email, sizeof(email), created, sizeof(created)) != 0)
        return 1;   /* token present but rejected, or the server is unreachable */

    ss_state_set_user(&app->state, id, email, created);
    api_set_user_id(id);
    ss_state_save_user(&app->state);
    return 0;
}

/* The app struct outlives every request, so the refresh callback can reach
 * it to persist a renewed access token. */
static app_t *g_app = NULL;

static void on_token_refreshed(const char *jwt) {
    if (!g_app) return;
    ss_state_set_jwt(&g_app->state, jwt);
    ss_state_save_jwt(&g_app->state);
}

int main(void) {
    /* Required before ncursesw will render multibyte glyphs. */
    setlocale(LC_ALL, "");

    app_t app;
    app_init(&app);
    g_app = &app;

    /* Its own app name and user agent: separate token storage from the
     * other two front ends, and a recognisable entry in the device list. */
    ss_state_set_app("tui");
    api_set_user_agent("simple-social-tui");
    api_set_token_refreshed_cb(on_token_refreshed);

    int auth = auto_login(&app);
    if (auth < 0) {
        app_free(&app);
        return 1;
    }

    if (ui_init() != 0) {
        fprintf(stderr, "error: failed to initialise the terminal\n");
        app_free(&app);
        api_cleanup();
        return 1;
    }

    /* Log in, use the app, and come back here if they log out again. */
    for (;;) {
        if (!ss_state_is_logged_in(&app.state)) {
            app.in_auth = 1;
            app.auth_sel = 0;
            int ok = auth_screen(&app);
            app.in_auth = 0;
            if (!ok) break;
            app_reset_data(&app);
        }

        app.quit = 0;
        app_run(&app);

        if (app.account_deleted) break;

        if (app.logged_out) {
            app.logged_out = 0;
            continue;
        }
        break;                       /* ordinary quit */
    }

    ui_teardown();

    if (app.account_deleted)
        printf("Account deleted.\n");

    app_free(&app);
    api_cleanup();
    return 0;
}
