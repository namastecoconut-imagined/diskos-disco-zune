/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#ifndef MULTIPICK_H
#define MULTIPICK_H
#include "lvgl/lvgl.h"
#include <stdint.h>

/* Picking several items in a list (multi-select), shared by the Library and the folder browser.
 * Swipe sideways across a row (starting at least 72 px in from either edge, so it is never the back swipe or the
 * navigation circle): picking starts with that row ticked. While picking, a tap ticks or unticks a row (a swipe does
 * too), the title reads "3 selected", the A-Z pill gives way to Play / Queue / More, and holding a row opens More
 * (Shuffle, Add to playlist, Add to Favourites, Select all). Back, unticking the last item or finishing an action
 * leaves picking. Ticks are kept per item index, so rows recycled while scrolling keep theirs. */
typedef struct mp_host {
    lv_obj_t *root, *list, *title;          /* the screen, its scrolling list, the header title label */
    int  (*count)(void);                     /* items in the list now */
    int  (*can_pick)(void);                  /* this list allows picking */
    int  (*pickable)(int index);             /* this item can be ticked (NULL: every item) */
    int  (*collect)(const uint8_t *sel, int n, int **ids);   /* the ticked items as exact songs (SONG.IDs, malloc'd):
                                                                the one list Play, Queue, Playlist and Favourites all use;
                                                                -1 when it couldn't be built completely */
    void (*az)(int show);                    /* the list's A-Z pill: hidden while picking, back after */
    /* state (owned by multipick.c) */
    int on, cnt, cap; uint8_t *sel;
    lv_obj_t *bar, *more, *pop; char saved_title[64];
} mp_host_t;

void mp_attach(mp_host_t *h);                /* once, after root/list/title exist: builds the hidden action bar */
void mp_row(mp_host_t *h, lv_obj_t *row, int index);   /* a row was built: picking gestures, and its tick if picking */
int  mp_active(const mp_host_t *h);
void mp_start(mp_host_t *h, int index);    /* start picking with this item ticked (Zune: from the hold menu) */
void mp_exit(mp_host_t *h);                  /* stop picking (Back, a reload of the list) */
void mp_demo(mp_host_t *h, const char *csv); /* renderer: tick these indices; "more" opens the More list */
#endif
