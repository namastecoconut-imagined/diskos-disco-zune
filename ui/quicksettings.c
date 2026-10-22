#include "simple_theme.h"
#include "onboarding.h"
#include "null_theme.h"
#include "battery.h"
#include "quicksettings.h"
#include "folderbrowser.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "fwcaps.h"
#include "theme.h"
#include "theme_kit.h"
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "orbit.h"
#include "ipc.h"
#include "config.h"
#include <math.h>
#include "scanner.h"
#include "i18n.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Quick Settings, "orbit on rails": six round toggles orbit a hub showing the playing cover inside its
 * progress ring (the Ring Now Playing motif). Brightness is a control arc on the top rim; the battery is
 * an indicator arc on the bottom rim with the Home screen's logic (white, red + slow pulse at <=15%).
 * A tap on the cover goes Home, the small centre button plays/pauses. A short tap on Wi-Fi / Bluetooth
 * toggles the radio, a long press opens its settings. Background: the album's blurred backdrop, dimmed. */

LV_FONT_DECLARE(font_theme_20)
LV_FONT_DECLARE(font_theme_24)
LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px */
#define WI_SUN "\xEF\x86\x85"           /* f185 sun */
#define QS_ARC_D     348                /* rim arcs */
#define QS_ARC_W     TH_ARC_CTL         /* brightness: a control arc, rounded ends */

enum { T_WIFI, T_BT, T_LIB, T_RESCAN, T_MODE, T_SET, T_N };
enum { Q_EQ=T_N, Q_SEARCH, Q_QUEUE, Q_UPNEXT, Q_SHORTCUTS, Q_WEATHER, Q_LYRICS, Q_INFO, Q_IMMERSIVE, Q_FOLDERS, Q_FAVS, Q_PLAYBACK, Q_COUNT };
static const qs_item_t CATALOG[Q_COUNT]={
    {"w","Wi-Fi",LV_SYMBOL_WIFI},{"b","Bluetooth",LV_SYMBOL_BLUETOOTH},
    {"l","Library",LV_SYMBOL_DIRECTORY},{"r","Rescan",LV_SYMBOL_REFRESH},
    {"m","Modes",LV_SYMBOL_SD_CARD},{"s","Settings",LV_SYMBOL_SETTINGS},
    {"e","Equalizer","EQ"},{"f","Search",TH_IC_SEARCH},
    {"q","Queue",LV_SYMBOL_LIST},{"u","Up Next",LV_SYMBOL_NEXT},
    {"c","Shortcuts",LV_SYMBOL_BARS},{"h","Weather",WI_SUN},
    {"y","Lyrics",LV_SYMBOL_AUDIO},{"i","Song Info",LV_SYMBOL_FILE},
    {"v","Immersive",LV_SYMBOL_IMAGE},{"d","Folders",LV_SYMBOL_DIRECTORY},
    {"a","Favourites","\xEF\x80\x84"},
    {"p","Play mode",LV_SYMBOL_LOOP}
};
int quicksettings_item_count(void){return Q_COUNT;}
int quicksettings_item_limit(void){return (th_null()||th_simple())?8:th_disco()?7:6;}
int quicksettings_playback_item(void){return Q_PLAYBACK;}
int quicksettings_equalizer_item(void){return Q_EQ;}
int quicksettings_item_active(int id){int p=simple_cfg_get_int("eq_preset",0);return id==T_WIFI?wifi_radio_live()>0:id==T_BT?bt_state()==BT_ON:id==T_RESCAN?scanner_active():id==T_MODE?ui_get_source_mode()!=0:id==Q_EQ?p>0&&p<=20:id==Q_PLAYBACK?simple_cfg_get_int("work_mode",0)!=0:0;}
static const char*playback_glyph(void){static const char*const g[]={LV_SYMBOL_RIGHT,LV_SYMBOL_SHUFFLE,LV_SYMBOL_LOOP "1",LV_SYMBOL_LOOP,LV_SYMBOL_RIGHT "1"};int v=simple_cfg_get_int("work_mode",0);return g[v>=0&&v<5?v:0];}
const qs_item_t *quicksettings_item(int id){return id>=0 && id<Q_COUNT?&CATALOG[id]:NULL;}
int quicksettings_items_load(int *out){
    const char *code=simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",NULL);int n=0,all[Q_COUNT],limit=quicksettings_item_limit();
    if(code)for(const char *p=code;*p && n<Q_COUNT;p++)for(int id=0;id<Q_COUNT;id++)if(*p==CATALOG[id].code[0]){
        int duplicate=0;for(int k=0;k<n;k++)if(all[k]==id)duplicate=1;
        if(!duplicate)all[n++]=id;
        break;
    }
    if(!code || (code[0] && !n)){
        if(th_null()||th_simple()){static const int nd[]={T_WIFI,T_BT,T_MODE,Q_EQ,T_RESCAN,Q_SEARCH,Q_WEATHER,T_SET};memcpy(out,nd,sizeof nd);return 8;}
        if(!th_disco_family()){for(int i=0;i<T_N;i++)out[i]=i;return T_N;}
        static const int defaults[]={T_WIFI,T_BT,T_MODE,T_LIB,Q_EQ,Q_SEARCH,T_SET};
        memcpy(out,defaults,sizeof defaults);return sizeof defaults/sizeof defaults[0];
    }
    while(n>limit){                                          /* an older, longer list: drop the last that isn't pinned */
        int k=n-1;while(k>=0 && (all[k]==T_WIFI || all[k]==T_BT || all[k]==T_SET))k--;
        if(k<0)break;
        memmove(all+k,all+k+1,(size_t)(n-k-1)*sizeof *all);n--;
    }
    if(n>limit)n=limit;
    memcpy(out,all,(size_t)n*sizeof *out);
    return n;
}
int quicksettings_items_save(const int *items,int count){
    if(count<0 || count>quicksettings_item_limit())return -1;
    char codes[QS_MAX_ITEMS+1],old[48];
    for(int i=0;i<count;i++){
        if(items[i]<0 || items[i]>=Q_COUNT)return -1;
        for(int j=0;j<i;j++)if(items[j]==items[i])return -1;
        codes[i]=CATALOG[items[i]].code[0];
    }
    codes[count]=0;snprintf(old,sizeof old,"%s",simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs"));
    if(simple_cfg_set_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",codes)<0){simple_cfg_set_str_deferred(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items",old);cfg_flush();return -1;}
    return 0;
}
static int g_items[QS_MAX_ITEMS],g_nitems,g_style=-1,g_style_raw=-1;
static char g_codes[48];static lv_obj_t *g_controls,*g_root;
static orbit_t g_orb;
static lv_obj_t *g_bright, *g_batt, *g_bg, *g_dim;
static lv_obj_t *g_cover_clip, *g_cover_img, *g_cover_note, *g_prog, *g_pp, *g_pp_glyph;
static lv_timer_t *g_prog_timer;
static battery_arc_t g_battery;
static lv_obj_t *g_batt_icon, *g_sun;
static int g_scanning = -1;
static void rescan_show(int on);
static void dq_paint(void);

