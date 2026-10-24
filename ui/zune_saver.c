/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zune lock screen / standby (Figma 02), whatever the saver style: with a track the "Metro stack" (zune41) - a huge
 * thin time, the weekday and the date stacked under it on the left, and a small now-playing row (cover, song, artist,
 * a hairline of progress) low on the glass; with nothing loaded a
 * type-only clock as big as the glass allows, the weekday and date hugging the curve under it. The battery as "82%"
 * at the top. Redraws once a minute. */
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "zune_dial.h"
#include "simple_theme.h"
#include "ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(font_zune_light_96) LV_FONT_DECLARE(font_zune_light_150) LV_FONT_DECLARE(font_zune_light_32)
LV_FONT_DECLARE(font_zune_semibold_12) LV_FONT_DECLARE(font_zune_regular_15)
LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_13) LV_FONT_DECLARE(font_zune_regular_17)
int zhome_battery_pct(void);
/* light_150 digit box inside the label (measured): top, height, baseline */
#define ZT_TOP 3
#define ZT_H 107
#define ZT_BASE 110
/* the Metro stack (with music): left edge, the now-playing row, light_96 digit box (top, height) */
#define ZS_X 44
#define ZS_ART 44
#define ZS_ROW_Y 274
#define ZS_TX (ZS_X + ZS_ART + 12)
#define ZS_TW 168
#define ZM_TOP 2
#define ZM_H 69
static int zlock_tint(void){ const char *e = getenv("ZLOCK_TINT"); return e ? atoi(e) : simple_cfg_get_int("zlock_tint", 4); }

static struct {
    lv_obj_t *root, *batt, *art, *art_img, *ph;
    lv_obj_t *mt, *mmer, *mday, *mdate, *mtitle, *martist, *hint;    /* with music */
    lv_obj_t *ptrack, *pfill, *ptime;                         /* with music: how far into the song */
    lv_timer_t *ptm;
    lv_obj_t *it, *ic, *im, *imer, *iday, *idate;             /* idle: hours, colon, minutes as three labels (tint) */
    lv_obj_t *dnp, *dwx;                                      /* desk clock (zune45): now playing, the weather */
    int have, dock;
    char t[32], wx[64];
} Z;

