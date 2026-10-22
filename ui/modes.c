#include "simple_theme.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include <string.h>
#include "orbit.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "ipc.h"
#include "theme_kit.h"
#include "modes.h"
#include "config.h"
#include <unistd.h>
#include <glob.h>
#include <math.h>

/* Working Mode (audio source) picker - mirrors stock's "Working mode" list. Tapping a mode replays
 * the captured V2.28 switch sequence via ui_set_source_mode() and marks it selected.
 * 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage. The fifth row, USB Audio (stock's "USB AUDIO"), is an OUTPUT
 * route of the Local source (the Disc feeds an external USB DAC), not a source mode: see the output section. */

typedef struct { const char *l1, *l2, *glyph; int mode; } modeinfo_t;
/* 2x3 grid. `mode` is the index ui_set_source_mode() takes; grid order is presentation only.
 * Row 1: Local / USB DAC / BT DAC   Row 2: BT streaming / AirPlay / USB storage
 * BT streaming (4) and AirPlay (5) use the V2.40-verified 0657 values 07 and 0A. */
static const modeinfo_t MODES[] = {
    { "Playback", "",         LV_SYMBOL_SD_CARD,    0 },
    { "USB",     "DAC",       LV_SYMBOL_USB,        1 },
    { "BT",      "DAC",       LV_SYMBOL_BLUETOOTH,  2 },
    { "BT",      "streaming", LV_SYMBOL_VOLUME_MAX, 4 },
    { "AirPlay", "",          LV_SYMBOL_WIFI,       5 },
    { "USB",     "storage",   LV_SYMBOL_DRIVE,      3 },
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))
#define ROW_USB_AUDIO 100 /* only present in DISKOS_TEST_OUTPUTS builds; not 4 (= BT streaming's mode) */

/* OUTPUT-ROUTE-BEGIN */
/* Output route: Internal DAC / SPDIF / USB Audio (SPDIF row + USB Audio picker row: -DDISKOS_TEST_OUTPUTS builds only).
 * Stock V2.57 (mq_ui_257.dis):
 *   Settings SPDIF toggle (0x480510 -> 0x464f0c):  0666 <6|4>, 0657 8                       (no 0642, no sleep)
 *   Working Mode callback 0x46bbac (200 ms one-shot): internal/SPDIF (0x46bd28): 0666 <6|4>, usleep 65 ms, 0642 0, 0657 8
 *                                                    USB AUDIO (0x46bd90):       0666 3, 0642 5, 0657 8
 * 0666 payloads are the player's out modes (mq_player_257 set_out_device 0x47bf24): 6 local analog, 4 SPDIF_TX, 3
 * USB_HOST. The player's 0666 handler (0x4f1294) closes the player and re-inits the output; 0657 8 then sets
 * LOCALPLAYER again, so a 0666 not followed by 0657 8 leaves NO_WORK_MODE (dead player). Stock sends no pause;
 * diskOS does (0666 into a live stream killed mq_player, scratch/device-runs/sacd-hang-0929): a switch is pause ->
 * PCM proven quiet for OUT_QUIET_MS -> route -> work mode -> resume. Leaving USB audio always uses the full sequence
 * (0642 0 drops the host request). USB_HOST needs the USB DAC card (else the player uses a silent USB_HOST_NULL
 * sink, 0x47c040), so diskOS re-checks it right before the frames and refuses instead.
 * If a send fails after the route moved, the internal sequence is retried until it fully lands on a live player
 * connection (recovery-pending: every play/route/source command is refused meanwhile). */
#ifndef OUT_PCM_GLOB
#define OUT_PCM_GLOB "/proc/asound/card*/pcm*p/sub*/status"
#endif
#ifndef OUT_USB_CARD
#define OUT_USB_CARD "/proc/asound/card1"   /* the USB DAC card; its usbid node exists for USB audio cards only (UNVERIFIED on this kernel) */
#endif
#define OUT_QUIET_MS 500
#define OUT_WAIT_MS  3000
#define OUT_WORKMODE "0657000C0008"
#define OUT_PAUSE    "0201000C0000"
typedef struct { const char *route, *gadget; unsigned settle_us; } out_seq_t;
static const out_seq_t OUT_SEQ[3] = {
    { "0666000C0006", "0642000C0000", 65000 },   /* OUT_INTERNAL */
    { "0666000C0004", "0642000C0000", 65000 },   /* OUT_SPDIF */
    { "0666000C0003", "0642000C0005", 0 },       /* OUT_USB */
};
static void modes_ui_refresh(void);
static int g_out_route = OUT_INTERNAL;
static unsigned g_out_gen = 0;
static struct { int target, was_playing, full; unsigned gen; char path[256]; long pos; uint32_t t0, quiet0; lv_timer_t *tm; } g_osw;
static int g_rec = 0; static unsigned g_rec_gen = 0; static lv_timer_t *g_rec_tm = NULL;

/* the route a switch really landed on, remembered across a restart (modes_boot_check): only where a switch lands -
 * the "a new player is on the internal DAC" resets at start must not wipe it before the boot check reads it */
