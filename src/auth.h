#ifndef TUI_AUTH_H
#define TUI_AUTH_H

#include "app.h"

/* The screen shown when there is no valid session: log in, register, or
 * reset a forgotten password. Returns 1 once a session exists, 0 if the
 * user chose to quit. */
int auth_screen(app_t *app);

/* Painted by ui_draw while app->in_auth is set, so modal prompts sit over
 * the menu rather than over the tab chrome. */
void auth_draw(app_t *app);

#endif