static void bright_change_cb(lv_event_t *e){ ui_backlight(lv_arc_get_value(lv_event_get_target(e))); }
static void bright_release_cb(lv_event_t *e){ ui_set_brightness(lv_arc_get_value(lv_event_get_target(e))); }
static void pp_cb(lv_event_t *e){ (void)e; if(ui_transport_command("0201000C0000")==0) ui_pp_glyph(g_pp_glyph,ui_pp_icon_playing(ui_is_playing())); }
static void home_cb(lv_event_t *e){ (void)e; screen_show(SCR_HOME); }            /* the cover leads Home */

/* the Mode toggle wears the icon of the mode the player is actually in (same set as Working Mode) */
static const char *mode_glyph(void){
    switch(ui_get_source_mode()){
        case 1:  return LV_SYMBOL_USB;
        case 2:  return LV_SYMBOL_BLUETOOTH;
        case 3:  return LV_SYMBOL_DRIVE;
        case 4:  return LV_SYMBOL_VOLUME_MAX;
        case 5:  return LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_SD_CARD;
    }
}
const char *quicksettings_item_glyph(int id){const qs_item_t *item=quicksettings_item(id);return id==Q_PLAYBACK?playback_glyph():id==T_MODE?mode_glyph():item?item->icon:LV_SYMBOL_SETTINGS;}
static void paint_radios(void){
    if(th_disco_family()){dq_paint();return;}
    lv_color_t acc = th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY);
    int pending=-1;
    for(int i=0;i<g_nitems;i++){int id=g_items[i];if(id==T_WIFI||id==T_BT||id==Q_PLAYBACK||id==Q_EQ)orbit_set_on(&g_orb,i,quicksettings_item_active(id),acc);if(id==T_BT&&bt_state()==BT_TURNING_ON)pending=i;}
    orbit_set_pending(&g_orb,pending,acc);
}
static void rescan_go(void){ ui_rescan_library(); ui_toast("Rescan requested"); rescan_show(1); }
static void eq_toggle(void){
 int preset=0;
 if(!quicksettings_item_active(Q_EQ)){preset=simple_cfg_get_int("eq_last",1);if(preset<1||preset>20)preset=1;}
 if(ui_eq_select(preset)<0){ui_toast("Couldn't switch EQ");return;}
 int remembered=preset?preset:simple_cfg_get_int("eq_last",1);if(remembered<1||remembered>20)remembered=1;
 char message[160];snprintf(message,sizeof message,"EQ %s\nPreset: %s",preset?"On":"Off",ui_eq_name(remembered));ui_toast(message);
 quicksettings_refresh(ui_is_playing());
}
static void pick_cb(int i){                                       /* short taps */
    switch(i){
        case T_WIFI: { int on = wifi_toggle(); orbit_set_on(&g_orb,T_WIFI,on,th_ringlike()?ui_current_accent():TC(ACCENT_PRIMARY));
                       ui_toast(on ? "Turning on Wi-Fi\xE2\x80\xA6" : "Wi-Fi off"); } break;
        case T_BT:   { int on = bt_toggle(); paint_radios();
                       ui_toast(on ? "Turning on Bluetooth\xE2\x80\xA6" : "Bluetooth off"); } break;
        case T_LIB:    screen_show(SCR_LIBRARY); break;
        case T_RESCAN: if(scanner_active()){ ui_toast("Already scanning"); break; }
                       fileops_confirm("Rescan library?", "Looks for new and changed music", "Rescan", rescan_go); break;
        case T_MODE:   modes_open(); break;
        case T_SET:    screen_show(SCR_SETTINGS); break;
        case Q_EQ: eq_toggle();break;
        case Q_SEARCH: screen_show(SCR_SEARCH);break;
        case Q_QUEUE: queue_open();break;
        case Q_UPNEXT: upnext_open();break;
        case Q_SHORTCUTS: apps_reload();screen_show(SCR_APPS);break;
        case Q_WEATHER: weather_app_open();break;
        case Q_LYRICS: lyrics_open();break;
        case Q_INFO: {track_state_t st;ipc_get_state(&st);if(!st.have_track){ui_toast("No track playing");break;}songinfo_unpin();songinfo_set(&st);screen_show(SCR_SONGINFO);break;}
        case Q_IMMERSIVE: disco_open_np_immersive();break;
        case Q_FOLDERS: folderbrowser_open();break;
        case Q_FAVS: library_open_favourites();screen_show(SCR_LIBRARY);break;
        case Q_PLAYBACK:ui_np_mode_next();break;
    }
}
static void base_pick(int slot){if(slot>=0&&slot<g_nitems){pick_cb(g_items[slot]);quicksettings_refresh(ui_is_playing());}}
static void base_hold(lv_event_t*e){int slot=(int)(intptr_t)lv_event_get_user_data(e);if(slot<0||slot>=g_nitems)return;quicksettings_null_action(g_items[slot],1);}
static void base_build(void){
 g_nitems=quicksettings_items_load(g_items);
 for(int i=0;i<T_N;i++){
  if(i>=g_nitems){lv_obj_add_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i])lv_obj_add_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);continue;}
  int id=g_items[i];lv_obj_remove_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i]){lv_obj_remove_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(g_orb.cap[i],id==T_MODE?"Mode":CATALOG[id].name);}
  orbit_set_glyph(&g_orb,i,quicksettings_item_glyph(id));
 }
 paint_radios();
}

