/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zune library covers - see zune_thumbs.h. One worker, a small request stack (newest first: the rows just scrolled
 * into view), one finished result handed over at a time, the same SD lease / decoder lock rules as the Album Wall. */
#include "zune_thumbs.h"
#include "screens.h"
#include "sdio.h"
#include "musicdb.h"
#include "artcache.h"
#include "art.h"
#include "theme.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ZT_SRC   148          /* the art cache's cover size */
#define ZT_CACHE 48           /* finished tiles kept for scrolling back */
#define ZT_REQ   96           /* queued requests (oldest dropped) */
#define ZT_BIND  160          /* widgets waiting for a cover */

void ui_decode_lock(void); void ui_decode_unlock(void); int ui_art_foreground_busy(void);

typedef struct { int id, px, none; uint8_t *buf; uint32_t used; } zt_entry_t;     /* buf: px*px RGB565 */
static zt_entry_t g_cache[ZT_CACHE];
static uint32_t g_use;
typedef struct { lv_obj_t *img; int id, px; } zt_bind_t;
static zt_bind_t g_bind[ZT_BIND];

typedef struct { int id, px; unsigned epoch; } zt_req_t;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static zt_req_t g_req[ZT_REQ]; static int g_nreq;
static struct { zt_req_t r; uint8_t *buf; int ok, ready; } g_done;
static atomic_uint g_epoch = 1;
static int g_started;
static lv_timer_t *g_poll;

