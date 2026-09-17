#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include "app.h"
#include "ui.h"
#include "ss_api.h"
#include "ss_config.h"
#include "ss_state.h"

/* Restore the session the CLI tools already established. The JWT lives in
 * ~/.simple-social-cli/, shared by all three front ends, so logging in with
 * any of them logs you in here too. */
static int auto_login(app_t *app) {
    config_t cfg;
    config_load(&cfg);

    if (api_init() != 0) {
        fprintf(stderr, "error: failed to initialise HTTP client\n");
        return -1;
    }

    if (ss_state_load_jwt(&app->state) != 0) return 1;   /* no session */
    api_set_jwt(app->state.jwt);

    int id = 0;
    char email[256] = {0}, created[32] = {0};
    if (api_get_my_info(&id, email, sizeof(email), created, sizeof(created)) != 0)
        return 1;   /* token present but rejected */

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
    if (auth < 0) { app_free(&app); return 1; }
    if (auth > 0) {
        fprintf(stderr,
            "Not logged in.\n\n"
            "The login screen is not built yet. For now, log in with either\n"
            "sibling tool -- the session is shared:\n\n"
            "  (cd ../simple-social-cli && ./simple-social-cli login)\n"
            "  (cd ../simple-social-cli-interactive && ./simple-social-cli-interactive)\n");
        app_free(&app);
        api_cleanup();
        return 1;
    }

    if (ui_init() != 0) {
        fprintf(stderr, "error: failed to initialise the terminal\n");
        app_free(&app);
        api_cleanup();
        return 1;
    }

    app_run(&app);

    ui_teardown();
    app_free(&app);
    api_cleanup();
    return 0;
}