/* the hub's progress ring follows the track (1 s poll; only redraws on a visible change) */
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(!g_prog || screen_current() != SCR_QUICK) return;
    rescan_show(scanner_active() ? 1 : 0);                     /* also picks up a scan started elsewhere */
    paint_radios();                                             /* BT comes up in the background: follow it */
    dq_paint();
    track_state_t st; ipc_get_state(&st);
    int v = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(abs(v - lv_arc_get_value(g_prog)) >= 3) lv_arc_set_value(g_prog, v);
}
/* rescan: while the library scan runs, the Rescan icon turns red and spins (1 turn/s) */
static void spin_exec(void *var, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0); }
static void rescan_show(int on){
    if(th_disco_family()||th_null()){g_scanning=on;return;}
    if(on == g_scanning) return;
    g_scanning = on;
    lv_obj_t *ic = g_orb.icon[T_RESCAN];
    lv_anim_delete(ic, spin_exec);
    lv_obj_set_style_text_color(ic, on ? TC(ACCENT_PRIMARY) : (th_braun() ? TC(ON_CONTROL) : TC(TEXT_PRIMARY)), 0);
    if(g_orb.cap[T_RESCAN]){ lv_label_set_text(g_orb.cap[T_RESCAN], on ? "Scanning" : "Rescan");
                            lv_obj_set_style_text_color(g_orb.cap[T_RESCAN], on ? TC(TEXT_PRIMARY) : TC(TEXT_SECONDARY), 0); }
    if(on){
        lv_obj_update_layout(ic);
        lv_obj_set_style_transform_pivot_x(ic, lv_obj_get_width(ic) / 2, 0);
        lv_obj_set_style_transform_pivot_y(ic, lv_obj_get_height(ic) / 2, 0);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, ic); lv_anim_set_exec_cb(&a, spin_exec);
        lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else lv_obj_set_style_transform_rotation(ic, 0, 0);
}

/* ---- Disco: preserve the music disc and rim arcs; configurable controls use the current rows
 * or an eight-button arc. The catalog and ordering are shared by both styles. */
