/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "multipick.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "null_theme.h"
#include "musicdb.h"
#include "onboarding.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MP_EDGE 72                       /* a pick swipe starts this far in from either edge (back swipe: < 64) */
#define MP_MAX_HOSTS 4
static mp_host_t *g_hosts[MP_MAX_HOSTS];
static int g_nhosts;
static const int g_tick_tag;             /* marks a row's tick badge */

static void row_cb(lv_event_t *e);
static mp_host_t *host_of(lv_obj_t *row){
    lv_obj_t *list = lv_obj_get_parent(row);
    for(int k = 0; k < g_nhosts; k++) if(g_hosts[k]->list == list) return g_hosts[k];
    return NULL;
}
static int row_index(lv_obj_t *r){                       /* the item a row shows (-1: not a pickable row) */
    for(uint32_t k = 0; k < lv_obj_get_event_count(r); k++){
        lv_event_dsc_t *d = lv_obj_get_event_dsc(r, k);
        if(d && lv_event_dsc_get_cb(d) == row_cb) return (int)(intptr_t)lv_event_dsc_get_user_data(d);
    }
    return -1;
}
static lv_obj_t *badge_of(lv_obj_t *r){
    for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){ lv_obj_t *c = lv_obj_get_child(r, (int32_t)k); if(lv_obj_get_user_data(c) == &g_tick_tag) return c; }
    return NULL;
}
static int is_trailing(lv_obj_t *c){                     /* the value / chevron on the right gives way to the tick */
    if(!lv_obj_check_type(c, &lv_label_class)) return 0;
    if(lv_obj_get_style_text_align(c, 0) == LV_TEXT_ALIGN_RIGHT) return 1;
    return !strcmp(lv_label_get_text(c), LV_SYMBOL_RIGHT);
}
static void style_row(mp_host_t *h, lv_obj_t *r, int i){
    int on = h->on && i >= 0 && i < h->cap && h->sel[i];
    lv_obj_t *b = badge_of(r);
    for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){
        lv_obj_t *c = lv_obj_get_child(r, (int32_t)k);
        if(is_trailing(c)){ if(h->on) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(c, LV_OBJ_FLAG_HIDDEN); }
    }
    if(!h->on){ if(b) lv_obj_delete(b); lv_obj_set_style_outline_width(r, 0, 0); return; }
    if(!b){
        b = lv_obj_create(r); lv_obj_remove_style_all(b); lv_obj_set_user_data(b, (void *)&g_tick_tag);
        lv_obj_set_size(b, 24, 24); lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_EVENT_BUBBLE); lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *t = lv_label_create(b); lv_label_set_text(t, LV_SYMBOL_OK); lv_obj_set_style_text_font(t, TF(UI_14), 0); lv_obj_center(t);
    }
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_border_width(b, on ? 0 : 2, 0); lv_obj_set_style_border_color(b, TC(TEXT_MUTED), 0);
    lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_t *t = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_color(t, theme_on_color(TC(ACCENT_PRIMARY)), 0);
    if(on) lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_outline_color(r, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_outline_pad(r, -1, 0);
    lv_obj_set_style_outline_width(r, on ? 2 : 0, 0);
}
static void restyle_all(mp_host_t *h){
    for(uint32_t k = 0; k < lv_obj_get_child_count(h->list); k++){
        lv_obj_t *r = lv_obj_get_child(h->list, (int32_t)k); int i = row_index(r);
        if(i >= 0) style_row(h, r, i);
    }
}
static void set_title(mp_host_t *h){
    if(!h->title) return;
    if(!h->on){ if(h->saved_title[0]) lv_label_set_text(h->title, h->saved_title); return; }
    char b[48]; snprintf(b, sizeof b, th_null() ? "%d picked" : h->cnt == 1 ? "1 selected" : "%d selected", h->cnt);   /* Null's pixel title is narrow */
    lv_label_set_text(h->title, b);
}
static void bar_show(mp_host_t *h, int on){
    static int32_t pad0 = -1;
    if(!h->bar) return;
    if(pad0 < 0) pad0 = lv_obj_get_style_pad_bottom(h->list, 0);
    if(on){
        if(h->az) h->az(0);
        lv_obj_remove_flag(h->bar, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(h->bar);
        lv_color_t bg = TC(CANVAS);                                          /* the screen's own colour */
        for(lv_obj_t *o = h->root; o; o = lv_obj_get_parent(o)) if(lv_obj_get_style_bg_opa(o, 0) > LV_OPA_50){ bg = lv_obj_get_style_bg_color(o, 0); break; }
        if(th_null()) bg = TC(FIXED_MEDIA_BLACK);                           /* Null: always on black */
        lv_obj_set_style_bg_color(h->bar, bg, 0); lv_obj_set_style_bg_grad_color(h->bar, bg, 0);
        lv_obj_set_style_bg_opa(h->bar, LV_OPA_COVER, 0);                   /* a theme pass may have cleared it */
        lv_obj_set_style_pad_bottom(h->list, pad0 + 70, 0);                 /* the last rows can scroll clear of the bar */
    } else {
        lv_obj_add_flag(h->bar, LV_OBJ_FLAG_HIDDEN); lv_obj_set_style_pad_bottom(h->list, pad0, 0);
    }
}
static void more_close(mp_host_t *h){ if(h->pop){ lv_obj_delete_async(h->pop); h->pop = NULL; } }
void mp_exit(mp_host_t *h){
    if(!h || !h->on) return;
    h->on = 0; h->cnt = 0; more_close(h);
    restyle_all(h); bar_show(h, 0); set_title(h);
    if(h->az) h->az(1);
}
int mp_active(const mp_host_t *h){ return h && h->on; }
static void toggle(mp_host_t *h, lv_obj_t *r, int i){
    int n = h->count ? h->count() : 0;
    if(i < 0 || i >= n || (h->pickable && !h->pickable(i))) return;
    if(!h->on){
        if(h->can_pick && !h->can_pick()) return;
        if(h->cap < n){ uint8_t *m = realloc(h->sel, (size_t)n); if(!m) return; h->sel = m; h->cap = n; }
        memset(h->sel, 0, (size_t)h->cap); h->cnt = 0; h->on = 1;
        snprintf(h->saved_title, sizeof h->saved_title, "%s", h->title ? lv_label_get_text(h->title) : "");
        restyle_all(h); bar_show(h, 1);
        onboarding_discovered(HINT_PICK);                                   /* found it: no need for the tip */
    }
    h->sel[i] = !h->sel[i]; h->cnt += h->sel[i] ? 1 : -1;
    if(r) style_row(h, r, i);
    set_title(h);
    if(h->cnt == 0) mp_exit(h);                                             /* nothing ticked any more: back to normal */
}
void mp_start(mp_host_t *h, int index){                                    /* start picking with this item ticked (the hold menu's Select) */
    if(!h || h->on) return;
    lv_obj_t *row = NULL;
    for(uint32_t k = 0; h->list && k < lv_obj_get_child_count(h->list); k++){
        lv_obj_t *r = lv_obj_get_child(h->list, (int32_t)k); if(row_index(r) == index){ row = r; break; } }
    toggle(h, row, index);
}
/* ---- the row gestures -------------------------------------------------------------------------------------- */
static uint32_t g_sw_t; static lv_point_t g_sw_p; static int g_sw_done;
static void row_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *r = lv_event_get_current_target(e);
    mp_host_t *h = host_of(r); if(!h) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if(c == LV_EVENT_PRESSED){ if(in) lv_indev_get_point(in, &g_sw_p); g_sw_done = 0; g_sw_t = lv_tick_get(); return; }
    if(c == LV_EVENT_PRESSING){
        if(g_sw_done || !in || (h->can_pick && !h->can_pick())) return;
        if(th_zune() && !h->on) return;                                    /* Zune: a sideways swipe turns the pivot; picking starts from the hold menu */
        if(g_sw_p.x < MP_EDGE || g_sw_p.x > 360 - MP_EDGE) return;         /* edge strips: back swipe / navigation circle */
        lv_point_t p; lv_indev_get_point(in, &p);
        int dx = p.x - g_sw_p.x, dy = p.y - g_sw_p.y;
        if(abs(dx) >= 36 && abs(dy) < 22 && lv_tick_elaps(g_sw_t) < 900){
            g_sw_done = 1; lv_indev_wait_release(in);                         /* no tap, no hold, no scroll from this touch */
            toggle(h, r, i);
        }
        return;
    }
    if(!h->on) return;
    if(c == LV_EVENT_SHORT_CLICKED || c == LV_EVENT_CLICKED){ if(c == LV_EVENT_SHORT_CLICKED) toggle(h, r, i); lv_event_stop_processing(e); }
    else if(c == LV_EVENT_LONG_PRESSED){ lv_event_stop_processing(e); if(h->more) lv_obj_send_event(h->more, LV_EVENT_CLICKED, NULL); }
}
void mp_row(mp_host_t *h, lv_obj_t *row, int index){
    if(!h || !row) return;
    lv_obj_add_event_cb(row, row_cb, LV_EVENT_ALL, (void *)(intptr_t)index);
    if(h->on) style_row(h, row, index);                                      /* a recycled row keeps its tick */
}
/* ---- the actions ------------------------------------------------------------------------------------------- */
/* The ticked items as exact songs; toasts and returns <= 0 when there are none or the list couldn't be built. */
static int songs_of(mp_host_t *h, int **ids){
    *ids = NULL;
    int n = h->collect ? h->collect(h->sel, h->cap, ids) : 0;
    if(n < 0){ free(*ids); *ids = NULL; ui_toast("Couldn't read those songs - try again"); return -1; }
    if(n == 0){ free(*ids); *ids = NULL; ui_toast("No songs in that selection"); }
    return n;
}
static void act_play(mp_host_t *h, int shuffle){
    int *ids = NULL, n = songs_of(h, &ids);
    if(n <= 0) return;
    if(shuffle) for(int k = n - 1; k > 0; k--){ int j = rand() % (k + 1), t = ids[k]; ids[k] = ids[j]; ids[j] = t; }
    mdb_plan_t pl = { .list_type = 5, .ids = ids, .count = n };
    queue_note_external_play();                                             /* the queue (if any) waits until after these */
    int ok = ui_play_plan(&pl, ids[0]);
    mdb_plan_free(&pl);
    if(ok){ mp_exit(h); screen_show(SCR_NOWPLAYING); }
}
static void act_queue(mp_host_t *h){
    int *ids = NULL, n = songs_of(h, &ids);
    if(n <= 0) return;
    int added = queue_add_ids(ids, n);                                      /* exact songs, saved once */
    free(ids);
    char b[48];
    if(added == -2){ ui_toast("Library busy - try again"); return; }   /* nothing added; the selection stays */
    if(added < 0){ snprintf(b, sizeof b, "Queue full - %d songs don't fit", n); ui_toast(b); return; }
    if(added == 0){ ui_toast("Couldn't queue those"); return; }
    if(added < n) snprintf(b, sizeof b, "%d queued, %d not found", added, n - added);
    else snprintf(b, sizeof b, added == 1 ? "1 song queued" : "%d songs queued", added);
    ui_toast(b);
    mp_exit(h);
}
static void act_playlist(mp_host_t *h){
    int *ids = NULL, n = songs_of(h, &ids);
    if(n <= 0) return;
    plpick_set_ids(ids, n); free(ids); mp_exit(h); screen_show(SCR_PLPICK);
}
static void act_favourite(mp_host_t *h){
    int *ids = NULL, n = songs_of(h, &ids), done = 0;
    if(n <= 0) return;
    for(int k = 0; k < n; k++){
        char p[300];
        if(mdb_song_path(ids[k], p, sizeof p) == 1 && mdb_set_favorite_path(p, 1) > 0) done++;   /* 1 = saved, 0 = failed */
    }
    free(ids);
    char b[48];
    if(done && done < n) snprintf(b, sizeof b, "%d saved, %d failed", done, n - done);
    else snprintf(b, sizeof b, done == 1 ? "1 favourite added" : "%d favourites added", done);
    ui_toast(done ? b : "Couldn't add favourites");
    if(done == n) mp_exit(h);                                               /* on failure the selection stays for a retry */
}
static mp_host_t *g_pop_host;
static void more_item_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    mp_host_t *h = g_pop_host; if(!h) return;
    int a = (int)(intptr_t)lv_event_get_user_data(e);
    more_close(h);
    if(a == 1) act_play(h, 1); else if(a == 2) act_playlist(h); else if(a == 3) act_favourite(h);
    else if(a == 4){ int n = h->count ? h->count() : 0, c = 0; if(n > h->cap) n = h->cap;
                     for(int i = 0; i < n; i++){ h->sel[i] = !h->pickable || h->pickable(i); c += h->sel[i]; }   /* only what can be ticked */
                     h->cnt = c; restyle_all(h); set_title(h); }
}
static void more_bg_cb(lv_event_t *e){
    mp_host_t *h = g_pop_host;
    if(h && lv_event_get_code(e) == LV_EVENT_CLICKED && lv_event_get_target(e) == h->pop) more_close(h);
}
static void more_open_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    mp_host_t *h = lv_event_get_user_data(e); if(!h || !h->on) return;
    more_close(h); g_pop_host = h;
    h->pop = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(h->pop);
    lv_obj_set_size(h->pop, 360, 360); lv_obj_set_style_bg_color(h->pop, TC(CANVAS), 0); lv_obj_set_style_bg_opa(h->pop, LV_OPA_70, 0);
    lv_obj_add_flag(h->pop, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(h->pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(h->pop, more_bg_cb, LV_EVENT_CLICKED, NULL);
    static const char *const L[] = { "Shuffle", "Add to playlist", "Add to Favourites", "Select all" };
    const int n = 4;
    lv_obj_t *card = lv_obj_create(h->pop); lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 236, 32 + n * 50); lv_obj_center(card);
    lv_obj_set_style_radius(card, 20, 0); lv_obj_set_style_bg_color(card, TC(SURFACE), 0); lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    for(int k = 0; k < n; k++){
        lv_obj_t *b = lv_obj_create(card); lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 212, 44); lv_obj_set_pos(b, 12, 16 + k * 50);
        lv_obj_set_style_radius(b, 22, 0); lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, more_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(k + 1));
        lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, tr(L[k]));
        lv_obj_set_style_text_font(l, TF(UI_18), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    }
}
static void play_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) act_play(lv_event_get_user_data(e), 0); }
static void queue_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) act_queue(lv_event_get_user_data(e)); }
static lv_obj_t *bar_btn(mp_host_t *h, int x, int w, const char *txt, lv_event_cb_t cb){
    lv_obj_t *b = lv_obj_create(h->bar); lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 40); lv_obj_set_pos(b, 70 + x, 22);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, TC(SURFACE_RAISED), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, h);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, TF(UI_16), 0); lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
    return b;
}
void mp_attach(mp_host_t *h){
    if(!h || !h->root || !h->list || h->bar) return;
    if(g_nhosts < MP_MAX_HOSTS) g_hosts[g_nhosts++] = h;
    h->bar = lv_obj_create(h->root); lv_obj_remove_style_all(h->bar);    /* a strip that fades in over the list's foot */
    lv_obj_set_size(h->bar, 360, 100); lv_obj_set_pos(h->bar, 0, 260);
    lv_obj_set_style_bg_color(h->bar, TC(CANVAS), 0); lv_obj_set_style_bg_grad_color(h->bar, TC(CANVAS), 0);
    lv_obj_set_style_bg_grad_dir(h->bar, LV_GRAD_DIR_VER, 0); lv_obj_set_style_bg_opa(h->bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(h->bar, LV_OPA_TRANSP, 0); lv_obj_set_style_bg_grad_opa(h->bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_stop(h->bar, 0, 0); lv_obj_set_style_bg_grad_stop(h->bar, 90, 0);
    lv_obj_add_flag(h->bar, LV_OBJ_FLAG_CLICKABLE);                      /* taps on the strip don't reach the rows under it */
    lv_obj_remove_flag(h->bar, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(h->bar, LV_OBJ_FLAG_HIDDEN);
    kit_keep(h->bar);                                                     /* the themes restyle its buttons, never the strip */
    bar_btn(h, 0, 80, LV_SYMBOL_PLAY " Play", play_cb);
    bar_btn(h, 86, 88, LV_SYMBOL_PLUS " Queue", queue_cb);
    h->more = bar_btn(h, 180, 40, LV_SYMBOL_LIST, more_open_cb);
}
void mp_demo(mp_host_t *h, const char *csv){
    if(!h) return;
    int n = h->count ? h->count() : 0;
    for(const char *p = csv; p && *p; ){
        if(*p >= '0' && *p <= '9'){ int i = atoi(p); if(i >= 0 && i < n && (!h->on || !h->sel[i])) toggle(h, NULL, i); }
        p = strchr(p, ','); if(p) p++;
    }
    restyle_all(h);
    if(csv && strstr(csv, "more") && h->more) lv_obj_send_event(h->more, LV_EVENT_CLICKED, NULL);
}
