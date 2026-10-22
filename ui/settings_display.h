/* Presentation only: TABLE retains saved keys, defaults and handlers. */
typedef struct {const char *key;void (*apply)(int);const char *page,*label;int order;} display_entry_t;
static const display_entry_t DISPLAY_ENTRIES[]={
    {"imm_scrub",NULL,"Immersive","Circular control",30},
    {"imm_scrub_action",NULL,"Immersive","Control action",40},
    {"brightness",NULL,"Display","Brightness",1},
    {"theme_preset",NULL,"Display","Theme",2},
    {"theme_variant",NULL,"Appearance","Light / dark",10},
    {"theme_auto",NULL,"Appearance","Automatic appearance",20},
    {"outdoor",NULL,"Appearance","Outdoor mode",30},
    {"null_palette",NULL,"Appearance","Colour scheme",40},
    {"disco_sheen",NULL,"Appearance","Rim sheen",50},
    {"disco_eq",NULL,"Appearance","Equalizer colours",60},
    {"disco_vol",NULL,"Appearance","Volume colours",70},
    {"null_drawer_state_colours",NULL,"Appearance","Menu colours",80},
    {"simple_background",NULL,"Music screen","Background",10},
    {"null_music_background",NULL,"Music screen","Background",20},
    {"disco_controls",NULL,"Music screen","Controls",30},
    {"null_controls",NULL,"Music screen","Controls",40},
    {"np_style",NULL,"Music screen","Now Playing style",80},
    {"np_font",NULL,"Music text","Track font",10},
    {"null_music_fonts",NULL,"Music text","Track font",20},
    {"disco_title_al",NULL,"Music text","Text alignment",30},
    {"null_text_align",NULL,"Music text","Text alignment",40},
    {"disco_clock",NULL,"Music info","Show clock",10},
    {"null_clock",NULL,"Music info","Show clock",20},
    {"disco_times",NULL,"Music info","Track times",30},
    {"null_music_album",NULL,"Music info","Show album",40},
    {"disco_trackno",NULL,"Music info","Track number",50},
    {"null_trackno",NULL,"Music info","Track number",60},
    {"simple_play_state",NULL,"Music info","Playback state",70},
    {"simple_play_mode",NULL,"Music info","Playback mode",80},
    {"disco_progress",NULL,"Music progress","Progress colour",10},
    {"disco_prog_shape",NULL,"Music progress","Progress shape",20},
    {"imm_style",NULL,"Immersive","Immersive style",10},
    {"imm_lyrics",NULL,"Immersive","Immersive lyrics",20},
    {"saver_idx",NULL,"Standby","Screensaver delay",10},
    {"screenoff_idx",NULL,"Standby","Screen-off delay",20},
    {"saver_style",NULL,"Standby","Screensaver style",30},
    {"disco_qs_style",NULL,"Quick settings","Layout",10},
    {"null_qs_layout",NULL,"Quick settings","Layout",20},
    {"disco_hints",NULL,"Navigation","Interaction hints",50},
    {"disco_side_pick",NULL,"Navigation","Navigation side",60},
    {"null_side",NULL,"Navigation","Navigation side",70},
    {"null_autohide",NULL,"Navigation","Menu timeout",80},
    {"disco_title_hold",NULL,"Gestures","Title hold",10},
    {"null_title_hold",NULL,"Gestures","Title hold",20},
    {"disco_nav_double",NULL,"Gestures","Menu double tap",30},
    {"null_nav_double",NULL,"Gestures","Menu double tap",40},
    {"disco_nav_hold",NULL,"Gestures","Menu hold",50},
    {"null_nav_hold",NULL,"Gestures","Menu hold",60},
    {"swipe_thresh",NULL,"Gestures","Back-swipe distance",70},
    {"font_size",NULL,"Layout & extras","Text size",10},
    {CTL_CFG_ROT,NULL,"Layout & extras","Screen rotation",20},
    {"time_24h",NULL,"Layout & extras","24-hour time",30},
    {"disco_scroll",NULL,"Layout & extras","List motion",40},
    {"null_scroll",NULL,"Layout & extras","List motion",50},
    {"anim",NULL,"Layout & extras","Screen transitions",60},
    {"track_numbers",NULL,"Layout & extras","Album track numbers",70},
    {"album_view",NULL,"Layout & extras","Album view",80},
    {"disc_colour",NULL,"Layout & extras","Startup disc colour",90},
    {"online_art",NULL,"Online & cache","Online album art",10},
    {"online_lyrics",NULL,"Online & cache","Online lyrics",20},
    {"artcache",NULL,"Online & cache","Artwork cache",30},
    {"weather_on",NULL,"Online & cache","Weather",40},
    {NULL,apply_grp_musictext,"Music screen","Text",50},
    {NULL,apply_grp_musicinfo,"Music screen","Information",60},
    {NULL,apply_grp_musicprogress,"Music screen","Progress",70},
    {NULL,apply_grp_immersive,"Music screen","Immersive",90},
    {NULL,apply_grp_themecolours,"Display","Appearance",3},
    {NULL,apply_disco_options,"Display","Music screen",4},
    {NULL,apply_grp_screenstandby,"Display","Standby",5},
    {NULL,apply_null_options,"Display","Navigation",6},
    {NULL,apply_grp_textlayout,"Display","Layout & extras",7},
    {NULL,apply_open_colorpick,"Appearance","Accent colour",40},
    {NULL,apply_disco_menu,"Navigation","Menu items",10},
    {NULL,apply_null_menu,NULL,"Menu items",10},
    {NULL,apply_grp_discoqs,"Navigation","Quick settings",20},
    {NULL,apply_null_qs,NULL,"Quick settings",20},
    {NULL,apply_shortcuts,"Navigation","Shortcuts",30},
    {NULL,apply_grp_discolayout,"Navigation","Gestures",40},
    {NULL,apply_hints_reset,"Navigation","Show hints again",80},
    {NULL,apply_disco_qs_items,"Quick settings","Items",30},
    {NULL,apply_grp_onlineextras,"Layout & extras","Online & cache",1000},
    {NULL,apply_grp_musicscreen,NULL,"Music screen",0},
    {NULL,apply_grp_discocolours,NULL,"Colours",0},
};