static void out_remember(int r){ if(cfg_get_int("out_usb_last", 0) != (r == OUT_USB)) cfg_set_int("out_usb_last", r == OUT_USB); }
static void out_set_state(int r){
    g_out_route = r; g_out_gen = ipc_generation();
    if(simple_cfg_get_int("spdif", 0) != (r == OUT_SPDIF)) simple_cfg_set_int("spdif", r == OUT_SPDIF);   /* the Settings toggle mirrors the real route, never a wish */
}
int modes_output_route(void){
    if(g_out_route != OUT_INTERNAL && g_out_gen != ipc_generation()){ out_set_state(OUT_INTERNAL); }   /* a restarted player is back on the internal DAC */
    return g_out_route;
}
void modes_output_reset(void){ out_set_state(OUT_INTERNAL); }
int modes_output_busy(void){ return g_osw.tm != NULL || g_rec; }
/* Route-aware local init. with_gadget (the boot timer): internal/SPDIF send 0642 0 first; USB always pairs 0666 3 with 0642 5. */
int modes_local_init(int with_gadget){
    int r = modes_output_route();
    if(r != OUT_USB && with_gadget && ipc_send_cmd(OUT_SEQ[r].gadget) < 0) return -1;
    int rc = ipc_send_cmd(OUT_SEQ[r].route) < 0 ? -1 : 0;
    if(rc < 0 && with_gadget) return -1;   /* the boot timer retries the whole init next tick */
    /* the play preamble (with_gadget 0) is best effort like the old inline pair: 0657 8 is attempted whatever 0666 returned */
    if(r == OUT_USB && ipc_send_cmd(OUT_SEQ[r].gadget) < 0) rc = -1;
    if(ipc_send_cmd(OUT_WORKMODE) < 0) rc = -1;
    return rc;
}
/* 1 only when every playback PCM node is readable and none is RUNNING or DRAINING; unreadable / no nodes = not proven */
static int out_pcm_quiet(void){
    glob_t g; int quiet = 1;
    if(glob(OUT_PCM_GLOB, 0, NULL, &g) != 0) return 0;
    for(size_t i = 0; i < g.gl_pathc && quiet; i++){
        FILE *f = fopen(g.gl_pathv[i], "r"); char l[96] = "";
        if(!f){ quiet = 0; break; }
        if(!fgets(l, sizeof l, f)) l[0] = 0;
        fclose(f);
        if(!l[0] || strstr(l, "RUNNING") || strstr(l, "DRAINING")) quiet = 0;
    }
    globfree(&g);
    return quiet;
}
static int out_usb_card_ok(void){            /* any USB audio card: a usbid node under /proc/asound, or "USB-Audio" in its cards list */
    char p[96]; snprintf(p, sizeof p, "%s/usbid", OUT_USB_CARD);
    if(access(p, F_OK) == 0) return 1;
    glob_t g; int ok = 0;
    if(glob("/proc/asound/card*/usbid", 0, NULL, &g) == 0){ ok = g.gl_pathc > 0; globfree(&g); }
    if(ok) return 1;
    FILE *f = fopen("/proc/asound/cards", "r"); if(!f) return 0;
    char l[256]; while(!ok && fgets(l, sizeof l, f)) ok = strstr(l, "USB-Audio") != NULL || strstr(l, "USB Audio") != NULL;
    fclose(f); return ok;
}
/* 0 = whole sequence sent; -1 = the 0666 was refused (nothing changed); -2 = the route moved but the tail failed */
static int out_seq_send(int t, int full){
    if(ipc_send_cmd(OUT_SEQ[t].route) < 0) return -1;
    if(full){
        if(OUT_SEQ[t].settle_us) usleep(OUT_SEQ[t].settle_us);
        if(ipc_send_cmd(OUT_SEQ[t].gadget) < 0) return -2;
    }
    if(ipc_send_cmd(OUT_WORKMODE) < 0) return -2;
    return 0;
}
/* recovery-pending: retry the full internal sequence until it lands on the same live player connection */
static void out_rec_clear(void){ if(g_rec_tm){ lv_timer_del(g_rec_tm); g_rec_tm = NULL; } g_rec = 0; }
static void out_recover_tick(lv_timer_t *t){
    (void)t;
    if(ipc_generation() != g_rec_gen){ out_rec_clear(); out_set_state(OUT_INTERNAL); modes_ui_refresh(); return; }   /* a new player initialises itself */
    if(!ipc_is_ready()) return;
    if(out_seq_send(OUT_INTERNAL, 1) == 0){
        out_rec_clear(); out_set_state(OUT_INTERNAL); out_remember(OUT_INTERNAL);
        ui_toast("Internal DAC restored"); modes_ui_refresh();
    }
}
static void out_rec_enter(void){
    out_set_state(OUT_INTERNAL);
    if(g_rec) return;
    g_rec = 1; g_rec_gen = ipc_generation();
    g_rec_tm = lv_timer_create(out_recover_tick, 500, NULL);
}
/* resume only the same track, and only if the seek and the play toggle were accepted */
static void out_resume(int moved){
    if(!g_osw.was_playing) return;
    track_state_t st; ipc_get_state(&st);
    if(moved && (ipc_generation() != g_osw.gen || !st.have_track || strcmp(st.path, g_osw.path) != 0)){ ui_toast("Output switched - press play"); return; }
    if(moved && g_osw.pos > 0 && ui_seek_to(g_osw.pos) < 0){ ui_toast("Couldn't resume - press play"); return; }
    if(ipc_send_cmd(OUT_PAUSE) < 0) ui_toast("Couldn't resume - press play");   /* toggle: plays again */
}
/* after a switch to USB audio: wait for the DAC to enumerate on the new host port and say what was found */
static void out_usb_name(char *out, size_t cap);
static lv_timer_t *g_uw; static uint32_t g_uw_t0;
static void out_usb_watch_tick(lv_timer_t *t){
    (void)t;
    if(modes_output_route() != OUT_USB){ lv_timer_del(g_uw); g_uw = NULL; return; }
    if(out_usb_card_ok()){
        lv_timer_del(g_uw); g_uw = NULL;
        char n[96], b[140]; out_usb_name(n, sizeof n);
        snprintf(b, sizeof b, th_zune() ? "playing through usb-c \xC2\xB7 %s" : "USB audio - %s", n[0] ? n : "usb audio device");
        ui_toast(b); return;
    }
    if(lv_tick_elaps(g_uw_t0) > 8000){
        lv_timer_del(g_uw); g_uw = NULL;
        ui_toast(th_zune() ? "no usb-c audio found \xC2\xB7 check the cable or adapter" : "No USB audio device found - check the cable");
    }
}
static void out_usb_watch(void){ if(g_uw) lv_timer_del(g_uw); g_uw_t0 = lv_tick_get(); g_uw = lv_timer_create(out_usb_watch_tick, 500, NULL); }
static int out_run(void){
    int target = g_osw.target;
    int rc = out_seq_send(target, g_osw.full);
    if(rc == -1){ out_set_state(modes_output_route()); ui_toast("Couldn't switch output"); out_resume(0); modes_ui_refresh(); return -1; }
    if(rc == 0){ out_set_state(target); out_remember(target); out_resume(1); modes_ui_refresh(); if(target == OUT_USB) out_usb_watch(); return 0; }
    /* the route moved but the tail failed: back to the internal DAC; never end without 0657 8 landing */
    if(out_seq_send(OUT_INTERNAL, 1) == 0){
        out_set_state(OUT_INTERNAL); out_remember(OUT_INTERNAL);
        ui_toast("Output switch failed - internal DAC restored");
        out_resume(1); modes_ui_refresh();
        return -1;
    }
    out_rec_enter();
    ui_toast("Output state uncertain - recovering");
    modes_ui_refresh();
    return -1;
}
static void osw_stop(const char *why, int resume){
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    out_set_state(modes_output_route());
    ui_toast(why);
    if(resume) out_resume(0);
    modes_ui_refresh();
}
static void osw_tick(lv_timer_t *t){
    (void)t;
    track_state_t st; ipc_get_state(&st);
    if(ipc_generation() != g_osw.gen){ osw_stop("Player restarted - try again", 0); return; }
    if(strcmp(st.path, g_osw.path) != 0){ osw_stop("Track changed - try again", 0); return; }
    if(lv_tick_elaps(g_osw.t0) >= OUT_WAIT_MS){ osw_stop("Player is busy - try again", 0); return; }   /* deadline first: a late tick never routes, even with the hold complete */
    if(ui_is_playing() || !out_pcm_quiet()) g_osw.quiet0 = 0;
    else if(!g_osw.quiet0) g_osw.quiet0 = lv_tick_get() ? lv_tick_get() : 1;
    if(!g_osw.quiet0 || lv_tick_elaps(g_osw.quiet0) < OUT_QUIET_MS){
        return;
    }
    const char *why = ui_output_blocked();
    if(why){ osw_stop(why, g_osw.was_playing); return; }
    /* no USB-card check here: the port stays a USB device (the PC-DAC gadget) until 0642 5 asks for host mode, so an
     * attached DAC can only enumerate AFTER the switch. Stock sends the frames blind; out_usb_watch() reports after. */
    lv_timer_del(g_osw.tm); g_osw.tm = NULL;
    out_run();
}
static int out_switch(int target, int via_mode){
    if(target < OUT_INTERNAL || target > OUT_USB) return -1;
    if(modes_output_busy()){ ui_toast("Switching..."); return -1; }
    const char *why = ui_output_blocked();
    if(why){ out_set_state(modes_output_route()); ui_toast(why); return -1; }
    int was_playing = ui_is_playing();
    if(!was_playing && !out_pcm_quiet()){ out_set_state(modes_output_route()); ui_toast("Player is busy - try again"); return -1; }   /* PCM live but not "playing": a pause toggle would START it */
    track_state_t st; ipc_get_state(&st);
    memset(&g_osw, 0, sizeof g_osw);
    g_osw.target = target; g_osw.was_playing = was_playing;
    g_osw.full = via_mode || target == OUT_USB || modes_output_route() == OUT_USB;   /* Settings toggle = stock's 2-frame form, except to/from USB */
    g_osw.gen = ipc_generation(); g_osw.pos = st.have_track ? st.position_ms : 0;
    snprintf(g_osw.path, sizeof g_osw.path, "%s", st.have_track ? st.path : "");
    if(was_playing && ipc_send_cmd(OUT_PAUSE) < 0){ out_set_state(modes_output_route()); ui_toast("Player is busy - try again"); return -1; }
    g_osw.t0 = lv_tick_get();
    g_osw.tm = lv_timer_create(osw_tick, 100, NULL);
    return 0;
}
int modes_output_switch(int target){ return out_switch(target, 0); }
int modes_output_mode_switch(int target){ return out_switch(target, 1); }
/* OUTPUT-ROUTE-END */
static orbit_t g_orb;                /* six modes orbiting a hub; the hub's ring shows the state */
static int slot_of_mode(int m){ for(int i=0;i<N_MODES;i++) if(MODES[i].mode == m) return i; return -1; }

