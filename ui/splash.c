/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* splash.c - the Disco! boot splash: a CD spins up and says its name.
 *
 * A CD fills the round screen, its rainbow rim turning; it spins up (the marks on its hub blur away at speed),
 * slows, and its hub becomes a "D" in Monoton with an accent ring drawn round it. The D rolls out to the left and
 * "isco!" unrolls behind it - "Disco!", the "!" in the accent colour. Then everything fades and Home is there.
 * 2.6 s, once per boot, on lv_layer_top; it swallows taps and deletes itself.
 *
 * Cost on the GPU-less renderer: the disc itself (grooves, its rainbow sheen, the hub) is one 360 px picture made once
 * and never redrawn - a real CD's reflections stay with the light, not the disc, so only what is printed on it turns:
 * two marks on the hub and the 36 coloured arcs of its rim. Those are small areas; nothing is scaled, masked or
 * blurred. The word unrolls inside a plain rectangular clip. */
#include "screens.h"
#include "theme.h"
#include "theme_fonts.h"
#include "theme_kit.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_MS   (th_zune() ? 3800 : 2600)   /* the whole splash (Zune: the mosaic - zune50: held longer so the covers are seen) */
static lv_obj_t *g_zgrid; static void mosaic_build(void);
#define CDR     178             /* the disc's radius */
#define HUBR    54              /* its clear centre */
#define NRIM    36              /* coloured arcs round the rim */

static struct {
    lv_obj_t *root, *cd, *rim[NRIM], *mark, *hubring, *banner, *dl, *clip, *rest;
    int frames, dw, ww, early;   /* early: built and drawn by splash_early(), not animating yet */         /* the D's width; the whole word's */
    uint32_t t0;
} g_sp;
static uint8_t *g_px;           /* the disc, XRGB8888, made once */
static lv_image_dsc_t g_dsc;
static lv_obj_t *g_cover;

static float clampf(float v){ return v < 0 ? 0 : v > 1 ? 1 : v; }
static float seg(float t, float a, float b){ return clampf((t - a) / (b - a)); }
static float smooth(float t){ t = clampf(t); return t * t * (3 - 2 * t); }
static float ease(float t){ t = clampf(t); float r = 1 - t; return 1 - r * r * r; }
static void rainbow(float h, float *r, float *g, float *b){   /* h 0..1 round the colour wheel */
    h -= floorf(h);
    *r = clampf(fabsf(h * 6 - 3) - 1); *g = clampf(2 - fabsf(h * 6 - 2)); *b = clampf(2 - fabsf(h * 6 - 4));
}
static uint32_t rainbow_rgb(float h){
    float r, g, b; rainbow(h, &r, &g, &b);
    return ((uint32_t)(255 * (0.4f + 0.6f * r)) << 16) | ((uint32_t)(255 * (0.4f + 0.6f * g)) << 8) | (uint32_t)(255 * (0.4f + 0.6f * b));
}