static const display_entry_t *display_entry(const setting_t *s){
    for(size_t i=0;i<sizeof DISPLAY_ENTRIES/sizeof DISPLAY_ENTRIES[0];i++){
        const display_entry_t *d=&DISPLAY_ENTRIES[i];
        if(d->key ? s->cfg_key && !strcmp(d->key,s->cfg_key) : s->type==ST_ACTION && s->apply==d->apply)return d;
    }
    return NULL;
}
static int display_variant_hidden(const setting_t *s){
    static const struct{const char *common,*null_key;} pairs[]={
        {"simple_background","null_music_background"},{"disco_controls","null_controls"},
        {"np_font","null_music_fonts"},{"disco_title_al","null_text_align"},
        {"disco_clock","null_clock"},{"disco_trackno","null_trackno"},
        {"disco_qs_style","null_qs_layout"},{"disco_side_pick","null_side"},
        {"disco_title_hold","null_title_hold"},{"disco_nav_double","null_nav_double"},
        {"disco_nav_hold","null_nav_hold"},{"disco_scroll","null_scroll"}
    };
    if(!s->cfg_key)return 0;
    for(size_t i=0;i<sizeof pairs/sizeof pairs[0];i++)if(!strcmp(s->cfg_key,th_null()?pairs[i].common:pairs[i].null_key))return 1;
    return 0;
}
static const char *display_label(const setting_t *s){const display_entry_t *d=display_entry(s);return d?d->label:s->label;}
static const char *display_disabled_reason(const setting_t *s){
    const char *k=s->cfg_key?s->cfg_key:"";
    if(!strcmp(k,"imm_scrub_action")&&!simple_cfg_get_int("imm_scrub",0))return "Enable Circular control first.";
    if(s->apply==apply_open_colorpick && (th_simple()||th_null()))return accent_disabled_reason();
    if(!strcmp(k,"theme_variant") && th_null())return "Null uses a fixed dark appearance. Choose a colour scheme instead.";
    if(!strcmp(k,"theme_auto") && !theme_auto_supported())return "Automatic light/dark switching is available in Disco, Ring and Braun.";
    if(!strcmp(k,"null_side"))return "Null keeps its navigation on the right.";
    if(s->apply==apply_output && !th_zune())return "The output picker belongs to the Zune theme.";
    if((!strcmp(k,"zlock_src") || !strcmp(k,"zlock_tint")) && !th_zune())return "This option belongs to the Zune lock screen.";
    if(!strcmp(k,"null_palette") && !th_null())return "Colour schemes are specific to Null. Use light/dark and accent settings for this theme.";
    if(!strncmp(k,"null_",5) && !th_null())return "This option is available in Null.";
    if(!strcmp(k,"simple_background") && !th_simple())return "This theme uses its own artwork background.";
    if(!strncmp(k,"simple_play_",12) && !th_simple())return "These indicators belong to the Squash music screen.";
    if(!strcmp(k,"saver_style") && (th_braun()||th_disco_family()||th_null()))return "This theme has its own screensaver design.";
    if((!strcmp(k,"np_style") || !strcmp(k,"disc_colour")) && (th_disco_family()||th_null()))return "This theme uses its own Music and startup design.";
    if(th_simple()){
        if(!strcmp(k,"disco_controls"))return "Squash uses gesture controls on its Music screen.";
        if(!strcmp(k,"disco_sheen"))return "Squash uses its own border styling.";
        if(!strcmp(k,"disco_side_pick"))return "Squash keeps its navigation on the right.";
        if(!strcmp(k,"disco_qs_style"))return "Squash uses its own fixed Quick settings layout.";
    }
    if(!th_disco_family() && !strncmp(k,"disco_",6) && strcmp(k,"disco_hints"))return "This option controls the Disco and Squash layout.";
    if(!th_disco_family() && !th_null() && (s->apply==apply_disco_menu || !strcmp(k,"np_font")))return "This option belongs to the Disco, Squash and Null music/navigation layouts.";
    if(!th_disco_family() && !th_null() && (s->apply==apply_hints_reset || !strcmp(k,"disco_hints")))return "Interaction hints are available in Disco, Squash and Null.";
    return NULL;
}
static int display_blocked(const setting_t *s){const char *reason=display_disabled_reason(s);if(reason){ui_toast(reason);return 1;}return 0;}
static const char *display_group_alias(const char *g){
    static const struct{const char *old,*page;} aliases[]={
        {"Theme Colours","Appearance"},{"Text & Layout","Layout & extras"},{"Online & Extras","Online & cache"},
        {"Disco Options","Music screen"},{"Null Options","Navigation"},{"Music Screen","Music screen"},
        {"Disco Colours","Appearance"},{"Disco Layout","Gestures"},{"Quick Settings","Quick settings"}
    };
    for(size_t i=0;i<sizeof aliases/sizeof aliases[0];i++)if(!strcmp(g,aliases[i].old))return aliases[i].page;
    return g;
}
