/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The "A - Z" pill and its letter grid, shared by the Library lists and Folders.
 * The pill opens a 5-column grid of A..Z and #; a tap on a letter jumps the list (the owner's callback) and closes the
 * grid, a tap outside closes it. Disco: a pill at the bottom of the screen (the rim's middle belongs to the navigation
 * circle); other themes: a round button on the right rim. The pill is 84 x 36 with 10 px of extra touch area around it
 * (it was 68 x 30 with none, and easy to miss). */
#include "azjump.h"
#include "screens.h"
#include "theme.h"
#include "theme_kit.h"
#include "i18n.h"
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "null_theme.h"

/* Letter wheel is allocated only while open. No per-song data or artwork. */
static azjump_t *active_wheel,*library_picker;
static const char wheel_letters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
static int wheel_mode(azjump_t *a){return a->library&&cfg_get_int("library_seeking",0)==1;}
int azjump_active(void){return active_wheel!=NULL;}
static void wheel_close(azjump_t *a,int commit){
    if(!a||!a->wheel)return;
    char letter=wheel_letters[a->current];
    if(a->timeout){lv_timer_delete(a->timeout);a->timeout=NULL;}
    lv_obj_t *ov=a->wheel;a->wheel=NULL;a->selected=NULL;active_wheel=NULL;
    a->tracking=a->held=0;lv_obj_delete(ov);
    if(commit&&a->jump)a->jump(letter);
}
int azjump_close_active(void){if(!active_wheel)return 0;wheel_close(active_wheel,0);return 1;}
int azjump_owns_point(int x,int y){
    if(active_wheel)return 1;
    azjump_t*a=library_picker;
    if(!a||!wheel_mode(a)||!lv_obj_is_visible(a->btn))return 0;
    lv_area_t box;lv_obj_get_coords(a->btn,&box);
    return x>=box.x1-8&&x<=box.x2+8&&y>=box.y1-8&&y<=box.y2+8;
}
static void wheel_paint(azjump_t*a){
    char text[2]={wheel_letters[a->current],0};lv_label_set_text(a->selected,text);
    for(int i=0;i<27;i++){
        lv_obj_set_style_bg_opa(a->letter[i],i==a->current?LV_OPA_COVER:LV_OPA_TRANSP,0);
        lv_obj_set_style_text_color(a->letter[i],i==a->current?TC(ON_ACCENT):TC(TEXT_SECONDARY),0);
    }
}
static void wheel_pointer(azjump_t*a,int begin){
    lv_indev_t*in=lv_indev_active();if(!in||!a->wheel)return;
    lv_point_t p;lv_indev_get_point(in,&p);int dx=p.x-180,dy=p.y-180;
    a->activity=lv_tick_get();
    if(dx*dx+dy*dy<80*80){a->tracking=0;return;}
    float angle=atan2f((float)dy,(float)dx);
    if(begin||!a->tracking){a->tracking=1;a->angle=angle;a->travel=0;return;}
    float delta=angle-a->angle;a->angle=angle;
    if(delta>3.14159265f)delta-=6.2831853f;
    if(delta< -3.14159265f)delta+=6.2831853f;
    /* Reject a discontinuous touch jump instead of skipping half the alphabet. */
    if(fabsf(delta)>1.2f){a->travel=0;return;}
    a->travel+=delta;const float step=6.2831853f/27;
    int old=a->current;
    while(a->travel>=step){a->current=(a->current+1)%27;a->travel-=step;}
    while(a->travel<=-step){a->current=(a->current+26)%27;a->travel+=step;}
    if(a->current!=old)wheel_paint(a);
}
static void wheel_event(lv_event_t*e){
    azjump_t*a=lv_event_get_user_data(e);lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_PRESSED){a->held=1;wheel_pointer(a,1);}
    else if(c==LV_EVENT_PRESSING)wheel_pointer(a,0);
    else if(c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST){a->held=0;a->tracking=0;a->activity=lv_tick_get();}
}
static void wheel_commit(lv_event_t*e){wheel_close(lv_event_get_user_data(e),1);}
static void wheel_timeout(lv_timer_t*t){
    azjump_t*a=lv_timer_get_user_data(t);
    if(!a->held&&lv_tick_elaps(a->activity)>=1000)wheel_close(a,1);
}
static lv_obj_t*wheel_text(lv_obj_t*p,const char*t,const lv_font_t*f,lv_color_t color){
    lv_obj_t*l=lv_label_create(p);lv_label_set_text(l,t);lv_obj_set_style_text_font(l,f,0);
    lv_obj_set_style_text_color(l,color,0);lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);kit_keep(l);return l;
}
static void wheel_open(azjump_t*a){
    if(a->wheel)return;
    azjump_close_active();lv_obj_add_flag(a->grid,LV_OBJ_FLAG_HIDDEN);
    a->wheel=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(a->wheel);
    lv_obj_set_size(a->wheel,360,360);lv_obj_set_style_bg_color(a->wheel,TC(CANVAS),0);
    lv_obj_set_style_bg_opa(a->wheel,LV_OPA_50,0);lv_obj_remove_flag(a->wheel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->wheel,LV_OBJ_FLAG_CLICKABLE);kit_keep(a->wheel);
    lv_obj_add_event_cb(a->wheel,wheel_event,LV_EVENT_ALL,a);
    /* Opaque annulus behind every letter, matching the centre control. The
     * surrounding overlay stays half-transparent so the Library remains visible. */
    lv_obj_t *ring=lv_arc_create(a->wheel);lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring,308,308);lv_obj_center(ring);
    lv_arc_set_bg_angles(ring,0,360);lv_arc_set_angles(ring,0,0);
    lv_obj_set_style_arc_width(ring,48,LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring,TC(SURFACE),LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring,LV_OPA_COVER,LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring,LV_OPA_TRANSP,LV_PART_INDICATOR);
    lv_obj_remove_flag(ring,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);kit_keep(ring);
    for(int i=0;i<27;i++){
        float angle=(-90+i*360.0f/27)*3.14159265f/180;
        char text[2]={wheel_letters[i],0};lv_obj_t*l=wheel_text(a->wheel,text,TF(UI_18),TC(TEXT_SECONDARY));a->letter[i]=l;
        lv_obj_set_size(l,28,28);lv_obj_set_pos(l,166+(int)lroundf(130*cosf(angle)),166+(int)lroundf(130*sinf(angle)));
        lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_pad_top(l,(28-lv_font_get_line_height(TF(UI_18)))/2,0);
        lv_obj_set_style_bg_color(l,TC(ACCENT_PRIMARY),0);lv_obj_set_style_radius(l,th_null()?0:LV_RADIUS_CIRCLE,0);
    }
    lv_obj_t*h=wheel_text(a->wheel,"JUMP TO LETTER",TF(UI_14),TC(TEXT_SECONDARY));lv_obj_align(h,LV_ALIGN_TOP_MID,0,99);
    lv_obj_t*hub=lv_button_create(a->wheel);lv_obj_remove_style_all(hub);lv_obj_set_size(hub,96,96);lv_obj_center(hub);
    lv_obj_set_style_radius(hub,th_null()?0:LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(hub,TC(SURFACE),0);lv_obj_set_style_bg_opa(hub,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(hub,1,0);lv_obj_set_style_border_color(hub,TC(ACCENT_PRIMARY),0);kit_keep(hub);
    lv_obj_add_event_cb(hub,wheel_commit,LV_EVENT_SHORT_CLICKED,a);lv_obj_add_flag(hub,LV_OBJ_FLAG_EVENT_BUBBLE);
    const lv_font_t*font=th_null()?null_track_font():th_simple()?TF(MUSIC_TITLE):ui_np_font(30);
    a->selected=wheel_text(hub,"A",font,TC(TEXT_PRIMARY));lv_obj_center(a->selected);
    h=wheel_text(a->wheel,"TAP TO JUMP",TF(UI_14),TC(TEXT_SECONDARY));lv_obj_align(h,LV_ALIGN_TOP_MID,0,246);
    a->activity=lv_tick_get();a->tracking=0;active_wheel=a;wheel_paint(a);
    a->timeout=lv_timer_create(wheel_timeout,100,a);
    /* Allocation failure leaves a working centre/back dismissal, never a dead overlay. */
}
void azjump_position(azjump_t*a,char letter,int x,int y){
    if(!wheel_mode(a)||!a->btn||lv_obj_has_flag(a->btn,LV_OBJ_FLAG_HIDDEN))return;
    const char*p=strchr(wheel_letters,letter);
    if(!a->wheel)a->current=p?(int)(p-wheel_letters):26;
    lv_obj_set_size(a->btn,28,28);lv_obj_set_pos(a->btn,x-14,y-14);
    lv_obj_set_style_radius(a->btn,th_null()?0:LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(a->btn,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->btn,LV_OPA_COVER,0);kit_keep(a->btn);
    lv_obj_t*l=lv_obj_get_child(a->btn,0);char text[2]={wheel_letters[a->current],0};lv_label_set_text(l,text);
    lv_obj_set_style_text_font(l,TF(UI_18),0);lv_obj_set_style_text_color(l,TC(ON_ACCENT),0);lv_obj_center(l);kit_keep(l);
}
static void wheel_button(lv_event_t*e){
    azjump_t*a=lv_event_get_user_data(e);if(!wheel_mode(a))return;
    lv_event_code_t c=lv_event_get_code(e);
    if(c==LV_EVENT_PRESSED)a->held=0;
    else if(c==LV_EVENT_LONG_PRESSED){a->held=1;wheel_open(a);wheel_pointer(a,1);}
    else if(c==LV_EVENT_PRESSING&&a->wheel)wheel_pointer(a,0);
    else if((c==LV_EVENT_RELEASED||c==LV_EVENT_PRESS_LOST)&&a->wheel){a->held=0;a->activity=lv_tick_get();a->tracking=0;}
}


static void letter_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *cell = lv_event_get_current_target(e);
    azjump_t *a = lv_obj_get_user_data(lv_obj_get_parent(cell));
    char L = (char)(intptr_t)lv_event_get_user_data(e);
    if(a){ lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN); if(a->jump) a->jump(L); }
}
static void grid_bg_cb(lv_event_t *e){                       /* a tap outside the letters closes the grid */
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) lv_obj_add_flag(lv_event_get_current_target(e), LV_OBJ_FLAG_HIDDEN);
}
static void zgrid_cb(lv_event_t *e){                        /* Zune: which tile was tapped (or none: close) */
    lv_obj_t *g = lv_event_get_current_target(e); azjump_t *a = lv_obj_get_user_data(g);
    lv_point_t p; lv_indev_get_point(lv_indev_active(), &p);
    for(uint32_t k = 0; k < lv_obj_get_child_count(g); k++){
        lv_obj_t *c = lv_obj_get_child(g, (int32_t)k); if(!lv_obj_get_user_data(c)) continue;
        lv_area_t ar; lv_obj_get_coords(c, &ar);
        if(p.x >= ar.x1 - 2 && p.x <= ar.x2 + 2 && p.y >= ar.y1 - 2 && p.y <= ar.y2 + 2){
            lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN); if(a && a->jump) a->jump((char)(intptr_t)lv_obj_get_user_data(c)); return; }
    }
    lv_obj_add_flag(g, LV_OBJ_FLAG_HIDDEN);
}
static void btn_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    azjump_t *a = lv_event_get_user_data(e);
    if(a && wheel_mode(a)){wheel_open(a);return;}
    if(a){ lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(a->grid); }
}