/* ---- the disc, once ---- */
static int disc_make(uint32_t bg){
    g_px = malloc(360 * 360 * 4);
    if(!g_px) return 0;
    float bb = (float)(bg & 255), bgc = (float)((bg >> 8) & 255), br = (float)((bg >> 16) & 255);
    for(int j = 0; j < 360; j++) for(int i = 0; i < 360; i++){
        uint8_t *o = g_px + ((size_t)j * 360 + i) * 4;
        float dx = i + 0.5f - 180, dy = j + 0.5f - 180, d = sqrtf(dx * dx + dy * dy);
        float c[3] = { bb, bgc, br };                                          /* B, G, R: the room */
        if(d < CDR + 0.5f){
            float a = atan2f(dy, dx), base;
            if(d < 15){ /* the hole */ }
            else if(d < HUBR){                                                 /* clear plastic, two moulded rings */
                base = 26 + (fabsf(d - 30) < 1 || fabsf(d - 44) < 1 ? 22 : 0);
                c[0] = base + 4; c[1] = base + 2; c[2] = base;
            } else if(d < HUBR + 8){                                           /* the mirror band */
                base = 70 + 40 * (0.5f + 0.5f * cosf(a * 2 + 0.8f));
                c[0] = base + 6; c[1] = base + 3; c[2] = base;
            } else {                                                           /* the data side */
                float k = (d - HUBR - 8) / (CDR - HUBR - 8);
                base = 46 - 26 * k + 2 * sinf(d * 1.7f);                       /* darker outwards, fine grooves */
                c[0] = base + 4; c[1] = base + 2; c[2] = base;
                /* two rainbow reflections, fixed to the light (up-left and down-right), colour changing with radius */
                for(int w = 0; w < 2; w++){
                    float da = a - (-2.2f + w * 3.14159f);
                    while(da > 3.14159f) da -= 6.28318f;
                    while(da < -3.14159f) da += 6.28318f;
                    float s = expf(-da * da / 0.09f) * (0.35f + 0.65f * k);
                    if(s < 0.01f) continue;
                    float r, g, b; rainbow(k * 0.85f + w * 0.1f, &r, &g, &b);
                    c[2] += (r * 255 - c[2]) * s * 0.55f; c[1] += (g * 255 - c[1]) * s * 0.55f; c[0] += (b * 255 - c[0]) * s * 0.55f;
                }
                float sh = expf(-(a + 0.9f) * (a + 0.9f) / 0.02f) * 0.25f * k;   /* a thin white glint */
                for(int q = 0; q < 3; q++) c[q] += (255 - c[q]) * sh;
            }
            float cov = clampf(CDR + 0.5f - d);                                /* antialiased edge */
            c[0] = bb + (c[0] - bb) * cov; c[1] = bgc + (c[1] - bgc) * cov; c[2] = br + (c[2] - br) * cov;
        }
        for(int q = 0; q < 3; q++) o[q] = (uint8_t)(c[q] < 0 ? 0 : c[q] > 255 ? 255 : c[q]);
        o[3] = 255;
    }
    memset(&g_dsc, 0, sizeof g_dsc);
    g_dsc.header.magic = LV_IMAGE_HEADER_MAGIC; g_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    g_dsc.header.w = g_dsc.header.h = 360; g_dsc.header.stride = 360 * 4;
    g_dsc.data = g_px; g_dsc.data_size = 360 * 360 * 4;
    return 1;
}

/* ---- the timeline ---- */
static void zframe(float t);
static void zdone(void);
static void splash_frame(int ms){
    float t = ms / (float)SP_MS;
    if(th_zune()){ zframe(t); return; }
    /* spin: up to speed by 0.42, then slowing to a stop at 0.64 (the angle is the integral of the speed) */
    float ang;
    if(t < 0.42f){ float u = t / 0.42f; ang = 2900 * u * u * u; }                  /* degrees, accelerating */
    else { float u = seg(t, 0.42f, 0.64f); ang = 2900 + 1100 * (1 - (1 - u) * (1 - u) * (1 - u)); }
    float speed = t < 0.42f ? t / 0.42f : 1 - seg(t, 0.42f, 0.64f);

    lv_obj_set_style_opa(g_sp.cd, (lv_opa_t)(255 * smooth(seg(t, 0, 0.12f))), 0);
    int a0 = ((int)ang % 360 + 360) % 360;
    for(int i = 0; i < NRIM; i++){
        int s = (a0 + i * 360 / NRIM) % 360;
        lv_arc_set_bg_angles(g_sp.rim[i], s, s + 360 / NRIM);
    }
    lv_arc_set_rotation(g_sp.mark, a0);
    lv_obj_set_style_opa(g_sp.mark, (lv_opa_t)(220 * (1 - speed * 0.85f) * (1 - seg(t, 0.56f, 0.62f))), 0);   /* blurs away at speed */

    /* the hub becomes the D, an accent ring drawn round it */
    float d = smooth(seg(t, 0.58f, 0.66f)), ring = ease(seg(t, 0.6f, 0.7f));
    lv_arc_set_bg_angles(g_sp.hubring, 270, 270 + (int)(360 * ring));
    lv_obj_set_style_opa(g_sp.hubring, (lv_opa_t)(255 * (1 - seg(t, 0.71f, 0.75f))), 0);
    /* then it rolls out: the D moves to where the word starts, "isco!" unrolls behind it on a growing dark pill */
    float roll = smooth(seg(t, 0.71f, 0.83f));
    int x0 = 180 - g_sp.dw / 2, x1 = 180 - g_sp.ww / 2;
    int dx = x0 + (int)((x1 - x0) * roll);
    lv_obj_set_x(g_sp.dl, dx);
    lv_obj_set_style_opa(g_sp.dl, (lv_opa_t)(255 * d), 0);
    lv_obj_set_style_transform_rotation(g_sp.dl, roll > 0 && roll < 1 ? (int)(-3600 * (1 - roll)) : 0, 0);   /* it rolls as it goes */
    int open = (int)((g_sp.ww - g_sp.dw) * roll);
    lv_obj_set_x(g_sp.clip, dx + g_sp.dw); lv_obj_set_width(g_sp.clip, open > 0 ? open : 1);
    if(open > 0) lv_obj_remove_flag(g_sp.clip, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_sp.clip, LV_OBJ_FLAG_HIDDEN);
    int bw = 2 * HUBR + 8 + (int)((g_sp.ww + 40 - 2 * HUBR - 8) * roll);
    lv_obj_set_x(g_sp.banner, 180 - bw / 2); lv_obj_set_width(g_sp.banner, bw);
    lv_obj_set_style_bg_opa(g_sp.banner, (lv_opa_t)(230 * smooth(seg(t, 0.58f, 0.66f))), 0);

    /* hand-over to Home */
    lv_obj_set_style_opa(g_sp.root, (lv_opa_t)(255 * (1 - seg(t, 0.9f, 1))), 0);   /* after a moment on the name */
}

