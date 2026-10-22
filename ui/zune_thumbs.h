/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zune library covers (zune_thumbs.c): album tiles need many small covers at once, so a worker reads them from the
 * art cache (decoding a track's embedded art when it isn't cached yet), shrinks them to the tile size and hands them
 * back; the UI keeps a small cache of finished tiles. Every image widget gets its OWN copy of the pixels (freed with
 * the widget), so nothing it draws can be freed under it. */
#ifndef ZUNE_THUMBS_H
#define ZUNE_THUMBS_H
#include "lvgl/lvgl.h"
/* Put song `song_id`'s cover, `px` square, into image `img` (sized px x px by the caller). Cached: set now.
 * Not yet: queued, and set when it arrives - as long as `img` still exists. No cover at all: `img` stays as it is
 * (the caller's placeholder behind it shows). */
void zthumb_bind(lv_obj_t *img, int song_id, int px);
/* drop queued work (a new list is being built); widgets already bound simply never get theirs */
void zthumb_cancel(void);
void zthumb_bump_visible(void);   /* covers now on screen go to the front of the queue (call after scrolling) */
/* a size x size 2 x 2 mosaic of up to four covers (song ids; one cover fills it all), at x, y in parent */
void zthumb_mosaic(lv_obj_t *parent, int x, int y, int size, const int *ids, int n);
/* a playlist's songs: returns the count; ids4 gets songs of up to four different albums (for the mosaic), ms the length */
int  zthumb_playlist(long pid, int *ids4, int *nids, long *ms);
#endif
