#ifndef TUI_SETTINGS_H
#define TUI_SETTINGS_H

#include "app.h"

/* Settings tab: what the session and config currently are, plus the three
 * account operations. Destructive ones live here rather than at top level
 * so they are reachable but not somewhere you land by accident. */

#define SETTINGS_ACTIONS 3

void settings_draw(app_t *app, int body_top);
void settings_move(app_t *app, int delta);
void settings_activate(app_t *app);

#endif
