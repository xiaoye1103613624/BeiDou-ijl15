#pragma once
// rs.h - Multi-resolution module (self-contained, no modifications to existing headers)

#define RS_SCREEN_WIDTH_MAX     2560
#define RS_SCREEN_HEIGHT_MAX    1440
#define RS_SCREEN_MESSAGE_WIDTH 400
#define RS_TIER_COUNT           19
#define RS_TIER_MAX             (RS_TIER_COUNT - 1)

// Runtime resolution globals
extern int rs_width;
extern int rs_height;
extern int rs_adjust_cy;
extern int rs_tier;
// BeiDou login/char-select resolution from config.ini width/height (kept at boot)
extern int rs_login_w;
extern int rs_login_h;
// true only when config.ini omits soScreenResolution — field stays at login dims.
// When false, rs_tier is the saved in-game resolution and must survive logout/relogin.
extern bool rs_field_follow_login;

// Accessors
int rs_get_width();
int rs_get_height();
int rs_get_adjust_cy();
void rs_set_login_dims(int w, int h);
int rs_tier_from_dims(int w, int h);
// Pin a CWnd layer to LT origin at screen-absolute (l,t). Safe no-op if layer missing.
bool rs_force_wnd_lt_abs(void* wnd, int l, int t);

// Main attach (call once, after game fully initialized)
void rs_attach();
// Apply field resolution (call from CField bootstrap AFTER rs_attach)
void rs_on_enter_field();
// Register with ModRegistry (call once at DllMain, after all static init done)
void rs_register();
// Custom.wz resource manager (call once at DllMain, BEFORE InitializeResMan)
void rs_resman_init();
/** Fail-soft FlushCachedObjects on g_rm. Safe no-op if ResMan not ready. */
void rs_resman_flush_cached(int nUsedBefore);
