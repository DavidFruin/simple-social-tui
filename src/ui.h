#ifndef TUI_UI_H
#define TUI_UI_H

#include "app.h"

/* Color pairs. Only the terminal's own 16 colors are used, and
 * use_default_colors() keeps the background transparent, so the TUI
 * inherits whatever scheme the terminal already has. */
#define CP_TAB_ACTIVE 1
#define CP_AUTHOR     2
#define CP_LIKED      3
#define CP_ERROR      4
#define CP_BADGE      5
#define CP_HINT       6

int  ui_init(void);
void ui_teardown(void);

void ui_draw(app_t *app);
void ui_modal_error(app_t *app, const char *msg);
void ui_help(app_t *app);

/* Modal yes/no. Returns 1 for yes. Defaults to no: anything that is not
 * y or Y is a decline, so a stray keypress never confirms a delete. */
int  ui_confirm(app_t *app, const char *question);

/* Rows available to the body between the tab bar and the status line. */
int  ui_body_height(void);

/* Copies at most max_cols display columns of UTF-8 from src, never
 * splitting a multibyte character, appending an ellipsis if it cut. */
void ui_utf8_take(char *dst, size_t dstsz, const char *src, int max_cols, int ellipsis);

/* Display width of a UTF-8 string in terminal columns. */
int  ui_utf8_width(const char *s);

#endif