/* Disco: the modes as rows (the Settings look); BT streaming left out (it's ordinary Bluetooth playback) */
static const int DISCO_ORD[] = { 0, 5, 2, 1, 4 };            /* MODES[] slots: Local, USB storage, BT DAC, USB DAC, AirPlay */
#define N_DISCO ((int)(sizeof DISCO_ORD / sizeof DISCO_ORD[0]))
static lv_obj_t *g_drow[N_MODES];
/* while a mode changes: the turning-arrows icon spins (instead of a "Switching..." text) */
static void spin_exec(void *o, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)o, v, 0); }
static void icon_spin(lv_obj_t *l, int on){
    if(!l) return;
    if(!on){ lv_anim_delete(l, spin_exec); lv_obj_set_style_transform_rotation(l, 0, 0); return; }
    if(lv_anim_get(l, spin_exec)) return;                       /* already turning */
    lv_obj_update_layout(l);
    lv_obj_set_style_transform_pivot_x(l, lv_obj_get_width(l) / 2, 0); lv_obj_set_style_transform_pivot_y(l, lv_obj_get_height(l) / 2, 0);
    lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, l); lv_anim_set_exec_cb(&a, spin_exec);
    lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000); lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}
static void disco_paint(int slot, int pending){
    lv_color_t acc = ui_current_accent();
    for(int i = 0; i < N_MODES; i++){
        lv_obj_t *r = g_drow[i]; if(!r) continue;
        int on = (i == slot);
        lv_obj_set_style_border_width(r, on ? 2 : 0, 0); lv_obj_set_style_border_color(r, acc, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(r, 0), on ? acc : TC(TEXT_SECONDARY), 0);
        lv_obj_t *v = lv_obj_get_child(r, 2);
        lv_label_set_text(v, on ? (pending ? LV_SYMBOL_REFRESH : LV_SYMBOL_OK) : "");
        lv_obj_set_style_text_color(v, acc, 0);
        icon_spin(v, on && pending);                                    /* the arrows turn while it switches */
    }
}
static void zmodes_paint(int slot, int pending); static lv_obj_t *g_zmname;
static void paint(int slot, const char *glyph){   /* slot = orbit index, or -1 for none */
    if(th_zune()){ if(g_zmname) zmodes_paint(slot, glyph && !strcmp(glyph, LV_SYMBOL_REFRESH)); return; }
    if((th_disco_family() || th_null())){ disco_paint(slot, glyph && !strcmp(glyph, LV_SYMBOL_REFRESH)); return; }
    int pending = (glyph && !strcmp(glyph, LV_SYMBOL_REFRESH));
    lv_color_t acc = TC(ACCENT_PRIMARY);
    if(pending){                                  /* the tapped mode gets a ring; the hub's ring spins */
        orbit_set_pending(&g_orb, slot, acc);
        orbit_hub_set(&g_orb, LV_SYMBOL_REFRESH, acc, "");                /* turning arrows, no text */
        icon_spin(g_orb.hub_icon, 1);
        orbit_hub_ring(&g_orb, ORBIT_RING_SPIN, acc, 0);
        return;
    }
    orbit_set_pending(&g_orb, -1, acc);
    icon_spin(g_orb.hub_icon, 0);
    for(int i = 0; i < N_MODES; i++) orbit_set_on(&g_orb, i, i == slot, acc);   /* the active mode: filled */
    if(slot >= 0){ orbit_hub_set(&g_orb, MODES[slot].glyph, acc, "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_FULL, acc, 0); }
    else { orbit_hub_set(&g_orb, LV_SYMBOL_SD_CARD, TC(TEXT_PRIMARY), "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_GREY, acc, 0); }
}
static void mark_selected_mode(int cur){ paint(slot_of_mode(cur), LV_SYMBOL_OK); }
static void mark_selected(void){ mark_selected_mode(ui_get_source_mode()); }
static void modes_ui_refresh(void){ mark_selected(); }


static void hub_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static int  cur_mode(void);
static void modeinfo_refresh_and_show(void);
static uint32_t g_last_switch = 0;   /* debounce: a switch takes a few seconds to apply in the player */

/* Pending state: the tapped row shows a "switching" glyph (not the confirmed checkmark) while the
 * gadget switch is in flight - there is no source-mode completion readback, so after the switch
 * window we settle to the selection best-effort (matches the honest "Switching..." toast). */
static void mark_pending(int m){ paint(slot_of_mode(m), LV_SYMBOL_REFRESH); }

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;   /* the mode a switch is settling to (so reopening the screen keeps showing "switching") */
/* M17: after the switch window, settle to the CONFIRMED mode read from the real USB gadget state,
 * not a blind assumption. If the gadget shows the switch didn't take, reflect reality + say so. */
static void settle_cb(lv_timer_t *t){
    (void)t;
    if(g_osw.tm) return;
    if(ui_source_switch_pending()) return;
    if(lv_tick_elaps(g_last_switch) < 3200) return;
    lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    if(ui_source_switch_failed()){ mark_selected_mode(-1); return; }
    /* M17: DISPLAY the ACTUAL gadget state (read-only) instead of a blind timer assumption. Do NOT
     * mutate the intent mirror (g_source_mode) - it also guards coldplug, and a transient mid-transition
     * sample must not flip that guard. */
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){   /* USB gadget modes are readback-confirmable */
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    /* intended == 2 (BT receiving) is gadget-invisible and needs a phone to connect - no reliable
     * readback here, so show the intent without asserting a false confirmation. */
    mark_selected_mode(show);
    if(show != 0 && screen_current() == SCR_WORKMODE) modeinfo_refresh_and_show();   /* the mode's own screen */
}

static void row_cb(int slot){                 /* a tap inside a slice (the wheel does the hit-testing) */
    if(slot < 0 || slot >= N_MODES) return;
    int m = MODES[slot].mode;
    /* Serialise: ignore taps while the previous switch is still applying (the player's gadget
     * state-machine is asynchronous). NB we do NOT early-return on "same mode" - re-issuing must
     * always be allowed so Local works as a recover even if our cached mode is stale. */
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000) return;   /* still switching: the arrows are turning */
    g_last_switch = lv_tick_get();
    if(m == ROW_USB_AUDIO){   /* USB Audio: an output route of the Local source, not a source mode */
        if(modes_output_mode_switch(OUT_USB) == 0){
            g_pending_mode = m; mark_pending(m);
            if(g_settle) lv_timer_del(g_settle);
            g_settle = lv_timer_create(settle_cb, 500, NULL);
            ui_toast("Switching to USB audio output");
        }
        return;
    }
    if(ui_set_source_mode(m) == 0){
        g_pending_mode = m;
        mark_pending(m);      /* async switch in flight: show "switching", not a confirmed selection */
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 500, NULL);   /* settle to the checkmark after the switch window */
        /* honest wording: the frames are queued; the async switch completes a moment later. */
        static const char *msg[] = {
            "Switching to playback", "Switching to USB DAC",
            "Switching to Bluetooth receiving", "Switching to USB storage",
            "Bluetooth streaming on", "AirPlay on \xE2\x80\x93 pick the Disc on your device" };
        ui_toast(msg[m]);
    } else g_last_switch = 0;                                  /* refused (the reason was toasted, if any): tappable again */
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(g_pending_mode);  /* a switch is still settling - keep showing it */
    else {
        mark_selected();
        if(cur_mode() != 0){ modeinfo_refresh_and_show(); return; }       /* a mode is active: its screen first */
    }
    screen_show(SCR_WORKMODE);
}

static void disco_row_cb(lv_event_t *e){ row_cb((int)(intptr_t)lv_event_get_user_data(e)); }
/* Match Shortcuts: five fixed pill rows, with a larger shared header. */
static void squash_modes_create(lv_obj_t *root){
    disco_title(root,"Working mode");
    static const char *names[N_MODES]={"Playback","USB DAC","Bluetooth DAC","BT streaming","AirPlay","USB storage"};
    memset(g_drow,0,sizeof g_drow);
    for(int k=0;k<N_DISCO;k++){
        int i=DISCO_ORD[k];
        lv_obj_t *r=squash_menu_row(root,k,MODES[i].glyph,TF(UI_24),names[i],NULL,NULL);g_drow[i]=r;
        ui_on(r,disco_row_cb,LV_EVENT_CLICKED,(void *)(intptr_t)i,"modes.select",UI_CORE);
    }
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);mark_selected();
}

/* ==== Zune (Figma 39 / 72): five modes round the glass as circles, the one in use filled in the accent; its name in
 * the middle with a quiet line under it; while switching the target's circle wears a turning arc and the middle says
 * so ==== */
LV_FONT_DECLARE(font_zune_light_32) LV_FONT_DECLARE(font_zune_regular_12) LV_FONT_DECLARE(font_zune_regular_13)
LV_FONT_DECLARE(font_zune_semibold_11) LV_FONT_DECLARE(font_zune_regular_14)
const lv_font_t *zune_font(const lv_font_t *base);
#define ZM_N 6
#define ZM_USBA 99                                             /* not a source mode: the Local source routed to a USB-C DAC */
static const int ZM_SLOT[ZM_N] = { 0, 1, 5, ZM_USBA, 4, 2 };   /* playback, usb dac, usb storage, usb-c audio, airplay, bt receiver */
static const char *const ZM_NAME[ZM_N] = { "playback", "usb dac", "usb storage", "usb-c audio", "airplay", "bt receiver" };
static const char *const ZM_SUB[ZM_N] = { "normal listening", "the disc is a computer's sound card", "the computer sees the sd card",
                                          "headphones or a dac", "stream from an iphone or mac", "a phone plays through the disc" };
static lv_obj_t *g_zmc[ZM_N], *g_zmg[ZM_N], *g_zmcap[ZM_N], *g_zmsub, *g_zmspin;
static int zm_of_slot(int slot){ for(int k = 0; k < ZM_N; k++) if(ZM_SLOT[k] == slot) return k; return -1; }
static void zspin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void zmodes_paint(int slot, int pending){
    if(!pending && slot == 0 && modes_output_route() == OUT_USB) slot = ZM_USBA;   /* playback, routed to usb-c */
    int on = zm_of_slot(slot);
    for(int k = 0; k < ZM_N; k++){
        int a = k == on && !pending;
        lv_obj_set_style_bg_color(g_zmc[k], a ? TC(ACCENT_PRIMARY) : TC(SURFACE), 0);
        lv_obj_set_style_text_color(g_zmg[k], TC(TEXT_PRIMARY), 0);
        lv_obj_set_style_text_color(g_zmcap[k], k == on ? TC(ACCENT_PRIMARY) : TC(TEXT_MUTED), 0);
    }
    lv_anim_delete(g_zmspin, zspin_exec);
    if(pending && on >= 0){
        lv_obj_remove_flag(g_zmspin, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align_to(g_zmspin, g_zmc[on], LV_ALIGN_CENTER, 0, 0);
        lv_anim_t an; lv_anim_init(&an); lv_anim_set_var(&an, g_zmspin); lv_anim_set_exec_cb(&an, zspin_exec);
        lv_anim_set_values(&an, 0, 360); lv_anim_set_duration(&an, 1100); lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&an);
        lv_label_set_text(g_zmname, "switching");
        char b[64]; snprintf(b, sizeof b, "to %s \xC2\xB7 playback pauses", ZM_NAME[on]); lv_label_set_text(g_zmsub, b);
    } else {
        lv_obj_add_flag(g_zmspin, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_zmname, on >= 0 ? ZM_NAME[on] : "playback");
        char b[80]; snprintf(b, sizeof b, "active \xC2\xB7 %s", ZM_SUB[on >= 0 ? on : 0]); lv_label_set_text(g_zmsub, b);
    }
}
/* usb-c audio (stock Working mode > USB AUDIO): the same frames stock sends - 0666 3, 0642 5 (USB host), 0657 8 -
 * behind diskOS's pause / wait-for-silence guard; playback here takes the route back to the jack */
static void zm_out(int target){
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000) return;
    if(modes_output_route() == target) return;
    g_last_switch = lv_tick_get();
    if(modes_output_mode_switch(target) == 0) paint(target == OUT_USB ? ZM_USBA : 0, LV_SYMBOL_REFRESH);
    else { g_last_switch = 0; mark_selected(); }
}
static void zm_tap(lv_event_t *e){
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if(ZM_SLOT[k] == ZM_USBA){ zm_out(OUT_USB); return; }
    if(ZM_SLOT[k] == 0 && modes_output_route() == OUT_USB && ui_get_source_mode() == 0){ zm_out(OUT_INTERNAL); return; }
    row_cb(ZM_SLOT[k]);
}
static void zmodes_create(lv_obj_t *root){
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    for(int k = 0; k < ZM_N; k++){
        float a = (-90.0f + k * 60.0f) * 0.0174533f;
        int cx = 180 + (int)lroundf(cosf(a) * 120), cy = 180 + (int)lroundf(sinf(a) * 120) - 10;
        lv_obj_t *c = lv_obj_create(root); lv_obj_remove_style_all(c); kit_keep(c); g_zmc[k] = c;
        lv_obj_set_size(c, 56, 56); lv_obj_set_pos(c, cx - 28, cy - 28); lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0); lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *g = lv_label_create(c); kit_keep(g); g_zmg[k] = g; lv_label_set_text(g, ZM_SLOT[k] == ZM_USBA ? LV_SYMBOL_AUDIO : MODES[ZM_SLOT[k]].glyph);
        lv_obj_set_style_text_font(g, TF(UI_20), 0); lv_obj_center(g);
        lv_obj_t *cap = lv_label_create(root); kit_keep(cap); g_zmcap[k] = cap; lv_label_set_text(cap, ZM_NAME[k]);
        lv_obj_set_style_text_font(cap, zune_font(&font_zune_regular_12), 0);
        lv_obj_update_layout(cap); lv_obj_set_pos(cap, cx - lv_obj_get_width(cap) / 2, cy + 32);
        if(k == 0) lv_obj_add_flag(cap, LV_OBJ_FLAG_HIDDEN);           /* the top one's name is the big one in the middle */
        lv_obj_t *h = lv_obj_create(root); lv_obj_remove_style_all(h); kit_keep(h);
        lv_obj_set_size(h, 24, 24); lv_obj_set_pos(h, cx - 12, cy - 12);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_ext_click_area(h, 20);
        ui_on(h, zm_tap, LV_EVENT_CLICKED, (void *)(intptr_t)k, "modes.zune", UI_CORE);
    }
    g_zmspin = lv_arc_create(root); kit_keep(g_zmspin);
    lv_obj_remove_style(g_zmspin, NULL, LV_PART_KNOB); lv_obj_remove_flag(g_zmspin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_zmspin, 68, 68); lv_arc_set_bg_angles(g_zmspin, 0, 90); lv_arc_set_value(g_zmspin, 0);
    lv_obj_set_style_arc_width(g_zmspin, 3, LV_PART_MAIN); lv_obj_set_style_arc_color(g_zmspin, TC(ACCENT_PRIMARY), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_zmspin, true, LV_PART_MAIN); lv_obj_set_style_arc_width(g_zmspin, 0, LV_PART_INDICATOR);
    lv_obj_add_flag(g_zmspin, LV_OBJ_FLAG_HIDDEN);
    g_zmname = lv_label_create(root); kit_keep(g_zmname);
    lv_obj_set_style_text_font(g_zmname, zune_font(&font_zune_light_32), 0); lv_obj_set_style_text_color(g_zmname, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_zmname, LV_ALIGN_TOP_MID, 0, 160);
    g_zmsub = lv_label_create(root); kit_keep(g_zmsub);
    lv_obj_set_style_text_font(g_zmsub, zune_font(&font_zune_regular_12), 0); lv_obj_set_style_text_color(g_zmsub, TC(ACCENT_PRIMARY), 0);
    lv_label_set_long_mode(g_zmsub, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_width(g_zmsub, 150);
    lv_obj_set_style_text_align(g_zmsub, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(g_zmsub, LV_ALIGN_TOP_MID, 0, 200);
    mark_selected();
}
void modes_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* back button (kept out of the clipped top-left corner) */
    /* no header: the wheel fills the panel and its hub is the way back */

    if(th_simple() || th_null()){squash_modes_create(root);return;}
    if(th_zune()){ zmodes_create(root); return; }
    if((th_disco_family() || th_null())){
        disco_title(root, "Working mode");
        static const char *NM[N_MODES] = { "Playback", "USB DAC", "Bluetooth DAC", "BT streaming", "AirPlay", "USB storage" };
        memset(g_drow, 0, sizeof g_drow);
        for(int k = 0; k < N_DISCO; k++){
            int i = DISCO_ORD[k];
            int y = 54 + k * 58, h = 56;
            g_drow[i] = disco_row(root, y, h, MODES[i].glyph,
                                 i == 1 ? TF(UI_20) : TF(UI_24), NM[i], disco_row_cb, (void *)(intptr_t)i);
            lv_obj_remove_event_cb(g_drow[i],disco_row_cb);
            ui_on(g_drow[i],disco_row_cb,LV_EVENT_CLICKED,(void *)(intptr_t)i,"modes.select",UI_CORE);
            lv_obj_set_style_bg_opa(g_drow[i], LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_set_style_border_width(g_drow[i], 2, LV_STATE_PRESSED);
            lv_obj_set_style_border_color(g_drow[i], ui_current_accent(), LV_STATE_PRESSED);
            lv_obj_set_style_border_opa(g_drow[i], LV_OPA_COVER, LV_STATE_PRESSED);
            /* Rounded caps fit the rim more closely than a rectangular bounding box.
             * Keep the final row readable while using the panel down to y=342. */
            int dy = y + h / 2 - 180, radius = h / 2;
            int half = (int)sqrtf((176 - radius) * (176 - radius) - dy * dy) + radius;
            int right = 180 + half;
            if(k > 0 && k < 4){
                int clear_right=disco_mirror()?360-disco_clear_left(y,y+h):disco_clear_right(y,y+h);
                if(!clear_right)clear_right=360;
                if(right > clear_right) right = clear_right; /* leave the section picker clear */
            }
            /* Shorter caps and a real hit-target gutter beyond Back's 64px zone.
             * Mirror the protected edge with Side; source switching is unchanged. */
            int left=180-half+12;
            /* Match QS's free-edge bow while retaining rounded-cap clearance
             * and a 4px gutter beyond Back's 64px strip. */
            int curve=(int)lroundf(0.6f*(180.0f-sqrtf((float)(180*180-dy*dy))));
            int safe_left=68+curve;
            if(left<safe_left)left=safe_left;
            right-=12;
            int width=right-left;
            if(width>222)width=222; /* middle caps bow away from the nav flyout */
            lv_obj_set_x(g_drow[i], disco_mx(left,width));
            lv_obj_set_width(g_drow[i],width);
            lv_obj_set_style_text_font(lv_obj_get_child(g_drow[i],0),TF(UI_20),0);
            lv_obj_align(lv_obj_get_child(g_drow[i],0),LV_ALIGN_LEFT_MID,8,0);
            lv_obj_t *label=lv_obj_get_child(g_drow[i],1);
            lv_obj_set_width(label,width-62);lv_obj_align(label,LV_ALIGN_LEFT_MID,34,0);
            lv_obj_set_style_text_font(label,TF(UI_22),0);
            lv_obj_set_style_text_letter_space(label,-1,0);
            lv_obj_align(lv_obj_get_child(g_drow[i],2),LV_ALIGN_RIGHT_MID,-8,0);
            lv_obj_set_style_text_font(lv_obj_get_child(g_drow[i], 2), TF(UI_20), 0);
        }
        mark_selected();
        return;
    }
    /* orbit: six modes, clockwise from the upper right; the top stays open for the title */
    orbit_title(root, "Working mode");
    static char caps[N_MODES][32];
    orbit_item_t it[N_MODES];
    for(int i = 0; i < N_MODES; i++){
        snprintf(caps[i], sizeof caps[i], "%s%s%s", MODES[i].l1, MODES[i].l2[0] ? " " : "", MODES[i].l2);
        it[i].glyph = MODES[i].glyph; it[i].cap = caps[i];
    }
    orbit_create(&g_orb, root, it, N_MODES, -60, row_cb);
    orbit_hub_create(&g_orb, root, hub_back_cb, LV_SYMBOL_SD_CARD, "Back");
    mark_selected();
    if(th_braun()) br_face(root);                          /* Braun: the grille face (knobs come from orbit.c) */
}


/* ================================================================== the active mode's screen (SCR_MODEINFO)
 * One screen for USB DAC / USB storage / BT DAC / BT streaming / AirPlay: a big icon in a status ring (Ring) or
 * on a dark knob with its lamp (Braun), the mode name, one status line and one detail line, and two buttons:
 * Modes (the picker) and Local (back to normal playback). Everything shown is read from the system in
 * cheap, non-blocking ways once a second while the screen is up. */
static lv_obj_t *g_mi_ring, *g_mi_glyph, *g_mi_knob, *g_mi_title, *g_mi_status, *g_mi_detail1, *g_mi_detail2;
static lv_timer_t *g_mi_timer;
static int g_mi_mode = -1, g_mi_wait = -2;

static int cur_mode(void){                                   /* the intent mirror, or the real gadget after a UI restart */
    int m = ui_get_source_mode();
    if(m == 0 && !(g_last_switch && lv_tick_elaps(g_last_switch) < 8000)){   /* not while a switch to Local is still tearing the gadget down */
        int d = ui_detect_source_mode(); if(d) m = d;
    }
    return m;
}
static int udc_host_connected(void){                         /* the USB device controller reports "configured" once a host enumerated us */
    DIR *d = opendir("/sys/class/udc"); if(!d) return 0;
    int ok = 0; struct dirent *e;
    while(!ok && (e = readdir(d))){
        if(e->d_name[0] == '.') continue;
        char p[128], b[32] = {0}; snprintf(p, sizeof p, "/sys/class/udc/%.60s/state", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(fgets(b, sizeof b, f)) ok = !strncmp(b, "configured", 10);
        fclose(f);
    }
    closedir(d); return ok;
}
static long card_gb(void){                                   /* SD capacity from the block device, readable while it is exported */
    FILE *f = fopen("/sys/block/mmcblk0/size", "r"); if(!f) return 0;
    long long sec = 0; if(fscanf(f, "%lld", &sec) != 1) sec = 0; fclose(f);
    return (long)((sec * 512LL + 500000000LL) / 1000000000LL);
}
static void mi_spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void mi_set_wait(int wait){                           /* Ring: a turning arc while waiting, a full ring when connected */
    if(wait == g_mi_wait) return;
    g_mi_wait = wait;
    if(g_mi_knob){ br_knob_set_on(g_mi_knob, !wait); return; }
    if(!g_mi_ring) return;
    lv_anim_delete(g_mi_ring, mi_spin_exec);
    lv_obj_set_style_arc_color(g_mi_ring, ui_current_accent(), LV_PART_INDICATOR);
    if(wait){
        lv_arc_set_value(g_mi_ring, 260);
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_mi_ring); lv_anim_set_exec_cb(&a, mi_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else { lv_arc_set_rotation(g_mi_ring, 270); lv_arc_set_value(g_mi_ring, 1000); }
}
static void mi_text(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

static void modeinfo_fill(int m);
static void mi_back_async(void *u){ (void)u; if(screen_current() == SCR_MODEINFO) screen_back(); }
void modeinfo_refresh(void){
    int m = cur_mode();
    if(m == 0){ lv_async_call(mi_back_async, NULL); return; }   /* nothing active: leave, after the screen change in progress */
    modeinfo_fill(m);
}
void modes_test_pending(int m){ mark_pending(m); }           /* host renders */
void modes_test_info(int m){ modeinfo_fill(m); }
static void modeinfo_fill(int m){
    if(m < 0 || m > 5) m = 1;
    int slot = slot_of_mode(m); if(slot < 0) slot = 1;       /* the SAME table the orbit menu is built from: identical icon + name */
    if(m != g_mi_mode){
        char nm[40]; snprintf(nm, sizeof nm, "%s%s%s", MODES[slot].l1, MODES[slot].l2[0] ? " " : "", MODES[slot].l2);
        if(th_zune()) for(char *c = nm; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
        g_mi_mode = m; g_mi_wait = -2; mi_text(g_mi_glyph, MODES[slot].glyph); mi_text(g_mi_title, nm);
    }
    char st[64] = "", d1[200] = "", d2[200] = ""; int wait = 1;
    track_state_t ts; ipc_get_state(&ts);
    switch(m){
        case 1: {                                            /* USB DAC */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected" : "Waiting for USB host");
            if(host){
                snprintf(d1, sizeof d1, "Volume %d", ts.volume > 0 ? ts.volume : 0);
                if(ts.sample_rate > 0) snprintf(d2, sizeof d2, "%d.%d kHz", ts.sample_rate / 1000, (ts.sample_rate % 1000) / 100);
            } else snprintf(d1, sizeof d1, "Plug the Disc into a computer");
        } break;
        case 3: {                                            /* USB storage */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected to computer" : "Waiting for USB host");
            long gb = card_gb(); if(gb > 0) snprintf(d1, sizeof d1, "SD card %ld GB", gb);
            snprintf(d2, sizeof d2, "Eject on the computer first");
        } break;
        case 2: {                                            /* BT DAC: a phone / computer plays to the Disc */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Connected" : "Waiting for a device");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair from the other device");
            snprintf(d2, sizeof d2, "Volume %d", ts.volume > 0 ? ts.volume : 0);
        } break;
        case 4: {                                            /* BT streaming: the Disc plays to a speaker */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Streaming to" : "No speaker connected");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair one in Bluetooth settings");
        } break;
        case 5: {                                            /* AirPlay */
            int has = ts.have_track && ts.title[0]; wait = !has;
            snprintf(st, sizeof st, has ? "Playing" : "Ready");
            if(has){ snprintf(d1, sizeof d1, "%s", ts.title); snprintf(d2, sizeof d2, "%s", ts.artist); }
            else snprintf(d1, sizeof d1, "Pick the Disc on your device");
        } break;
    }
    mi_set_wait(wait);
    if(th_zune()){                                            /* the Zune voice: status lower case, the detail line in capitals */
        for(char *c = st; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
        if(m != 5) for(char *c = d1; *c; c++) if(*c >= 'A' && *c <= 'Z' && (c == d1 || c[-1] != ' ' || 1)) { if(m == 1 || m == 3) *c += 32; }
        if(m != 5) for(char *c = d2; *c; c++) if(*c >= 'a' && *c <= 'z') *c -= 32;
    }
    mi_text(g_mi_status, st); mi_text(g_mi_detail1, d1); mi_text(g_mi_detail2, d2);
}
static void modeinfo_refresh_and_show(void){ screen_show(SCR_MODEINFO); }
static void mi_tick(lv_timer_t *t){ (void)t; if(screen_current() == SCR_MODEINFO) modeinfo_refresh(); }
static void mi_modes_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_show(SCR_WORKMODE); }
static lv_obj_t *mi_pill(lv_obj_t *root, const char *txt, int x, int primary, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 104, 40); lv_obj_align(b, LV_ALIGN_CENTER, x, 128);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    uint32_t bg = th_braun() ? (primary ? BR_ACC : BR_KNOB) : (primary ? TH_ACCENT : TH_SURF1);
    lv_obj_set_style_bg_color(b, theme_color_from_rgb(bg), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, th_braun() ? br_font(14, 0) : TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(l, th_braun() ? TC(ON_CONTROL) : TC(TEXT_PRIMARY), 0);
    lv_obj_center(l);
    return b;
}
static lv_obj_t *mi_label(lv_obj_t *root, const lv_font_t *font, uint32_t col, int y, int w){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, theme_color_from_rgb(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(l, w); lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}
/* Zune (Figma 40 / 41 / 43): the rim as the live ring, "WORKING MODE" small, the mode's icon in an accent circle, its
 * name in Light, the status under it, the detail in small accent capitals, and "back to playback" */
static void zmi_back(lv_event_t *e){ (void)e; row_cb(slot_of_mode(0)); screen_show(SCR_WORKMODE); }
static void zmodeinfo_style(lv_obj_t *root){
    lv_obj_set_size(g_mi_ring, 352, 352); lv_obj_center(g_mi_ring);
    lv_obj_set_style_arc_width(g_mi_ring, 2, LV_PART_MAIN); lv_obj_set_style_arc_width(g_mi_ring, 4, LV_PART_INDICATOR);
    lv_obj_t *cap = lv_label_create(root); kit_keep(cap); lv_label_set_text(cap, "WORKING MODE");
    lv_obj_set_style_text_font(cap, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, TC(TEXT_MUTED), 0); lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_t *c = lv_obj_create(root); lv_obj_remove_style_all(c); kit_keep(c);
    lv_obj_set_size(c, 76, 76); lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 64); lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(c, 3, 0); lv_obj_set_style_border_color(c, TC(ACCENT_PRIMARY), 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_parent(g_mi_glyph, c); lv_obj_set_style_text_font(g_mi_glyph, TF(UI_24), 0); lv_obj_center(g_mi_glyph);
    lv_obj_set_style_text_font(g_mi_title, zune_font(&font_zune_light_32), 0); lv_obj_set_style_text_color(g_mi_title, TC(TEXT_PRIMARY), 0);
    lv_obj_align(g_mi_title, LV_ALIGN_TOP_MID, 0, 156);
    lv_obj_set_style_text_font(g_mi_status, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(g_mi_status, TC(TEXT_MUTED), 0);
    lv_obj_set_width(g_mi_status, 240); lv_obj_align(g_mi_status, LV_ALIGN_TOP_MID, 0, 198);
    lv_obj_set_style_text_font(g_mi_detail1, zune_font(&font_zune_regular_13), 0); lv_obj_set_style_text_color(g_mi_detail1, TC(TEXT_MUTED), 0);
    lv_obj_set_width(g_mi_detail1, 240); lv_obj_align(g_mi_detail1, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_set_style_text_font(g_mi_detail2, &font_zune_semibold_11, 0); lv_obj_set_style_text_color(g_mi_detail2, TC(ACCENT_PRIMARY), 0);
    lv_obj_set_style_text_letter_space(g_mi_detail2, 1, 0); lv_obj_align(g_mi_detail2, LV_ALIGN_TOP_MID, 0, 246);
    lv_obj_t *bk = lv_label_create(root); kit_keep(bk); lv_label_set_text(bk, "back to playback");
    lv_obj_set_style_text_font(bk, zune_font(&font_zune_regular_14), 0); lv_obj_set_style_text_color(bk, TC(ACCENT_PRIMARY), 0);
    lv_obj_align(bk, LV_ALIGN_TOP_MID, 0, 286); lv_obj_add_flag(bk, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_ext_click_area(bk, 14);
    ui_on(bk, zmi_back, LV_EVENT_CLICKED, NULL, "modeinfo.zune.back", UI_CORE);
    lv_obj_t *ls[4] = { g_mi_title, g_mi_status, g_mi_detail1, g_mi_detail2 }; for(int k = 0; k < 4; k++) kit_keep(ls[k]);
    kit_keep(g_mi_ring); kit_keep(g_mi_glyph);
}
void modeinfo_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, TC(CANVAS), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    int br = th_braun();
    if(br) br_face(root);
    if(br){
        g_mi_knob = br_knob(root, 180, 104, 56, LV_SYMBOL_USB, TF(UI_48));
        g_mi_glyph = lv_obj_get_child(g_mi_knob, 0);                        /* the icon label the knob made */
        if(g_mi_glyph && !lv_obj_check_type(g_mi_glyph, &lv_label_class)) g_mi_glyph = NULL;
    } else {
        g_mi_ring = lv_arc_create(root);
        lv_obj_remove_style(g_mi_ring, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(g_mi_ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(g_mi_ring, 132, 132); lv_obj_align(g_mi_ring, LV_ALIGN_CENTER, 0, -76);
        lv_arc_set_bg_angles(g_mi_ring, 0, 360); lv_arc_set_range(g_mi_ring, 0, 1000); lv_arc_set_rotation(g_mi_ring, 270);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_MAIN); lv_obj_set_style_arc_color(g_mi_ring, TC(CONTROL_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_mi_ring, true, LV_PART_INDICATOR);
        g_mi_glyph = lv_label_create(root);
        lv_label_set_text(g_mi_glyph, LV_SYMBOL_USB);
        lv_obj_set_style_text_font(g_mi_glyph, TF(UI_48), 0);
        lv_obj_set_style_text_color(g_mi_glyph, TC(TEXT_PRIMARY), 0);
        lv_obj_align(g_mi_glyph, LV_ALIGN_CENTER, 0, -76);
    }
    g_mi_title   = mi_label(root, br ? br_font(22, 1) : TH_F_TITLE,  br ? BR_TXT  : TH_TXT1, 6,  260);
    g_mi_status  = mi_label(root, br ? br_font(16, 0) : TF(UI_16), br ? BR_ACC : TH_ACCENT, 36, 260);
    g_mi_detail1 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT2 : TH_TXT2, 64, 250);
    g_mi_detail2 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT3 : TH_TXT3, 86, 250);
    if(th_zune()){ zmodeinfo_style(root); g_mi_timer = lv_timer_create(mi_tick, 1000, NULL); return; }
    mi_pill(root, "Modes", 0, 0, mi_modes_cb);                       /* back to Playback: from the Modes list */
    g_mi_timer = lv_timer_create(mi_tick, 1000, NULL);
}

/* ---- Zune output picker (Figma 81) + the USB-C plug-in toast (Figma 82) ---------------------------------------
 * A dial of the outputs: the jack (internal DAC), usb-c (an external USB DAC / USB-C headphones: the stock
 * "USB AUDIO" route), spdif, bluetooth. The current one in the accent; an output with nothing attached says why.
 * Switching uses the guarded sequence above (pause, wait for silence, route, resume). */
#include "zune_dial.h"
#ifndef OUT_ASOUND_CARDS
#define OUT_ASOUND_CARDS "/proc/asound/cards"
#endif
static void out_usb_name(char *out, size_t cap){     /* the USB card's long name from /proc/asound/cards ("1 [xxx ]: USB-Audio - Name") */
    out[0] = 0;
    FILE *f = fopen(OUT_ASOUND_CARDS, "r"); if(!f) return;
    char l[256];
    while(fgets(l, sizeof l, f)){
        char *p = l; while(*p == ' ') p++;
        if(!strstr(p, "USB-Audio") && !strstr(p, "USB Audio")) continue;
        char *d = strstr(p, " - "); if(!d) break;
        d += 3; d[strcspn(d, "\r\n")] = 0;
        snprintf(out, cap, "%s", d);
        for(char *c = out; *c; c++) if(*c >= 'A' && *c <= 'Z') *c += 32;
        break;
    }
    fclose(f);
}
enum { ZO_JACK, ZO_USB, ZO_SPDIF, ZO_BT, ZO_N };
static lv_obj_t *g_zo;
static int zo_current(void){
    const char *m = ui_route_mac(); if(m && m[0]) return ZO_BT;
    int r = modes_output_route();
    return r == OUT_USB ? ZO_USB : r == OUT_SPDIF ? ZO_SPDIF : ZO_JACK;
}
static void zo_close(void){ if(g_zo){ lv_obj_t *o = g_zo; g_zo = NULL; lv_obj_delete_async(o); } }
static void zo_go(int i){
    int cur = zo_current();
    if(i == cur) return;
    if(i == ZO_BT){ bt_open(); return; }
    if(cur == ZO_BT) ui_route_analog();                       /* off the speaker first: back to the DAC */
    int target = i == ZO_USB ? OUT_USB : i == ZO_SPDIF ? OUT_SPDIF : OUT_INTERNAL;
    if(target == modes_output_route()) return;
    if(modes_output_mode_switch(target) == 0)
        ui_toast(i == ZO_USB ? "switching to usb-c \xC2\xB7 playback pauses for a moment" :
                 i == ZO_SPDIF ? "switching to spdif \xC2\xB7 playback pauses for a moment" : "switching to the headphone jack \xC2\xB7 playback pauses for a moment");
}
static void zo_pick_async(void *u){ zo_close(); zo_go((int)(intptr_t)u); }
static void zo_pick(int i){ lv_async_call(zo_pick_async, (void *)(intptr_t)i); }
static void zo_bg_cb(lv_event_t *e){ (void)e; zo_close(); }
LV_FONT_DECLARE(font_zune_semibold_11)
void modes_output_open(void){
    zo_close();
    g_zo = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(g_zo); kit_keep(g_zo);
    lv_obj_set_size(g_zo, 360, 360);
    lv_obj_set_style_bg_color(g_zo, TC(CANVAS), 0); lv_obj_set_style_bg_opa(g_zo, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_zo, LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(g_zo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_zo, zo_bg_cb, LV_EVENT_CLICKED, NULL);
    static const char *const LB[ZO_N] = { "headphones", "usb-c", "spdif", "bluetooth" };
    int cur = zo_current(), usb = out_usb_card_ok();
    zdial_build(g_zo, LB, ZO_N, cur, zo_pick, 56, "output.zune");
    char un[96], sub[ZO_N][128]; out_usb_name(un, sizeof un);
    const char *bt = ui_route_mac();
    snprintf(sub[ZO_JACK], sizeof sub[0], "headphone jack \xC2\xB7 internal dac");
    if(usb) snprintf(sub[ZO_USB], sizeof sub[0], "%s", un[0] ? un : "usb dac connected");
    else    snprintf(sub[ZO_USB], sizeof sub[0], "usb-c headphones or a dac");   /* the port is a USB device until we switch: no way to see it first */
    snprintf(sub[ZO_SPDIF], sizeof sub[0], "digital out \xC2\xB7 needs a coaxial cable");
    snprintf(sub[ZO_BT], sizeof sub[0], "%s", bt && bt[0] ? "connected" : "pick headphones");
    for(int i = 0; i < ZO_N; i++){
        char b[150];
        if(i == cur){ snprintf(b, sizeof b, "current \xC2\xB7 %s", sub[i]); zdial_set_sub(i, b); zdial_set_color(i, TC(ACCENT_PRIMARY)); }
        else zdial_set_sub(i, sub[i]);
    }
    lv_obj_t *cap = lv_label_create(g_zo); kit_keep(cap); lv_label_set_text(cap, "OUTPUT");
    lv_obj_set_style_text_font(cap, &font_zune_semibold_11, 0); lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_set_style_text_color(cap, TC(TEXT_MUTED), 0); lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 30);
}
/* Figma 82: plugging in a USB-C DAC / headphones offers to switch (6 s). Unplugging while on it falls back to the
 * jack; the player has already lost the device, so this only puts the route (and the picker) right. Called from the
 * 3 s status poll. */
static void zo_switch_usb(void){ if(modes_output_mode_switch(OUT_USB) == 0) ui_toast("switching to usb-c \xC2\xB7 playback pauses for a moment"); }
void modes_usb_poll(void){
    static int was = -1;
    int now = out_usb_card_ok();
    if(was < 0){ was = now; return; }
    if(now == was) return;
    was = now;
    if(now){
        if(modes_output_route() == OUT_USB) return;
        char n[96]; out_usb_name(n, sizeof n);
        if(th_zune()) zune_toast_action(LV_SYMBOL_USB, n[0] ? n : "usb-c headphones", "switch output?", "switch", zo_switch_usb, 6000);
        else ui_toast("USB DAC connected - choose it in Output");
    } else if(modes_output_route() == OUT_USB && !modes_output_busy()){
        if(modes_output_mode_switch(OUT_INTERNAL) != 0) modes_output_reset();
        ui_toast(th_zune() ? "usb-c unplugged \xC2\xB7 back to the headphone jack" : "USB DAC unplugged - back to the headphone jack");
    }
}
void modes_test_route(int r){ out_set_state(r); if(g_zmname) mark_selected(); }   /* host renders */

/* Boot after a USB-C session: the player keeps its USB host route (and its host request) across a restart, so with
 * nothing plugged in every track fails and it runs on through the queue without a sound. If the last route diskOS set
 * was USB-C, put it back on the headphone jack once the player is up (the guarded switch: pause, silence, route).
 * Called from the 3 s status poll; gives up after a few tries so a busy player is never nagged. */
void modes_boot_check(void){
    static int done, tries;
    if(done) return;
    if(cfg_get_int("out_usb_last", -1) == 0){ done = 1; return; }   /* -1: never recorded (first start of this build, after a
                                                                       * USB-C session on the last one): check once too */
    if(!ipc_is_ready() || modes_output_busy() || ui_output_blocked()) return;
    if(++tries > 6){ done = 1; return; }
    out_set_state(OUT_USB);                                    /* what the player is really on: forces the full sequence */
    if(modes_output_mode_switch(OUT_INTERNAL) == 0){ done = 1; fprintf(stderr, "modes: boot - USB route left over, back to the jack\n"); }
}