static lv_obj_t *lbl(const lv_font_t *f, lv_color_t c){
    lv_obj_t *l = lv_label_create(Z.root); kit_keep(l);
    lv_obj_set_style_text_font(l, f, 0); lv_obj_set_style_text_color(l, c, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}
static void show(lv_obj_t *o, int on){ if(!o) return; if(on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void fmt_ms(char *b, size_t n, long ms){ long s = ms / 1000; if(s < 0) s = 0;
    if(s >= 3600) snprintf(b, n, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60); else snprintf(b, n, "%ld:%02ld", s / 60, s % 60); }
static void zprog_tick(lv_timer_t *t){
    (void)t;
    if(!Z.root || !Z.ptrack || Z.have <= 0 || (t && screen_current() != SCR_SAVER)) return;
    track_state_t st; ipc_get_state(&st);
    long dur = st.duration_ms, pos = st.position_ms; if(pos > dur && dur > 0) pos = dur;
    char a[16], b[16], l[40]; fmt_ms(a, sizeof a, pos); fmt_ms(b, sizeof b, dur);
    if(dur > 0) snprintf(l, sizeof l, "%s / %s", a, b); else snprintf(l, sizeof l, "%s", a);
    lv_label_set_text(Z.ptime, st.have_track ? l : "");
    lv_obj_set_width(Z.pfill, dur > 0 ? (int)((long)ZS_TW * pos / dur) : 0);
}
void zsaver_create(lv_obj_t *root){
    memset(&Z, 0, sizeof Z);
    Z.root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    Z.batt = lbl(zune_font(&font_zune_regular_12), TC(TEXT_PRIMARY));
    lv_obj_align(Z.batt, LV_ALIGN_TOP_MID, 0, 24); lv_obj_set_style_text_color(Z.batt, TC(TEXT_MUTED), 0);
    /* with music */
    Z.art = lv_obj_create(root); lv_obj_remove_style_all(Z.art); kit_keep(Z.art);
    lv_obj_set_size(Z.art, ZS_ART, ZS_ART); lv_obj_set_pos(Z.art, ZS_X + 2, ZS_ROW_Y);
    lv_obj_set_style_bg_color(Z.art, TC(SURFACE), 0); lv_obj_set_style_bg_opa(Z.art, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.art, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.art_img = lv_image_create(Z.art); lv_obj_set_size(Z.art_img, ZS_ART, ZS_ART); lv_obj_center(Z.art_img);
    Z.mt = lbl(&font_zune_light_96, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.mt, -3, 0);
    Z.mmer = lbl(zune_font(&font_zune_regular_17), TC(TEXT_MUTED));
    Z.mday = lbl(zune_font(&font_zune_light_32), TC(TEXT_PRIMARY));
    Z.mdate = lbl(zune_font(&font_zune_regular_15), TC(TEXT_MUTED));
    Z.mtitle = lbl(zune_font(&font_zune_semibold_12), TC(TEXT_PRIMARY)); lv_obj_set_pos(Z.mtitle, ZS_TX, ZS_ROW_Y + 2);
    lv_label_set_long_mode(Z.mtitle, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(Z.mtitle, ZS_TW);
    Z.martist = lbl(zune_font(&font_zune_regular_12), TC(TEXT_MUTED)); lv_obj_set_pos(Z.martist, ZS_TX, ZS_ROW_Y + 21);
    lv_label_set_long_mode(Z.martist, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(Z.martist, ZS_TW);
    Z.ptrack = lv_obj_create(root); lv_obj_remove_style_all(Z.ptrack); kit_keep(Z.ptrack);
    lv_obj_set_size(Z.ptrack, ZS_TW, 2); lv_obj_set_pos(Z.ptrack, ZS_TX, ZS_ROW_Y + ZS_ART - 2);
    lv_obj_set_style_bg_color(Z.ptrack, TC(CONTROL_TRACK), 0); lv_obj_set_style_bg_opa(Z.ptrack, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.ptrack, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.pfill = lv_obj_create(Z.ptrack); lv_obj_remove_style_all(Z.pfill); kit_keep(Z.pfill);
    lv_obj_set_size(Z.pfill, 0, 2); lv_obj_set_style_bg_color(Z.pfill, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(Z.pfill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(Z.pfill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    Z.ptime = lbl(zune_font(&font_zune_regular_12), TC(TEXT_MUTED)); lv_obj_add_flag(Z.ptime, LV_OBJ_FLAG_HIDDEN);
    Z.hint = lbl(zune_font(&font_zune_regular_12), TC(TEXT_PRIMARY)); lv_label_set_text(Z.hint, "tap to wake");
    lv_obj_align(Z.hint, LV_ALIGN_TOP_MID, 0, 300);
    /* idle */
    Z.it = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.it, -4, 0);
    Z.ic = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.ic, -4, 0);
    Z.im = lbl(&font_zune_light_150, TC(TEXT_PRIMARY)); lv_obj_set_style_text_letter_space(Z.im, -4, 0);
    Z.imer = lbl(zune_font(&font_zune_regular_17), TC(TEXT_MUTED));
    Z.iday = lbl(zune_font(&font_zune_light_32), TC(TEXT_PRIMARY)); lv_obj_align(Z.iday, LV_ALIGN_TOP_MID, 0, 208);
    Z.idate = lbl(zune_font(&font_zune_regular_17), TC(TEXT_MUTED)); lv_obj_align(Z.idate, LV_ALIGN_TOP_MID, 0, 250);
    Z.dnp = lbl(zune_font(&font_zune_regular_13), TC(ACCENT_PRIMARY));      /* desk clock: what's playing, above the time */
    lv_label_set_long_mode(Z.dnp, LV_LABEL_LONG_DOT); lv_obj_set_width(Z.dnp, 230); lv_obj_set_style_text_align(Z.dnp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(Z.dnp, LV_ALIGN_TOP_MID, 0, 58);
    Z.dwx = lbl(zune_font(&font_zune_regular_13), TC(TEXT_MUTED));           /* ...and the weather, under the date */
    zune_sleep_ring(root, 354);
    Z.have = -1;
    if(!Z.ptm) Z.ptm = lv_timer_create(zprog_tick, 1000, NULL);
    zprog_tick(NULL);
}
static void layout(void){
    int m = !Z.dock && Z.have > 0 && simple_cfg_get_int("zlock_src", 0) == 0;   /* Clock Only (and the desk clock): the big clock */
    show(Z.art, m); show(Z.mt, m); show(Z.mday, m); show(Z.mdate, m); show(Z.mtitle, m); show(Z.martist, m); show(Z.hint, 0); show(Z.ptrack, m); show(Z.ptime, 0);
    show(Z.it, !m); show(Z.ic, !m); show(Z.im, !m); show(Z.iday, !m); show(Z.idate, !m);
    show(Z.mmer, m && lv_label_get_text(Z.mmer)[0]); show(Z.imer, !m && lv_label_get_text(Z.imer)[0]);
    show(Z.dnp, Z.dock && Z.have > 0); show(Z.dwx, Z.dock && Z.wx[0]);
}
/* desk clock (zune45): while the Disc sits on the charger the lock screen stays on, dimmed, as a clock with the date,
 * the weather, the battery and what's playing (main.c turns it on and off) */
void zsaver_set_dock(int on){ if(!Z.root || Z.dock == on) return; Z.dock = on; layout(); }
void zsaver_set_weather(const char *t){
    if(!Z.root) return;
    char b[64] = ""; const char *p = t ? t : "";
    while(*p && (unsigned char)*p >= 0x80) p++;                     /* drop the leading weather glyph (the font has none) */
    while(*p == ' ') p++;
    snprintf(b, sizeof b, "%s", p); for(char *c = b; *c; c++) if(*c >= 'A' && *c <= 'Z' && !(c > b && (unsigned char)c[-1] == 0xB0)) *c += 32;   /* keep the C of 18°C */
    snprintf(Z.wx, sizeof Z.wx, "%s", b); lv_label_set_text(Z.dwx, b); layout();
}
void zsaver_set_clock(const char *t){
    if(!Z.root) return;
    char d[32]; snprintf(d, sizeof d, "%s", t ? t : "--:--");
    char *sp = strchr(d, ' '), mer[8] = "";                        /* "7:29 PM": the digits big, "pm" small beside */
    if(sp){ snprintf(mer, sizeof mer, "%s", sp + 1); *sp = 0; for(char *c = mer; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32; }
    lv_label_set_text(Z.mt, d);
    lv_label_set_text(Z.mmer, mer); lv_label_set_text(Z.imer, mer);
    /* idle: the time dead centre of the glass, hours | colon | minutes as three labels so any part can take the
     * accent (cfg "zlock_tint": 0 white, 1 all accent, 2 minutes, 3 colon, 4 hours); "pm" small on its baseline */
    char hh[16] = "", mm[16] = ""; const char *col = strchr(d, ':');
    if(col){ snprintf(hh, sizeof hh, "%.*s", (int)(col - d), d); snprintf(mm, sizeof mm, "%s", col + 1); }
    else snprintf(hh, sizeof hh, "%s", d);
    lv_label_set_text(Z.it, hh); lv_label_set_text(Z.ic, col ? ":" : ""); lv_label_set_text(Z.im, mm);
    lv_point_t a, b2, c;
    lv_text_get_size(&a, hh, &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&b2, col ? ":" : "", &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&c, mm, &font_zune_light_150, -4, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int sx = a.x + b2.x + c.x - 8;
    int merw = 0; if(mer[0]){ lv_point_t ms; lv_text_get_size(&ms, mer, zune_font(&font_zune_regular_17), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); merw = ms.x + 6; }
    int room = 286 - merw, sc = sx > room ? 256 * room / sx : 256;
    int cw = sx * sc / 256 + merw, ix = 180 - cw / 2;
    int iy = 180 - (ZT_TOP + ZT_H / 2) * sc / 256;                 /* the digits' middle on the glass's middle */
    lv_obj_t *parts[3] = { Z.it, Z.ic, Z.im }; int w[3] = { a.x - 4, b2.x - 4, c.x };
    int tint = zlock_tint(); lv_color_t ac = TC(ACCENT_PRIMARY), wh = TC(TEXT_PRIMARY);
    int x = ix;
    for(int k = 0; k < 3; k++){
        lv_obj_set_style_transform_pivot_x(parts[k], 0, 0); lv_obj_set_style_transform_pivot_y(parts[k], 0, 0);
        lv_obj_set_style_transform_scale(parts[k], sc, 0);
        lv_obj_set_pos(parts[k], x, iy); x += w[k] * sc / 256;
        int on = tint == 1 || (tint == 2 && k == 2) || (tint == 3 && k == 1) || (tint == 4 && k == 0);
        lv_obj_set_style_text_color(parts[k], on ? ac : wh, 0);
    }
    lv_obj_set_pos(Z.imer, x + 6, iy + ZT_BASE * sc / 256 - 22);
    lv_obj_set_style_text_color(Z.imer, tint == 1 ? ac : TC(TEXT_MUTED), 0);
    int below = iy + (ZT_TOP + ZT_H) * sc / 256;                    /* weekday and date follow the digits */
    lv_obj_align(Z.iday, LV_ALIGN_TOP_MID, 0, below + 24);
    lv_obj_align(Z.idate, LV_ALIGN_TOP_MID, 0, below + 66);
    lv_obj_align(Z.dwx, LV_ALIGN_TOP_MID, 0, below + 92);
    /* with music: the time huge and left, the weekday and date stacked under it (Metro) */
    lv_point_t ts; lv_text_get_size(&ts, d, &font_zune_light_96, -3, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int ty = 74 - ZM_TOP;
    lv_obj_set_pos(Z.mt, ZS_X - 4, ty);
    lv_obj_set_pos(Z.mmer, ZS_X - 4 + ts.x + 6, ty + ZM_TOP + ZM_H - 18);
    lv_obj_set_pos(Z.mday, ZS_X, ty + ZM_TOP + ZM_H + 16);
    lv_obj_set_pos(Z.mdate, ZS_X + 2, ty + ZM_TOP + ZM_H + 60);
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    static const char *WD[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
    static const char *MO[] = { "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december" };
    char b[48];
    lv_label_set_text(Z.mday, WD[tm.tm_wday]);
    snprintf(b, sizeof b, "%s %d", MO[tm.tm_mon], tm.tm_mday); lv_label_set_text(Z.mdate, b);
    lv_label_set_text(Z.iday, WD[tm.tm_wday]);
    snprintf(b, sizeof b, "%s %d", MO[tm.tm_mon], tm.tm_mday); lv_label_set_text(Z.idate, b);
    int p = zhome_battery_pct();
    int usage_charging(void);
    if(p >= 0){ if(Z.dock && usage_charging()) snprintf(b, sizeof b, "charging \xC2\xB7 %d%%", p); else snprintf(b, sizeof b, "%d%%", p);
                lv_label_set_text(Z.batt, b); } else lv_label_set_text(Z.batt, "");
    layout();
    zprog_tick(NULL);
}
void zsaver_set_track(const char *title, const char *artist){
    if(!Z.root) return;
    Z.have = title != NULL;
    lv_label_set_text(Z.mtitle, title ? title : "");
    { char b[200]; snprintf(b, sizeof b, "%s%s%s", title ? title : "", artist && artist[0] ? " \xC2\xB7 " : "", artist ? artist : "");
      for(char *c = b; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32; lv_label_set_text(Z.dnp, b); }
    lv_label_set_text(Z.martist, artist ? artist : "");
    const lv_image_dsc_t *dsc = ui_current_cover_dsc();
    lv_image_set_src(Z.art_img, NULL);
    if(dsc && dsc->header.w > 0){ lv_image_set_src(Z.art_img, dsc); lv_image_set_scale(Z.art_img, (uint32_t)(256 * ZS_ART / dsc->header.w)); }
    layout();
    zprog_tick(NULL);
}
