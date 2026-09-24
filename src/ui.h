#ifndef TUI_UI_H
#define TUI_UI_H

#include "app.h"

/* One fixed palette, forced to a black background (assume_default_colors()
 * in ui_init(), not use_default_colors()) so the app looks the same
 * everywhere instead of inheriting whatever scheme the terminal has. Each
 * of the 8 base ANSI colors has one job, mirroring the role each CSS
 * variable plays on the web app (--color-primary, --color-success, ...):
 *   white   - body text (the unpaired default, pair 0)
 *   cyan    - author / usernames
 *   green   - liked / success
 *   red     - error / destructive
 *   yellow  - badges / unread markers
 *   blue    - primary: active tab fill, logo, borders, hints
 *   magenta - @mentions and tags in post/comment text
 */
#define CP_TAB_ACTIVE 1   /* black-on-blue filled pill: the active tab */
#define CP_AUTHOR     2
#define CP_LIKED      3
#define CP_ERROR      4
#define CP_BADGE      5
#define CP_PRIMARY    6   /* blue: borders/rules (dim), logo (bold), hints (dim) */
#define CP_MENTION    7

int  ui_init(void);
void ui_teardown(void);

void ui_draw(app_t *app);
void ui_modal_error(app_t *app, const char *msg);
void ui_help(app_t *app);

/* Modal yes/no. Returns 1 for yes. Defaults to no: anything that is not
 * y or Y is a decline, so a stray keypress never confirms a delete. danger
 * draws the border/prompt in red instead of blue, mirroring the web app's
 * btn-danger vs btn-primary -- use it for anything irreversible. */
int  ui_confirm(app_t *app, const char *question, int danger);

/* Modal single-line input. Returns 1 when Enter was pressed with content,
 * 0 on Esc or an empty Enter. With hidden set, keystrokes echo as dots --
 * used for passwords and OTP codes. */
int  ui_prompt(app_t *app, const char *title, const char *label,
               int hidden, char *out, size_t outsz);

/* Rows available to the body between the tab bar and the status line. */
int  ui_body_height(void);

/* Draws an h x w box (ACS line-drawing, so it degrades on limited
 * terminals) with its top-left corner at (y, x) on stdscr. emphasize
 * draws a bold border instead of a dim one -- used to mark the selected
 * card among a list of collapsed ones. */
void ui_draw_box(int y, int x, int h, int w, int emphasize);

/* Draws a scrollable post list with the selection expanded in place.
 * Shared by the feed and a profile's posts. */
void ui_draw_post_list(post_list_t *pl, int body_top, int bottom,
                       const char *empty_msg, const char *end_label);

/* Copies at most max_cols display columns of UTF-8 from src, never
 * splitting a multibyte character, appending an ellipsis if it cut. */
void ui_utf8_take(char *dst, size_t dstsz, const char *src, int max_cols, int ellipsis);

/* Display width of a UTF-8 string in terminal columns. */
int  ui_utf8_width(const char *s);

#endif
