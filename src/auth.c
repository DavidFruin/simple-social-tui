#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include "auth.h"
#include "ui.h"
#include "ss_api.h"
#include "ss_state.h"

#define AUTH_CHOICES 4

static const char *CHOICE[AUTH_CHOICES] = {
    "Log in",
    "Register",
    "Forgot password",
    "Quit"
};

static void busy(app_t *app, const char *msg) {
    app_set_status(app, "%s", msg);
    ui_draw(app);
    refresh();
}

void auth_draw(app_t *app) {
    int sel = app->auth_sel;
    erase();

    int top = (LINES - 12) / 2;
    if (top < 1) top = 1;

    const char *title = "Simple Social";
    int tw = ui_utf8_width(title);
    int left = (COLS - tw) / 2;
    if (left < 0) left = 0;

    attron(A_BOLD);
    mvaddstr(top, left, title);
    attroff(A_BOLD);

    attron(A_DIM);
    const char *sub = "no session -- this tool signs in separately from the CLI tools";
    int sw = ui_utf8_width(sub);
    int sl = (COLS - sw) / 2;
    if (sl < 0) sl = 0;
    mvaddstr(top + 1, sl, sub);
    attroff(A_DIM);

    for (int i = 0; i < AUTH_CHOICES; i++) {
        int row = top + 3 + i;
        if (row >= LINES - 1) break;

        int w = ui_utf8_width(CHOICE[i]);
        int x = (COLS - w) / 2;
        if (x < 2) x = 2;

        if (i == sel) {
            attron(A_REVERSE);
            mvhline(row, x - 2, ' ', w + 4);
        }
        mvaddstr(row, x, CHOICE[i]);
        if (i == sel) attroff(A_REVERSE);
    }

    if (app->status[0]) {
        char buf[256];
        ui_utf8_take(buf, sizeof(buf), app->status, COLS - 2, 1);
        if (app->status_is_error) attron(COLOR_PAIR(CP_ERROR) | A_BOLD);
        mvaddstr(LINES - 1, 0, buf);
        if (app->status_is_error) attroff(COLOR_PAIR(CP_ERROR) | A_BOLD);
    } else {
        attron(A_DIM | COLOR_PAIR(CP_HINT));
        mvaddstr(LINES - 1, 0, "j/k:move   enter:choose   q:quit");
        attroff(A_DIM | COLOR_PAIR(CP_HINT));
    }

    wnoutrefresh(stdscr);
    doupdate();
}

/* Adopts a fresh session: store both tokens and learn who we are. Every
 * way of arriving at a new login - signing in, registering, resetting a
 * password - comes through here, so the refresh token is saved once rather
 * than at each call site. */
static int adopt_session(app_t *app, const char *jwt) {
    ss_state_set_jwt(&app->state, jwt);
    api_set_jwt(jwt);
    ss_state_save_jwt(&app->state);

    /* Without this the login lasts only as long as the access token. */
    ss_state_set_refresh(&app->state, api_get_refresh_token());
    ss_state_save_refresh(&app->state);

    int id = 0;
    char email[256] = {0}, created[32] = {0};
    if (api_get_my_info(&id, email, sizeof(email), created, sizeof(created)) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return -1;
    }

    ss_state_set_user(&app->state, id, email, created);
    api_set_user_id(id);
    ss_state_save_user(&app->state);
    return 0;
}