void azjump_create(azjump_t *a, lv_obj_t *root, azjump_cb_t jump, const char *tag){
    a->library=!strcmp(tag,"library");a->current=0;
    if(a->library)library_picker=a;
    a->jump = jump;
    a->btn = lv_button_create(root);
    lv_obj_remove_style_all(a->btn);
    lv_obj_set_pos(a->btn, 308, 158); lv_obj_set_size(a->btn, 44, 44);
    lv_obj_set_style_radius(a->btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a->btn, th_null()?TC(FIXED_MEDIA_BLACK):TC(SURFACE_RAISED), 0);
    lv_obj_set_style_bg_opa(a->btn, th_null()?128:LV_OPA_90, 0);
    lv_obj_set_style_bg_color(a->btn, TC(SURFACE_SELECTED), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(a->btn, 10);
    lv_obj_add_flag(a->btn,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(a->btn, wheel_button, LV_EVENT_ALL, a);
    lv_obj_add_event_cb(a->btn, btn_cb, LV_EVENT_SHORT_CLICKED, a);
    lv_obj_t *azl = lv_label_create(a->btn); lv_label_set_text(azl, "A-Z");
    lv_obj_set_style_text_font(azl, TF(UI_14), 0);
    if(th_disco_family() || th_null()){ lv_obj_set_pos(a->btn, 138, 316); lv_obj_set_size(a->btn, 84, 36); lv_label_set_text(azl, "A - Z");
                    lv_obj_set_style_text_font(azl, TF(UI_16), 0); }
    lv_obj_set_style_text_color(azl, TC(TEXT_PRIMARY), 0); lv_obj_center(azl);
    lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN);

    /* the letter grid */
    a->grid = lv_obj_create(root);
    lv_obj_remove_style_all(a->grid);
    lv_obj_set_user_data(a->grid, a);
    lv_obj_set_size(a->grid, 360, 360); lv_obj_set_pos(a->grid, 0, 0);
    lv_obj_set_style_bg_color(a->grid, th_null()?TC(FIXED_MEDIA_BLACK):TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(a->grid, th_null()?128:th_simple()?255:LV_OPA_80, 0);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(a->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->grid, grid_bg_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *gt = lv_label_create(a->grid);                  /* says it's navigation, not a sort control */
    lv_label_set_text(gt, tr("Jump to"));
    lv_obj_align(gt, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(gt, th_simple()?TF(UI_22):TF(UI_14), 0);
    lv_obj_set_style_text_color(gt, th_simple()?TC(ACCENT_PRIMARY):TC(TEXT_MUTED), 0);
    static const char *AZ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
    int cols = 5, cw = 52, ch = 48, n = 27, gw = cols * cw, x0 = (360 - gw) / 2, rows = (n + cols - 1) / cols, y0 = 48;
    for(int i = 0; i < n; i++){
        int r = i / cols, c = i % cols;
        int in_row = (r == rows - 1) ? (n - r * cols) : cols;        /* the last row may be partial: centred */
        int rx0 = x0 + ((cols - in_row) * cw) / 2;
        lv_obj_t *cell = lv_button_create(a->grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, rx0 + c * cw, y0 + r * ch); lv_obj_set_size(cell, cw - 4, ch - 4);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(cell, TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(cell, TC(ON_ACCENT), LV_STATE_PRESSED);
        lv_obj_add_event_cb(cell, letter_cb, LV_EVENT_CLICKED, (void *)(intptr_t)AZ[i]);
        lv_obj_t *l = lv_label_create(cell);
        char b[2] = { AZ[i], 0 }; lv_label_set_text(l, b);
        lv_obj_set_style_text_font(l, th_simple()?TF(UI_24):TF(UI_20), 0);
        lv_obj_set_style_text_color(l, TC(TEXT_PRIMARY), 0); lv_obj_center(l);
        if(th_zune()){                                            /* Figma 31: square tiles in rows that fit the glass */
            static const char *ORD = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ"; static const int ROWN[5] = { 3, 6, 7, 7, 4 };
            int k = (int)(strchr(ORD, AZ[i]) - ORD), row = 0, base = 0;
            while(row < 4 && k >= base + ROWN[row]){ base += ROWN[row]; row++; }
            int col = k - base, x = 180 - ROWN[row] * 40 / 2 + col * 40 + 2, y = 82 + row * 40;
            lv_obj_set_pos(cell, x, y); lv_obj_set_size(cell, 36, 36);
            lv_obj_set_style_radius(cell, 0, 0);
            lv_obj_set_style_bg_color(cell, TC(ACCENT_PRIMARY), 0); lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell, TC(TEXT_PRIMARY), LV_STATE_PRESSED);
            LV_FONT_DECLARE(font_zune_regular_19)
            char lo[2] = { (char)(AZ[i] >= 'A' && AZ[i] <= 'Z' ? AZ[i] + 32 : AZ[i]), 0 }; lv_label_set_text(l, lo);
            lv_obj_set_style_text_font(l, &font_zune_regular_19, 0); lv_obj_set_style_text_color(l, TC(ON_ACCENT), 0);
            kit_keep(cell); kit_keep(l);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);      /* the kit would round a clickable tile: the grid takes the tap */
            lv_obj_set_user_data(cell, (void *)(intptr_t)AZ[i]);
        }
    }
    if(th_zune()){ lv_obj_set_style_bg_opa(a->grid, LV_OPA_COVER, 0); lv_obj_add_flag(gt, LV_OBJ_FLAG_HIDDEN); kit_keep(a->grid);
                   lv_obj_remove_event_cb(a->grid, grid_bg_cb); lv_obj_add_event_cb(a->grid, zgrid_cb, LV_EVENT_CLICKED, NULL); }
}
void azjump_show(azjump_t *a, int on){
    if(!a->btn) return;
    if(on){
        lv_obj_clear_flag(a->btn,LV_OBJ_FLAG_HIDDEN);
        if(th_zune() && !wheel_mode(a)){                    /* Zune (Figma 29:67): a 34 px magenta dot on the right rim, the current letter in it */
            lv_obj_t*l=lv_obj_get_child(a->btn,0); kit_keep(a->btn); kit_keep(l);
            lv_obj_set_pos(a->btn,305,179);lv_obj_set_size(a->btn,34,34);lv_obj_set_style_radius(a->btn,LV_RADIUS_CIRCLE,0);
            lv_obj_set_style_bg_color(a->btn,TC(ACCENT_PRIMARY),0);lv_obj_set_style_bg_opa(a->btn,LV_OPA_COVER,0);
            lv_obj_set_style_text_font(l,TF(UI_16),0);lv_obj_set_style_text_color(l,TC(ON_ACCENT),0);
            if(!strcmp(lv_label_get_text(l),"A-Z")||!strcmp(lv_label_get_text(l),"A - Z"))lv_label_set_text(l,"a");
            lv_obj_set_style_pad_bottom(l,3,0);lv_obj_center(l);
        } else
        if(!wheel_mode(a)){
            lv_obj_t*l=lv_obj_get_child(a->btn,0);
            int pill=th_disco_family()||th_null();lv_obj_set_pos(a->btn,pill?138:308,pill?316:158);lv_obj_set_size(a->btn,pill?84:44,pill?36:44);
            lv_obj_set_style_radius(a->btn,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(a->btn,th_null()?TC(FIXED_MEDIA_BLACK):TC(SURFACE_RAISED),0);lv_obj_set_style_bg_opa(a->btn,th_null()?128:LV_OPA_90,0);
            lv_label_set_text(l,pill?"A - Z":"A-Z");lv_obj_set_style_text_font(l,pill?TF(UI_16):TF(UI_14),0);lv_obj_set_style_text_color(l,TC(TEXT_PRIMARY),0);lv_obj_center(l);
        }
    }
    else { wheel_close(a,0);lv_obj_add_flag(a->btn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN); }
}

/* Zune: the dot shows where the list is (its first visible row's letter, lower case) */
void azjump_letter(azjump_t *a, char letter){
    if(!th_zune() || !a || !a->btn || wheel_mode(a) || lv_obj_has_flag(a->btn, LV_OBJ_FLAG_HIDDEN)) return;
    char t[2] = { (char)((letter >= 'A' && letter <= 'Z') ? letter + 32 : letter ? letter : '#'), 0 };
    lv_obj_t *l = lv_obj_get_child(a->btn, 0);
    if(l && strcmp(lv_label_get_text(l), t)){ lv_label_set_text(l, t); lv_obj_center(l); }
}
void azjump_test_grid(void){ if(library_picker && library_picker->grid){ lv_obj_clear_flag(library_picker->grid, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(library_picker->grid); } }   /* host renders */
