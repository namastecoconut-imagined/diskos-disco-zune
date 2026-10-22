/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "A - Z" pill and its letter grid, shared by the Library lists and Folders. */
#ifndef AZJUMP_H
#define AZJUMP_H
#include "lvgl/lvgl.h"
typedef void (*azjump_cb_t)(char letter);       /* 'A'..'Z' or '#' */
typedef struct { lv_obj_t *btn, *grid, *wheel, *selected, *letter[27]; lv_timer_t *timeout;
    azjump_cb_t jump; int library, current, held, tracking; float angle, travel; uint32_t activity; } azjump_t;
void azjump_create(azjump_t *a, lv_obj_t *root, azjump_cb_t jump, const char *tag);
void azjump_show(azjump_t *a, int on);           /* the pill (off also closes the grid) */
void azjump_position(azjump_t *,char letter,int x,int y);
int azjump_active(void);
int azjump_owns_point(int x,int y);
int azjump_close_active(void);
void azjump_letter(azjump_t *a, char letter);   /* Zune: the letter in the jump dot */
#endif