static int do_login(app_t *app) {
    char email[256], pw[256];

    if (!ui_prompt(app, "Log in", "Email:", 0, email, sizeof(email))) return 0;
    if (!ui_prompt(app, "Log in", "Password:", 1, pw, sizeof(pw))) return 0;

    busy(app, "Logging in...");

    char jwt[STATE_MAX_JWT] = {0};
    int user_id = 0;
    int rc = api_login(email, pw, jwt, sizeof(jwt), &user_id);
    memset(pw, 0, sizeof(pw));

    if (rc != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    if (adopt_session(app, jwt) != 0) return 0;

    app_set_status(app, "Logged in as %s.", app->state.user.email);
    return 1;
}

/* register = send a code, verify it, then set the password. */
static int do_register(app_t *app) {
    char email[256], otp[64], pw1[256], pw2[256];

    if (!ui_prompt(app, "Register", "Email:", 0, email, sizeof(email))) return 0;

    busy(app, "Sending code...");
    if (api_register_send_otp(email) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    if (!ui_prompt(app, "Register", "Code emailed to you:", 0, otp, sizeof(otp))) return 0;

    busy(app, "Checking code...");
    if (api_register_verify_otp(email, otp) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    if (!ui_prompt(app, "Register", "Choose a password:", 1, pw1, sizeof(pw1))) return 0;
    if (!ui_prompt(app, "Register", "Again, to confirm:", 1, pw2, sizeof(pw2))) {
        memset(pw1, 0, sizeof(pw1));
        return 0;
    }

    if (strcmp(pw1, pw2) != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "those did not match -- start again");
        return 0;
    }

    busy(app, "Creating account...");
    int rc = api_register_finish(email, pw1, pw2);
    if (rc != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    /* Registered, so log straight in with what they just chose. */
    busy(app, "Logging in...");
    char jwt[STATE_MAX_JWT] = {0};
    int user_id = 0;
    rc = api_login(email, pw1, jwt, sizeof(jwt), &user_id);
    memset(pw1, 0, sizeof(pw1));
    memset(pw2, 0, sizeof(pw2));

    if (rc != 0) {
        app_set_error(app, "account created, but logging in failed: %s",
                      api_get_last_error());
        return 0;
    }

    if (adopt_session(app, jwt) != 0) return 0;

    app_set_status(app, "Welcome, %s.", app->state.user.email);
    return 1;
}

static int do_reset(app_t *app) {
    char email[256], otp[64], pw1[256], pw2[256];

    if (!ui_prompt(app, "Forgot password", "Email:", 0, email, sizeof(email))) return 0;

    busy(app, "Sending code...");
    if (api_send_otp(email) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    if (!ui_prompt(app, "Forgot password", "Code emailed to you:", 0, otp, sizeof(otp)))
        return 0;

    busy(app, "Checking code...");
    if (api_verify_otp(email, otp) != 0) {
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    if (!ui_prompt(app, "Forgot password", "New password:", 1, pw1, sizeof(pw1))) return 0;
    if (!ui_prompt(app, "Forgot password", "Again, to confirm:", 1, pw2, sizeof(pw2))) {
        memset(pw1, 0, sizeof(pw1));
        return 0;
    }

    if (strcmp(pw1, pw2) != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "those did not match -- start again");
        return 0;
    }

    busy(app, "Setting password...");
    int rc = api_reset_password(email, pw1, pw2);
    if (rc != 0) {
        memset(pw1, 0, sizeof(pw1));
        memset(pw2, 0, sizeof(pw2));
        app_set_error(app, "%s", api_get_last_error());
        return 0;
    }

    busy(app, "Logging in...");
    char jwt[STATE_MAX_JWT] = {0};
    int user_id = 0;
    rc = api_login(email, pw1, jwt, sizeof(jwt), &user_id);
    memset(pw1, 0, sizeof(pw1));
    memset(pw2, 0, sizeof(pw2));

    if (rc != 0) {
        app_set_error(app, "password changed, but logging in failed: %s",
                      api_get_last_error());
        return 0;
    }

    if (adopt_session(app, jwt) != 0) return 0;

    app_set_status(app, "Logged in as %s.", app->state.user.email);
    return 1;
}

int auth_screen(app_t *app) {
    int sel = 0;

    /* ui_prompt calls ui_draw, which draws the tab chrome, so the menu is
     * repainted after every prompt rather than relying on ui_draw. */
    for (;;) {
        app->auth_sel = sel;
        auth_draw(app);

        int ch = getch();
        if (ch == ERR) continue;

        if (app->status[0] && !app->status_is_error) app->status[0] = '\0';

        switch (ch) {
            case 'q':
                return 0;

            case 'j': case KEY_DOWN: if (sel < AUTH_CHOICES - 1) sel++; break;
            case 'k': case KEY_UP:   if (sel > 0) sel--; break;

            case '\r': case '\n': case KEY_ENTER: {
                int done = 0;
                switch (sel) {
                    case 0: done = do_login(app);    break;
                    case 1: done = do_register(app); break;
                    case 2: done = do_reset(app);    break;
                    case 3: return 0;
                }
                if (done) return 1;
                break;
            }

            case KEY_RESIZE:
            default:
                break;
        }
    }
}
