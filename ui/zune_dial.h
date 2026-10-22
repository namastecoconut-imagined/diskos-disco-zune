/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Zune theme's dial list (zune_dial.c): items in a column that bends with the round glass, the one on the
 * focus line large and white, neighbours smaller and fainter. Used by the Now Playing actions and play-mode menus.
 * One dial at a time: building a new one replaces the previous. */
#ifndef ZUNE_DIAL_H
#define ZUNE_DIAL_H
#include "lvgl/lvgl.h"
#define ZDIAL_MAX 24          /* items in one dial (a looping dial keeps three copies of them) */
#define ZDIAL_ROWS (ZDIAL_MAX * 3)
/* labels: n items (lower case); focus: the item on the focus line at start; pick(i): a tap on item i.
 * top: rows above this screen y are hidden (room for a header); action: ui_on name prefix, e.g. "npmenu.zune" */
void zdial_build(lv_obj_t *parent, const char *const *labels, int n, int focus, void (*pick)(int), int top, const char *action);
void zdial_wrap_next(void);   /* the next zdial_build loops: past the last item comes the first again */
void zdial_set_color(int i, lv_color_t c);   /* e.g. the accent for the current choice */
void zdial_set_text(int i, const char *t);
void zdial_set_sub(int i, const char *t);   /* a small grey line under item i, shown while it is on the focus line */
int  zdial_focus(void);                       /* the item on the focus line now (-1: no dial) */
void zdial_set_bottom(int y);
void zdial_set_icon(int i, const char *glyph, const char *badge);   /* an icon-font glyph before item i (badge: e.g. "1") */              /* rows whose centre is below y are hidden (room for a footer) */
#endif

/* ---- shared Zune pieces (zune_dial.c) ---- */
#ifndef ZUNE_KIT_PIECES
#define ZUNE_KIT_PIECES
/* an Open Sans cut with the international/CJK fallback chain behind it (user text: lyrics, titles) */
const lv_font_t *zune_font(const lv_font_t *base);
/* the hairline progress ring on the rim (Figma: 352 px, track white 18%, magenta fill from 12 o'clock, 12 px knob).
 * It follows the playing track by itself while screen `scr` is showing. */
lv_obj_t *zune_rim(lv_obj_t *parent, int scr, int knob);
/* pivot header: the title in Light 50, lower case, left at x 75, y 40, running off the right edge; a 36 px round
 * back button left of it, centred on the x-height. back_cb NULL = screen_back(). Returns the title label. */
lv_obj_t *zune_pivot(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
/* time + battery, centred at the top (as on Home), kept current while screen `scr` is showing */
lv_obj_t *zune_status(lv_obj_t *root, int scr);
void zune_status_refresh(void);
/* a yes / no question on the whole glass (Figma 07): cancel | <yes>, the yes half in the accent; on_yes runs after it closes */
void zune_confirm(const char *title, const char *body, const char *no, const char *yes, void (*on_yes)(void));
int  zune_confirm_open(void);
void zune_radio_skin(lv_obj_t *root, const char *title, const char *onlabel, const char *scanlabel, lv_obj_t *ring, lv_obj_t *glyph,
                     lv_obj_t *name, lv_obj_t *sub, lv_obj_t *sw, lv_obj_t *rb, lv_obj_t *list);
void zune_radio_row(lv_obj_t *r, lv_obj_t *name, const char *line, int on);
lv_obj_t *zune_info_page(lv_obj_t *root, const char *title, lv_event_cb_t back_cb);
void zune_retype(lv_obj_t *root);
void zune_scan_show(void);   /* Figma 34: the full-glass scan progress and summary */
void zune_empty(lv_obj_t *parent, const char *msg, const char *help);   /* Figma 35: an empty state in the list's place */   /* move a stock screen's labels onto the Zune Open Sans cuts */
void zune_info_row(lv_obj_t *list, const char *key, const char *val);
lv_obj_t *zune_info_action(lv_obj_t *list, const char *label, lv_event_cb_t cb, const char *action);
void zune_confirm_close(void);           /* the clock, battery or sleep timer changed: repaint the visible status lines */

/* full-glass takeovers (Figma 45 / 63 / 74) */
typedef struct {
    const char *icon; int icon_ring; int icon_accent;   /* icon_ring: 0 = bare glyph, else ring diameter (64 failed, 80 pairing) */
    int spin; int ring_pct;                              /* spin: turning accent arc on the rim; else ring_pct (-1 = track only) */
    const char *title, *big, *sub, *note;
    const char *l1; void (*cb1)(void);                   /* with two links the first is the accent one */
    const char *l2; void (*cb2)(void);
} ztake_t;
void zune_takeover(const ztake_t *s);
void zune_takeover_close(void);
int  zune_takeover_open(void);
void zune_takeover_big(const char *txt);
void zune_takeover_ring(int pct);
void zune_rim_spin(lv_obj_t *scr, int on);   /* a turning accent arc on the rim while scanning */
void zune_toast_action(const char *icon, const char *title, const char *sub, const char *action, void (*cb)(void), int ms);   /* Figma 82 */
#endif
