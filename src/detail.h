#ifndef TUI_DETAIL_H
#define TUI_DETAIL_H

#include <stdarg.h>
#include "app.h"

void detail_draw(app_t *app, int body_top, int body_h);
void detail_scroll_by(app_t *app, int delta);
void detail_move_selection(app_t *app, int delta);
int  detail_open_media(app_t *app);

#endif
