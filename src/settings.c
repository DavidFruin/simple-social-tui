#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include "settings.h"
#include "ui.h"
#include "ss_api.h"
#include "ss_config.h"
#include "ss_state.h"

static const char *ACTION_LABEL[SETTINGS_ACTIONS] = {
    "Change password",
    "Log out",
    "Delete account"
};

void settings_move(app_t *app, int delta) {
    app->settings_sel += delta;
    if (app->settings_sel < 0) app->settings_sel = 0;
    if (app->settings_sel >= SETTINGS_ACTIONS) app->settings_sel = SETTINGS_ACTIONS - 1;
}

void settings_draw(app_t *app, int body_top) {
    int row = body_top;
    int bottom = LINES - 2;

    attron(A_BOLD);
    mvaddstr(row++, 1, "Session");
    attroff(A_BOLD);

    attron(A_DIM);
    mvprintw(row++, 3, "%s  (id %d)", app->state.user.email, app->state.user.user_id);
    mvprintw(row++, 3, "token in ~/.simple-social-cli/, shared with the CLI tools");
    attroff(A_DIM);
    row++;

    attron(A_BOLD);
    mvaddstr(row++, 1, "Config");
    attroff(A_BOLD);

    attron(A_DIM);
    char shown[512];
    ui_utf8_take(shown, sizeof(shown), app->cfg.path, COLS - 6, 1);
    mvaddstr(row++, 3, shown);
    mvprintw(row++, 3, "api        %s", config_get_base_url());
    mvprintw(row++, 3, "composer   %s",
             app->cfg.compose_mode == CFG_COMPOSE_EXTERNAL ? "external ($EDITOR)" : "inline");
    mvprintw(row++, 3, "refresh    feed %ds, badge %ds",
             app->cfg.feed_refresh_secs, app->cfg.badge_refresh_secs);
    mvprintw(row++, 3, "page size  %d", app->cfg.page_size);
    attroff(A_DIM);
    row++;

    attron(A_BOLD);
    mvaddstr(row++, 1, "Account");
    attroff(A_BOLD);

    for (int i = 0; i < SETTINGS_ACTIONS && row <= bottom; i++, row++) {
        int selected = (i == app->settings_sel);
        int danger = (i == 2);

        if (selected) { attron(A_REVERSE); mvhline(row, 0, ' ', COLS); }
        else if (danger) attron(COLOR_PAIR(CP_ERROR));

        mvprintw(row, 3, "%s", ACTION_LABEL[i]);

        if (selected) attroff(A_REVERSE);
        else if (danger) attroff(COLOR_PAIR(CP_ERROR));
    }
}

/* ---------- actions ---------- */

/* Same OTP dance as the web app's reset: code to the address on file,
 * verify it, then set the new password twice. */
static void change_password(app_t *app) {
    const char *email = app->state.user.email;

    if (!ui_confirm(app, "Email a one-time code to your address?")) {
        app_set_status(app, "Cancelled.");
        return;
    }

    app_set_status(app, "Sending code...");
    ui_draw(app);
    refresh();

    if (api_send_otp(email) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        ui_modal_error(app, app->status);
        return;
    }

    char otp[64];
    if (!ui_prompt(app, "Change password", "Code emailed to you:", 0, otp, sizeof(otp))) {
        app_set_status(app, "Cancelled.");
        return;
    }

    app_set_status(app, "Checking code...");
    ui_draw(app);
    refresh();

    if (api_verify_otp(email, otp) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        ui_modal_error(app, app->status);
        return;
    }

    char pw1[256], pw2[256];
    if (!ui_prompt(app, "Change password", "New password:", 1, pw1, sizeof(pw1))) {
        app_set_status(app, "Cancelled.");
        return;
    }
    if (!ui_prompt(app, "Change password", "Again, to confirm:", 1, pw2, sizeof(pw2))) {
        memset(pw1, 0, sizeof(pw1));
        app_set_status(app, "Cancelled.");
        return;
    }

    if (strcmp(pw1, pw2) != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "those did not match -- nothing changed");
        ui_modal_error(app, app->status);
        return;
    }

    app_set_status(app, "Setting password...");
    ui_draw(app);
    refresh();

    int rc = api_reset_password(email, pw1, pw2);
    memset(pw1, 0, sizeof(pw1));
    memset(pw2, 0, sizeof(pw2));

    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        ui_modal_error(app, app->status);
        return;
    }

    app_set_status(app, "Password changed.");
}

/* The token file is shared, so logging out here logs out the CLI tools
 * too -- which is the point of sharing it. */
static void clear_session_files(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.simple-social-cli/jwt.txt", home);
    unlink(path);
    snprintf(path, sizeof(path), "%s/.simple-social-cli/user.json", home);
    unlink(path);
}

static void do_logout(app_t *app) {
    if (!ui_confirm(app, "Log out? This also logs out the CLI tools, "
                         "since the token is shared.")) {
        app_set_status(app, "Still logged in.");
        return;
    }

    app_set_status(app, "Logging out...");
    ui_draw(app);
    refresh();

    api_logout();                 /* best effort; the local token is what matters */
    clear_session_files();
    ss_state_clear(&app->state);
    api_set_jwt("");

    app->logged_out = 1;          /* main decides whether to show the login screen */
}

static void delete_account(app_t *app) {
    char pw1[256], pw2[256];

    if (!ui_prompt(app, "Delete account", "Your password:", 1, pw1, sizeof(pw1))) {
        app_set_status(app, "Cancelled.");
        return;
    }
    if (!ui_prompt(app, "Delete account", "Again, to confirm:", 1, pw2, sizeof(pw2))) {
        memset(pw1, 0, sizeof(pw1));
        app_set_status(app, "Cancelled.");
        return;
    }

    if (strcmp(pw1, pw2) != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "those did not match -- nothing deleted");
        ui_modal_error(app, app->status);
        return;
    }

    /* Third gate, after the two matching entries. */
    if (!ui_confirm(app, "Delete your account and everything in it? "
                         "This cannot be undone.")) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_status(app, "Not deleted.");
        return;
    }

    app_set_status(app, "Deleting account...");
    ui_draw(app);
    refresh();

    int rc = api_delete_account(pw1);
    memset(pw1, 0, sizeof(pw1));
    memset(pw2, 0, sizeof(pw2));

    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        ui_modal_error(app, app->status);
        return;
    }

    clear_session_files();
    ss_state_clear(&app->state);

    /* Nothing left to show. */
    app->quit = 1;
    app->account_deleted = 1;
}

void settings_activate(app_t *app) {
    switch (app->settings_sel) {
        case 0: change_password(app); break;
        case 1: do_logout(app);       break;
        case 2: delete_account(app);  break;
        default: break;
    }
}
