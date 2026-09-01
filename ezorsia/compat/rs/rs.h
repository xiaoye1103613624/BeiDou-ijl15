#pragma once
// rs.h - Multi-resolution module (self-contained, no modifications to existing headers)

#define RS_SCREEN_WIDTH_MAX     2560
#define RS_SCREEN_HEIGHT_MAX    1440
#define RS_SCREEN_MESSAGE_WIDTH 400
#define RS_TIER_COUNT           9
#define RS_TIER_MAX             (RS_TIER_COUNT - 1)

// Runtime resolution globals
extern int rs_width;
extern int rs_height;
extern int rs_adjust_cy;
extern int rs_tier;
// BeiDou login/char-select resolution from config.ini (not forced 800x600)
extern int rs_login_w;
extern int rs_login_h;
// true only when neither config.ini nor rs_soScreenResolution.txt has a tier —
// field stays at login dims. A saved tier always wins over follow-login.
extern bool rs_field_follow_login;

// Accessors
int rs_get_width();
int rs_get_height();
int rs_get_adjust_cy();
void rs_set_login_dims(int w, int h);
int rs_tier_from_dims(int w, int h);
// Re-read field tier from DLL-dir config.ini / sidecar into rs_tier / follow_login.
// Returns true when an explicit field tier is active (follow_login cleared).
bool rs_sync_tier_from_ini();
// Pin a CWnd layer to LT origin at screen-absolute (l,t). Safe no-op if layer missing.
bool rs_force_wnd_lt_abs(void* wnd, int l, int t);

// Main attach (call once, after game fully initialized)
void rs_attach();
// Apply field resolution (call from CField bootstrap; may run BEFORE rs_attach)
void rs_on_enter_field();
// Register with ModRegistry (call once at DllMain, after all static init done)
void rs_register();
// Custom.wz resource manager (call once at DllMain, BEFORE InitializeResMan)
void rs_resman_init();
