/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zune quickplay (zune41): the Zune HD home's right-hand pane on its own screen. "history" - the albums played last,
 * newest first - and "new" - the albums added to the card last - each as a row of covers that scrolls sideways and
 * runs off the round glass. A tap on a cover opens that album; a hold plays it. Rebuilt each time it opens. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "zune_dial.h"
#include "zune_thumbs.h"
#include "musicdb.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_regular_12)
const lv_font_t *zune_font(const lv_font_t *base);
void library_open_album(const char *name);
int ui_play_list(int list_type, const char *name, int pos1); void ui_set_workmode(int mode);

#define QP_TILE 76
#define QP_MAX 12
#define QP_X 44                          /* the headings' left edge */

typedef struct { int id; char album[MDB_STR]; } qp_alb_t;
static lv_obj_t *g_root, *g_rows[2], *g_empty[2];
static qp_alb_t g_alb[2][QP_MAX]; static int g_n[2];

static int seen(const qp_alb_t *a, int n, const char *album){ for(int i = 0; i < n; i++) if(!strcmp(a[i].album, album)) return 1; return 0; }
/* history: the recently played songs, folded to their albums */
static int load_history(qp_alb_t *out){
    int cap = 200, n = 0;
    mdb_song_t *s = malloc(sizeof *s * (size_t)cap); if(!s) return 0;
    int k = mdb_recent(s, cap);
    for(int i = 0; i < k && n < QP_MAX; i++){
        if(!s[i].album[0] || seen(out, n, s[i].album)) continue;
        out[n].id = s[i].id; snprintf(out[n].album, MDB_STR, "%s", s[i].album); n++;
    }
    free(s);
    return n;
}
/* new: the albums whose songs were indexed last (the highest song IDs) */
static int load_new(qp_alb_t *out){
    int total = mdb_song_count(), n = 0;
    for(int round = 0; round < QP_MAX; round++){
        int best = -1, best_id = -1;
        for(int i = 0; i < total; i++){
            const mdb_song_t *s = mdb_song(i);
            if(!s || !s->album[0] || s->id <= best_id || seen(out, n, s->album)) continue;
            best = i; best_id = s->id;
        }
        if(best < 0) break;
        const mdb_song_t *s = mdb_song(best);
        out[n].id = s->id; snprintf(out[n].album, MDB_STR, "%s", s->album); n++;
    }
    return n;
}