static void splash_anim_cb(void *var, int32_t v){ (void)var; if(g_sp.root) splash_frame((int)v); }
static void splash_drawn_cb(lv_event_t *e){ (void)e; g_sp.frames++; }   /* a frame actually drawn to the screen */
static void splash_done(lv_anim_t *a){
    (void)a;
    lv_display_remove_event_cb_with_user_data(lv_display_get_default(), splash_drawn_cb, NULL);
    fprintf(stderr, "splash: %d frames drawn in %u ms\n", g_sp.frames, (unsigned)lv_tick_elaps(g_sp.t0));   /* how smoothly this Disc drew it */
    if(th_zune()) zdone();
    if(g_sp.root) lv_obj_delete(g_sp.root);
    memset(&g_sp, 0, sizeof g_sp);
    free(g_px); g_px = NULL;
}
static lv_obj_t *plain(lv_obj_t *parent){
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o); kit_keep(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *bare_arc(lv_obj_t *parent, int r, int w, lv_color_t c, lv_opa_t opa){
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_style_all(a); kit_keep(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(a, 2 * r, 2 * r); lv_obj_set_pos(a, 180 - r, 180 - r);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN); lv_obj_set_style_arc_color(a, c, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_MAIN); lv_obj_set_style_arc_rounded(a, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    return a;
}
static lv_obj_t *word_label(lv_obj_t *parent, const char *s, lv_color_t c){
    lv_obj_t *l = lv_label_create(parent); kit_keep(l);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, &diskos_splash_disco_72, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}
static int zbuild(void);
static int build(void){
    if(th_zune()) return zbuild();
    if(!disc_make((uint32_t)lv_color_to_u32(TC(FIXED_SPLASH_BG)) & 0xFFFFFF)) return 0;
    g_sp.root = plain(lv_layer_top());
    lv_obj_set_size(g_sp.root, 360, 360);
    lv_obj_set_style_bg_color(g_sp.root, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.root, LV_OBJ_FLAG_CLICKABLE);             /* swallow taps during the splash */

    g_sp.cd = lv_image_create(g_sp.root); kit_keep(g_sp.cd);
    lv_image_set_src(g_sp.cd, &g_dsc); lv_obj_set_pos(g_sp.cd, 0, 0);
    for(int i = 0; i < NRIM; i++)                                   /* the turning rainbow rim */
        g_sp.rim[i] = bare_arc(g_sp.cd, CDR - 1, 4, theme_color_from_rgb(rainbow_rgb((float)i / NRIM)), 130);
    g_sp.mark = bare_arc(g_sp.cd, 42, 4, TC(FIXED_SPLASH_3), LV_OPA_COVER);   /* two marks on the hub, opposite */
    lv_arc_set_bg_angles(g_sp.mark, 0, 14);
    lv_obj_set_style_arc_opa(g_sp.mark, LV_OPA_COVER, LV_PART_INDICATOR); lv_obj_set_style_arc_color(g_sp.mark, TC(FIXED_SPLASH_3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_sp.mark, 4, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_sp.mark, 0, LV_PART_INDICATOR);
    lv_arc_set_range(g_sp.mark, 0, 360); lv_arc_set_bg_angles(g_sp.mark, 0, 14);
    lv_arc_set_angles(g_sp.mark, 180, 194);

    g_sp.banner = plain(g_sp.root);                                 /* the dark pill the word sits on */
    lv_obj_set_style_radius(g_sp.banner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_sp.banner, TC(FIXED_SPLASH_BG), 0);
    lv_obj_set_y(g_sp.banner, 180 - HUBR - 4); lv_obj_set_height(g_sp.banner, 2 * HUBR + 8);
    g_sp.hubring = bare_arc(g_sp.root, HUBR + 4, 3, ui_current_accent(), LV_OPA_COVER);
    lv_arc_set_bg_angles(g_sp.hubring, 270, 270);

    /* the word: the D on its own, and "isco!" inside a clip that widens as it unrolls */
    g_sp.dl = word_label(g_sp.root, "D", TC(FIXED_SPLASH_3));
    lv_obj_update_layout(g_sp.dl);
    g_sp.dw = lv_obj_get_width(g_sp.dl);
    int lh = lv_obj_get_height(g_sp.dl);
    lv_obj_set_y(g_sp.dl, 180 - lh / 2);
    lv_obj_set_style_transform_pivot_x(g_sp.dl, g_sp.dw / 2, 0); lv_obj_set_style_transform_pivot_y(g_sp.dl, lh / 2, 0);
    g_sp.clip = plain(g_sp.root);
    lv_obj_set_y(g_sp.clip, 180 - lh / 2); lv_obj_set_height(g_sp.clip, lh);
    g_sp.rest = plain(g_sp.clip);
    lv_obj_set_size(g_sp.rest, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g_sp.rest, LV_FLEX_FLOW_ROW);
    word_label(g_sp.rest, "isco", TC(FIXED_SPLASH_3));
    word_label(g_sp.rest, "!", ui_current_accent());
    lv_obj_update_layout(g_sp.rest);
    g_sp.ww = g_sp.dw + lv_obj_get_width(g_sp.rest);
    lv_obj_add_flag(g_sp.clip, LV_OBJ_FLAG_HIDDEN);
    return 1;
}

/* A plain cover in the splash's colour, put up before the first frame is drawn: Home is built and painted during
 * startup, well before the splash can start (the loop isn't running yet), and showed for a moment without it. */
void splash_cover(void){
    if(g_cover || g_sp.root) return;
    g_cover = plain(lv_layer_top());
    lv_obj_set_size(g_cover, 360, 360);
    lv_obj_set_style_bg_color(g_cover, TC(FIXED_SPLASH_BG), 0); lv_obj_set_style_bg_opa(g_cover, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_cover, LV_OBJ_FLAG_CLICKABLE);
}
static void cover_off(void){ if(g_cover){ lv_obj_delete(g_cover); g_cover = NULL; } }
void splash_start(void){
    if(g_sp.root && !g_sp.early){ cover_off(); return; }           /* one splash at a time */
    if(!g_sp.root && !build()){ free(g_px); g_px = NULL; cover_off(); return; }   /* no memory for the disc: straight to Home */
    if(g_sp.early && th_zune() && !g_zgrid) mosaic_build();
    g_sp.early = 0;
    cover_off();                                                    /* the splash's own black takes over from here */
    g_sp.t0 = lv_tick_get();
    lv_display_add_event_cb(lv_display_get_default(), splash_drawn_cb, LV_EVENT_RENDER_READY, NULL);
    splash_frame(0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_sp.root);
    lv_anim_set_values(&a, 0, SP_MS);
    lv_anim_set_duration(&a, SP_MS);
    lv_anim_set_exec_cb(&a, splash_anim_cb);
    lv_anim_set_completed_cb(&a, splash_done);
    lv_anim_start(&a);
}
/* zune49: the splash's first pose, drawn the moment the screen is ready - before the slow start-up work (the screens,
 * the library, the player connection) - so the glass isn't black for those seconds. splash_start() animates on from it. */
void splash_early(void){
    if(!th_zune() || g_sp.root) return;
    g_sp.early = 1;
    if(!build()){ g_sp.early = 0; return; }
    splash_frame(0);
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
}
/* host renders: the splash standing at `ms` (no animation, nothing deleted) */
void splash_test_frame(int ms){
    if(!g_sp.root && !build()) return;
    splash_frame(ms);
}

/* ==== Zune boot splash (zune49): mosaic. "disc" stands dim on black with the rim's hairline from the first frame
 * (splash_early puts that up before the slow start-up work); then covers from the card - the albums played last,
 * then the newest - drop into a 5 x 5 grid behind it in a scattered order while the name comes up to white and the
 * accent fills the rim, and it all fades to Home. ==== */
LV_FONT_DECLARE(font_zune_light_64) LV_FONT_DECLARE(font_zune_semibold_11) LV_FONT_DECLARE(font_zune_regular_12)
const lv_font_t *zune_font(const lv_font_t *base);
#include "zune_thumbs.h"
#include "musicdb.h"
#define ZM_T   70                 /* a tile */
#define ZM_G   4                  /* the gap */
#define ZM_N   25
static lv_obj_t *g_zword, *g_zdot, *g_zsub, *g_zload, *g_ztrack, *g_ztile[ZM_N];
static int g_zwx, g_zwy;
static int zm_seen(char (*seen)[48], int n, const char *album){ for(int k = 0; k < n; k++) if(!strncmp(seen[k], album, 47)) return 1; return 0; }
static void mosaic_build(void){
    int ids[ZM_N], n = 0; char seen[ZM_N][48];
    int cap = 200; mdb_song_t *r = malloc(sizeof *r * (size_t)cap);       /* played last: their covers are likely cached */
    if(r){ int k = mdb_recent(r, cap);
           for(int i = 0; i < k && n < ZM_N; i++) if(r[i].album[0] && !zm_seen(seen, n, r[i].album)){ snprintf(seen[n], 48, "%.47s", r[i].album); ids[n++] = r[i].id; }
           free(r); }
    for(int i = mdb_song_count() - 1; i >= 0 && n < ZM_N; i--){          /* then the newest on the card */
        const mdb_song_t *so = mdb_song(i);
        if(so && so->album[0] && !zm_seen(seen, n, so->album)){ snprintf(seen[n], 48, "%.47s", so->album); ids[n++] = so->id; }
    }
    g_zgrid = plain(g_sp.root); lv_obj_set_size(g_zgrid, 360, 360); lv_obj_set_pos(g_zgrid, 0, 0);
    const int X0 = (360 - (5 * ZM_T + 4 * ZM_G)) / 2;
    for(int k = 0; k < ZM_N; k++){
        lv_obj_t *t = plain(g_zgrid);
        lv_obj_set_size(t, ZM_T, ZM_T); lv_obj_set_pos(t, X0 + (k % 5) * (ZM_T + ZM_G), X0 + (k / 5) * (ZM_T + ZM_G));
        lv_obj_set_style_bg_color(t, TC(SURFACE), 0); lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        if(getenv("ZSPLASH_FAKE")){ static const uint32_t F[6] = { 0x3a5a78, 0x8a4b2f, 0x2f6b4f, 0x6b2f5f, 0x9a8a3a, 0x40406a };   /* host renders: stand-ins */
            lv_obj_set_style_bg_color(t, theme_color_from_rgb(F[(k * 5 + k / 5) % 6]), 0); }
        if(n){ lv_obj_t *img = lv_image_create(t); lv_obj_set_size(img, ZM_T, ZM_T); lv_obj_center(img);
               lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); zthumb_bind(img, ids[(k * 7) % n], ZM_T); }
        lv_obj_set_style_opa(t, 0, 0);
        g_ztile[k] = t;
    }
    lv_obj_t *dim = plain(g_zgrid); lv_obj_set_size(dim, 360, 360);           /* the name stays readable on top */
    lv_obj_set_style_bg_color(dim, TC(CANVAS), 0); lv_obj_set_style_bg_opa(dim, 150, 0);
    lv_obj_move_to_index(g_zgrid, 0);
}
static int zbuild(void){
    g_sp.root = plain(lv_layer_top());
    lv_obj_set_size(g_sp.root, 360, 360);
    lv_obj_set_style_bg_color(g_sp.root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_sp.root, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_sp.root, LV_OBJ_FLAG_CLICKABLE);
    if(!g_sp.early) mosaic_build();                  /* early: the library isn't loaded yet - splash_start adds it */
    g_ztrack = bare_arc(g_sp.root, 176, 2, TC(CONTROL_TRACK), LV_OPA_COVER);
    lv_arc_set_bg_angles(g_ztrack, 0, 360);
    g_zload = bare_arc(g_sp.root, 176, 3, ui_current_accent(), LV_OPA_COVER);
    lv_obj_set_style_arc_rounded(g_zload, true, LV_PART_MAIN);
    lv_arc_set_bg_angles(g_zload, 270, 271);
    g_zword = lv_label_create(g_sp.root); kit_keep(g_zword);
    lv_label_set_text(g_zword, "disc");
    lv_obj_set_style_text_font(g_zword, &font_zune_light_64, 0); lv_obj_set_style_text_color(g_zword, TC(TEXT_PRIMARY), 0);
    lv_obj_update_layout(g_zword);
    int w = lv_obj_get_width(g_zword), h = lv_obj_get_height(g_zword);
    g_zwx = 180 - (w + 14) / 2; g_zwy = 174 - h / 2 - 8;
    lv_obj_set_pos(g_zword, g_zwx, g_zwy);
    g_zdot = plain(g_sp.root);
    lv_obj_set_size(g_zdot, 10, 10); lv_obj_set_style_radius(g_zdot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_zdot, ui_current_accent(), 0); lv_obj_set_style_bg_opa(g_zdot, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_zdot, g_zwx + w + 4, g_zwy + h - 18);
    g_zsub = lv_label_create(g_sp.root); kit_keep(g_zsub);
    lv_label_set_text(g_zsub, "snowsky");
    lv_obj_set_style_text_font(g_zsub, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(g_zsub, 3, 0);
    lv_obj_set_style_text_color(g_zsub, TC(TEXT_MUTED), 0);
    lv_obj_align(g_zsub, LV_ALIGN_TOP_MID, 0, 222);
    return 1;
}
static void zframe(float t){
    /* 3.8 s: covers drop in over the first ~2 s (~70 ms apart), the name comes up to white early, the grid holds
     * complete for about a second, and only the last 0.3 s fades to Home. "snowsky" is only a brief caption. */
    for(int k = 0; k < ZM_N; k++) if(g_ztile[k]){
        int ord = (k * 11 + 3) % ZM_N;
        lv_obj_set_style_opa(g_ztile[k], (lv_opa_t)(255 * smooth(seg(t, 0.03f + ord * 0.019f, 0.09f + ord * 0.019f))), 0);
    }
    lv_opa_t w = (lv_opa_t)(90 + 165 * smooth(seg(t, 0.08f, 0.22f)));
    lv_obj_set_style_opa(g_zword, w, 0); lv_obj_set_style_opa(g_zdot, w, 0);
    lv_obj_set_style_opa(g_zsub, (lv_opa_t)(255 * smooth(seg(t, 0.12f, 0.2f)) * (1 - smooth(seg(t, 0.36f, 0.44f)))), 0);
    lv_obj_set_style_opa(g_zload, t < 0.02f ? 0 : LV_OPA_COVER, 0);            /* no 1-degree speck before it moves */
    lv_arc_set_bg_angles(g_zload, 270, 271 + (int)(358 * ease(seg(t, 0.03f, 0.75f))));   /* 629 at most: 630 would wrap to an empty arc */
    lv_obj_set_style_opa(g_sp.root, (lv_opa_t)(255 * (1 - seg(t, 0.92f, 1))), 0);
}
static void zdone(void){
    g_zword = g_zdot = g_zsub = g_zload = g_ztrack = g_zgrid = NULL;
    memset(g_ztile, 0, sizeof g_ztile);
}

/* ==== Zune power-off (zune49): the boot run backwards - the name, "shutting down" under it, and the rim's accent
 * ring draining anticlockwise while the name dims. Built on the shutdown overlay `o`. ==== */
static lv_obj_t *g_zoff_arc, *g_zoff_word, *g_zoff_dot;
static void zoff_exec(void *v, int32_t x){
    (void)v; if(!g_zoff_arc) return;
    lv_arc_set_bg_angles(g_zoff_arc, 270, 270 + (x > 1 ? x : 1));
    lv_opa_t o = (lv_opa_t)(90 + 165 * x / 360);
    if(g_zoff_word) lv_obj_set_style_opa(g_zoff_word, o, 0);
    if(g_zoff_dot) lv_obj_set_style_opa(g_zoff_dot, o, 0);
}
static void zoff_del(lv_event_t *e){ (void)e; lv_anim_delete(NULL, zoff_exec); g_zoff_arc = g_zoff_word = g_zoff_dot = NULL; }
void zune_shutdown_build(lv_obj_t *o){
    lv_obj_t *tr = bare_arc(o, 176, 2, TC(CONTROL_TRACK), LV_OPA_COVER); lv_arc_set_bg_angles(tr, 0, 360);
    g_zoff_arc = bare_arc(o, 176, 3, ui_current_accent(), LV_OPA_COVER);
    lv_obj_set_style_arc_rounded(g_zoff_arc, true, LV_PART_MAIN);
    lv_arc_set_bg_angles(g_zoff_arc, 270, 270 + 359);
    lv_obj_t *w = lv_label_create(o); kit_keep(w); lv_label_set_text(w, "disc");
    lv_obj_set_style_text_font(w, &font_zune_light_64, 0); lv_obj_set_style_text_color(w, TC(TEXT_PRIMARY), 0);
    lv_obj_update_layout(w);
    int ww = lv_obj_get_width(w), wh = lv_obj_get_height(w), x = 180 - (ww + 14) / 2, y = 174 - wh / 2 - 8;
    lv_obj_set_pos(w, x, y); g_zoff_word = w;
    lv_obj_t *d = plain(o); lv_obj_set_size(d, 10, 10); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, ui_current_accent(), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_pos(d, x + ww + 4, y + wh - 18); g_zoff_dot = d;
    lv_obj_t *s = lv_label_create(o); kit_keep(s); lv_label_set_text(s, "shutting down");
    lv_obj_set_style_text_font(s, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(s, 3, 0);
    lv_obj_set_style_text_color(s, TC(TEXT_MUTED), 0); lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 222);
    lv_obj_t *k = lv_label_create(o); kit_keep(k); lv_label_set_text(k, "your place is saved");
    lv_obj_set_style_text_font(k, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(k, TC(TEXT_MUTED), 0);
    lv_obj_align(k, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_add_event_cb(o, zoff_del, LV_EVENT_DELETE, NULL);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_zoff_arc); lv_anim_set_exec_cb(&a, zoff_exec);
    lv_anim_set_values(&a, 359, 0); lv_anim_set_duration(&a, 4000); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
void zune_shutdown_test(int deg){ lv_anim_delete(NULL, zoff_exec); zoff_exec(NULL, deg); }   /* host renders */
