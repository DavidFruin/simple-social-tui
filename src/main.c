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

/* Restore the session the CLI tools already established. The JWT lives in
 * ~/.simple-social-cli/, shared by all three front ends, so logging in with
 * any of them logs you in here too.
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

int main(void) {
    /* Required before ncursesw will render multibyte glyphs. */
    setlocale(LC_ALL, "");

    app_t app;
    app_init(&app);

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