/* ---- worker -------------------------------------------------------------------------------------------- */
static int read_bmp(const char *path, uint8_t *rgb){            /* 148x148 24-bit bottom-up -> top-down RGB */
    FILE *f = fopen(path, "rb"); if(!f) return -1;
    uint8_t h[54]; if(fread(h, 1, 54, f) != 54){ fclose(f); return -1; }
    uint32_t off = h[10] | (h[11] << 8) | (h[12] << 16) | ((uint32_t)h[13] << 24);
    int w = h[18] | (h[19] << 8), hh = h[22] | (h[23] << 8), bpp = h[28] | (h[29] << 8);
    if(w != ZT_SRC || hh != ZT_SRC || bpp != 24 || fseek(f, off, SEEK_SET)){ fclose(f); return -1; }
    for(int y = 0; y < ZT_SRC; y++){
        uint8_t *d = rgb + (ZT_SRC - 1 - y) * ZT_SRC * 3;
        if(fread(d, 1, ZT_SRC * 3, f) != ZT_SRC * 3){ fclose(f); return -1; }   /* BGR */
    }
    fclose(f); return 0;
}
static void shrink(const uint8_t *bgr, uint8_t *out, int px){    /* box filter 148 -> px, to RGB565 */
    uint16_t *o = (uint16_t *)out;
    for(int y = 0; y < px; y++){
        int y0 = y * ZT_SRC / px, y1 = (y + 1) * ZT_SRC / px; if(y1 <= y0) y1 = y0 + 1;
        for(int x = 0; x < px; x++){
            int x0 = x * ZT_SRC / px, x1 = (x + 1) * ZT_SRC / px; if(x1 <= x0) x1 = x0 + 1;
            unsigned b = 0, g = 0, r = 0, n = 0;
            for(int yy = y0; yy < y1; yy++) for(int xx = x0; xx < x1; xx++){
                const uint8_t *p = bgr + (yy * ZT_SRC + xx) * 3; b += p[0]; g += p[1]; r += p[2]; n++; }
            r /= n; g /= n; b /= n;
            o[y * px + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}
static int cur(const zt_req_t *r){ return atomic_load(&g_epoch) == r->epoch; }
static int load_one(const zt_req_t *r, uint8_t *bgr){
    char path[512], cover[600];
    if(!mdb_song_path(r->id, path, sizeof path) || !path[0]) return 0;
    if(!sd_io_begin()) return -1;                                   /* the cache lives on the card: read it under a lease */
    int hit = artcache_cover_path(path, cover, sizeof cover) == 0 && read_bmp(cover, bgr) == 0;
    sd_io_end();
    if(hit) return 1;
    if(ui_art_foreground_busy() || !cur(r)) return -1;              /* busy now: ask again later, don't remember "none" */
    char dir[] = "/tmp/.diskos-zt-XXXXXX"; int ok = -1;              /* not cached yet: decode it (and cache it) */
    if(!mkdtemp(dir)) return -1;
    char c[128], t[128], b[128], fp[24];
    snprintf(c, sizeof c, "%s/c.bmp", dir); snprintf(t, sizeof t, "%s/t.bmp", dir); snprintf(b, sizeof b, "%s/b.bmp", dir);
    if(sd_io_begin()){
        int keyed = artcache_key(path, fp, sizeof fp) == 0;
        ui_decode_lock();
        int rc = (cur(r) && !ui_art_foreground_busy()) ? art_make_all_ex_gen(path, c, t, b, 1, art_cancel_gen()) : -2;
        ui_decode_unlock();
        if(!rc){ ok = read_bmp(c, bgr) == 0; if(keyed) artcache_put_if(path, fp, c, t, b); }
        else if(rc != -2) ok = 0;                                      /* the track has no usable cover */
        sd_io_end();
    }
    unlink(c); unlink(t); unlink(b); rmdir(dir);
    return ok;
}
static void *worker(void *u){
    (void)u; nice(15);
    uint8_t *bgr = malloc(ZT_SRC * ZT_SRC * 3);
    for(;;){
        pthread_mutex_lock(&g_mu);
        while(!g_nreq || g_done.ready) pthread_cond_wait(&g_cv, &g_mu);
        zt_req_t r = g_req[--g_nreq];                                  /* newest first */
        pthread_mutex_unlock(&g_mu);
        if(!cur(&r)) continue;
        uint8_t *out = NULL; int ok = bgr ? load_one(&r, bgr) : -1;
        if(ok == 1){ out = malloc((size_t)r.px * r.px * 2); if(out) shrink(bgr, out, r.px); else ok = -1; }
        pthread_mutex_lock(&g_mu);
        g_done.r = r; g_done.buf = out; g_done.ok = ok; g_done.ready = 1;
        pthread_mutex_unlock(&g_mu);
        usleep(ok < 0 ? 150000 : 20000);                               /* leave room for playback (more while art is busy) */
    }
    return NULL;
}

/* ---- UI side ------------------------------------------------------------------------------------------- */
typedef struct { lv_image_dsc_t dsc; uint8_t px[]; } zt_pix_t;
static void img_del(lv_event_t *e){
    zt_pix_t *p = lv_event_get_user_data(e);
    for(int k = 0; k < ZT_BIND; k++) if(g_bind[k].img == lv_event_get_target(e)) g_bind[k].img = NULL;
    if(p){ lv_image_cache_drop(&p->dsc); free(p); }
}
static void unbind_cb(lv_event_t *e){
    for(int k = 0; k < ZT_BIND; k++) if(g_bind[k].img == lv_event_get_target(e)) g_bind[k].img = NULL;
}
static void apply(lv_obj_t *img, const zt_entry_t *c){
    size_t n = (size_t)c->px * c->px * 2;
    zt_pix_t *p = malloc(sizeof *p + n); if(!p) return;
    memcpy(p->px, c->buf, n);
    memset(&p->dsc, 0, sizeof p->dsc);
    p->dsc.header.magic = LV_IMAGE_HEADER_MAGIC; p->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    p->dsc.header.w = c->px; p->dsc.header.h = c->px; p->dsc.header.stride = c->px * 2;
    p->dsc.data = p->px; p->dsc.data_size = (uint32_t)n;
    lv_image_set_src(img, &p->dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(img, img_del, LV_EVENT_DELETE, p);
}
static zt_entry_t *find(int id, int px){
    for(int k = 0; k < ZT_CACHE; k++) if(g_cache[k].id == id && g_cache[k].px == px && (g_cache[k].buf || g_cache[k].none)){ g_cache[k].used = ++g_use; return &g_cache[k]; }
    return NULL;
}
static void store(int id, int px, uint8_t *buf){
    int v = 0;
    for(int k = 1; k < ZT_CACHE; k++) if(g_cache[k].used < g_cache[v].used) v = k;
    free(g_cache[v].buf);
    g_cache[v] = (zt_entry_t){ .id = id, .px = px, .none = !buf, .buf = buf, .used = ++g_use };
}
static void poll_cb(lv_timer_t *t){
    (void)t;
    pthread_mutex_lock(&g_mu);
    if(!g_done.ready){ pthread_mutex_unlock(&g_mu); return; }
    zt_req_t r = g_done.r; uint8_t *buf = g_done.buf; int ok = g_done.ok;
    g_done.ready = 0; g_done.buf = NULL; pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    if(!cur(&r)){ free(buf); return; }                                 /* stale */
    if(ok < 0){                                                        /* busy just now: ask again, after the rest */
        free(buf);
        int want = 0; for(int k = 0; k < ZT_BIND; k++) if(g_bind[k].img && g_bind[k].id == r.id && g_bind[k].px == r.px) want = 1;
        pthread_mutex_lock(&g_mu);
        if(want && g_nreq < ZT_REQ){ memmove(g_req + 1, g_req, (size_t)g_nreq * sizeof g_req[0]); g_req[0] = r; g_nreq++; pthread_cond_broadcast(&g_cv); }
        pthread_mutex_unlock(&g_mu);
        return;
    }
    store(r.id, r.px, ok == 1 ? buf : NULL);
    zt_entry_t *c = find(r.id, r.px);
    for(int k = 0; k < ZT_BIND; k++){
        if(!g_bind[k].img || g_bind[k].id != r.id || g_bind[k].px != r.px) continue;
        lv_obj_t *img = g_bind[k].img; g_bind[k].img = NULL;
        lv_obj_remove_event_cb(img, unbind_cb);
        if(c && c->buf) apply(img, c);
    }
}
void zthumb_cancel(void){
    atomic_fetch_add(&g_epoch, 1);
    pthread_mutex_lock(&g_mu); g_nreq = 0; pthread_mutex_unlock(&g_mu);
}
void zthumb_bind(lv_obj_t *img, int song_id, int px){
    if(!img || song_id <= 0 || px <= 0 || px > ZT_SRC) return;
    zt_entry_t *c = find(song_id, px);
    if(c){ if(c->buf) apply(img, c); return; }
    if(!g_started){
        pthread_t th; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&a, 128 * 1024);
        if(pthread_create(&th, &a, worker, NULL) == 0) g_started = 1;
        pthread_attr_destroy(&a);
        if(!g_started) return;
        g_poll = lv_timer_create(poll_cb, 30, NULL);
    }
    int slot = -1;
    for(int k = 0; k < ZT_BIND; k++) if(!g_bind[k].img){ slot = k; break; }
    if(slot < 0) return;
    g_bind[slot] = (zt_bind_t){ img, song_id, px };
    lv_obj_add_event_cb(img, unbind_cb, LV_EVENT_DELETE, NULL);
    pthread_mutex_lock(&g_mu);
    int dup = 0; for(int k = 0; k < g_nreq; k++) if(g_req[k].id == song_id && g_req[k].px == px){   /* already asked: move it to the top */
        zt_req_t q = g_req[k]; memmove(g_req + k, g_req + k + 1, (size_t)(g_nreq - k - 1) * sizeof q); g_req[g_nreq - 1] = q; dup = 1; break; }
    if(!dup){
        if(g_nreq == ZT_REQ){ memmove(g_req, g_req + 1, (ZT_REQ - 1) * sizeof g_req[0]); g_nreq--; }   /* the oldest goes */
        g_req[g_nreq++] = (zt_req_t){ song_id, px, atomic_load(&g_epoch) };
    }
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

/* the covers on screen first: their requests move to the top of the stack (top-left ends up first) */
static int vis_cmp(const void *a, const void *b){
    const lv_area_t *x = &((const struct { lv_area_t a; int k; } *)a)->a, *y = &((const struct { lv_area_t a; int k; } *)b)->a;
    if(x->y1 != y->y1) return x->y1 > y->y1 ? -1 : 1;                 /* bottom rows first ... */
    return x->x1 > y->x1 ? -1 : x->x1 < y->x1;                          /* ... right to left: pushed last = served first */
}
void zthumb_bump_visible(void){
    struct { lv_area_t a; int k; } v[32]; int n = 0;
    for(int k = 0; k < ZT_BIND && n < 32; k++){
        if(!g_bind[k].img) continue;
        lv_obj_t *p = lv_obj_get_parent(g_bind[k].img);                  /* the tile (the image stays hidden until it loads) */
        if(!p || !lv_obj_is_visible(p)) continue;
        lv_area_t a; lv_obj_get_coords(p, &a);
        if(a.y2 < 0 || a.y1 > 360 || a.x2 < 0 || a.x1 > 360) continue;
        v[n].a = a; v[n].k = k; n++;
    }
    if(!n) return;
    qsort(v, (size_t)n, sizeof v[0], vis_cmp);
    pthread_mutex_lock(&g_mu);
    unsigned ep = atomic_load(&g_epoch);
    for(int i = 0; i < n; i++){
        zt_bind_t *b = &g_bind[v[i].k];
        int at = -1; for(int j = 0; j < g_nreq; j++) if(g_req[j].id == b->id && g_req[j].px == b->px){ at = j; break; }
        zt_req_t q = { b->id, b->px, ep };
        if(at >= 0){ memmove(g_req + at, g_req + at + 1, (size_t)(g_nreq - at - 1) * sizeof q); g_nreq--; }
        else if(g_nreq == ZT_REQ){ memmove(g_req, g_req + 1, (ZT_REQ - 1) * sizeof q); g_nreq--; }
        g_req[g_nreq++] = q;
    }
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

/* ---- playlist mosaics (Figma 25/26/68): the covers of up to four different albums in a 2 x 2 ---- */
void zthumb_mosaic(lv_obj_t *parent, int x, int y, int size, const int *ids, int n){
    lv_obj_t *m = lv_obj_create(parent); lv_obj_remove_style_all(m);
    lv_obj_set_size(m, size, size); lv_obj_set_pos(m, x, y);
    lv_obj_set_style_bg_color(m, TC(SURFACE), 0); lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if(n <= 0) return;
    int half = size / 2, one = n == 1;
    for(int k = 0; k < (one ? 1 : 4); k++){
        int px = one ? size : half; if(px > ZT_SRC) px = ZT_SRC;
        lv_obj_t *t = lv_obj_create(m); lv_obj_remove_style_all(t);       /* each quarter its own tile: the loader's unit */
        lv_obj_set_size(t, one ? size : half, one ? size : half); lv_obj_set_pos(t, one ? 0 : (k & 1) * half, one ? 0 : (k >> 1) * half);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *img = lv_image_create(t);
        lv_obj_set_size(img, one ? size : half, one ? size : half); lv_obj_center(img);
        if(one && size > ZT_SRC) lv_image_set_scale(img, (uint32_t)(256 * size / ZT_SRC));
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        zthumb_bind(img, ids[k % n], px);
    }
}
int zthumb_playlist(long pid, int *ids4, int *nids, long *ms){ return mdb_playlist_cover_ids(pid, ids4, 4, nids, ms); }