#define DQ_CX 272                                                   /* the field's visible middle */
#define DQ_CY 180
#define DQ_OX (disco_mirror() ? 180 - DQ_CX : DQ_CX - 180)               /* the field's middle as an offset (Disco Left: on the left) */
static int g_hub_d = ORBIT_HUB;                                    /* the cover disc (Disco: bigger) */
static lv_obj_t *g_dq_row[QS_MAX_ITEMS], *g_dq_ico[QS_MAX_ITEMS], *g_dq_val[QS_MAX_ITEMS], *g_dq_name[QS_MAX_ITEMS], *g_dq_badge[QS_MAX_ITEMS];
static void set_text_if(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void set_col_if(lv_obj_t *l, lv_color_t c){ if(l && !lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0); }
static void dq_paint(void){
    if(!th_disco_family())return;
    lv_color_t acc=ui_current_accent();
    for(int i=0;i<g_nitems;i++){
        int id=g_items[i],on=quicksettings_item_active(id);
        if(!g_dq_ico[i])continue;
        if(g_dq_row[i] && !lv_color_eq(lv_obj_get_style_border_color(g_dq_row[i],0),acc)){
            lv_obj_set_style_border_color(g_dq_row[i],acc,0);
            lv_obj_set_style_border_color(g_dq_row[i],acc,LV_STATE_PRESSED);
        }
        set_col_if(g_dq_ico[i],on?acc:TC(TEXT_PRIMARY));
        if(id==T_WIFI || id==T_BT || id==Q_EQ){
            set_text_if(g_dq_val[i],id==T_BT && bt_state()==BT_TURNING_ON?"...":on?"On":"Off");
            if(g_dq_badge[i]){
                lv_color_t color=on?acc:TC(SURFACE_RAISED);
                if(!lv_color_eq(lv_obj_get_style_bg_color(g_dq_badge[i],0),color))lv_obj_set_style_bg_color(g_dq_badge[i],color,0);
                set_col_if(g_dq_val[i],on?theme_on_color(acc):TC(TEXT_PRIMARY));
            }
        }
        if(id==T_RESCAN)set_text_if(g_dq_name[i],on?"Scanning":"Rescan");
        if(id==T_MODE||id==Q_PLAYBACK)set_text_if(g_dq_ico[i],quicksettings_item_glyph(id));
    }
}
static void dq_row_cb(lv_event_t *e){
    int slot=(int)(intptr_t)lv_event_get_user_data(e);if(slot<0 || slot>=g_nitems)return;
    int id=g_items[slot];
    if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED){
        quicksettings_null_action(id,1);
    }else if(lv_event_get_code(e)==LV_EVENT_SHORT_CLICKED){pick_cb(id);dq_paint();}
}
static const lv_font_t *row_font_list(const char *text,int width,int height){   /* the Library's row font, smaller only if needed */
    const lv_font_t *fonts[]={TH_F_LIST,TF(UI_16),TF(UI_14)};
    for(int i=0;i<3;i++){
        lv_point_t size;lv_text_get_size(&size,text,fonts[i],0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        if(size.x<=width && size.y<=height)return fonts[i];
    }
    return fonts[2];
}
/* Arc: 76 px circles; six on a circle round the screen's centre (the rim's curve, inside the brightness and battery
 * arcs), from upper right (285 degrees) round the left to lower right (75), and one in the middle beside the music */
#define RING_D 76
#define RING_R 113
#define RING_A0 75
#define RING_A1 285
#define RING_CX 150
static void controls_build(lv_obj_t *root){
    if(g_controls){lv_obj_delete(g_controls);g_controls=NULL;}
    memset(g_dq_row,0,sizeof g_dq_row);memset(g_dq_ico,0,sizeof g_dq_ico);memset(g_dq_val,0,sizeof g_dq_val);
    memset(g_dq_name,0,sizeof g_dq_name);memset(g_dq_badge,0,sizeof g_dq_badge);
    g_nitems=quicksettings_items_load(g_items);g_style_raw=simple_cfg_get_int("disco_qs_style",1)==1;g_style=g_style_raw;
    snprintf(g_codes,sizeof g_codes,"%s",simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs"));
    for(int i=0;i<T_N;i++){lv_obj_add_flag(g_orb.btn[i],LV_OBJ_FLAG_HIDDEN);if(g_orb.cap[i])lv_obj_add_flag(g_orb.cap[i],LV_OBJ_FLAG_HIDDEN);}
    g_controls=lv_obj_create(root);lv_obj_remove_style_all(g_controls);lv_obj_set_size(g_controls,360,360);
    lv_obj_clear_flag(g_controls,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    if(g_sun){lv_obj_set_style_transform_scale(g_sun,176,0);lv_obj_set_style_transform_pivot_x(g_sun,lv_pct(50),0);lv_obj_set_style_transform_pivot_y(g_sun,lv_pct(50),0);
        lv_obj_align(g_sun,LV_ALIGN_CENTER,disco_mirror()?-120:120,-107);}   /* centred between the field and the end of the brightness arc */
    if(g_batt_icon){lv_obj_set_style_text_font(g_batt_icon,TF(UI_20),0);lv_obj_align(g_batt_icon,LV_ALIGN_CENTER,disco_mirror()?-120:120,107);}   /* and between the field and the end of the battery arc */
    /* Arc: Wi-Fi in the middle beside the music; the rest on a circle round the screen's centre (the rim's curve, inside
     * the brightness and battery arcs): Bluetooth first at the upper right, round the left, Settings last at the lower
     * right. List: Wi-Fi and Bluetooth on top (the fixed pair), Settings the last row. */
    int order[QS_MAX_ITEMS],no=0,centre=-1;
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_WIFI)centre=i;
    if(centre<0)for(int i=0;i<g_nitems;i++)if(g_items[i]!=T_BT && g_items[i]!=T_SET){centre=i;break;}   /* no Wi-Fi: the first free one */
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_BT)order[no++]=i;
    for(int i=0;i<g_nitems;i++)if(i!=centre && g_items[i]!=T_BT && g_items[i]!=T_SET)order[no++]=i;
    for(int i=0;i<g_nitems;i++)if(g_items[i]==T_SET)order[no++]=i;
    int arcpos[QS_MAX_ITEMS];for(int i=0;i<g_nitems;i++)arcpos[i]=-1;
    for(int k=0;k<no;k++)arcpos[order[k]]=k;
    int settings_last=-1;for(int i=0;i<g_nitems;i++)if(g_items[i]==T_SET)settings_last=i;
    int rows=0,row_index=0;
    for(int i=0;i<g_nitems;i++)if(g_items[i]!=T_WIFI && g_items[i]!=T_BT)rows++;
    /* The first gap below the fixed radios equals every inter-row gap.
     * Use integer dimensions so no one-pixel remainder gets put into a gap. */
    /* List: rows stack from the top like a Library list (4 px apart), never taller than 44 px */
    int row_gap=4,row_h=rows?(240-row_gap*rows)/rows:0;
    if(row_h>44)row_h=44;
    for(int slot=0;slot<g_nitems;slot++){
        int id=g_items[slot],radio=id==T_WIFI || id==T_BT;
        int x,y,w,h;
        if(g_style){
            w=h=RING_D;
            if(slot==centre){x=RING_CX-RING_D/2;y=180-RING_D/2;}
            else{float a=(no<2?RING_A1:RING_A1-(RING_A1-RING_A0)*(float)arcpos[slot]/(no-1))*0.0174533f;   /* even, both ends pinned */
                 x=180+(int)lroundf(RING_R*cosf(a))-RING_D/2;y=180+(int)lroundf(RING_R*sinf(a))-RING_D/2;}
            x=disco_mx(x,w);
        }
        else{
            if(radio){h=38;y=42;x=id==T_WIFI?80:184;w=98;}
            else{h=row_h;int ri=slot==settings_last?rows-1:row_index++;y=80+row_gap+ri*(row_h+row_gap);
                /* List: rows sit and curve like the Library's (curvelist Classic): a row is at most 268 px wide,
                 * centred, and narrows with the round screen; here its right end stops short of the music field. */
                int dy=abs(y+h/2-180)+h/2;
                int half=dy<176?(int)sqrtf((float)(180*180-dy*dy))-10:0;
                if(half>134)half=134;
                x=180-half;
                int right=180+half;
                if(y<270 && y+h>90){                                   /* beside the music field: clear of it */
                    int near=y>180?y:y+h<180?y+h:180;
                    int d=near-180;
                    int field_left=286-(int)sqrtf((float)(90*90-d*d));
                    if(right>field_left-6)right=field_left-6;
                }else if(right>236)right=236;                          /* above / below it: clear of the sun and battery icons */
                w=right-x;
                }
            x=disco_mx(x,w);
        }
        lv_obj_t *r=g_dq_row[slot]=lv_obj_create(g_controls);lv_obj_remove_style_all(r);lv_obj_set_pos(r,x,y);lv_obj_set_size(r,w,h);
        lv_obj_set_style_radius(r,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(r,TC(SURFACE),0);lv_obj_set_style_bg_opa(r,150,0);
        lv_obj_set_style_bg_color(r,TC(SURFACE_RAISED),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_set_style_border_width(r,g_style?1:0,0);lv_obj_set_style_border_color(r,ui_current_accent(),0);lv_obj_set_style_border_opa(r,80,0);
        lv_obj_set_style_border_width(r,2,LV_STATE_PRESSED);lv_obj_set_style_border_color(r,ui_current_accent(),LV_STATE_PRESSED);lv_obj_set_style_border_opa(r,LV_OPA_COVER,LV_STATE_PRESSED);
        lv_obj_add_flag(r,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_USER_2);if(g_style)lv_obj_add_flag(r,LV_OBJ_FLAG_ADV_HITTEST);lv_obj_clear_flag(r,LV_OBJ_FLAG_SCROLLABLE);
        ui_on(r,dq_row_cb,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)slot,"qs.item",UI_CORE);ui_on(r,dq_row_cb,LV_EVENT_LONG_PRESSED,(void*)(intptr_t)slot,"qs.item.hold",UI_CORE);
        g_dq_ico[slot]=lv_label_create(r);lv_label_set_text(g_dq_ico[slot],quicksettings_item_glyph(id));
        int small=!g_style && h<30;
        lv_obj_set_style_text_font(g_dq_ico[slot],id==Q_SEARCH?(small?&font_theme_20:&font_theme_24):
            (id==Q_WEATHER || id==Q_FAVS)?(small?TF(ICON_20):TF(ICON_28)):
            id==Q_EQ?(g_style?TF(UI_24):TF(UI_20)):g_style?TF(UI_28):small?TF(UI_20):TF(UI_24),0);
        if(!g_style && !radio)lv_obj_set_style_text_font(g_dq_ico[slot],
            id==Q_SEARCH?&font_theme_20:(id==Q_WEATHER || id==Q_FAVS)?TF(ICON_20):id==Q_EQ?TF(UI_14):TF(UI_20),0);
        if(g_style){lv_obj_align(g_dq_ico[slot],LV_ALIGN_CENTER,0,radio?-9:0);
            if(radio){g_dq_val[slot]=lv_label_create(r);lv_obj_set_style_text_font(g_dq_val[slot],TF(UI_14),0);lv_obj_set_style_text_color(g_dq_val[slot],TC(TEXT_PRIMARY),0);lv_obj_align(g_dq_val[slot],LV_ALIGN_BOTTOM_MID,0,-4);}}
        else if(radio){
            lv_obj_align(g_dq_ico[slot],LV_ALIGN_LEFT_MID,18,0);
            lv_obj_t *badge=g_dq_badge[slot]=lv_obj_create(r);lv_obj_remove_style_all(badge);lv_obj_set_size(badge,h-8,h-8);lv_obj_align(badge,LV_ALIGN_RIGHT_MID,-4,0);
            lv_obj_set_style_radius(badge,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(badge,LV_OPA_COVER,0);lv_obj_clear_flag(badge,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            g_dq_val[slot]=lv_label_create(badge);lv_obj_set_style_text_font(g_dq_val[slot],TF(UI_14),0);lv_obj_center(g_dq_val[slot]);
        }else{
            lv_obj_align(g_dq_ico[slot],LV_ALIGN_LEFT_MID,16,0);
            g_dq_name[slot]=lv_label_create(r);lv_label_set_text(g_dq_name[slot],CATALOG[id].name);
            int text_w=w-58;
            lv_obj_set_style_text_font(g_dq_name[slot],row_font_list(CATALOG[id].name,text_w,h-4),0);
            lv_obj_set_style_text_letter_space(g_dq_name[slot],0,0);
            lv_obj_set_style_text_color(g_dq_name[slot],TC(TEXT_PRIMARY),0);
            lv_label_set_long_mode(g_dq_name[slot],LV_LABEL_LONG_CLIP);
            lv_obj_set_width(g_dq_name[slot],text_w);
            lv_obj_set_style_text_align(g_dq_name[slot],LV_TEXT_ALIGN_LEFT,0);
            lv_obj_align(g_dq_name[slot],LV_ALIGN_LEFT_MID,50,0);
        }
    }
    dq_paint();
}
static void disco_layout(lv_obj_t *root){
    /* the field: the open navigation circle's shape, its right end cut by the rim */
    lv_obj_t *f = lv_obj_create(root); lv_obj_remove_style_all(f);
    lv_obj_set_pos(f, disco_mx(196, 230), 90); lv_obj_set_size(f, 230, 180);
    lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f, TC(SCRIM), 0); lv_obj_set_style_bg_opa(f, 204, 0);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(f, lv_obj_get_index(g_dim) + 1);
    g_hub_d = 120;                                                   /* a bigger cover in its ring */
    lv_obj_set_size(g_cover_clip, g_hub_d, g_hub_d); lv_obj_set_size(g_prog, g_hub_d + 16, g_hub_d + 16);
    lv_obj_align(g_prog, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    lv_obj_align(g_cover_clip, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    /* the album-colour glow: soft outline rings, not a 24 px blurred shadow (it sits inside the progress ring, which
     * redraws every second, and an uncached shadow was recomputed with it) */
    lv_obj_set_style_outline_width(g_cover_clip, 5, 0); lv_obj_set_style_outline_opa(g_cover_clip, 70, 0); lv_obj_set_style_outline_pad(g_cover_clip, 0, 0);
    lv_obj_set_style_outline_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_align(g_pp, LV_ALIGN_CENTER, DQ_OX, DQ_CY - 180);
    controls_build(root);
}
/* Battery fill: normal text colour, low red, charging green; no pulse or glow. */
static void zqs_create(lv_obj_t *root);
static void zqs_paint(void);
void quicksettings_create(lv_obj_t *root)
{
    g_root=root;
    if(th_zune()){ zqs_create(root); return; }
    if(th_simple()){simple_qs_create(root);return;}
    if(th_null()){null_qs_create(root);return;}
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* media surface: the album's blurred backdrop, dimmed well down (hidden when there is no art) */
    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360); lv_obj_center(g_bg);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    g_dim = lv_obj_create(root);
    lv_obj_remove_style_all(g_dim);
    lv_obj_set_size(g_dim, 360, 360);
    lv_obj_set_style_bg_color(g_dim, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_dim,th_ringlike()?85:210,0);
    lv_obj_clear_flag(g_dim, LV_OBJ_FLAG_CLICKABLE);

    /* brightness: a control arc on the top rim, rounded ends; ADV_HITTEST keeps its touch area on the band */
    g_bright = lv_arc_create(root);
    lv_obj_remove_style(g_bright, NULL, LV_PART_KNOB);
    lv_obj_set_size(g_bright, QS_ARC_D, QS_ARC_D);
    lv_obj_center(g_bright);
    lv_obj_add_flag(g_bright, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_rotation(g_bright, 0);
    lv_arc_set_bg_angles(g_bright, 228, 312);
    lv_arc_set_range(g_bright, 4, 40);
    lv_arc_set_value(g_bright, ui_get_brightness());
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_bright, th_braun() ? TC(TEXT_PRIMARY) : TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_bright, bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *sun = lv_label_create(root);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sun, WI_SUN);
    lv_obj_set_style_text_font(sun, TF(ICON_28), 0);
    lv_obj_set_style_transform_scale(sun, 128, 0);            /* half size: a small marker under the arc */
    lv_obj_set_style_text_color(sun, TC(TEXT_SECONDARY), 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 18);
    g_sun = sun;

    /* battery: an indicator arc on the bottom rim, filling left to right; display only */
    battery_arc_create(&g_battery,root,QS_ARC_D,QS_ARC_W,48,132,1);g_batt=g_battery.arc;
    g_batt_icon = lv_label_create(root);                        /* battery glyph above the arc, the sun's mirror */
    lv_obj_clear_flag(g_batt_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(g_batt_icon, TF(UI_14), 0);
    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
    lv_obj_align(g_batt_icon, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* the six toggles, clockwise from the upper right, with captions */
    static const orbit_item_t it[T_N] = {
        { LV_SYMBOL_WIFI, "Wi-Fi" }, { LV_SYMBOL_BLUETOOTH, "Bluetooth" }, { LV_SYMBOL_DIRECTORY, "Library" },
        { LV_SYMBOL_REFRESH, "Rescan" }, { LV_SYMBOL_SD_CARD, "Mode" }, { LV_SYMBOL_SETTINGS, "Settings" } };
    orbit_create(&g_orb, root, it, T_N, -60, th_disco_family()?pick_cb:base_pick);
    if(th_disco_family()) for(int i = 0; i < T_N; i++){
        /* Also cover the optional icon-arcs layout. No timer or animation. */
        lv_obj_set_style_bg_opa(g_orb.btn[i], LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(g_orb.btn[i], 2, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(g_orb.btn[i], ui_current_accent(), LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(g_orb.btn[i], LV_OPA_COVER, LV_STATE_PRESSED);
    }
    for(int i=0;i<T_N;i++)lv_obj_add_event_cb(g_orb.btn[i],base_hold,LV_EVENT_LONG_PRESSED,(void*)(intptr_t)i);
    if(!th_disco_family())base_build();
    if(th_disco_family())orbit_set_glyph(&g_orb,T_MODE,mode_glyph());
    paint_radios();

    /* hub: progress ring around the cover; the cover goes Home, the centre button plays/pauses */
    g_prog = lv_arc_create(root);
    lv_obj_remove_style(g_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_prog, ORBIT_RING, ORBIT_RING);
    lv_obj_center(g_prog);
    lv_arc_set_rotation(g_prog, 270);
    lv_arc_set_bg_angles(g_prog, 0, 360);
    lv_arc_set_range(g_prog, 0, 1000);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(SURFACE) : TC(CONTROL_TRACK_STRONG), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_prog, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
    g_cover_clip = lv_button_create(root);
    lv_obj_remove_style_all(g_cover_clip);
    lv_obj_set_size(g_cover_clip, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(g_cover_clip);
    lv_obj_set_style_radius(g_cover_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_cover_clip, true, 0);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_bg_opa(g_cover_clip, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_cover_clip, home_cb, LV_EVENT_CLICKED, NULL);
    g_cover_note = lv_label_create(g_cover_clip);                 /* shown when there's no cover */
    lv_label_set_text(g_cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_cover_note, TF(UI_20), 0);
    lv_obj_set_style_text_color(g_cover_note, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_cover_note, LV_ALIGN_CENTER, 0, -22);
    g_cover_img = lv_image_create(g_cover_clip);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    g_pp = lv_button_create(root);
    lv_obj_remove_style_all(g_pp);
    lv_obj_set_size(g_pp, 40, 40);
    lv_obj_center(g_pp);
    lv_obj_set_style_radius(g_pp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pp, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(g_pp, 170, 0);
    lv_obj_set_style_bg_opa(g_pp, 230, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_pp, TC(SURFACE_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(g_pp, 1, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(g_pp, ui_current_accent(), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_pp, 4);
    lv_obj_add_event_cb(g_pp, pp_cb, LV_EVENT_CLICKED, NULL);
    g_pp_glyph = lv_label_create(g_pp);
    lv_label_set_text(g_pp_glyph, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_pp_glyph, TF(UI_16), 0);
    lv_obj_set_style_text_color(g_pp_glyph, TC(TEXT_PRIMARY), 0);
    lv_obj_center(g_pp_glyph);

    if(!g_prog_timer) g_prog_timer = lv_timer_create(prog_tick, 1000, NULL);
    if(th_disco_family()) disco_layout(root);
    if(th_braun()){ br_face(root); if(g_dim) lv_obj_add_flag(g_dim, LV_OBJ_FLAG_HIDDEN);   /* Braun: the grille face, no dimming veil */
                    lv_obj_set_style_text_color(g_batt_icon, TC(TEXT_SECONDARY), 0);
                    for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){ lv_obj_t *o = lv_obj_get_child(root, k);   /* the sun marker */
                        if(lv_obj_check_type(o, &lv_label_class) && lv_obj_get_style_text_font(o, 0) == TF(ICON_28)) lv_obj_set_style_text_color(o, TC(TEXT_SECONDARY), 0); } }
}

/* the cover (RAM decode, scaled into the hub) and the blurred backdrop; called on track / art changes */
void quicksettings_set_art(const void *cover_dsc, const void *backdrop_src)
{
    if(th_simple()){simple_qs_art(cover_dsc);return;}
    if(!g_cover_img) return;
    lv_image_set_src(g_cover_img, NULL);
    if(cover_dsc){
        const lv_image_dsc_t *d = cover_dsc;
        lv_image_set_src(g_cover_img, cover_dsc);
        if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(g_hub_d * 256 / d->header.w) + 2);
        lv_obj_center(g_cover_img);
        lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    }
    lv_image_set_src(g_bg, NULL);
    if(backdrop_src && !th_braun()){ lv_image_set_src(g_bg, backdrop_src); lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }   /* Braun: the grille, no album wash */
    else lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_battery(int pct, int charging)
{
    if(th_simple()){simple_qs_battery(pct,charging);return;}
    if(th_null()){null_qs_battery(pct,charging);return;}
    if(!g_batt) return;
    if(pct < 0) return;
    if(pct > 100) pct = 100;
    battery_arc_set(&g_battery,pct,charging);
    if(g_batt_icon){
        const char *g = charging ? LV_SYMBOL_CHARGE : pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        if(strcmp(lv_label_get_text(g_batt_icon), g)) lv_label_set_text(g_batt_icon, g);
        lv_obj_set_style_text_color(g_batt_icon, charging?TC(STATUS_SUCCESS):pct<=15?TC(STATUS_DANGER):TC(TEXT_SECONDARY), 0);
    }
    quicksettings_refresh(ui_is_playing());
}
void quicksettings_set_now_playing(const char *title, const char *artist, int playing)
{
    if(th_simple()){simple_qs_track(title,artist,playing);return;}
    (void)title; (void)artist;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_prog) lv_obj_set_style_arc_color(g_prog, th_braun() ? TC(ACCENT_PRIMARY) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_volume(int vol){ (void)vol; }       /* the volume arc gave way to the battery */
void quicksettings_refresh(int playing)
{
    if(th_simple()){simple_qs_refresh(playing);return;}
    if(th_null()){null_qs_refresh();return;}
    if(th_zune()){ zqs_paint(); return; }
    if(!g_prog)return;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_bright && !lv_obj_has_state(g_bright, LV_STATE_PRESSED))   /* don't fight an active drag */
        lv_arc_set_value(g_bright, ui_get_brightness());
    if(th_disco_family())orbit_set_glyph(&g_orb,T_MODE,mode_glyph());
    else for(int i=0;i<g_nitems;i++)if(g_items[i]==T_MODE||g_items[i]==Q_PLAYBACK)orbit_set_glyph(&g_orb,i,quicksettings_item_glyph(g_items[i]));
    paint_radios();
}

/* ---- compatibility with 1.1's screen manager ----------------------------------------------------
 * Upstream's Quick Settings is user-configurable: a tile palette plus a config screen (SCR_QSCONFIG),
 * and the manager rebuilds the panel when that changes. Disco rebuilds only its configurable controls; other themes retain the fixed panel. */
void quicksettings_build(void){
    if(th_simple()){simple_qs_build();return;}
    if(th_null()){null_qs_build();return;}
    if(!g_root)return;
    if(th_zune()){ zqs_paint(); return; }
    if(!th_disco_family()){base_build();return;}
    int style=simple_cfg_get_int("disco_qs_style",1)==1;
    if(style!=g_style_raw || strcmp(g_codes,simple_cfg_get_str(th_null()?"null_qs_items":th_disco_family()?"disco_qs_items":"qs_items","wbmlefs")))controls_build(g_root);
    dq_paint();
}

static lv_obj_t *g_qscfg_root;
static void base_edit(lv_event_t*e){(void)e;disco_qs_menu_open();}
void qsconfig_create(lv_obj_t *root){
    g_qscfg_root = root;
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Quick Settings");
    if(!th_disco_family()&&!th_null()){
      lv_obj_t*b=lv_button_create(root);lv_obj_set_size(b,200,52);lv_obj_center(b);lv_obj_add_event_cb(b,base_edit,LV_EVENT_CLICKED,NULL);lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,"Edit quick items");lv_obj_center(l);return;
    }
    lv_obj_t *m = lv_label_create(root);
    lv_label_set_text(m, th_disco_family()?"Choose Style and Items in\nDisplay > Disco Options >\nQuick Settings.":"This theme uses a fixed panel:\n"
                         "brightness and battery arcs,\nthe cover and six toggles.");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(m, 50, 120); lv_obj_set_size(m, 260, 120);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(m, TC(TEXT_SECONDARY), 0);
}
void qsconfig_refresh(void){ }

/* Host regression seams; no hardware commands are issued by these helpers. */
lv_obj_t *quicksettings_test_button(int slot){extern lv_obj_t*null_test_qs_field(int);if(th_simple())return simple_test_qs_button(slot);if(th_null())return null_test_qs_field(slot);return slot>=0&&slot<g_nitems?(th_disco()?g_dq_row[slot]:g_orb.btn[slot]):NULL;}
lv_obj_t *quicksettings_test_disc(void){return g_cover_clip;}

void quicksettings_null_action(int id,int hold){if(hold && (id==T_WIFI || id==T_BT))onboarding_discovered(HINT_RADIO_HOLD);if(hold&&id==T_WIFI)wifi_open();else if(hold&&id==T_BT)bt_open();else if(hold&&id==Q_EQ)screen_show(SCR_EQ);else if(hold && id>=0 && id<Q_COUNT)ui_toast(CATALOG[id].name);else if(!hold)pick_cb(id);}

/* ==== Zune (Figma 04): six round toggles in two rows - wi-fi, bluetooth, gain, shuffle, sleep, mode - each with its
 * name and state under it (on: the accent); brightness as an arc round the bottom of the glass with the sun above ====
 * Taps act as before (a radio toggles, its hold opens its page); gain and shuffle toggle; sleep and mode open theirs. */
#include "zune_dial.h"
LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_13)
enum { ZQ_WIFI, ZQ_BT, ZQ_GAIN, ZQ_SHUF, ZQ_SLEEP, ZQ_MODE, ZQ_N };
static lv_obj_t *g_zq_btn[ZQ_N], *g_zq_ic[ZQ_N], *g_zq_sub[ZQ_N], *g_zq_bright;
static char g_zq_ssid[64]; static uint32_t g_zq_ssid_at; static int g_zq_ssid_ok;
int wifi_current_ssid(char *out, int cap);
int ui_sleep_state(int *secs_left);
void npmenu_open_sleep(void);
void ui_set_gain(int high);
static const char *zq_mode_name(void){
    switch(ui_get_source_mode()){ case 1: return "usb dac"; case 2: return "bt receiver"; case 3: return "usb storage";
                                  case 4: return "bt streaming"; case 5: return "airplay"; default: return "playback"; }
}
static void zqs_paint(void){
    if(!g_zq_btn[0]) return;
    int on[ZQ_N]; char sub[ZQ_N][40];
    int wl = wifi_radio_live() > 0;
    if(wl && (!g_zq_ssid_at || lv_tick_elaps(g_zq_ssid_at) > 8000)){ g_zq_ssid_ok = wifi_current_ssid(g_zq_ssid, sizeof g_zq_ssid); g_zq_ssid_at = lv_tick_get(); }
    on[ZQ_WIFI] = wl; snprintf(sub[ZQ_WIFI], 40, "%s", !wl ? "off" : g_zq_ssid_ok && g_zq_ssid[0] ? g_zq_ssid : "on");
    int bs = bt_state(); char pn[64] = ""; int conn = bs == BT_ON ? bt_peer_name(pn, sizeof pn) : 0;
    on[ZQ_BT] = bs != BT_OFF; snprintf(sub[ZQ_BT], 40, "%s", bs == BT_TURNING_ON ? "\xE2\x80\xA6" : bs == BT_OFF ? "off" : conn && pn[0] ? pn : "on");
    on[ZQ_GAIN] = simple_cfg_get_int("audio_gain", 0) != 0; snprintf(sub[ZQ_GAIN], 40, "%s", on[ZQ_GAIN] ? "high" : "low");
    on[ZQ_SHUF] = simple_cfg_get_int("work_mode", 0) == 1; snprintf(sub[ZQ_SHUF], 40, "%s", on[ZQ_SHUF] ? "on" : "off");
    int secs = 0; on[ZQ_SLEEP] = ui_sleep_state(&secs) == 1;
    if(on[ZQ_SLEEP]) snprintf(sub[ZQ_SLEEP], 40, "%dm", (secs + 59) / 60); else snprintf(sub[ZQ_SLEEP], 40, "off");
    on[ZQ_MODE] = ui_get_source_mode() != 0; snprintf(sub[ZQ_MODE], 40, "%s", zq_mode_name());
    lv_label_set_text(g_zq_ic[ZQ_MODE], mode_glyph());
    for(int i = 0; i < ZQ_N; i++){
        lv_obj_set_style_bg_color(g_zq_btn[i], on[i] ? ui_current_accent() : TC(SURFACE), 0);
        lv_obj_set_style_text_color(g_zq_ic[i], on[i] ? TC(ON_ACCENT) : TC(TEXT_PRIMARY), 0);
        if(strcmp(lv_label_get_text(g_zq_sub[i]), sub[i])) lv_label_set_text(g_zq_sub[i], sub[i]);
    }
    if(g_zq_bright && !lv_obj_has_state(g_zq_bright, LV_STATE_PRESSED)) lv_arc_set_value(g_zq_bright, ui_get_brightness());
}
static void zq_tap(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    switch(i){
        case ZQ_WIFI: { int on = wifi_toggle(); ui_toast(on ? "Turning on Wi-Fi\xE2\x80\xA6" : "Wi-Fi off"); g_zq_ssid_at = 0; } break;
        case ZQ_BT:   { int on = bt_toggle(); ui_toast(on ? "Turning on Bluetooth\xE2\x80\xA6" : "Bluetooth off"); } break;
        case ZQ_GAIN: { int hi = !simple_cfg_get_int("audio_gain", 0); ui_set_gain(hi); simple_cfg_set_int("audio_gain", hi);
                        ui_toast(hi ? "High gain" : "Low gain"); } break;
        case ZQ_SHUF: ui_np_set_playmode(simple_cfg_get_int("work_mode", 0) == 1 ? 0 : 1); break;
        case ZQ_SLEEP: npmenu_open_sleep(); return;
        case ZQ_MODE: modes_open(); return;
    }
    zqs_paint();
}
static void zq_hold(lv_event_t *e){
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(i == ZQ_WIFI){ onboarding_discovered(HINT_RADIO_HOLD); wifi_open(); }
    else if(i == ZQ_BT){ onboarding_discovered(HINT_RADIO_HOLD); bt_open(); }
}
static void zq_tick(lv_timer_t *t){ (void)t; if(screen_current() == SCR_QUICK) zqs_paint(); }
static void zqs_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0); lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    static const char *const NAME[ZQ_N] = { "wi-fi", "bluetooth", "gain", "shuffle", "sleep", "mode" };
    static const char *const ICON[ZQ_N] = { LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_CHARGE, LV_SYMBOL_SHUFFLE, "\xEF\x86\x86", LV_SYMBOL_SD_CARD };
    static const char *const ACT[ZQ_N] = { "qs.zune.wifi", "qs.zune.bt", "qs.zune.gain", "qs.zune.shuffle", "qs.zune.sleep", "qs.zune.mode" };
    for(int i = 0; i < ZQ_N; i++){
        int cx = 92 + (i % 3) * 88, y = 52 + (i / 3) * 102;
        lv_obj_t *b = lv_obj_create(root); lv_obj_remove_style_all(b); kit_keep(b);
        lv_obj_set_size(b, 62, 62); lv_obj_set_pos(b, cx - 31, y);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        /* the tap lands on a small invisible target with a wide hit area (the theme's pass would restyle a clickable
         * 62 px circle as one of its own ring buttons) */
        lv_obj_t *hit = lv_obj_create(root); lv_obj_remove_style_all(hit); kit_keep(hit);
        lv_obj_set_size(hit, 24, 24); lv_obj_set_pos(hit, cx - 12, y + 19); lv_obj_set_ext_click_area(hit, 24);
        lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
        ui_on(hit, zq_tap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i, ACT[i], UI_CORE);
        lv_obj_add_event_cb(hit, zq_hold, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        lv_obj_t *ic = lv_label_create(b); kit_keep(ic); lv_label_set_text(ic, ICON[i]);
        lv_obj_set_style_text_font(ic, i == ZQ_SLEEP ? TF(ICON_28) : TF(UI_20), 0);
        if(i == ZQ_SLEEP){ lv_obj_set_style_transform_pivot_x(ic, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(ic, LV_PCT(50), 0);
                           lv_obj_set_style_transform_scale(ic, 190, 0); }
        lv_obj_center(ic);
        g_zq_btn[i] = b; g_zq_ic[i] = ic;
        lv_obj_t *n = lv_label_create(root); kit_keep(n); lv_label_set_text(n, NAME[i]);
        lv_obj_set_style_text_font(n, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(n, TC(TEXT_PRIMARY), 0);
        lv_obj_set_width(n, 86); lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_pos(n, cx - 43, y + 66);
        lv_obj_t *sb = lv_label_create(root); kit_keep(sb); lv_label_set_text(sb, "");
        lv_label_set_long_mode(sb, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(sb, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(sb, TC(TEXT_MUTED), 0);
        lv_obj_set_size(sb, 86, lv_font_get_line_height(zune_font(&font_zune_regular_12)));
        lv_obj_set_style_text_align(sb, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_pos(sb, cx - 43, y + 83);
        g_zq_sub[i] = sb;
    }
    /* brightness: an arc round the bottom of the glass, the level filling from the left, a white dot on it */
    lv_obj_t *br = lv_arc_create(root); kit_keep(br);
    lv_obj_set_size(br, 328, 328); lv_obj_center(br);
    lv_obj_add_flag(br, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_rotation(br, 0); lv_arc_set_bg_angles(br, 35, 145); lv_arc_set_mode(br, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(br, 4, 40); lv_arc_set_value(br, ui_get_brightness());
    lv_obj_set_style_arc_width(br, 4, LV_PART_MAIN); lv_obj_set_style_arc_color(br, TC(CONTROL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(br, 6, LV_PART_INDICATOR); lv_obj_set_style_arc_color(br, TC(TEXT_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(br, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(br, TC(TEXT_PRIMARY), LV_PART_KNOB); lv_obj_set_style_bg_opa(br, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(br, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(br, bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(br, bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(br, bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    g_zq_bright = br;
    lv_obj_t *sun = lv_label_create(root); kit_keep(sun); lv_label_set_text(sun, WI_SUN);
    lv_obj_set_style_text_font(sun, TF(ICON_28), 0); lv_obj_set_style_text_color(sun, TC(TEXT_MUTED), 0);
    lv_obj_set_style_transform_pivot_x(sun, LV_PCT(50), 0); lv_obj_set_style_transform_pivot_y(sun, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(sun, 150, 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_t *bl = lv_label_create(root); kit_keep(bl); lv_label_set_text(bl, "brightness");
    lv_obj_set_style_text_font(bl, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(bl, TC(TEXT_MUTED), 0);
    lv_obj_align(bl, LV_ALIGN_TOP_MID, 0, 288);
    zune_status(root, SCR_QUICK);
    lv_timer_create(zq_tick, 1000, NULL);
    zqs_paint();
}