static void tile_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    qp_alb_t *a = lv_event_get_user_data(e);
    if(!a || !a->album[0]) return;
    if(c == LV_EVENT_LONG_PRESSED){ cfg_set_int("work_mode", 0); ui_set_workmode(0);   /* as the cover flow's long press */
        if(ui_play_list(3, a->album, 1)) screen_show(SCR_NOWPLAYING); return; }
    if(c == LV_EVENT_SHORT_CLICKED){ library_open_album(a->album); screen_show(SCR_LIBRARY); }
}
static void fill(int r){
    lv_obj_t *row = g_rows[r];
    lv_obj_clean(row);
    for(int i = 0; i < g_n[r]; i++){
        lv_obj_t *ph = lv_obj_create(row); lv_obj_remove_style_all(ph); kit_keep(ph);
        lv_obj_set_size(ph, QP_TILE, QP_TILE);
        lv_obj_set_style_bg_color(ph, TC(SURFACE), 0); lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
        lv_obj_remove_flag(ph, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ph, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(ph, tile_cb, LV_EVENT_SHORT_CLICKED, &g_alb[r][i]);
        lv_obj_add_event_cb(ph, tile_cb, LV_EVENT_LONG_PRESSED, &g_alb[r][i]);
        lv_obj_t *l = lv_label_create(ph); kit_keep(l);                     /* under the cover until it arrives */
        lv_label_set_text(l, g_alb[r][i].album); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, QP_TILE - 10); lv_obj_set_style_max_height(l, 3 * lv_font_get_line_height(zune_font(&font_zune_regular_12)), 0);
        lv_obj_set_style_text_font(l, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(l, TC(TEXT_MUTED), 0);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, 5, -4);
        lv_obj_t *img = lv_image_create(ph);
        lv_obj_set_size(img, QP_TILE, QP_TILE); lv_obj_center(img);
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        zthumb_bind(img, g_alb[r][i].id, QP_TILE);
    }
    if(g_empty[r]){ if(g_n[r]) lv_obj_add_flag(g_empty[r], LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(g_empty[r], LV_OBJ_FLAG_HIDDEN); }
    lv_obj_scroll_to_x(row, 0, LV_ANIM_OFF);
}
void zquickplay_open(void){
    if(!g_root) return;
    zthumb_cancel();
    g_n[0] = load_history(g_alb[0]);
    g_n[1] = load_new(g_alb[1]);
    fill(0); fill(1);
    screen_show(SCR_QUICKPLAY);
}
static lv_obj_t *heading(const char *t, int y){
    lv_obj_t *l = lv_label_create(g_root); kit_keep(l);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, zune_font(&font_zune_light_32), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(l, QP_X, y);
    return l;
}
void zquickplay_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    zune_status(root, SCR_QUICKPLAY);
    static const char *const H[2] = { "history", "new" };
    static const char *const E[2] = { "nothing played yet", "no albums on the card yet" };
    for(int r = 0; r < 2; r++){
        int y = r ? 180 : 40;
        heading(H[r], y);
        lv_obj_t *row = lv_obj_create(root); lv_obj_remove_style_all(row); kit_keep(row);
        lv_obj_set_size(row, 360, QP_TILE); lv_obj_set_pos(row, 0, y + 46);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_left(row, QP_X + 2, 0); lv_obj_set_style_pad_right(row, 60, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_scroll_dir(row, LV_DIR_HOR); lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        g_rows[r] = row;
        lv_obj_t *e = lv_label_create(root); kit_keep(e);
        lv_label_set_text(e, E[r]);
        lv_obj_set_style_text_font(e, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(e, TC(TEXT_MUTED), 0);
        lv_obj_set_pos(e, QP_X + 2, y + 52); lv_obj_add_flag(e, LV_OBJ_FLAG_HIDDEN);
        g_empty[r] = e;
    }
}

/* ---- gesture guide (zune47): shown once on the first Zune start, again from Settings > Help ----
 * One card per gesture, a tap moves on; "skip" ends it. */
#define GG_N 5
static const char *const GG_T[GG_N] = { "go back", "quick settings", "now playing", "scroll anywhere", "jump to a letter" };
static const char *const GG_B[GG_N] = {
    "Swipe in from the left edge, or tap the round arrow beside a title.",
    "Pull down from the top edge for brightness, wi-fi, bluetooth and the sleep timer.",
    "Swipe sideways to skip a song. Swipe up for the full-screen cover, and tap it for the controls.",
    "Drag your finger round the rim to scroll any list. Let go and it keeps going for a moment.",
    "In a long list, drag the round letter on the right around the rim, then let go on a letter." };
static lv_obj_t *g_gg, *g_gg_t, *g_gg_b, *g_gg_n; static int g_gg_i;
static void gg_close(void){ if(g_gg){ lv_obj_t *o = g_gg; g_gg = NULL; lv_obj_delete_async(o); } cfg_set_int("zguide_done", 1); }
static void gg_show(void){
    char n[24]; snprintf(n, sizeof n, "%d of %d", g_gg_i + 1, GG_N);
    lv_label_set_text(g_gg_n, n); lv_label_set_text(g_gg_t, GG_T[g_gg_i]); lv_label_set_text(g_gg_b, GG_B[g_gg_i]);
}
static void gg_next_cb(lv_event_t *e){ (void)e; if(++g_gg_i >= GG_N){ zmodal_close_top(); return; } gg_show(); }
static void gg_skip_cb(lv_event_t *e){ (void)e; zmodal_close_top(); }
void zune_guide_open(void){
    if(g_gg) return;
    g_gg_i = 0;
    g_gg = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(g_gg); kit_keep(g_gg);
    lv_obj_set_size(g_gg, 360, 360); lv_obj_set_pos(g_gg, 0, 0);
    lv_obj_set_style_bg_color(g_gg, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_gg, 245, 0);
    lv_obj_add_flag(g_gg, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(g_gg, LV_OBJ_FLAG_SCROLLABLE);
    ui_on(g_gg, gg_next_cb, LV_EVENT_CLICKED, NULL, "zune.guide.next", UI_REACHABLE);
    g_gg_n = lv_label_create(g_gg); kit_keep(g_gg_n);
    lv_obj_set_style_text_font(g_gg_n, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(g_gg_n, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_pos(g_gg_n, 52, 92);
    g_gg_t = lv_label_create(g_gg); kit_keep(g_gg_t);
    lv_obj_set_style_text_font(g_gg_t, zune_font(&font_zune_light_32), 0); lv_obj_set_style_text_color(g_gg_t, TC(TEXT_PRIMARY), 0);
    lv_obj_set_pos(g_gg_t, 50, 110);
    g_gg_b = lv_label_create(g_gg); kit_keep(g_gg_b);
    lv_obj_set_style_text_font(g_gg_b, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(g_gg_b, TC(TEXT_MUTED), 0);
    lv_label_set_long_mode(g_gg_b, LV_LABEL_LONG_WRAP); lv_obj_set_width(g_gg_b, 250); lv_obj_set_pos(g_gg_b, 52, 160);
    lv_obj_set_style_text_line_space(g_gg_b, 3, 0);
    lv_obj_t *tap = lv_label_create(g_gg); kit_keep(tap); lv_label_set_text(tap, "tap for next");
    lv_obj_set_style_text_font(tap, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(tap, TC(TEXT_MUTED), 0);
    lv_obj_align(tap, LV_ALIGN_TOP_MID, -40, 270);
    lv_obj_t *sk = lv_label_create(g_gg); kit_keep(sk); lv_label_set_text(sk, "skip");
    lv_obj_set_style_text_font(sk, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(sk, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(sk, LV_ALIGN_TOP_MID, 50, 270); lv_obj_add_flag(sk, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(sk, 18);
    ui_on(sk, gg_skip_cb, LV_EVENT_CLICKED, NULL, "zune.guide.skip", UI_REACHABLE);
    gg_show();
    zmodal_push(g_gg, gg_close);
}
void zune_guide_preview(int i){ zune_guide_open(); if(i >= 0 && i < GG_N){ g_gg_i = i; gg_show(); } }
