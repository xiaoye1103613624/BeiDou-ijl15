// rs.cpp - Multi-resolution module. Self-contained, hooks via ModRegistry.
#include "stdafx.h"
#include "rs.h"
#include "../ModRegistry.h"
#include "../hook.h"
#include "../../Client.h"
#include "../wvs/config.h"
#include "../wvs/wnd.h"
#include "../wvs/tooltip.h"
#include "../wvs/tempstat.h"
#include "../wvs/statusbar.h"
#include "../wvs/ctrlwnd.h"
#include "../wvs/field.h"
#include "../wvs/rtti.h"
#include "../wvs/util.h"
#include "../wvs/wvsapp.h"
#include "../ztl/ztl.h"
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <vector>
#include <cstring>
#include <string>
#include <cstdio>
#include <algorithm>

// ===== Globals =====
int rs_width = 800;
int rs_height = 600;
int rs_adjust_cy = 0;
int rs_tier = 0;
int rs_login_w = 800;
int rs_login_h = 600;
bool rs_field_follow_login = true;

int rs_get_width() { return rs_width; }
int rs_get_height() { return rs_height; }
int rs_get_adjust_cy() { return rs_adjust_cy; }

void rs_set_login_dims(int w, int h) {
    if (w < 640) w = 800;
    if (h < 480) h = 600;
    rs_login_w = w;
    rs_login_h = h;
}

// Stable soScreenResolution IDs. 0..8 W×H must never remap (saved configs).
// 9+ are append-only wider FOV ladders (same height, more width). Max 2560×1440.
// Origin math unchanged: RT/RB abs X = W-800.
static const int kRsTierDims[RS_TIER_COUNT][2] = {
    {800, 600},     // 0
    {1024, 768},    // 1
    {1366, 768},    // 2
    {1600, 900},    // 3
    {1920, 1080},   // 4
    {1280, 720},    // 5
    {1440, 900},    // 6
    {1680, 1050},   // 7
    {2560, 1440},   // 8
    {1440, 720},    // 9
    {1600, 720},    // 10
    {1600, 768},    // 11
    {1800, 720},    // 12
    {1920, 720},    // 13
    {1920, 768},    // 14
    {2240, 720},    // 15
    {2560, 720},    // 16
    {2560, 768},    // 17
    {2560, 1080},   // 18
};
static_assert(sizeof(kRsTierDims) / sizeof(kRsTierDims[0]) == RS_TIER_COUNT,
              "kRsTierDims size must match RS_TIER_COUNT");

static void rs_tier_dims(int tier, int& nw, int& nh) {
    if (tier < 0 || tier > RS_TIER_MAX) {
        nw = 800;
        nh = 600;
        return;
    }
    nw = kRsTierDims[tier][0];
    nh = kRsTierDims[tier][1];
}

int rs_tier_from_dims(int w, int h) {
    for (int i = 0; i < RS_TIER_COUNT; ++i) {
        if (kRsTierDims[i][0] == w && kRsTierDims[i][1] == h) return i;
    }
    // Unknown dims: nearest by width, then height (FOV tracks W; 1920x720 ≠ 1920x1080).
    int best = 0;
    int bestDw = 0x7FFFFFFF;
    int bestDh = 0x7FFFFFFF;
    for (int i = 0; i < RS_TIER_COUNT; ++i) {
        int dw = kRsTierDims[i][0] - w;
        if (dw < 0) dw = -dw;
        int dh = kRsTierDims[i][1] - h;
        if (dh < 0) dh = -dh;
        if (dw < bestDw || (dw == bestDw && dh < bestDh)) {
            bestDw = dw;
            bestDh = dh;
            best = i;
        }
    }
    return best;
}

// SysOpt combo: native m_nSelect / SetSelect are DISPLAY INDEX (this+0x68).
// AddItem dwParam is stored on the item, NOT in m_nSelect — never treat m_nSelect
// as a stable soScreenResolution ID after width-sort (display 4 ≠ tier 4).
static int rs_cb_order[RS_TIER_COUNT]; // display i -> stable id
static bool rs_cb_order_ready = false;

static void rs_fill_width_sorted_ids(int outIds[RS_TIER_COUNT]) {
    struct Item { int id, w, h; };
    Item items[RS_TIER_COUNT];
    for (int i = 0; i < RS_TIER_COUNT; ++i) {
        items[i].id = i;
        items[i].w = kRsTierDims[i][0];
        items[i].h = kRsTierDims[i][1];
    }
    std::sort(items, items + RS_TIER_COUNT, [](const Item& a, const Item& b) {
        if (a.w != b.w) return a.w < b.w;
        if (a.h != b.h) return a.h < b.h;
        return a.id < b.id;
    });
    for (int i = 0; i < RS_TIER_COUNT; ++i) outIds[i] = items[i].id;
}

static void rs_ensure_cb_order() {
    if (rs_cb_order_ready) return;
    rs_fill_width_sorted_ids(rs_cb_order);
    rs_cb_order_ready = true;
}

static int rs_combo_index_from_tier(int tier) {
    rs_ensure_cb_order();
    for (int i = 0; i < RS_TIER_COUNT; ++i) {
        if (rs_cb_order[i] == tier) return i;
    }
    return 0;
}

static int rs_tier_from_combo_index(int idx) {
    rs_ensure_cb_order();
    if (idx < 0 || idx > RS_TIER_MAX) return 0;
    return rs_cb_order[idx];
}

// Origin matrix (DLL-owned)
static IWzVector2DPtr rs_orgEx[9];
static IWzVector2DPtr rs_orgStatusBar;
static IWzVector2DPtr rs_orgScreenMsg;
static IWzVector2DPtr rs_orgQuickSlot;

// ===== Origin matrix (forward decl needed by set_stage) =====
static void RsResetOrgWindow();
static void RsCreateOrigins();
static void RsDestroyOrigins();
static void RsApplyOriginStatusBarMetrics();
static void RsReanchorExistingHud();
static void rs_installLayoutHooks();
static IWzVector2DPtr& RsGetOrgWindow();
static void RsLogFlush(const char* line);

// ===== CWndMan instance access (raw pointer, no header dependency) =====
static void* RsGetCWndMan() {
    return *reinterpret_cast<void**>(0x00BEC20C);
}

// ===== Pattern scan =====
static unsigned char* g_ModuleBase(const char* dll, size_t* pSize) {
    HMODULE h = GetModuleHandleA(dll);
    if (!h) { h = LoadLibraryA(dll); if (!h) return nullptr; }
    MODULEINFO mi;
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) return nullptr;
    *pSize = mi.SizeOfImage;
    return static_cast<unsigned char*>(mi.lpBaseOfDll);
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void* FindPattern(const char* dll, const char* pat) {
    size_t sz;
    unsigned char* base = g_ModuleBase(dll, &sz);
    if (!base) return nullptr;
    unsigned char buf[256], mask[256];
    size_t n = 0;
    while (*pat) {
        if (*pat == ' ') { pat++; continue; }
        if (pat[0] == '?' && pat[1] == '?') { mask[n] = 0; buf[n] = 0; }
        else {
            int hi = HexVal(pat[0]), lo = HexVal(pat[1]);
            if (hi < 0 || lo < 0) return nullptr;
            buf[n] = (unsigned char)((hi << 4) | lo);
            mask[n] = 0xFF;
        }
        pat += 2; n++;
    }
    for (size_t i = 0; i <= sz - n; i++) {
        size_t j;
        for (j = 0; j < n; j++) if ((base[i + j] & mask[j]) != (buf[j] & mask[j])) break;
        if (j == n) return &base[i];
    }
    return nullptr;
}

// ===== CWzGr2D: D3D resolution switch (kaentake-style FindScreenMode) =====
// Offsets are absolute from `this` (MEMBER_AT) — do NOT use C++ padded fields after
// IWzGr2D vptr; that shifted m_screenMode/m_bInitialized by +4 and broke switches.
class RsGr2D : public IWzGr2D {
public:
    struct SCREENMODE {
        unsigned char pad0[0x5C];
        MEMBER_AT(int, 0x0, nWidth)
        MEMBER_AT(int, 0x4, nHeight)
        // Gr2D Reset uses D3DPRESENT_PARAMETERS at this+0x30 = SCREENMODE+0x10.
        // FindScreenMode copies W/H to both +0/+4 and +0x10/+0x14. Custom sizes
        // that only poke +0/+4 leave the backbuffer at the old size → HWND 2560
        // stretches 1024/1920 and looks blurry.
        MEMBER_AT(int, 0x10, nBackBufferWidth)
        MEMBER_AT(int, 0x14, nBackBufferHeight)
        MEMBER_AT(int, 0x2C, nWindowed) // D3DPRESENT_PARAMETERS.Windowed
        MEMBER_AT(int, 0x3C, nRefreshHz) // FullScreen_RefreshRateInHz (0 if windowed)
        MEMBER_AT(int, 0x58, bFullScreen)
    };

    MEMBER_AT(SCREENMODE, 0x20, m_screenMode)
    // +0x90 is IDirect3DDevice8* (TestCooperativeLevel/Reset). Logged as "init="
    // historically; the value is a pointer, not a 0/1 flag.
    MEMBER_AT(void*, 0x90, m_pD3DDevice)
    MEMBER_AT(int, 0x94, m_hrErrorCode)

    typedef int(__thiscall* FindScreenMode_t)(RsGr2D*, SCREENMODE*, int bFullScreen, int w, int h, int unused);
    static FindScreenMode_t FindScreenMode;

    void ApplyPresentSize(int nWidth, int nHeight) {
        m_screenMode.nWidth = nWidth;
        m_screenMode.nHeight = nHeight;
        m_screenMode.nBackBufferWidth = nWidth;
        m_screenMode.nBackBufferHeight = nHeight;
        if (Client::WindowedMode) {
            m_screenMode.nWindowed = 1;
            m_screenMode.nRefreshHz = 0;
            m_screenMode.bFullScreen = 0;
        }
    }

    void ForceWindowedFlag() {
        m_screenMode.bFullScreen = 0;
        m_screenMode.nWindowed = 1;
        m_screenMode.nRefreshHz = 0;
        try {
            PutfullScreen(0);
        } catch (...) {
            RsLogFlush("[RS] PutfullScreen(0) threw");
        }
    }

    HRESULT ScreenResolution(int nWidth, int nHeight) {
        if (!nWidth || !nHeight) return E_INVALIDARG;
        if (m_screenMode.nWidth == nWidth && m_screenMode.nHeight == nHeight) {
            if (Client::WindowedMode)
                ForceWindowedFlag();
            RsLogFlush("[RS] ScreenResolution already matched on Gr2D object");
            return S_OK;
        }
        if (!FindScreenMode) {
            // kaentake / 多分辨率.md: pattern lives in full Gr2D_DX8 (~258KB), NOT the
            // 20KB DX8→Gr2DLib/DX9 stub (pattern miss + garbage m_screenMode → E_FAIL).
            FindScreenMode = reinterpret_cast<FindScreenMode_t>(
                FindPattern("GR2D_DX8.DLL", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 68"));
            if (FindScreenMode) {
                RsLogFlush("[RS] FindScreenMode resolved in GR2D_DX8.DLL");
            } else {
                // DX9 fallback kept for diagnostics only; stock DX9 also lacks this prologue.
                FindScreenMode = reinterpret_cast<FindScreenMode_t>(
                    FindPattern("GR2D_DX9.DLL", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 EC 68"));
                if (FindScreenMode)
                    RsLogFlush("[RS] FindScreenMode resolved in GR2D_DX9.DLL");
            }
        }
        // Do not hard-fail on missing device pointer; pattern miss is the real gate.
        // Soft UpdateResolution-on-fail scrambles HUD — leave alone.
        if (!FindScreenMode) {
            size_t dx8sz = 0, dx9sz = 0;
            g_ModuleBase("GR2D_DX8.DLL", &dx8sz);
            g_ModuleBase("GR2D_DX9.DLL", &dx9sz);
            char buf[192];
            sprintf_s(buf,
                      "[RS] FindScreenMode pattern miss dev=%p dx8_img=%zu dx9_img=%zu "
                      "(need full Gr2D_DX8 ~258KB, not 20KB stub)",
                      m_pD3DDevice, dx8sz, dx9sz);
            RsLogFlush(buf);
            return E_FAIL;
        }
        // SCREENMODE+0x58 is NOT a reliable 0/1 fullscreen flag: after a native
        // FindScreenMode copy it becomes -1. Passing that as `fs` makes the next
        // custom size miss, and `if (!fs)` skips the windowed fallback — SysOpt
        // looks dead after an ultrawide. WindowedMode from config.ini is the source.
        const int fs = Client::WindowedMode ? 0 : 1;
        if (Client::WindowedMode)
            ForceWindowedFlag();
        SCREENMODE mode{};
        if (FindScreenMode(this, &mode, fs, nWidth, nHeight, 0)) {
            m_screenMode = mode;
            ApplyPresentSize(nWidth, nHeight);
            if (Client::WindowedMode)
                ForceWindowedFlag();
            m_hrErrorCode = 0x88760869; // D3DERR_DEVICENOTRESET
            {
                char buf[192];
                sprintf_s(buf, "[RS] FindScreenMode ok -> DEVICENOTRESET %dx%d pp=%dx%d win=%d",
                          nWidth, nHeight, m_screenMode.nBackBufferWidth,
                          m_screenMode.nBackBufferHeight, m_screenMode.nWindowed);
                RsLogFlush(buf);
            }
            return S_OK;
        }
        // Windowed: EnumAdapterModes often omits custom/wide sizes (1920x720,
        // 2560x1080, 1440x900, 1680x1050, …). Keep format/swap, patch W/H AND
        // D3DPRESENT_PARAMETERS at SCREENMODE+0x10 so Reset actually recreates
        // the backbuffer (otherwise HWND 2560 stretches the old 1024/1920 buffer).
        if (Client::WindowedMode) {
            ApplyPresentSize(nWidth, nHeight);
            ForceWindowedFlag();
            m_hrErrorCode = 0x88760869; // D3DERR_DEVICENOTRESET
            {
                char buf[224];
                sprintf_s(buf,
                    "[RS] FindScreenMode miss %dx%d — windowed custom pp=%dx%d win=%d",
                    nWidth, nHeight, m_screenMode.nBackBufferWidth,
                    m_screenMode.nBackBufferHeight, m_screenMode.nWindowed);
                RsLogFlush(buf);
            }
            return S_OK;
        }
        {
            char buf[160];
            sprintf_s(buf, "[RS] FindScreenMode failed %dx%d fs=%d dev=%p",
                      nWidth, nHeight, fs, m_pD3DDevice);
            RsLogFlush(buf);
        }
        return E_FAIL;
    }
};
RsGr2D::FindScreenMode_t RsGr2D::FindScreenMode = nullptr;

// Persist field tier into config.ini so next boot leaves follow-login mode.
static std::string rs_config_ini_path_from_module(HMODULE mod) {
    char path[MAX_PATH] = {};
    if (!mod || !GetModuleFileNameA(mod, path, MAX_PATH))
        return {};
    std::string p(path);
    const size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos)
        return {};
    return p.substr(0, slash + 1) + "config.ini";
}

static std::string rs_config_ini_path() {
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&rs_config_ini_path), &self) && self) {
        const std::string dllIni = rs_config_ini_path_from_module(self);
        if (!dllIni.empty())
            return dllIni;
    }
    const std::string exeIni = rs_config_ini_path_from_module(nullptr);
    if (!exeIni.empty())
        return exeIni;
    return "config.ini";
}

static int rs_read_tier_from_ini() {
    return GetPrivateProfileIntA("general", "soScreenResolution", -1, rs_config_ini_path().c_str());
}

static void rs_persist_tier_to_ini(int tier) {
    char buf[16];
    sprintf_s(buf, "%d", tier);
    const std::string ini = rs_config_ini_path();
    if (WritePrivateProfileStringA("general", "soScreenResolution", buf, ini.c_str())) {
        std::cout << "[RS] wrote soScreenResolution=" << tier << " to " << ini << std::endl;
    } else {
        std::cout << "[RS] failed writing soScreenResolution to " << ini
                  << " err=" << GetLastError() << std::endl;
    }
}

// WindowedMode: D3D backbuffer can change while HWND client area stays old size.
static void rs_resizeGameWindow(int nw, int nh) {
    HWND hWnd = nullptr;
    if (CWvsApp::IsInstantiated())
        hWnd = CWvsApp::GetInstance()->m_hWnd;
    if (!hWnd) {
        // CInputSystem singleton @ 0xBEC33C — MEMBER_AT HWND offset 0
        auto* pInput = *reinterpret_cast<void**>(0x00BEC33C);
        if (pInput)
            hWnd = *reinterpret_cast<HWND*>(pInput);
    }
    if (!hWnd || !IsWindow(hWnd)) {
        std::cout << "[RS] resize HWND unavailable" << std::endl;
        return;
    }

    RECT rc = { 0, 0, nw, nh };
    DWORD style = (DWORD)GetWindowLongA(hWnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongA(hWnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;

    RECT cur{};
    GetWindowRect(hWnd, &cur);
    // Keep 1:1 pixels (shrinking HWND below backbuffer = bilinear blur). Only
    // nudge X/Y so a 2560-wide window is not left hanging off a 1920 panel.
    int x = cur.left;
    int y = cur.top;
    const int scrW = GetSystemMetrics(SM_CXSCREEN);
    const int scrH = GetSystemMetrics(SM_CYSCREEN);
    if (x + winW > scrW) x = (scrW > winW) ? (scrW - winW) / 2 : 0;
    if (y + winH > scrH) y = (scrH > winH) ? (scrH - winH) / 2 : 0;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    const BOOL ok = SetWindowPos(hWnd, nullptr, x, y, winW, winH,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client{};
    GetClientRect(hWnd, &client);
    char buf[224];
    sprintf_s(buf, "[RS] SetWindowPos want=%dx%d frame=%dx%d client=%dx%d monitor=%dx%d ok=%d",
              nw, nh, winW, winH, client.right - client.left, client.bottom - client.top,
              scrW, scrH, ok ? 1 : 0);
    RsLogFlush(buf);
}

static RECT& rs_questDlgRect = *reinterpret_cast<RECT*>(0x00BE2DF0);

// Client::WorldMap codecave reads these once at boot — refresh on every HD switch.
extern int wordMapX;
extern int wordMapY;
// Client::UpdateResolution LimitedView DrawViewRange caves — refresh on HD switch.
extern int darkCircleX;
extern int darkCircleY;

static bool rs_layoutHooksInstalled = false;
// True only while field HD Origin layout is live. Login must keep UpdateResolution
// (no CreateDlg→CC / 9-grid skew) even if hooks remain attached.
static bool rs_originActive = false;
// SysOpt OK queues a tier; apply after the dialog destroys (avoids switch-under-modal hang).
static int rs_deferredTier = -1;
// After ScreenResolution queues D3DERR_DEVICENOTRESET — run ReloadBack on next CallUpdate.
static bool rs_pendingFieldRefresh = false;
// StatusBar Origin-bound size (skip recreate when unchanged).
static int rs_hudBoundW = 0;
static int rs_hudBoundH = 0;

class RsCtx : public TSingleton<RsCtx, 0x00BE7918> {
public:
    MEMBER_AT(CTemporaryStatView, 0x2EA8, m_temporaryStatView)
};

class RsPhy : public TSingleton<RsPhy, 0x00BEBFA0> {
public:
    MEMBER_AT(RECT, 0x24, m_rcMBR)
};

static void rs_syncClientDims(int nw, int nh) {
    Client::m_nGameWidth = nw;
    Client::m_nGameHeight = nh;
}

// UpdateResolution writes these at boot from config.ini login size (1280x720).
// Field Origin switches skip UpdateResolution (it would smash HUD), so mouse
// stay clamped to login 1280x720 → clicks miss characters/NPCs on the extra FOV.
static void rs_patchMouseLimits(int w, int h) {
    if (w < 640) w = 640;
    if (h < 480) h = 480;
    Memory::WriteInt(0x0059AC09 + 1, w); // CInputSystem::UpdateMouse clamp X (was 800)
    Memory::WriteInt(0x0059AC22 + 1, h); // CInputSystem::UpdateMouse clamp Y (was 600)
    Memory::WriteInt(0x0059A898 + 1, w); // SetCursorPos clamp X
    Memory::WriteInt(0x0059A8B1 + 1, h); // SetCursorPos clamp Y
    Memory::WriteInt(0x0059A169 + 2, static_cast<unsigned int>(0 - w / 2)); // cursor vector X
    Memory::WriteInt(0x0059A15D + 2, static_cast<unsigned int>(0 - h / 2)); // cursor vector Y
    Memory::WriteInt(0x0059A09C + 2, static_cast<unsigned int>(w / 2)); // LoadCursorState center X
    Memory::WriteInt(0x0059A0A2 + 6, static_cast<unsigned int>(h / 2)); // LoadCursorState center Y
    Memory::WriteInt(0x009DFE68 + 3, w); // viewport width
    Memory::WriteInt(0x009DFCF0 + 3, h); // viewport height
    char buf[96];
    sprintf_s(buf, "[RS] mouse/viewport limits %dx%d", w, h);
    RsLogFlush(buf);
}

// WorldMap codecave (0x009EB594) pushes (wordMapX, wordMapY) into CreateDlg.
// Always feed screen-LT center. CreateWnd_hook parents WorldMap to LT and ForceLtAbs —
// do NOT use CC+(67,38): pairing CC origin with BeiDou's (W-666)/2 codecave double-shifted,
// and logging via CWnd::GetAbsLeft() after a bad origin hung the client (未响应).
static void rs_refreshWorldMapCenter() {
    wordMapX = (rs_width - 666) / 2;
    wordMapY = (rs_height - 524) / 2;
    if (wordMapX < 0) wordMapX = 0;
    if (wordMapY < 0) wordMapY = 0;
    char buf[128];
    sprintf_s(buf, "[RS] WorldMap center LT %d,%d screen=%dx%d",
              wordMapX, wordMapY, rs_width, rs_height);
    RsLogFlush(buf);
}

// NPC/quest/auction scripts (CUtilDlgEx) — kaentake CreateUtilDlgEx.
// Without this, CreateDlg → CreateWnd_hook rebinds to CC while coords are LT-absolute
// → dialogs shift 偏右下 by CC abs (e.g. +400,+150 @1600x900).
class CUtilDlgEx : public CWnd {
public:
    MEMBER_AT(int, 0x98, m_wndWidth)
    MEMBER_AT(int, 0x9C, m_wndHeight)
    MEMBER_HOOK(void, 0x009A3E38, CreateUtilDlgEx)
};

void CUtilDlgEx::CreateUtilDlgEx_hook() {
    int nLeft = zclamp<int>(rs_questDlgRect.left - m_wndWidth / 2, 0, rs_width);
    int nTop = zclamp<int>(rs_questDlgRect.top - m_wndHeight / 2, 0, rs_height);
    // Call CreateWnd directly (return addr ≠ CreateDlg) so origin stays LT/m_pOrgWindow.
    CreateWnd(nLeft, nTop, m_wndWidth, m_wndHeight, 10, 1, nullptr, 1);
    std::cout << "[RS] UtilDlgEx at " << nLeft << "," << nTop
              << " size=" << m_wndWidth << "x" << m_wndHeight
              << " screen=" << rs_width << "x" << rs_height << std::endl;
}

// StatusBar bottom chrome (OnButton 0x8D423D — kaentake has no special case; Origin does):
//   id 1000 Shop→商城; id 1002 →GameMenu 界面 @849DE2 CreateFadeWnd(666,423);
//   id 1007 →ShortCut 目录 @84A560 CreateFadeWnd(707,296); id 1005 KeySet; id 1009 Claim.
// Kaentake resolution.cpp: CreateWnd ret 0x51FA03 (CFadeWnd) → ms_pOrgStatusBar.
// LB StatusBar org abs=(0,H-600) ⇒ same Y-from-bottom; X stays 666 vs 707.
// Client::UpdateResolution patches 849E39/84A5B7 to H-177/H-281 (abs Y for flush-bottom);
// Origin must restore 423/296 — do NOT leave login-H seeds (720→543/439).
// ArtMegaphone OnButton 0x7E0C06 / CUIMenu@999320 are NOT 目录/界面 (why old FIRE never ran).
static auto rs_OrigCreateWnd =
    reinterpret_cast<void(__thiscall*)(CWnd*, int, int, int, int, int, int, void*, int)>(0x009DE4D2);
static auto rs_OrigCUIMenuCtor =
    reinterpret_cast<void*(__thiscall*)(void*, int, int, int)>(0x00999320);
static auto rs_OrigCreateDlg =
    reinterpret_cast<int(__thiscall*)(CWnd*, int, int, int, int, int, int, void*)>(0x004EDA94);
// Real CUIStatusBar::OnButtonClicked — NOT ArtMegaphone 0x7E0C06.
static auto rs_OrigStatusBarBtn =
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>(0x008D423D);
static bool rs_cursorDlgPatchesInstalled = false;
static bool rs_cuiMenuCtorDetoured = false;
static bool rs_createDlgDetoured = false;
static bool rs_statusBarBtnDetoured = false;
static constexpr uintptr_t kRsMiniMapCreateWndRet = 0x00858F37;
static constexpr uintptr_t kRsGameMenuVtable = 0x00B39B60;  // UI/UIWindow.img/GameMenu
static constexpr uintptr_t kRsShortCutVtable = 0x00B39BF8;  // UI/UIWindow.img/ShortCut

// Forward — defined with StatusBar helpers below; used by CursorDlg logs.
static int RsWndAbsLeft(CWnd* pWnd);
static int RsWndAbsTop(CWnd* pWnd);

static void RsUiLogPath(char* out, size_t outLen) {
    char exe[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
        strcpy_s(out, outLen, "rs_ui.log");
        return;
    }
    char* slash = strrchr(exe, '\\');
    if (slash) *(slash + 1) = '\0';
    sprintf_s(out, outLen, "%srs_ui.log", exe);
}

static void RsLogFlush(const char* line) {
    if (!line) return;
    std::cout << line << std::endl;
    std::cout.flush();
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
    char path[MAX_PATH + 32]{};
    RsUiLogPath(path, sizeof(path));
    if (FILE* f = nullptr; fopen_s(&f, path, "a") == 0 && f) {
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
    }
}

static void RsGetClientCursor(POINT& pt) {
    auto* pInput = *reinterpret_cast<void**>(0x00BEC33C);
    HWND hWnd = pInput ? *reinterpret_cast<HWND*>(pInput) : nullptr;
    if (!hWnd || !::GetCursorPos(&pt) || !::ScreenToClient(hWnd, &pt)) {
        pt.x = rs_width / 2;
        pt.y = rs_height / 2;
    }
}

// StatusBar 目录 button sits near vanilla x=407 inside the right-aligned 800px chrome.
// HD bar is full-width (AbsLeft=0): button abs ≈ (W-800)+407 = W-393.
static int RsStatusBarMenuOffsetX() {
    if (rs_width <= 800) return 407;
    return rs_width - 800 + 407;
}

// ArtMegaphone (NOT StatusBar): patch add eax,407h @0x7E0FDC for CUIMenu X on HD.
static void RsPatchStatusBarMenuOffset() {
    const int off = RsStatusBarMenuOffsetX();
    Memory::WriteInt(0x007E0FDD, static_cast<unsigned int>(off));
    char buf[128];
    sprintf_s(buf, "[RS] ArtMegaphone CUIMenu offsetX=%d (W=%d) @0x7E0FDC", off, rs_width);
    RsLogFlush(buf);
}

// ArtMegaphone seeds Y with add eax,1Eh (AbsTop+30). Keep vanilla — real mh clamp in CUIMenu.
static constexpr int kRsMenuGapAboveBtn = 6;
static constexpr int kRsStatusBarMenuYSeed = 30; // vanilla add eax,1Eh
// Original bytes @0x7E0FEB..7E0FFA (add +30 / push 90 / mov ecx,BF0B00 / mov [ebp-2C],eax).
static const unsigned char kRsStatusBarMenuYVanilla[16] = {
    0x83, 0xC0, 0x1E,
    0x68, 0x90, 0x00, 0x00, 0x00,
    0xB9, 0x00, 0x0B, 0xBF, 0x00,
    0x89, 0x45, 0xD4,
};

// ArtMegaphone button slots (sub_7E0759) — NOT StatusBar 目录/界面.
// Kept for legacy CUIMenu btn-abs heuristics only.
static constexpr uintptr_t kRsSbMenuBtnOff[] = { 0xA0, 0xA8, 0xB8 };

static int RsCtrlAbsLeft(void* pCtrl) {
    if (!pCtrl) return -1;
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E03C5)(
        reinterpret_cast<char*>(pCtrl) + 4);
}
static int RsCtrlAbsTop(void* pCtrl) {
    if (!pCtrl) return -1;
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E0447)(
        reinterpret_cast<char*>(pCtrl) + 4);
}
// CCtrlWnd::CreateCtrl stores height at +0x20.
static int RsCtrlHeight(void* pCtrl) {
    if (!pCtrl) return 0;
    const int h = *reinterpret_cast<int*>(reinterpret_cast<char*>(pCtrl) + 0x20);
    if (h <= 0 || h > 80) return 18;
    return h;
}

// Read 目录/界面 button Abs. Prefer button nearest cursor Y; else bottom-most (max AbsTop).
// Returns false if no usable button — caller falls back.
static bool RsGetStatusBarMenuBtnAbs(int& outL, int& outT, int& outH, int* outOff = nullptr) {
    outL = -1;
    outT = -1;
    outH = 0;
    if (outOff) *outOff = -1;
    auto* sb = CUIStatusBar::GetInstance();
    if (!sb) return false;

    POINT cur{};
    RsGetClientCursor(cur);

    void* nearBtn = nullptr;
    void* bottomBtn = nullptr;
    int nearDist = 0x7FFFFFFF;
    int bottomT = -1;
    uintptr_t nearOff = 0, bottomOff = 0;

    for (uintptr_t off : kRsSbMenuBtnOff) {
        void* btn = *reinterpret_cast<void**>(reinterpret_cast<char*>(sb) + off);
        if (!btn) continue;
        const int t = RsCtrlAbsTop(btn);
        if (t < 0 || t > rs_height + 100) continue;
        const int dist = (t > static_cast<int>(cur.y)) ? (t - static_cast<int>(cur.y))
                                                       : (static_cast<int>(cur.y) - t);
        if (dist < nearDist) {
            nearDist = dist;
            nearBtn = btn;
            nearOff = off;
        }
        if (t >= bottomT) {
            bottomT = t;
            bottomBtn = btn;
            bottomOff = off;
        }
    }

    // Cursor within ~64px of a button → that button; else bottom chrome (界面/目录 row).
    void* best = nullptr;
    uintptr_t bestOff = 0;
    if (nearBtn && nearDist <= 64) {
        best = nearBtn;
        bestOff = nearOff;
    } else if (bottomBtn) {
        best = bottomBtn;
        bestOff = bottomOff;
    } else {
        best = nearBtn;
        bestOff = nearOff;
    }
    if (!best) return false;

    outL = RsCtrlAbsLeft(best);
    outT = RsCtrlAbsTop(best);
    outH = RsCtrlHeight(best);
    if (outOff) *outOff = static_cast<int>(bestOff);
    return outT >= 0;
}

// Restore ArtMegaphone AbsTop+30 seed (undo any prior Y cave).
static void RsRestoreStatusBarMenuYVanilla() {
    for (size_t i = 0; i < sizeof(kRsStatusBarMenuYVanilla); ++i) {
        Memory::WriteByte(static_cast<DWORD>(0x007E0FEB + i), kRsStatusBarMenuYVanilla[i]);
    }
    RsLogFlush("[RS] ArtMegaphone Y restored vanilla AbsTop+30 @0x7E0FEB");
}

// CUIMenu ctor still clamps to vanilla 800×600 (forces X=700 / Y≤600-h) — rewrite to live size.
static void RsPatchUIMenuClamps() {
    const int w = rs_width > 0 ? rs_width : 800;
    const int h = rs_height > 0 ? rs_height : 600;
    const int maxX = w > 100 ? w - 100 : 700;
    Memory::WriteInt(0x009994D9, static_cast<unsigned int>(h));   // mov ecx, 600 → H
    Memory::WriteInt(0x009994EF, static_cast<unsigned int>(w));   // cmp edi, 800 → W
    Memory::WriteInt(0x009994F6, static_cast<unsigned int>(maxX)); // mov edx, 700 → W-100
    char buf[128];
    sprintf_s(buf, "[RS] UIMenu clamps → %dx%d maxX=%d", w, h, maxX);
    RsLogFlush(buf);
}

static IUnknown* RsLtOriginUnk() {
    if (rs_orgEx[0]) return static_cast<IUnknown*>(rs_orgEx[0]);
    if (RsGetCWndMan()) {
        auto& org = RsGetOrgWindow();
        if (org) return static_cast<IUnknown*>(org);
    }
    return nullptr;
}

// Force layer to LT and RelMove to screen-absolute (l,t). Returns false if no layer.
static bool RsForceLtAbs(CWnd* pThis, int l, int t) {
    if (!pThis || !pThis->m_pLayer) return false;
    if (IUnknown* lt = RsLtOriginUnk()) {
        pThis->m_pLayer->origin = lt;
        pThis->m_pLayer->RelMove(l, t);
        return true;
    }
    pThis->m_pLayer->RelMove(l, t);
    return false;
}

bool rs_force_wnd_lt_abs(void* wnd, int l, int t) {
    return RsForceLtAbs(static_cast<CWnd*>(wnd), l, t);
}

static void RsClampMenuRect(int& l, int& t, int nWidth, int nHeight) {
    if (nWidth <= 0) nWidth = 100;
    if (nHeight <= 0) nHeight = 40;
    if (l + nWidth > rs_width) l = rs_width - nWidth;
    if (t + nHeight > rs_height) t = rs_height - nHeight;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
}

// Vanilla CUIMenu Y (sub_999320): seedT=AbsTop+30, mh=15*(nItem+2),
// if (seedT+mh > bottom) seedT = bottom - mh. bottom was 600; HD uses btnAbsTop-gap (else H).
// seedL/seedT from StatusBar (AbsLeft+offX, AbsTop+30). nHeight MUST be real list height.
static void RsCalcUIMenuPlace(int nWidth, int nHeight, int seedL, int seedT,
                              int& outL, int& outT,
                              int* logBtnL = nullptr, int* logBtnT = nullptr,
                              int* logBtnH = nullptr, int* logBtnOff = nullptr) {
    if (nWidth <= 0) nWidth = 100;
    if (nHeight <= 0) nHeight = 90;
    outL = seedL;
    outT = seedT;

    int btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsGetStatusBarMenuBtnAbs(btnL, btnT, btnH, &btnOff);

    // If caller passed junk seed (e.g. early hook), rebuild vanilla seed from StatusBar Abs.
    if (outT < 0 || outT > rs_height + 200) {
        if (auto* sb = CUIStatusBar::GetInstance()) {
            const int sbT = RsWndAbsTop(sb);
            const int sbL = RsWndAbsLeft(sb);
            if (sbT >= 0) outT = sbT + kRsStatusBarMenuYSeed;
            if (sbL >= 0 && (outL < 0 || outL > rs_width + 200))
                outL = sbL + RsStatusBarMenuOffsetX();
        }
    }

    int bottom = rs_height > 0 ? rs_height : 600;
    if (btnT >= 0)
        bottom = btnT - kRsMenuGapAboveBtn;
    if (bottom < nHeight) bottom = nHeight;
    if (outT + nHeight > bottom)
        outT = bottom - nHeight; // vanilla overflow branch — uses REAL mh

    if (logBtnL) *logBtnL = btnL;
    if (logBtnT) *logBtnT = btnT;
    if (logBtnH) *logBtnH = btnH;
    if (logBtnOff) *logBtnOff = btnOff;
    RsClampMenuRect(outL, outT, nWidth, nHeight);
}

// Place at screen-abs (l,t) with forced LT origin (never CreateDlg→CC).
// l,t are pass-through from caller — only edge-clamp, never invent a Y formula.
static void RsPlaceAbsDlg(CWnd* pThis, int l, int t, int nWidth, int nHeight,
                          void* pData, const char* tag) {
    POINT cur{};
    RsGetClientCursor(cur);
    int sbL = -1, sbT = -1;
    if (auto* sb = CUIStatusBar::GetInstance()) {
        sbL = RsWndAbsLeft(sb);
        sbT = RsWndAbsTop(sb);
    }

    if (nWidth <= 0) nWidth = 100;
    if (nHeight <= 0) nHeight = 40;
    const int inL = l, inT = t;
    RsClampMenuRect(l, t, nWidth, nHeight);

    char buf[288];
    sprintf_s(buf,
        "[RS] CursorDlg %s pass-through in=%d,%d place=%d,%d size=%dx%d "
        "cursor=%d,%d StatusBar=%d,%d screen=%dx%d originActive=%d",
        tag, inL, inT, l, t, nWidth, nHeight, cur.x, cur.y, sbL, sbT,
        rs_width, rs_height, rs_originActive ? 1 : 0);
    RsLogFlush(buf);

    rs_OrigCreateWnd(pThis, l, t, nWidth, nHeight, 10, 1, pData, 1);
    // Always re-assert LT: OrigCreateWnd bypasses CreateWnd_hook, so CC cannot be
    // applied there — but layer may still inherit a non-LT origin from construction.
    RsForceLtAbs(pThis, l, t);

    const int absL = RsWndAbsLeft(pThis);
    const int absT = RsWndAbsTop(pThis);
    int btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsGetStatusBarMenuBtnAbs(btnL, btnT, btnH, &btnOff);
    const int dCurX = absL - cur.x;
    const int dCurY = absT - cur.y;
    const int dBtnY = (btnT >= 0) ? (absT + nHeight - btnT) : 0; // menu bottom vs btn top
    sprintf_s(buf,
        "[RS] CursorDlg %s placed abs=%d,%d size=%dx%d "
        "cursor=%d,%d dCursor=%d,%d StatusBar=%d,%d btnAbs=%d,%d btnH=%d off=0x%X dBtnBottom=%d screen=%dx%d",
        tag, absL, absT, nWidth, nHeight, cur.x, cur.y, dCurX, dCurY,
        sbL, sbT, btnL, btnT, btnH, btnOff >= 0 ? btnOff : 0, dBtnY, rs_width, rs_height);
    RsLogFlush(buf);
}

class CUIContextMenu : public CWnd {
public:
    MEMBER_AT(int, 0xCC, m_nBtNumber)
};

class CUIMenu : public CWnd {
public:
    MEMBER_AT(int, 0x84, m_nItem)
};

// ContextMenu (右键等): kaentake uses cursor as CreateDlg top-left.
static void RsHandleContextMenu(CUIContextMenu* pThis, void* pData) {
    int nBt = pThis ? pThis->m_nBtNumber : 0;
    if (nBt < 0) nBt = 0;
    POINT cur{};
    RsGetClientCursor(cur);
    RsPlaceAbsDlg(pThis, cur.x, cur.y, 100, 15 * (nBt + 2), pData, "ContextMenu");
}

// UIMenu (目录/界面): vanilla AbsTop+30 seed + real mh clamp + LT.
static void RsHandleUIMenu(CUIMenu* pThis, int l, int t, void* pData) {
    int nCount = pThis ? pThis->m_nItem : 0;
    if (nCount < 0) nCount = 0;
    const int mh = 15 * (nCount + 2);
    int nl = 0, nt = 0;
    RsCalcUIMenuPlace(100, mh, l, t, nl, nt);
    char buf[192];
    sprintf_s(buf, "[RS] CursorDlg UIMenu FIRE seed=%d,%d place=%d,%d h=%d items=%d",
              l, t, nl, nt, mh, nCount);
    RsLogFlush(buf);
    RsPlaceAbsDlg(pThis, nl, nt, 100, mh, pData, "UIMenu");
}

static void RsHandlePopup(CWnd* pThis, int l, int t, int w, int h, void* pData) {
    char buf[128];
    sprintf_s(buf, "[RS] CursorDlg Popup FIRE this=%p args=%d,%d,%d,%d", pThis, l, t, w, h);
    RsLogFlush(buf);
    RsPlaceAbsDlg(pThis, l, t, w > 0 ? w : 80, h > 0 ? h : 39, pData, "Popup");
}

// CUIMenu::CreateDlg site @0x999506 — vanilla seed (l,t=AbsTop+30) + REAL h + LT.
void __fastcall rs_UIMenu_CreateDlg_hook(CUIMenu* pThis, void* /*edx*/,
    int l, int t, int w, int h, int /*z*/, int /*bScreenCoord*/, void* pData) {
    int nCount = pThis ? pThis->m_nItem : 0;
    if (nCount < 0) nCount = 0;
    const int mh = (h > 0) ? h : (15 * (nCount + 2));
    const int mw = (w > 0) ? w : 100;
    int nl = 0, nt = 0, btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsCalcUIMenuPlace(mw, mh, l, t, nl, nt, &btnL, &btnT, &btnH, &btnOff);
    char buf[256];
    sprintf_s(buf,
        "[RS] UIMenu CreateDlg FIRE seed=%d,%d place=%d,%d size=%dx%d items=%d "
        "btnAbs=%d,%d (vanilla: Y=AbsTop+30, clamp up by real mh)",
        l, t, nl, nt, mw, mh, nCount, btnL, btnT);
    RsLogFlush(buf);
    RsPlaceAbsDlg(pThis, nl, nt, mw, mh, pData, "UIMenu");
}

void __fastcall rs_ContextMenu_CreateDlg_hook(CUIContextMenu* pThis, void* /*edx*/,
    int /*l*/, int /*t*/, int /*w*/, int /*h*/, int /*z*/, int /*bScreenCoord*/, void* pData) {
    RsLogFlush("[RS] ContextMenu CreateDlg PatchCall FIRE");
    RsHandleContextMenu(pThis, pData);
}

void __fastcall rs_Popup_CreateDlg_hook(CWnd* pThis, void* /*edx*/,
    int l, int t, int w, int h, int /*z*/, int /*bScreenCoord*/, void* pData) {
    RsLogFlush("[RS] Popup CreateDlg PatchCall FIRE");
    RsHandlePopup(pThis, l, t, w, h, pData);
}

// StatusBar @0x7E1018 — pass vanilla seed (l,t) into ctor; CreateDlg hook clamps by real mh.
void* __fastcall rs_StatusBar_OpenUIMenu(void* pThis, void* /*edx*/, int a2, int l, int t) {
    char buf[256];
    sprintf_s(buf, "[RS] StatusBar OpenUIMenu FIRE a2=%d seed=%d,%d (vanilla AbsTop+30 path)",
              a2, l, t);
    RsLogFlush(buf);
    void* ret = rs_OrigCUIMenuCtor(pThis, a2, l, t);
    if (!ret || rs_width <= 800) return ret;

    auto* menu = reinterpret_cast<CUIMenu*>(ret);
    int nCount = menu->m_nItem;
    if (nCount < 0) nCount = 0;
    int mh = 15 * (nCount + 2);
    if (mh < 45) mh = 45;
    int nl = 0, nt = 0, btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsCalcUIMenuPlace(100, mh, l, t, nl, nt, &btnL, &btnT, &btnH, &btnOff);
    const bool ok = RsForceLtAbs(menu, nl, nt);
    sprintf_s(buf,
        "[RS] StatusBar OpenUIMenu LT -> %d,%d menuH=%d items=%d "
        "btnAbs=%d,%d seed=%d,%d abs=%d,%d force=%d "
        "(vanilla: clamp seedY up by real mh vs btn/H)",
        nl, nt, mh, nCount, btnL, btnT, l, t,
        RsWndAbsLeft(menu), RsWndAbsTop(menu), ok ? 1 : 0);
    RsLogFlush(buf);
    return ret;
}

// CUIMenu ctor — ONLY caller in v083 is StatusBar @0x7E1018 (IDA xref_count=1).
// CreateDlg hook does primary place; this re-asserts LT with real mh if needed.
void* __fastcall rs_CUIMenuCtor_hook(void* pThis, void* /*edx*/, int a2, int l, int t) {
    char buf[256];
    sprintf_s(buf, "[RS] CUIMenu ctor FIRE a2=%d seed=%d,%d screen=%dx%d",
              a2, l, t, rs_width, rs_height);
    RsLogFlush(buf);

    void* ret = rs_OrigCUIMenuCtor(pThis, a2, l, t);
    if (!ret || rs_width <= 800) return ret;

    auto* menu = reinterpret_cast<CUIMenu*>(ret);
    int nCount = menu->m_nItem;
    if (nCount < 0) nCount = 0;
    int mh = 15 * (nCount + 2);
    if (mh < 45) mh = 45;
    int nl = 0, nt = 0, btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsCalcUIMenuPlace(100, mh, l, t, nl, nt, &btnL, &btnT, &btnH, &btnOff);
    const bool ok = RsForceLtAbs(menu, nl, nt);
    sprintf_s(buf,
        "[RS] CUIMenu LT place -> %d,%d menuH=%d items=%d "
        "btnAbs=%d,%d seed=%d,%d abs=%d,%d force=%d",
        nl, nt, mh, nCount, btnL, btnT, l, t,
        RsWndAbsLeft(menu), RsWndAbsTop(menu), ok ? 1 : 0);
    RsLogFlush(buf);
    return ret;
}

static void RsInstallCUIMenuCtorDetour() {
    if (rs_cuiMenuCtorDetoured) return;
    if (Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigCUIMenuCtor),
                        CastHook(&rs_CUIMenuCtor_hook))) {
        rs_cuiMenuCtorDetoured = true;
        RsLogFlush("[RS] CUIMenu ctor Detour attached");
    } else {
        RsLogFlush("[RS] CUIMenu ctor Detour FAILED");
    }
}

// Forward — defined after CUIMenu helpers (vtable/size match).
static void RsInstallCreateDlgDetour();
static void RsInstallStatusBarBtnDetour();

static void RsInstallCursorDlgPatches() {
    // 目录/界面 placement = kaentake CreateWnd_hook case 0x51FA03 → StatusBar origin
    // (see rs_CreateWnd_hook). No Y caves / no +(W-800,H-600) invention.
    //
    // ArtMegaphone CUIMenu (7E0xxx / 999320) is a separate cash list — keep clamps.
    // StatusBar OnButton is 0x8D423D (NOT ArtMegaphone 0x7E0C06 — that never FIRE'd).
    RsPatchStatusBarMenuOffset();
    RsRestoreStatusBarMenuYVanilla();
    RsPatchUIMenuClamps();
    RsInstallCUIMenuCtorDetour();
    RsInstallCreateDlgDetour();
    RsInstallStatusBarBtnDetour();

    // ContextMenu: kaentake PatchCall @0x9966E3 (cursor + clamp, CreateWnd LT).
    PatchCall(0x009966E3, CastHook(&rs_ContextMenu_CreateDlg_hook));
    PatchCall(0x0085F387, CastHook(&rs_Popup_CreateDlg_hook));

    {
        const unsigned char btnOp = *reinterpret_cast<const unsigned char*>(0x008D423D);
        char buf[288];
        sprintf_s(buf,
            "[RS] patch verify StatusBarBtn@8D423D=%02X BtnDetour=%d "
            "GameMenu/ShortCut=kaentake FadeWnd->StatusBarOrg @51FA03 "
            "(GameMenu 666,423 / ShortCut 707,296)",
            btnOp, rs_statusBarBtnDetoured ? 1 : 0);
        RsLogFlush(buf);
    }

    rs_cursorDlgPatchesInstalled = true;
}

// ===== Resolution switch helper (arbitrary size) =====
// loginUi=true  → leave Origin, UpdateResolution center (char-select / login only).
// loginUi=false → keep Origin field layout even when dims == login size.
// Bug: SysOpt picking 1280x720 (same as config.ini login) used to take the login
// path while still in-field → Origin off + no ReloadBack → black screen / hang.

static void rs_afterSuccessfulSwitch(int nw, int nh, bool grChanged, bool loginUi) {
    rs_width = nw;
    rs_height = nh;
    rs_adjust_cy = (nh - 600) / 2;
    if (rs_adjust_cy < 0) rs_adjust_cy = 0;
    rs_syncClientDims(nw, nh);
    rs_patchMouseLimits(nw, nh);
    {
        char buf[192];
        sprintf_s(buf, "[RS] Switched to %dx%d adj=%d grChanged=%d loginUi=%d",
                  nw, nh, rs_adjust_cy, grChanged ? 1 : 0, loginUi ? 1 : 0);
        RsLogFlush(buf);
    }

    if (Client::WindowedMode) {
        rs_resizeGameWindow(nw, nh);
    }

    if (loginUi) {
        // Leave HD Origin layout: re-apply UpdateResolution login patches + center org.
        rs_originActive = false;
        rs_adjust_cy = 0; // UpdateResolution login uses plain -H/2 (no letterbox adj)
        RsLogFlush("[RS] login UI — UpdateResolution layout (Origin inactive)");
        Client::UpdateResolution();
        rs_syncClientDims(nw, nh);
        // Re-assert CursorDlg call sites after UpdateResolution nearby immediates.
        if (rs_cursorDlgPatchesInstalled) {
            RsInstallCursorDlgPatches();
        }
        if (RsGetCWndMan()) {
            RsResetOrgWindow();
            auto& org = RsGetOrgWindow();
            if (org) {
                // Force login center — leftover HD (-800,-600) skews book to bottom-right.
                org->origin = static_cast<IUnknown*>(get_gr()->center);
                org->RelMove(-(nw / 2), -(nh / 2));
                char buf[192];
                sprintf_s(buf, "[RS] login org force RelMove(%d,%d) rxy=(%d,%d) xy=(%d,%d)",
                          -(nw / 2), -(nh / 2), org->rx, org->ry, org->x, org->y);
                RsLogFlush(buf);
            } else {
                RsLogFlush("[RS] login org NULL after ResetOrgWindow");
            }
        }
        rs_refreshWorldMapCenter();
    } else {
        // Field play (kaentake-style): first time install hooks+recreate HUD; later only
        // rematch origins/metrics. Destroying StatusBar + ReloadBack while SysOpt was still
        // open / D3D DEVICENOTRESET pending caused 2nd-switch black-screen hang.
        //
        // follow-login only: login dims == field dims → UpdateResolution (no Origin).
        // Do NOT treat !grChanged as skip-Origin: soft-fail / SysOpt same-size heal used to
        // leave Origin off forever and keep UpdateResolution flush-bottom QuickSlot/StatusBar
        // (815/H-578), which misplaces info bar / hotkeys / minimap chrome on HD.
        // When Gr2D already matches nw×nh, Origin rematch is still correct (kaentake).
        if (rs_field_follow_login) {
            rs_originActive = false;
            rs_adjust_cy = 0;
            RsLogFlush("[RS] follow-login field — skip Origin (UpdateResolution layout)");
            Client::UpdateResolution();
            rs_syncClientDims(nw, nh);
            if (rs_cursorDlgPatchesInstalled) {
                RsInstallCursorDlgPatches();
            }
            rs_refreshWorldMapCenter();
        } else {
            rs_originActive = true;
            if (!rs_layoutHooksInstalled) {
                RsLogFlush("[RS] first field Origin install");
                rs_installLayoutHooks();
            } else {
                RsLogFlush("[RS] rematch field Origin (metrics+ResetOrg, no HUD recreate)");
                RsApplyOriginStatusBarMetrics();
                if (RsGetCWndMan()) {
                    RsResetOrgWindow();
                    rs_questDlgRect.left = rs_width / 2;
                    rs_questDlgRect.top = rs_height / 2;
                }
                rs_hudBoundW = rs_width;
                rs_hudBoundH = rs_height;
            }
            RsLogFlush("[RS] field Origin layout applied");
        }
    }

    if (rs_originActive && RsCtx::IsInstantiated()) {
        RsCtx::GetInstance()->m_temporaryStatView.AdjustPosition_hook();
    }
    CField* field = get_field();
    if (field && rs_originActive) {
        // Let the game process DEVICENOTRESET for one frame before ReloadBack.
        rs_pendingFieldRefresh = true;
        RsLogFlush("[RS] RestoreViewRange+ReloadBack scheduled (next CallUpdate)");
    } else if (field && !rs_originActive) {
        RsLogFlush("[RS] WARNING in-field with Origin inactive (should only happen mid set_stage)");
    } else {
        RsLogFlush("[RS] no field yet — VR/ReloadBack deferred to next enter");
    }
}

static void rs_switchToSize(int nw, int nh, bool loginUi = false) {
    if (nw <= 0 || nh <= 0) return;
    const bool sameSize = (nw == rs_width && nh == rs_height);
    // Re-apply when: (a) leaving field to login UI while Origin still on, or
    // (b) entering/staying in field at same dims but Origin was off (e.g. after bug).
    const bool needLoginUiReapply = loginUi && rs_originActive;
    const bool needFieldOriginHeal = !loginUi && !rs_originActive;
    if (sameSize && !needLoginUiReapply && !needFieldOriginHeal) {
        rs_syncClientDims(nw, nh);
        // Login UpdateResolution clobbers mouse clamps; heal on field re-enter / map warp
        // even when Gr2D dims already match (relogin, deferred tier, same-size Origin).
        if (!loginUi)
            rs_patchMouseLimits(nw, nh);
        char buf[96];
        sprintf_s(buf, "[RS] switch no-op (already %dx%d loginUi=%d)", nw, nh, loginUi ? 1 : 0);
        RsLogFlush(buf);
        return;
    }
    auto* gr = reinterpret_cast<RsGr2D*>(get_gr().GetInterfacePtr());
    if (!gr) {
        RsLogFlush("[RS] get_gr null — cannot ScreenResolution");
        return;
    }
    const int prevW = gr->m_screenMode.nWidth;
    const int prevH = gr->m_screenMode.nHeight;
    HRESULT hr = S_OK;
    if (!sameSize) {
        hr = gr->ScreenResolution(nw, nh);
        char buf[192];
        sprintf_s(buf, "[RS] ScreenResolution(%dx%d)=0x%08X prev=%dx%d now=%dx%d pp=%dx%d",
                  nw, nh, (unsigned)hr, prevW, prevH,
                  gr->m_screenMode.nWidth, gr->m_screenMode.nHeight,
                  gr->m_screenMode.nBackBufferWidth, gr->m_screenMode.nBackBufferHeight);
        RsLogFlush(buf);
        if (FAILED(hr)) {
            // Gold (E:\pro\BeiDou-ijl15): leave layout alone. Soft UpdateResolution with
            // grChanged=0 desyncs Client patches from Gr2D and scrambles StatusBar/QuickSlot/
            // SideToolbar/minimap close (Origin off + flush-bottom immediates).
            RsLogFlush("[RS] ScreenResolution FAILED — leave layout alone");
            return;
        }
    } else {
        char buf[128];
        sprintf_s(buf, "[RS] same dims — re-apply layout loginUi=%d originWas=%d",
                  loginUi ? 1 : 0, rs_originActive ? 1 : 0);
        RsLogFlush(buf);
    }

    const bool grChanged = (prevW != gr->m_screenMode.nWidth || prevH != gr->m_screenMode.nHeight);
    rs_afterSuccessfulSwitch(nw, nh, grChanged, loginUi);
}

static void rs_switchToTier(int tier) {
    int nw = 800, nh = 600;
    rs_tier_dims(tier, nw, nh);
    char buf[96];
    sprintf_s(buf, "[RS] switchToTier %d -> %dx%d (field Origin)", tier, nw, nh);
    RsLogFlush(buf);
    rs_switchToSize(nw, nh, false);
    if (rs_width == nw && rs_height == nh) {
        rs_tier = tier;
        rs_field_follow_login = false;
        if (CConfig::IsInstantiated())
            CConfig::GetInstance()->SetOpt_Int(CConfig::GLOBAL_OPT, "soScreenResolution", rs_tier);
        rs_persist_tier_to_ini(rs_tier);
    } else {
        char fail[160];
        sprintf_s(fail, "[RS] switchToTier %d aborted — live remains %dx%d (saved tier=%d)",
                  tier, rs_width, rs_height, rs_tier);
        RsLogFlush(fail);
    }
}

static void rs_restoreLoginResolution() {
    // Display-only: login/char-select goes back to width×height. Do not change
    // rs_tier or persist soScreenResolution — next field enter still uses the
    // saved in-game tier.
    if (rs_deferredTier >= 0) {
        const int t = rs_deferredTier;
        rs_deferredTier = -1;
        rs_tier = t;
        rs_field_follow_login = false;
        if (CConfig::IsInstantiated())
            CConfig::GetInstance()->SetOpt_Int(CConfig::GLOBAL_OPT, "soScreenResolution", rs_tier);
        rs_persist_tier_to_ini(rs_tier);
        char deferBuf[96];
        sprintf_s(deferBuf, "[RS] restore login — flushed deferred tier %d to config", t);
        RsLogFlush(deferBuf);
    }
    char buf[192];
    sprintf_s(buf, "[RS] restore login %dx%d (was %dx%d originActive=%d keep field tier=%d)",
              rs_login_w, rs_login_h, rs_width, rs_height, rs_originActive ? 1 : 0, rs_tier);
    RsLogFlush(buf);
    rs_switchToSize(rs_login_w, rs_login_h, true);
}

void rs_on_enter_field() {
    // First field enter: set_stage already running when CField ctor fires ModRegistry,
    // so the set_stage hook misses this transition — apply here instead.
    if (rs_field_follow_login) {
        std::cout << "[RS] on_enter_field follow-login " << rs_login_w << "x" << rs_login_h
                  << " (tier UI default=" << rs_tier << ")" << std::endl;
        rs_switchToSize(rs_login_w, rs_login_h);
        return;
    }
    std::cout << "[RS] on_enter_field explicit tier=" << rs_tier << std::endl;
    rs_switchToTier(rs_tier);
}

// ===== set_stage hook =====
static auto s_set_stage = reinterpret_cast<void(__cdecl*)(CStage*, void*)>(0x00777347);

void __cdecl rs_set_stage_hook(CStage* pStage, void* pParam) {
    // Free ResMan canvas/property cache before stage builds (World Select was E_POINTER
    // with maxFree≈5MB). IDA set_stage@0x777347 — existing RS hook, no new VA.
    // Skip pre-flush on in-field warp (CField→CField): NPC UtilDlgEx may still be
    // drawing when warp arrives; flush frees its canvas → E_POINTER (FM warp 2026-08-24).
    const bool enteringField =
        pStage && pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED758));
    const bool inFieldWarp = enteringField && get_field() != nullptr;
    if (!inFieldWarp) {
        rs_resman_flush_cached(0);
    }

    if (pStage && pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED758))) {
        std::cout << "[RS] set_stage -> CField tier=" << rs_tier
                  << " follow_login=" << (rs_field_follow_login ? 1 : 0) << std::endl;
        if (rs_field_follow_login) {
            rs_switchToSize(rs_login_w, rs_login_h);
        } else {
            rs_switchToTier(rs_tier);
        }
        // Single pre-stage flush already done above; avoid double flush into SET_FIELD
        // (2026-08-21 AV: live char refs freed mid-enter when flush was too aggressive).
        s_set_stage(pStage, pParam);
        return;
    }
    // Login / char-select / null teardown: restore BEFORE set_stage so CLogin
    // builds under UpdateResolution dims. Prior order (set_stage then restore)
    // created the book UI under HD Origin → black left / book bottom-right.
    // Skip CInterStage (loading) only.
    if (!pStage || !pStage->IsKindOf(reinterpret_cast<const CRTTI*>(0x00BED874))) {
        if (rs_originActive || rs_width != rs_login_w || rs_height != rs_login_h) {
            std::cout << "[RS] set_stage pre-restore login before stage build" << std::endl;
            rs_restoreLoginResolution();
        }
    }
    s_set_stage(pStage, pParam);
}

// ===== CConfig hooks =====
// MiniMap (UIType=4): default / heal to top-left on HD.
// Prior RS Origin builds snapped to right edge or mid-screen; force LT (8,8).
static void RsRemapUiWndXFrom800(int nUIType, int* x) {
    if (!x || nUIType != 4 || rs_width <= 800) return;
    // Any non-LT saved X (right-snap, center, mid) → top-left.
    if (*x != 8) *x = 8;
}

static void RsForceMiniMapLt(int* x, int* y) {
    if (x) *x = 8;
    if (y) *y = 8;
}

// kaentake get_default_position — UI window defaults when saved pos is off-screen.
static void rs_get_default_position(int nUIType, int* pnDefaultX, int* pnDefaultY) {
    int nDefaultX;
    int nDefaultY;
    switch (nUIType) {
    // CUIMiniMap (ctor passes nUIType=4). Vanilla + HD: top-left.
    case 4:
        nDefaultX = 8;
        nDefaultY = 8;
        break;
    case 8:  nDefaultX = 500; nDefaultY = 50;  break;
    case 9:  nDefaultX = 250; nDefaultY = 100; break;
    case 22: nDefaultX = 500; nDefaultY = 100; break;
    case 14: nDefaultX = 600; nDefaultY = 35;  break;
    case 15: nDefaultX = 730; nDefaultY = 400; break;
    case 18: nDefaultX = 11;  nDefaultY = 24;  break;
    case 20: nDefaultX = 720; nDefaultY = 80;  break;
    case 23: case 31: case 33:
        nDefaultX = 100; nDefaultY = 100; break;
    case 24: case 25: case 26: case 27: case 29: case 32:
        nDefaultX = 244; nDefaultY = 105; break;
    case 30: nDefaultX = 769; nDefaultY = 343; break;
    default:
        nDefaultX = 8 * (3 * nUIType + 3);
        nDefaultY = nDefaultX;
        break;
    }
    if (pnDefaultX) *pnDefaultX = nDefaultX;
    if (pnDefaultY) *pnDefaultY = nDefaultY;
}

// UIType map (BeiDou v083 — verified IDA 20260801):
//   1 = CUIEquip   (ctor sub_7FDE7C, singleton BED64C, CreateWnd ret 0x00801A93)
//   5 = CUIKeyConfig (ctor sub_832586, singleton BED650, path L"KeyConfig",
//                     CreateWnd ret 0x008327D8)
// Prior ADDON_DOCK_UI stamp wrongly treated UIType=5 / 0x008327D8 as Equip and
// ForceLtAbs'd + healed it to default 144,144 → 键盘设定 stuck left.
// Do NOT heal uiWndX5 to LT. KeyConfig CreateWnd_hook recenters on HD instead.

void CConfig::GetUIWndPos_hook(int nUIType, int* x, int* y, int* op) {
    CConfig::GetUIWndPos(this, nUIType, x, y, op);
    if (!x || !y) return;
    // MiniMap: always LT on HD (overrides stale Global.opt from right/center snaps).
    if (nUIType == 4 && rs_width > 800) {
        RsForceMiniMapLt(x, y);
        return;
    }
    if (*x < -5 || *x > rs_width - 6 || *y < -5 || *y > rs_height - 6) {
        rs_get_default_position(nUIType, x, y);
    } else {
        RsRemapUiWndXFrom800(nUIType, x);
        if (*x < -5 || *x > rs_width - 6) {
            rs_get_default_position(nUIType, x, y);
        }
    }
}

void CConfig::LoadCharacter_hook(int nWorldID, unsigned int dwCharacterId) {
    CConfig::LoadCharacter(this, nWorldID, dwCharacterId);
    for (size_t i = 0; i < 34; ++i) {
        int nDefaultX = 0, nDefaultY = 0;
        rs_get_default_position(static_cast<int>(i), &nDefaultX, &nDefaultY);
        char sBuffer[64];
        sprintf_s(sBuffer, "uiWndX%zu", i);
        m_nUIWnd_X[i] = GetOpt_Int(GLOBAL_OPT, sBuffer, nDefaultX, -5, rs_width - 6);
        RsRemapUiWndXFrom800(static_cast<int>(i), &m_nUIWnd_X[i]);
        if (m_nUIWnd_X[i] < -5 || m_nUIWnd_X[i] > rs_width - 6) {
            m_nUIWnd_X[i] = nDefaultX;
        }
        sprintf_s(sBuffer, "uiWndY%zu", i);
        m_nUIWnd_Y[i] = GetOpt_Int(GLOBAL_OPT, sBuffer, nDefaultY, -5, rs_height - 6);
        // MiniMap Y also reset to LT (X remapped above).
        if (i == 4 && rs_width > 800) {
            RsForceMiniMapLt(&m_nUIWnd_X[i], &m_nUIWnd_Y[i]);
        }
    }
}

void CConfig::LoadGlobal_hook() {
    CConfig::LoadGlobal(this);
    // config.ini soScreenResolution is the field-tier source of truth. A stale or
    // missing Global.opt default (often 0 = 800x600) must not clobber it on relogin.
    const int iniTier = rs_read_tier_from_ini();
    if (iniTier >= 0 && iniTier <= RS_TIER_MAX) {
        rs_tier = iniTier;
        rs_field_follow_login = false;
    }
    if (!rs_field_follow_login) {
        SetOpt_Int(GLOBAL_OPT, "soScreenResolution", rs_tier);
    }
}

void CConfig::SaveGlobal_hook() {
    if (!rs_field_follow_login) {
        SetOpt_Int(GLOBAL_OPT, "soScreenResolution", rs_tier);
        rs_persist_tier_to_ini(rs_tier);
    }
    CConfig::SaveGlobal(this);
}

// ===== CUISysOpt: resolution combo =====
class CUISysOpt {
public:
    MEMBER_HOOK(void, 0x00994163, OnCreate, void* pData)
    MEMBER_HOOK(void, 0x007FF4AA, Destructor)
};
// Raw pointer only — do NOT use ZRef<CCtrlComboBox> here.
// BeiDou ZRef::operator=(ZRefCounted*) stores the MI-adjusted ZRefCounted*
// subobject (+8) as T*, so CreateCtrl runs with a bad `this` and SysOpt crashes.
// After CreateCtrl the window owns the child; we only keep a non-owning observe ptr.
static CCtrlComboBox* rs_cb = nullptr;
static int rs_cb_last_select = 0;

void CUISysOpt::OnCreate_hook(void* pData) {
    RsLogFlush("[RS] SysOpt OnCreate enter");
    CUISysOpt::OnCreate(this, pData);
    CCtrlComboBox::CREATEPARAM cp;
    cp.nBackColor = 0xFFEEEEEE;
    cp.nBackFocusedColor = 0xFFA5A198;
    cp.nBorderColor = 0xFF999999;
    rs_cb = new CCtrlComboBox();
    // Gold / kaentake SysOpt + 分辨率.png：combo y=338, OK/Cancel y=372.
    rs_cb->CreateCtrl(this, 2000, 0, 76, 338, 166, 18, &cp);
    // Display sorted by width then height. dwParam = stable soScreenResolution ID (0..18).
    // Native SetSelect/m_nSelect are display index — always map via rs_cb_order.
    rs_ensure_cb_order();
    for (int i = 0; i < RS_TIER_COUNT; i++) {
        const int id = rs_cb_order[i];
        char label[32];
        sprintf_s(label, "%d x %d", kRsTierDims[id][0], kRsTierDims[id][1]);
        rs_cb->AddItem(label, (unsigned int)id);
    }
    if (rs_tier < 0 || rs_tier > RS_TIER_MAX) rs_tier = 0;
    rs_cb->SetSelect(rs_combo_index_from_tier(rs_tier));
    rs_cb_last_select = rs_tier;
    char buf[128];
    sprintf_s(buf, "[RS] SysOpt combo created select=%d follow_login=%d",
              rs_tier, rs_field_follow_login ? 1 : 0);
    RsLogFlush(buf);
}

void CUISysOpt::Destructor_hook() {
    // 7FF4AA is an adjustor-entry dtor (ecx = CUISysOpt+8); keep `this` as-is for trampoline.
    if (rs_cb) rs_cb_last_select = rs_tier_from_combo_index(rs_cb->m_nSelect);
    CUISysOpt::Destructor(this);
    rs_cb = nullptr; // non-owning; game already tore down the child
    // Apply resolution AFTER SysOpt is gone (switch-under-modal + StatusBar recreate hung).
    if (rs_deferredTier >= 0) {
        const int t = rs_deferredTier;
        rs_deferredTier = -1;
        char buf[80];
        sprintf_s(buf, "[RS] SysOpt closed — apply deferred tier %d", t);
        RsLogFlush(buf);
        rs_switchToTier(t);
    }
}

static void rs_reloadBackSeh(CField* field) {
    __try {
        field->RestoreViewRange_hook();
        reinterpret_cast<void(__thiscall*)(CMapLoadable*)>(0x00644491)(field);
        RsLogFlush("[RS] deferred RestoreViewRange+ReloadBack OK");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RsLogFlush("[RS] deferred ReloadBack SEH caught");
    }
}

static void rs_logGr2dAfterReset() {
    auto* gr = reinterpret_cast<RsGr2D*>(get_gr().GetInterfacePtr());
    if (gr && Client::WindowedMode)
        gr->ForceWindowedFlag();
    IWzGr2DPtr pGr = get_gr();
    if (!pGr) return;
    unsigned gw = 0, gh = 0;
    int gfs = -99;
    try {
        gw = pGr->Getwidth();
        gh = pGr->Getheight();
        gfs = pGr->GetfullScreen();
    } catch (...) {
    }
    char buf[192];
    sprintf_s(buf, "[RS] after Reset Gr2D %ux%u fullScreen=%d (layout %dx%d)",
              gw, gh, gfs, rs_width, rs_height);
    RsLogFlush(buf);
    if (gw && gh && (gw != (unsigned)rs_width || gh != (unsigned)rs_height)) {
        RsLogFlush("[RS] Gr2D/HWND size mismatch — would stretch/blur; leave layout, do not shrink HWND");
    }
}

static void rs_flushPendingFieldRefresh() {
    if (!rs_pendingFieldRefresh) return;
    rs_pendingFieldRefresh = false;
    if (!rs_originActive) {
        RsLogFlush("[RS] skip deferred ReloadBack (Origin inactive)");
        return;
    }
    CField* field = get_field();
    if (!field) {
        RsLogFlush("[RS] skip deferred ReloadBack (no field)");
        return;
    }
    rs_reloadBackSeh(field);
    rs_logGr2dAfterReset();
}

void CWvsApp::CallUpdate_hook(int tCurTime) {
    // Weather: release field layers on logout/char-select (LoadMap only runs on enter).
    // Ground FX run per RENDERED frame here, not in the 30ms logic tick.
    extern void Weather_Tick();
    extern void WeatherPuddle_Frame();
    extern void WeatherAccum_Frame();
    extern void WeatherSplash_Frame();
    extern void WeatherMove_Frame();
    extern void WeatherMove_Restore();
    extern void WeatherSway_Frame();
    extern void HairSway_Tick();
    extern bool Weather_IsFieldActive();
    extern bool Weather_HasFallingSky();
    Weather_Tick();
    HairSway_Tick();
    if (Weather_IsFieldActive()) {
        if (Weather_HasFallingSky()) {
            WeatherSplash_Frame();
            WeatherPuddle_Frame();
            WeatherAccum_Frame();
            WeatherMove_Frame();
        } else {
            WeatherMove_Restore();
        }
        WeatherSway_Frame();
    }

    CWvsApp::CallUpdate(this, tCurTime);
    rs_flushPendingFieldRefresh();
}

// ===== CConfig::ApplySysOpt =====
void CConfig::ApplySysOpt_hook(void* pSysOpt, int bApplyVideo) {
    CConfig::ApplySysOpt(this, pSysOpt, bApplyVideo);
    {
        char buf[192];
        sprintf_s(buf,
            "[RS] ApplySysOpt pSysOpt=%d bApplyVideo=%d cb=%d follow_login=%d cur_tier=%d",
            pSysOpt ? 1 : 0, bApplyVideo, rs_cb ? 1 : 0,
            rs_field_follow_login ? 1 : 0, rs_tier);
        RsLogFlush(buf);
    }
    if (!pSysOpt || !bApplyVideo) return;

    int sel;
    if (rs_cb) {
        const int idx = rs_cb->m_nSelect;
        sel = rs_tier_from_combo_index(idx);
        rs_cb_last_select = sel; // stable id, not display index (combo dies on close)
        {
            char buf[128];
            sprintf_s(buf, "[RS] ApplySysOpt combo idx=%d -> tier %d", idx, sel);
            RsLogFlush(buf);
        }
    } else {
        sel = rs_cb_last_select;
    }
    if (sel < 0 || sel > RS_TIER_MAX) {
        char buf[64];
        sprintf_s(buf, "[RS] ApplySysOpt invalid select=%d", sel);
        RsLogFlush(buf);
        return;
    }
    int wantW = 800, wantH = 600;
    rs_tier_dims(sel, wantW, wantH);
    // Prefer re-apply when live dims still mismatch (prior ScreenResolution soft/hard fail).
    if (sel == rs_tier && !rs_field_follow_login && rs_width == wantW && rs_height == wantH) {
        rs_persist_tier_to_ini(sel);
        char buf[96];
        sprintf_s(buf, "[RS] ApplySysOpt no-op (tier %d already live %dx%d)", sel, rs_width, rs_height);
        RsLogFlush(buf);
        return;
    }
    // Do not persist until the switch actually lands — a failed ultrawide used
    // to rewrite soScreenResolution while the window stayed on the old size.
    rs_deferredTier = sel;
    {
        char buf[128];
        sprintf_s(buf, "[RS] ApplySysOpt queue tier %d (%dx%d, live=%dx%d) on SysOpt close",
                  sel, wantW, wantH, rs_width, rs_height);
        RsLogFlush(buf);
    }
}

// ===== CInputSystem: cursor =====
class RsInput : public TSingleton<RsInput, 0x00BEC33C> {
public:
    MEMBER_AT(HWND, 0x0, m_hWnd)
    MEMBER_AT(IWzVector2DPtr, 0x9B0, m_pVectorCursor)
    MEMBER_HOOK(void, 0x0059A0CB, SetCursorVectorPos, int x, int y)
    MEMBER_HOOK(int, 0x0059A887, SetCursorPos, int x, int y)
    int GetCursorPos(POINT* pt) { return ::GetCursorPos(pt) && ::ScreenToClient(m_hWnd, pt); }
};

void RsInput::SetCursorVectorPos_hook(int x, int y) {
    // When Origin inactive (login), adj must stay 0 even if hook remains attached.
    const int adj = rs_originActive ? rs_adjust_cy : 0;
    m_pVectorCursor->RelMove(x - rs_width / 2, y - rs_height / 2 - adj);
}

int RsInput::SetCursorPos_hook(int x, int y) {
    POINT pt;
    pt.x = zclamp(x, 0, rs_width);
    pt.y = zclamp(y, 0, rs_height);
    SetCursorVectorPos_hook(pt.x, pt.y);
    return ::ClientToScreen(m_hWnd, &pt) && ::SetCursorPos(pt.x, pt.y);
}

// ===== CUIToolTip bounds =====
// Screen-edge clamp ONLY (SetItem companion tip, BossHP %, equip tips, etc.).
// Never re-origin or StatusBar-snap tooltips — callers own (x,y).
IWzCanvasPtr* CUIToolTip::MakeLayer_hook(IWzCanvasPtr* result, int nLeft, int nTop,
    int bDoubleOutline, int bLogin, int bCharToolTip, unsigned int uColor) {
    CUIToolTip::MakeLayer(this, result, nLeft, nTop, bDoubleOutline, bLogin, bCharToolTip, uColor);
    // Screen-edge clamp only when size is valid. Skip if width/height unset (companion
    // SetItem / Potential / Hyper tips) — RelMove(boundX - 0) shoved tips off-screen.
    if (!bCharToolTip && m_pLayer && m_nWidth > 0 && m_nHeight > 0) {
        if (nLeft < 0) nLeft = 0;
        if (nTop < 0) nTop = 0;
        int nBoundX = rs_width - 1;
        if (nLeft + m_nWidth > nBoundX) nLeft = nBoundX - m_nWidth;
        int nBoundY = rs_height - 1;
        if (nTop + m_nHeight > nBoundY) nTop = nBoundY - m_nHeight;
        if (nLeft < 0) nLeft = 0;
        if (nTop < 0) nTop = 0;
        m_pLayer->RelMove(nLeft, nTop);
    }
    return result;
}

// ===== CTemporaryStatView: buff icons =====
void CTemporaryStatView::AdjustPosition_hook() {
    int nOffsetX = (rs_width / 2) - 3 + (-32 * m_lTemporaryStat.GetCount());
    int nOffsetY = (rs_height / 2) + rs_adjust_cy - 23;
    auto pos = m_lTemporaryStat.GetHeadPosition();
    while (pos) {
        auto pNext = m_lTemporaryStat.GetNext(pos);
        pNext->pLayer->RelMove((32 - pNext->pLayer->width) / 2 + nOffsetX,
            (32 - pNext->pLayer->height) / 2 - nOffsetY);
        pNext->pLayerShadow->RelMove((32 - pNext->pLayerShadow->width) / 2 + nOffsetX,
            (32 - pNext->pLayerShadow->height) / 2 - nOffsetY);
        nOffsetX += 32;
    }
}

int CTemporaryStatView::ShowToolTip_hook(CUIToolTip& uiToolTip, const POINT& ptCursor, int rx, int ry) {
    POINT ptAdjust = { ptCursor.x + 800 - rs_width, ptCursor.y };
    return CTemporaryStatView::ShowToolTip(this, uiToolTip, ptAdjust, rx, ry);
}

int CTemporaryStatView::FindIcon_hook(const POINT& ptCursor, int& nType, int& nID) {
    POINT ptAdjust = { ptCursor.x + 800 - rs_width, ptCursor.y };
    return CTemporaryStatView::FindIcon(this, ptAdjust, nType, nID);
}

// ===== CUIScreenMsg: canvas width + RelMove (kaentake LB StatusBar path) =====
HRESULT __stdcall rs_ScreenMsg_raw_RelMove_hook(IWzVector2D* pThis, int nX, int nY,
    VARIANT nTime, VARIANT nType) {
    nX = nX + 290 - RS_SCREEN_MESSAGE_WIDTH;
    if (rs_width > 800) {
        auto* sb = CUIStatusBar::GetInstance();
        if (sb && sb->m_bQuickSlotUp)
            nY = nY + 443 - 365;
    }
    return pThis->raw_RelMove(nX, nY, nTime, nType);
}

HRESULT __fastcall rs_ScreenMsg_RelMove_hook(IWzVector2D* pThis, void* /*edx*/, int nX, int nY,
    const Ztl_variant_t& nTime, const Ztl_variant_t& nType) {
    nX = nX + 290 - RS_SCREEN_MESSAGE_WIDTH;
    if (rs_width > 800) {
        auto* sb = CUIStatusBar::GetInstance();
        if (sb && sb->m_bQuickSlotUp)
            nY = nY + 443 - 365;
    }
    return pThis->RelMove(nX, nY, nTime, nType);
}

// CField::ShowMobHPTag nLeft — PatchCall @0x533705 replaces 15 bytes that both
// computed minimap width into EAX and stored it to [ebp-44h] (v184).
//
// Layer is parented to Origin_CT. BeiDou Origin CT screen X is (W-800)/2 (not W/2):
//   CT RelMove X = -W/2 + (W-800)/2  →  screen = center + that = (W-800)/2
// Vanilla: nLeft = miniW, width = 800 - miniW  (at W=800, CT at x=0 → bar after minimap).
// Broken TIP restore: nLeft=-W/2 + width=(W-15)-nLeft → overshoots right on HD.
//
// Correct (CT):
//   nLeft  = miniW - (W-800)/2     → screenLeft = miniW
//   width  = (W-15) - miniW        → screenRight = W-15
// Width cannot reuse the same [ebp-44] as CT-adjusted nLeft, so NOP the sub and
// write the final width immediate at 0x533B03 each call.
class RsMiniMap : public CUIWnd, public TSingleton<RsMiniMap, 0x00BED788> {};
static int rs_ReadMiniMapWidth() {
    constexpr int kFallbackMiniW = 150;
    int miniW = 0;
    __try {
        if (RsMiniMap::IsInstantiated()) {
            if (RsMiniMap* mm = RsMiniMap::GetInstance()) {
                miniW = mm->m_width; // CWnd::m_width @ +0x24
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        miniW = 0;
    }
    if (miniW < 32) {
        miniW = kFallbackMiniW;
    }
    if (miniW > rs_width / 2) {
        miniW = rs_width / 2;
    }
    return miniW;
}
static void rs_EnsureBossHpWidthNop() {
    // 0x533B08: sub eax,[ebp-44] → NOP (width immediate is final bar width)
    Memory::WriteByte(0x00533B08, 0x90);
    Memory::WriteByte(0x00533B09, 0x90);
    Memory::WriteByte(0x00533B0A, 0x90);
}
static int rs_ShowMobHPTag_nLeft_calc() {
    const int W = rs_width > 0 ? rs_width : 800;
    const int miniW = rs_ReadMiniMapWidth();
    int barW = W - 15 - miniW;
    if (barW < 100) {
        barW = 100;
    }
    // mov eax, imm32 @ CreateLayer width site
    Memory::WriteByte(0x00533B03, 0xB8);
    Memory::WriteInt(0x00533B03 + 1, barW);
    rs_EnsureBossHpWidthNop();

    // CT screen X = (W-800)/2  →  nLeft so screenLeft = miniW
    const int ctScreenX = (W - 800) / 2;
    const int nLeft = miniW - ctScreenX;
    static int s_logged = 0;
    if (s_logged < 3) {
        ++s_logged;
        std::cout << "[RS] BossHP nLeft=" << nLeft << " miniW=" << miniW
                  << " barW=" << barW << " W=" << W
                  << " ctX=" << ctScreenX << std::endl;
    }
    return nLeft;
}
void __declspec(naked) rs_ShowMobHPTag_nLeft_hook() {
    __asm {
        call rs_ShowMobHPTag_nLeft_calc
        mov  dword ptr [ebp-0x44], eax
        ret
    }
}

// ===== CUIStatusBar: quickslot =====
int CUIStatusBar::GetShortCutIndexByPos_hook(int x, int y) {
    if (rs_orgStatusBar && rs_orgQuickSlot) {
        x = x + rs_orgStatusBar->x - rs_orgQuickSlot->x;
        y = y + rs_orgStatusBar->y - rs_orgQuickSlot->y;
    }
    return CUIStatusBar::GetShortCutIndexByPos(this, x, y);
}

// ===== CMapLoadable: view range =====
void CMapLoadable::RestoreViewRange_hook() {
    auto* pSpace2D = RsPhy::GetInstance();
    m_rcViewRange.left = get_int32(m_pPropFieldInfo->item[L"VRLeft"], pSpace2D->m_rcMBR.left - 20) + rs_width / 2;
    m_rcViewRange.top = get_int32(m_pPropFieldInfo->item[L"VRTop"], pSpace2D->m_rcMBR.top - 60) + rs_height / 2;
    m_rcViewRange.right = get_int32(m_pPropFieldInfo->item[L"VRRight"], pSpace2D->m_rcMBR.right + 20) - rs_width / 2;
    m_rcViewRange.bottom = get_int32(m_pPropFieldInfo->item[L"VRBottom"], pSpace2D->m_rcMBR.bottom + 190) - rs_height / 2;
    if (m_rcViewRange.right - m_rcViewRange.left <= 0) {
        int mid = (m_rcViewRange.left + m_rcViewRange.right) / 2;
        m_rcViewRange.left = mid; m_rcViewRange.right = mid;
    }
    if (m_rcViewRange.bottom - m_rcViewRange.top <= 0) {
        int mid = (m_rcViewRange.top + m_rcViewRange.bottom) / 2;
        m_rcViewRange.top = mid; m_rcViewRange.bottom = mid;
    }
    m_rcViewRange.top += rs_adjust_cy;
    m_rcViewRange.bottom += rs_adjust_cy;
}

// ===== MakeGrid =====
static auto rs_grid_jmp = 0x0063EAD6;
static auto rs_grid_ret = 0x0063EADC;
__declspec(naked) void rs_grid_hook() {
    __asm {
        sar ecx, 1
        neg eax
        sub eax, ecx
        sub eax, rs_adjust_cy
        jmp[rs_grid_ret]
    }
}

// ===== Origin 9-grid anchor matrix =====
static const uintptr_t RS_CWNDMAN_ORG_WND = 0xDC; // m_pOrgWindow offset in CWndMan

// Access CWndMan's m_pOrgWindow via raw offset
static IWzVector2DPtr& RsGetOrgWindow() {
    auto* pMan = reinterpret_cast<unsigned char*>(RsGetCWndMan());
    return *reinterpret_cast<IWzVector2DPtr*>(pMan + RS_CWNDMAN_ORG_WND);
}

// Access CWndMan's global window list
static ZList<CWnd*>& RsGetWndList() {
    return *reinterpret_cast<ZList<CWnd*>*>(0x00BF1648);
}

// Recalculate all origin positions based on current rs_width/rs_height
static bool rs_originsReady = false;

// UpdateResolution (ezorsia) flush-bottom StatusBar via nStatusBarY=H-578 and
// hard-coded QuickSlot coords. Origin mode needs vanilla CreateWnd(0,22) +
// QuickSlot (647,533) / hit-test (647,427); HD offset comes from rs_orgQuickSlot.
//
// Login UpdateResolution also patches StatusBar *child* RelMove/canvas sizes to
// H-19 / H-22. Those fight LB+(0,22)/height=578 and leave a visual gap under the
// chrome even when Abs top == H-578. Restore every StatusBar site to 600-era
// constants; only widths follow rs_width (kaentake uses SCREEN_WIDTH_MAX).
//
// LongQuickSlot (Client::LongQuickSlot) widens the QS canvas 151→1008 and lays out
// 26 keys to x≈459. qsOrg=W-800 only right-aligns the vanilla 153px strip, so the
// expanded 键盘 clips past X=W. Right-align the key grid instead (see RsQsOrgX).
extern int nStatusBarY;
static constexpr int kRsQsCreateX = 647;       // vanilla CQuickSlot CreateWnd X
static constexpr int kRsQsVanillaPanel = 153;  // 800 - 647
static constexpr int kRsQsLongKeyExtent = 459; // LongQuickSlot key grid right extent

// qsOrg.x relative to LB so CreateWnd(647)+keyExtent lands on the right edge.
static int RsQsOrgX() {
    if (rs_width <= 800) return 0;
    return rs_width - kRsQsCreateX - kRsQsLongKeyExtent; // W-1106
}
static int RsQsExpectL() {
    if (rs_width <= 800) return kRsQsCreateX;
    return rs_width - kRsQsLongKeyExtent; // W-459
}

// Weather / LimitedView / ScreenMsg capacity — refresh on every HD switch.
// Do NOT install kaentake WrapClip/CopyEx PatchCalls: BeiDou darkMap codecaves
// already own DrawViewRange (0x55BEE6/C07F/C1C5). Live dims match UpdateResolution.
static void RsApplyFieldEffectMetrics() {
    const unsigned w = static_cast<unsigned>(rs_width);
    const unsigned h = static_cast<unsigned>(rs_height);
    const unsigned halfW = w / 2;
    const unsigned halfH = h / 2;
    const int adj = rs_adjust_cy;

    // CMapLoadable::TransientLayer_Weather immediates (kaentake list, live dims)
    Patch4(0x0064043E + 1, halfW);
    Patch4(0x00640443 + 1, halfH);
    Patch4(0x00640599 + 2, halfW > 10 ? halfW - 10 : 0);
    Patch4(0x006405BA + 2, h > 10 ? h - 10 : 0);
    Patch4(0x00640606 + 1, w);
    Patch4(0x00640618 + 1, h);
    Patch4(0x00640626 + 1, halfW);
    Patch4(0x00640639 + 1, w);
    Patch4(0x0064064B + 1, h);
    Patch4(0x00640656 + 2, static_cast<unsigned>(-static_cast<int>(halfW)));
    Patch4(0x006406C3 + 1, w);
    Patch4(0x006406D5 + 1, h);
    Patch4(0x006406FA + 2, halfH);
    // Related weather / player-display immediates also touched by UpdateResolution
    Memory::WriteInt(0x00641038 + 2, static_cast<int>(h));
    Memory::WriteInt(0x0064103F + 2, static_cast<int>(w));
    Memory::WriteInt(0x00641048 + 1, -static_cast<int>(halfH));
    Memory::WriteInt(0x00641050 + 1, -static_cast<int>(halfW));

    // CField_LimitedView::Init — canvas size + layer RelMove (letterbox adj)
    Patch4(0x0055B808 + 1, h);
    Patch4(0x0055B80D + 1, w);
    Patch4(0x0055B884 + 1, h); // DrawRectangle height (kaentake; Client had width by mistake)
    Patch4(0x0055BB2F + 1, static_cast<unsigned>(-static_cast<int>(halfH) - adj));
    Patch4(0x0055BB35 + 1, static_cast<unsigned>(-static_cast<int>(halfW)));
    // BeiDou darkMap caves — keep in sync with current center
    darkCircleX = static_cast<int>(halfW) - 163;
    darkCircleY = static_cast<int>(halfH) - 190;

    // CUIScreenMsg canvas width
    Patch4(0x0089AF33 + 1, RS_SCREEN_MESSAGE_WIDTH);
    Patch4(0x0089B2C6 + 1, RS_SCREEN_MESSAGE_WIDTH);
}

static void RsApplyOriginStatusBarMetrics() {
    const unsigned barW = rs_width > 800 ? static_cast<unsigned>(rs_width) : 800u;
    nStatusBarY = 22; // AdjustStatusBar cave → push 22 (vanilla)
    // CUIStatusBar::CUIStatusBar CreateWnd (w,h)
    Patch4(0x008CFD4B + 1, 578u);
    Patch4(0x008CFD50 + 1, barW);
    // OnCreate / Draw canvas sizes + other CreateWnd height/width
    Memory::WriteInt(0x008D1D50 + 1, 578);
    Memory::WriteInt(0x008D1D55 + 1, static_cast<int>(barW));
    Memory::WriteInt(0x008D1FF4 + 1, 578);
    Memory::WriteInt(0x008D1FF9 + 1, static_cast<int>(barW));
    Memory::WriteInt(0x008D82F5 + 1, 578);
    Memory::WriteInt(0x008D82FA + 1, static_cast<int>(barW));
    // Child RelMove Y — vanilla 600-based (NOT current H-19 / H-20 / H-33)
    Memory::WriteInt(0x008D247B + 1, 567);
    Memory::WriteInt(0x008DEB93 + 1, 580);
    Memory::WriteInt(0x008DEE2F + 1, 580);
    Memory::WriteInt(0x008D2765 + 1, 581);
    Memory::WriteInt(0x008DA11C + 1, 581);
    Memory::WriteInt(0x008D29B4 + 1, 581);
    Memory::WriteInt(0x008D8BFE + 1, 581);
    Memory::WriteInt(0x008D937E + 1, 581);
    Memory::WriteInt(0x008D9AC9 + 1, 581);
    // CQuickSlot init / toggle RelMove — restore vanilla 533 / 647
    Memory::WriteInt(0x008D1793 + 1, 0x215); // add eax, 533
    Memory::WriteInt(0x008D179A + 1, 0x287); // push 647
    Memory::WriteInt(0x008DF782 + 2, 0x215); // add esi, 533
    Memory::WriteInt(0x008DF7F8 + 1, 0x287); // push 647
    // GetShortCutIndexByPos lea [reg-imm] — vanilla -647 / -427
    Memory::WriteInt(0x008DE8E5 + 2, static_cast<int>(-0x287));
    Memory::WriteInt(0x008DE8EE + 2, static_cast<int>(-0x1AB));

    // UpdateResolution wrote screen-abs Y for GameMenu/ShortCut CreateFadeWnd
    // (H-177 / H-281). With StatusBar origin at (0,H-600) that double-counts the
    // bottom offset — login H=720 → rel Y 543/439. Restore vanilla RelMove seeds
    // (kaentake keeps 666,423 / 707,296; org already embeds H-600).
    Memory::WriteInt(0x00849E39 + 1, 423); // GameMenu 界面 CreateFadeWnd Y
    Memory::WriteInt(0x0084A5B7 + 1, 296); // ShortCut 目录 CreateFadeWnd Y

    // UpdateResolution patches that fight Origin (keep vanilla 800-based coords;
    // RT/RB origins already sit at abs X = W-800 so CreateWnd(800,…) → screen right).
    Memory::WriteInt(0x0045A5CB + 1, 800); // CAvatarMegaphone CreateWnd left
    Memory::WriteInt(0x0045B97E + 1, 800); // ByeAvatarMegaphone RelMove X
    // CConfig UI position clamp max (loaded once at boot with login dims)
    Memory::WriteInt(0x0049D218 + 1, rs_width - 16);
    Memory::WriteInt(0x0049D268 + 1, rs_height - 16);
    // SlideNotice / top banner width follow current tier
    Memory::WriteInt(0x007E15BE + 1, rs_width);
    Memory::WriteInt(0x007E16BE + 1, rs_width);
    Memory::WriteInt(0x007E1E07 + 2, rs_width);

    // Boss HP bar span (Client::UpdateResolution) — re-assert on Origin HD so
    // login-sized 800 patches are not left stale. Does not touch BossHP % tip.
    Memory::WriteByte(0x00533B03, 0xb8);
    Memory::WriteInt(0x00533B03 + 1, rs_width - 15);
    Memory::WriteByte(0x00534370, 0xb9);
    Memory::WriteInt(0x00534370 + 1, rs_width - 22);
    Memory::WriteInt(0x005362B2 + 1, (rs_width / 2) - 129);
    Memory::WriteInt(0x005364AA + 2, (rs_width / 2) - 128);

    // UIMenu / ContextMenu / Popup: keep clamps at live W×H. Hooks place at cursor +
    // clamp (kaentake); offsetX patch is belt-and-suspenders for StatusBar ctor args.
    Memory::WriteInt(0x009966B5 + 1, rs_height);           // ContextMenu Y max
    Memory::WriteInt(0x009966CA + 2, rs_width);            // ContextMenu X cmp
    Memory::WriteInt(0x009966D2 + 1, rs_width - 100);      // ContextMenu X overflow
    Memory::WriteInt(0x009994D8 + 1, rs_height);           // UIMenu Y max
    Memory::WriteInt(0x009994ED + 2, rs_width);            // UIMenu X cmp
    Memory::WriteInt(0x009994F5 + 1, rs_width - 100);      // UIMenu X overflow
    Memory::WriteInt(0x0085F361 + 1, rs_height - 39);      // Popup Y
    Memory::WriteInt(0x0085F36C + 2, rs_width);            // Popup X cmp
    Memory::WriteInt(0x0085F374 + 1, rs_width - 80);       // Popup X overflow
    // UtilDlgEx / CreateDlg body clamps: UtilDlgEx bypasses CreateDlg; body is dead
    // after PatchJmp. Leave 800×600 so any fallback CC path stays vanilla-relative.
    Memory::WriteInt(0x009A3E7F + 1, 600);
    Memory::WriteInt(0x009A3E72 + 1, 800);
    Memory::WriteInt(0x004EDB78 + 1, 600);
    Memory::WriteInt(0x004EDB89 + 1, 800);
    // Width-dependent immediates only — do NOT reinstall detours here (idempotent WriteInt
    // is fine; re-PatchCall/SetHook during D3D reset while SysOpt is open hung the client).
    RsPatchStatusBarMenuOffset();
    RsPatchUIMenuClamps();
    rs_refreshWorldMapCenter();
    RsApplyFieldEffectMetrics();
}

// StatusBar/QuickSlot created under UpdateResolution before Origin hooks: m_pLayer
// RelMove alone cannot fix OnCreate children still parented to m_pOrgWindow (LT).
// Kaentake never needs this — origins exist before CreateWnd. Recreate StatusBar
// so CreateWnd_hook + GetOrgWindow bind every layer to LB from birth.
static constexpr uintptr_t RS_STATUSBAR_QUICKSLOT_LAYER = 0xCC4;
static auto RsFactoryStatusBar = reinterpret_cast<CUIStatusBar*(__cdecl*)()>(0x00A14AB7);

static IUnknown* RsOriginUnk(const Ztl_variant_t& v) {
    if (V_VT(&v) == VT_UNKNOWN || V_VT(&v) == VT_DISPATCH)
        return V_UNKNOWN(&v);
    return nullptr;
}

// Screen-ish abs via same formula as CWnd AbsLeft/Top: vec.x - m_pOrgWindow.x
static void RsVecScreenAbs(IWzVector2D* v, int& outL, int& outT) {
    outL = outT = -99999;
    if (!v) return;
    try {
        IWzVector2D* base = nullptr;
        if (auto* pMan = RsGetCWndMan()) {
            auto& org = RsGetOrgWindow();
            base = org;
        }
        if (base) {
            outL = v->x - base->x;
            outT = v->y - base->y;
        } else {
            outL = v->x;
            outT = v->y;
        }
    } catch (...) {
    }
}

static void RsLogVec(const char* tag, IWzVector2D* v) {
    if (!v) {
        std::cout << "[RS] " << tag << " null" << std::endl;
        return;
    }
    IUnknown* org = nullptr;
    try {
        org = RsOriginUnk(v->origin);
    } catch (...) {
        org = reinterpret_cast<IUnknown*>(1); // log failure marker
    }
    int absL = 0, absT = 0;
    RsVecScreenAbs(v, absL, absT);
    std::cout << "[RS] " << tag
              << " xy=(" << v->x << "," << v->y << ")"
              << " rxy=(" << v->rx << "," << v->ry << ")"
              << " abs=(" << absL << "," << absT << ")"
              << " org=" << org
              << " sbOrg=" << static_cast<IUnknown*>(rs_orgStatusBar)
              << " qsOrg=" << static_cast<IUnknown*>(rs_orgQuickSlot)
              << " LB=" << static_cast<IUnknown*>(rs_orgEx[6])
              << " screen=" << rs_width << "x" << rs_height
              << std::endl;
}

// Kaentake 9-grid: right column abs X = W-800 (NOT W). Windows using RT/RB add
// vanilla +800 (e.g. AvatarMega CreateWnd left=800) to reach the true right edge.
// m_pOrgWindow rxy Y = -(H/2)-adj; for 1600x900 adj=150 → -600 (correct, not -450).
static void RsLogOrigins(const char* why) {
    const int expectRB = rs_width > 800 ? (rs_width - 800) : 0;
    const int expectMid = rs_width > 800 ? (rs_width - 800) / 2 : 0;
    const int expectBot = rs_height > 600 ? (rs_height - 600) : 0;
    std::cout << "[RS] Origins " << why
              << " screen=" << rs_width << "x" << rs_height
              << " adj=" << rs_adjust_cy
              << " qsOff=(" << RsQsOrgX() << ",68)"
              << " expectQS=" << RsQsExpectL()
              << " (vanilla8=" << (rs_width - kRsQsVanillaPanel) << ")"
              << std::endl;
    std::cout << "[RS] Origin expect abs"
              << " LT=(0,0) CT=(" << expectMid << ",0) RT=(" << expectRB << ",0)"
              << " LC=(0," << expectBot / 2 << ") CC=(" << expectMid << "," << expectBot / 2
              << ") RC=(" << expectRB << "," << expectBot / 2 << ")"
              << " LB=(0," << expectBot << ") CB=(" << expectMid << "," << expectBot
              << ") RB=(" << expectRB << "," << expectBot << ")"
              << "  // RB=W-800 by design, not W"
              << std::endl;
    if (auto* pMan = RsGetCWndMan()) {
        auto& org = RsGetOrgWindow();
        if (org) RsLogVec("m_pOrgWindow", org);
    }
    static const char* kOrgTag[9] = {
        "LT", "CT", "RT", "LC", "CC", "RC", "LB", "CB", "RB"
    };
    for (int i = 0; i < 9; ++i) {
        if (rs_orgEx[i]) RsLogVec(kOrgTag[i], rs_orgEx[i]);
    }
    if (rs_orgStatusBar) RsLogVec("sbOrg", rs_orgStatusBar);
    if (rs_orgQuickSlot) RsLogVec("qsOrg", rs_orgQuickSlot);
    if (rs_orgScreenMsg) RsLogVec("scrMsg", rs_orgScreenMsg);
}

// IUIMsgHandler subobject is +4 from CWnd (IGObj vptr). Call game abs helpers directly.
static int RsWndAbsLeft(CWnd* pWnd) {
    if (!pWnd) return -1;
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E03C5)(
        reinterpret_cast<char*>(pWnd) + 4);
}
static int RsWndAbsTop(CWnd* pWnd) {
    if (!pWnd) return -1;
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E0447)(
        reinterpret_cast<char*>(pWnd) + 4);
}

// Game pattern (CWvsContext): Destroy + ZRefCounted deleting-dtor(this+8, 1)
static void RsDeleteWndInstance(CWnd* pWnd) {
    if (!pWnd) return;
    reinterpret_cast<void(__thiscall*)(CWnd*)>(0x009E00AF)(pWnd);
    void* zref = reinterpret_cast<char*>(pWnd) + 8;
    auto* vt = *reinterpret_cast<void***>(zref);
    if (!vt || !vt[0]) return;
    reinterpret_cast<void(__thiscall*)(void*, int)>(vt[0])(zref, 1);
}

static void RsLogStatusBarHud(const char* tag, CUIStatusBar* sb) {
    if (!sb) {
        std::cout << "[RS] " << tag << " StatusBar null" << std::endl;
        return;
    }
    char buf[96];
    const int absL = RsWndAbsLeft(sb);
    const int absT = RsWndAbsTop(sb);
    std::cout << "[RS] " << tag << " StatusBar Abs=" << absL << "," << absT
              << " expect=(0," << (rs_height - 578) << ")"
              << " screen=" << rs_width << "x" << rs_height << std::endl;
    if (sb->m_pLayer) {
        sprintf_s(buf, "%s StatusBar layer", tag);
        RsLogVec(buf, sb->m_pLayer);
    }
    auto& qsLayer = *reinterpret_cast<IWzVector2DPtr*>(
        reinterpret_cast<unsigned char*>(sb) + RS_STATUSBAR_QUICKSLOT_LAYER);
    if (qsLayer) {
        sprintf_s(buf, "%s QuickSlot", tag);
        RsLogVec(buf, qsLayer);
        int qsAbsL = 0, qsAbsT = 0;
        RsVecScreenAbs(qsLayer, qsAbsL, qsAbsT);
        // LongQuickSlot key-grid right-align (W-459). Vanilla 8-key was W-153.
        std::cout << "[RS] " << tag << " QuickSlot abs=(" << qsAbsL << "," << qsAbsT
                  << ") expectL~" << RsQsExpectL()
                  << " longKey=" << kRsQsLongKeyExtent
                  << " (rxy Y 453=up / 533=down)" << std::endl;
    } else {
        std::cout << "[RS] " << tag << " QuickSlot layer missing" << std::endl;
    }
}

static void RsReanchorExistingHud() {
    if (!rs_orgStatusBar || !rs_orgQuickSlot) {
        std::cout << "[RS] Reanchor skipped — origins not ready" << std::endl;
        return;
    }

    RsLogOrigins("before reanchor");

    auto* sb = CUIStatusBar::GetInstance();
    if (sb && rs_hudBoundW == rs_width && rs_hudBoundH == rs_height) {
        const int absL = RsWndAbsLeft(sb);
        const int absT = RsWndAbsTop(sb);
        if (absL == 0 && absT == rs_height - 578) {
            std::cout << "[RS] Reanchor skip — HUD already Origin-bound at "
                      << rs_width << "x" << rs_height << std::endl;
            RsLogStatusBarHud("skip", sb);
            return;
        }
    }

    if (!sb) {
        std::cout << "[RS] Reanchor: no StatusBar — factory create with Origin" << std::endl;
        sb = RsFactoryStatusBar();
        rs_hudBoundW = rs_width;
        rs_hudBoundH = rs_height;
        RsLogStatusBarHud("created", sb);
        return;
    }

    RsLogStatusBarHud("before", sb);

    // Drop UpdateResolution-era instance; factory rebuilds under Origin hooks.
    RsDeleteWndInstance(sb);

    // If deleting-dtor failed to clear singleton, force it so factory can run.
    if (CUIStatusBar::GetInstance()) {
        std::cout << "[RS] Reanchor: singleton still set after delete — forcing null" << std::endl;
        *reinterpret_cast<CUIStatusBar**>(0x00BEC208) = nullptr;
    }

    auto* neu = RsFactoryStatusBar();
    if (!neu || !neu->m_pLayer) {
        std::cout << "[RS] Reanchor: factory failed after delete" << std::endl;
        return;
    }

    rs_hudBoundW = rs_width;
    rs_hudBoundH = rs_height;
    RsLogOrigins("after recreate");
    RsLogStatusBarHud("after", neu);
}

static void RsResetOrgWindow() {
    // Primary origin (same as kaentake ResetOrgWindow). Login uses UpdateResolution
    // center (-W/2,-H/2) without letterbox adj; HD Origin keeps adj_cy.
    const int adj = rs_originActive ? rs_adjust_cy : 0;
    if (auto* pMan = RsGetCWndMan()) {
        auto& org = RsGetOrgWindow();
        if (org) {
            org->origin = static_cast<IUnknown*>(get_gr()->center);
            org->RelMove(-(rs_width / 2), -(rs_height / 2) - adj);
        }
    }

    if (!rs_originActive) {
        // Login: only m_pOrgWindow — leave 9-grid alone (not used without Origin).
        return;
    }

    for (int i = 0; i < 9; ++i) {
        if (!rs_orgEx[i]) continue;
        int nX = -(rs_width / 2);
        if (i % 3 == 1) nX += (rs_width - 800) / 2;
        else if (i % 3 == 2) nX += (rs_width - 800);
        int nY = -(rs_height / 2) - rs_adjust_cy;
        if (i / 3 == 1) nY += (rs_height - 600) / 2;
        else if (i / 3 == 2) nY += (rs_height - 600);
        rs_orgEx[i]->origin = static_cast<IUnknown*>(get_gr()->center);
        rs_orgEx[i]->RelMove(nX, nY);
    }

    if (!rs_orgStatusBar || !rs_orgScreenMsg || !rs_orgQuickSlot) return;
    rs_orgStatusBar->origin = static_cast<IUnknown*>(rs_orgEx[6]); // LB
    rs_orgScreenMsg->origin = static_cast<IUnknown*>(rs_orgEx[8]); // RB
    rs_orgQuickSlot->origin = static_cast<IUnknown*>(rs_orgEx[6]); // LB
    if (rs_width > 800) {
        rs_orgScreenMsg->RelMove(0, -10);
        rs_orgQuickSlot->RelMove(RsQsOrgX(), 68);
    } else {
        rs_orgScreenMsg->RelMove(0, 0);
        rs_orgQuickSlot->RelMove(0, 0);
    }
}

// Create IWzVector2D objects for all origins
static void RsCreateOrigins() {
    for (int i = 0; i < 9; ++i)
        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgEx[i], nullptr);
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgStatusBar, nullptr);
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgScreenMsg, nullptr);
    PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgQuickSlot, nullptr);
}

// Release origin references
static void RsDestroyOrigins() {
    for (int i = 0; i < 9; ++i) rs_orgEx[i] = nullptr;
    rs_orgStatusBar = nullptr; rs_orgScreenMsg = nullptr; rs_orgQuickSlot = nullptr;
}

// CWndMan::Constructor hook — would create origins if CWndMan is re-created
static auto rs_OrigCtor = reinterpret_cast<void(__thiscall*)(void*, HWND)>(0x009E2C42);
void __fastcall rs_Ctor_hook(void* pThis, void* edx, HWND hWnd) {
    rs_OrigCtor(pThis, hWnd);
    RsCreateOrigins();
    RsResetOrgWindow();
}

// CWndMan::Destructor hook — cleanup
static auto rs_OrigDtor = reinterpret_cast<void(__thiscall*)(void*)>(0x009E3026);
void __fastcall rs_Dtor_hook(void* pThis, void* edx) {
    rs_OrigDtor(pThis);
    RsDestroyOrigins();
}

// CWndMan::GetOrgWindow hook — return matching origin based on caller
// Boss HP bar (ShowMobHPTag @0x533B77) MUST stay CT — never redirect to LT/StatusBar.
static auto rs_OrigGetOrg = reinterpret_cast<IWzVector2DPtr*(__thiscall*)(void*, IWzVector2DPtr*)>(0x0048BBA5);
IWzVector2DPtr* __fastcall rs_GetOrg_hook(void* pThis, void* edx, IWzVector2DPtr* result) {
    if (!rs_originActive) {
        return rs_OrigGetOrg(pThis, result);
    }
    auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    switch (ret) {
    case 0x00533B77: case 0x005555B7: case 0x0058C91C: case 0x006CD787:
        result->GetInterfacePtr() = rs_orgEx[1]; break;            // CT: BossHP bar, Dojang clock
    case 0x009603F2:
        result->GetInterfacePtr() = rs_orgEx[2]; break;            // RT: Combo counter
    case 0x0053502B:
        result->GetInterfacePtr() = rs_orgEx[4]; break;            // CC: Screen effect
    case 0x008DEB75: case 0x008DEE11:
        result->GetInterfacePtr() = rs_orgStatusBar; break;        // HP/MP flash
    case 0x0089AF82:
        result->GetInterfacePtr() = rs_orgScreenMsg; break;        // ScreenMsg
    case 0x008D15EE:
        result->GetInterfacePtr() = rs_orgQuickSlot; break;        // QuickSlot
    default:
        if (ret >= 0x00554005 && ret <= 0x0055478F)
            result->GetInterfacePtr() = rs_orgEx[1];               // Dojang Init → CT
        else if (ret >= 0x008D01B2 && ret <= 0x008D3ADF)
            result->GetInterfacePtr() = rs_orgStatusBar;           // StatusBar::OnCreate
        else
            result->GetInterfacePtr() = RsGetOrgWindow();          // Default: raw origin
        break;
    }
    result->AddRef();
    return result;
}

// kaentake MoveWndToAbsPos — convert screen-abs to layer RelMove under non-LT origin
static void RsMoveWndToAbsPos(CWnd* pWnd, int l, int t) {
    if (!pWnd || !pWnd->m_pLayer || !RsGetCWndMan()) return;
    auto& absOrg = RsGetOrgWindow();
    if (!absOrg) return;
    IWzVector2DPtr pWndOrigin = static_cast<IUnknown*>(pWnd->m_pLayer->origin);
    if (!pWndOrigin) {
        pWnd->m_pLayer->RelMove(l, t);
        return;
    }
    const int nOffsetX = absOrg->x - pWndOrigin->x;
    const int nOffsetY = absOrg->y - pWndOrigin->y;
    pWnd->m_pLayer->RelMove(l + nOffsetX, t + nOffsetY);
}

static auto rs_OrigOnMoveWnd = reinterpret_cast<void(__thiscall*)(CWnd*, int, int)>(0x009DEB57);
void __fastcall rs_OnMoveWnd_hook(CWnd* pThis, void* /*edx*/, int l, int t) {
    if (!rs_originActive) {
        rs_OrigOnMoveWnd(pThis, l, t);
        return;
    }
    // Use +4 IUIMsgHandler abs helpers — CWnd::GetAbsLeft virtual on mixed objects is unsafe.
    int nLeft = RsWndAbsLeft(pThis);
    int nTop = RsWndAbsTop(pThis);
    int nWidth = pThis->m_pLayer ? pThis->m_pLayer->width : 0;
    int nHeight = pThis->m_pLayer ? pThis->m_pLayer->height : 0;
    POINT pt{};
    RsGetClientCursor(pt);
    POINT ptRel{ pt.x - nLeft, pt.y - nTop };
    if (pThis->m_ptCursorRel.x == -1 && pThis->m_ptCursorRel.y == -1) {
        pThis->m_ptCursorRel.x = ptRel.x;
        pThis->m_ptCursorRel.y = ptRel.y;
    }

    // kaentake: snap to nearby windows (skip self / addons / StatusBar).
    RECT rcThis{};
    SetRect(&rcThis, nLeft, nTop, nLeft + nWidth, nTop + nHeight);
    auto* pos = RsGetWndList().GetHeadPosition();
    while (pos) {
        auto* pNext = RsGetWndList().GetNext(pos);
        if (pNext == pThis || pNext == CUIStatusBar::GetInstance()) continue;
        // IsMyAddOn is per-subclass virtual on the live game object.
        if (pThis->IsMyAddOn(pNext)) continue;
        const int nNextLeft = RsWndAbsLeft(pNext);
        const int nNextTop = RsWndAbsTop(pNext);
        const int nNextWidth = pNext->m_pLayer ? pNext->m_pLayer->width : 0;
        const int nNextHeight = pNext->m_pLayer ? pNext->m_pLayer->height : 0;
        RECT rcNext{}, rcIntersect{};
        SetRect(&rcNext, nNextLeft - 10, nNextTop - 10,
                nNextLeft + nNextWidth + 10, nNextTop + nNextHeight + 10);
        if (!IntersectRect(&rcIntersect, &rcThis, &rcNext)) continue;
        if (abs(nLeft - nNextLeft - nNextWidth) <= 10)
            RsMoveWndToAbsPos(pThis, nNextLeft + nNextWidth, nTop);
        if (abs(nLeft - nNextLeft + nWidth) <= 10)
            RsMoveWndToAbsPos(pThis, nNextLeft - nWidth, nTop);
        if (abs(nTop - nNextTop - nNextHeight) <= 10)
            RsMoveWndToAbsPos(pThis, nLeft, nNextTop + nNextHeight);
        if (abs(nTop - nNextTop + nHeight) <= 10)
            RsMoveWndToAbsPos(pThis, nLeft, nNextTop - nHeight);
        nLeft = RsWndAbsLeft(pThis);
        nTop = RsWndAbsTop(pThis);
        SetRect(&rcThis, nLeft, nTop, nLeft + nWidth, nTop + nHeight);
    }

    // Screen-border snap
    if (abs(nLeft) <= 10) RsMoveWndToAbsPos(pThis, 0, nTop);
    if (abs(nTop) <= 10) RsMoveWndToAbsPos(pThis, nLeft, 0);
    if (nWidth > 0 && abs(nLeft + nWidth - rs_width) <= 10)
        RsMoveWndToAbsPos(pThis, rs_width - nWidth, nTop);
    if (nHeight > 0 && abs(nTop + nHeight - rs_height) <= 10)
        RsMoveWndToAbsPos(pThis, nLeft, rs_height - nHeight);
    if (abs(pThis->m_ptCursorRel.x - ptRel.x) > 15)
        RsMoveWndToAbsPos(pThis, pt.x - pThis->m_ptCursorRel.x, nTop);
    if (abs(pThis->m_ptCursorRel.y - ptRel.y) > 15)
        RsMoveWndToAbsPos(pThis, nLeft, pt.y - pThis->m_ptCursorRel.y);
    nLeft = RsWndAbsLeft(pThis);
    nTop = RsWndAbsTop(pThis);
    rs_OrigOnMoveWnd(pThis, nLeft, nTop);
}

// CUIMenu vtable (sub_999320: *this = &off_B3DD98). CreateDlg→CreateWnd ret @0x4EDAB3.
// ONLY match by vtable. Stack-scan for 0x99950B false-positive'd CUISysOpt (299x396,
// vt=B38AFC) as "stack99950B" and ForceLtAbs'd it → SysOpt hang / black screen.
static constexpr uintptr_t kRsCUIMenuVtable = 0x00B3DD98;
static constexpr uintptr_t kRsCUISysOptVtable = 0x00B39C98; // primary CWnd vt (ctor)

static bool RsIsCUIMenuWnd(CWnd* pThis) {
    return pThis && *reinterpret_cast<uintptr_t*>(pThis) == kRsCUIMenuVtable;
}

static bool RsIsCUISysOptWnd(CWnd* pThis) {
    if (!pThis) return false;
    const uintptr_t vt = *reinterpret_cast<uintptr_t*>(pThis);
    // Primary B39C98; secondary adjustor path may show B38AFC on some subobjects.
    return vt == kRsCUISysOptVtable || vt == 0x00B38AFC;
}

static bool RsLooksLikeUIMenuSize(int w, int h) {
    // Vanilla CUIMenu CreateDlg: w=100, h=15*(nItem+2). Never 299x396 SysOpt.
    return w == 100 && h >= 45 && h <= 400 && (h % 15) == 0;
}

static bool RsTryPlaceCUIMenuLt(CWnd* pThis, int l, int t, int w, int h, uintptr_t ret, const char* why) {
    if (!pThis || !pThis->m_pLayer || rs_width <= 800) return false;
    const int mw = (w > 0) ? w : 100;
    const int mh = (h > 0) ? h : 90;
    int nl = 0, nt = 0, btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsCalcUIMenuPlace(mw, mh, l, t, nl, nt, &btnL, &btnT, &btnH, &btnOff);
    const bool ok = RsForceLtAbs(pThis, nl, nt);
    const uintptr_t vt = *reinterpret_cast<uintptr_t*>(pThis);
    char buf[320];
    sprintf_s(buf,
        "[RS] CreateWnd CUIMenu LT FIRE ret=%08X vt=%08X why=%s seed=%d,%d place=%d,%d "
        "size=%dx%d btnAbs=%d,%d abs=%d,%d force=%d (avoid CC skew)",
        static_cast<unsigned>(ret), static_cast<unsigned>(vt), why, l, t, nl, nt, mw, mh,
        btnL, btnT, RsWndAbsLeft(pThis), RsWndAbsTop(pThis), ok ? 1 : 0);
    RsLogFlush(buf);
    return true;
}

// CWnd::CreateDlg @0x4EDA94 — reliable gate (PatchCall sites never FIRE'd on click).
// CUIMenu only by vtable (+ optional w==100 size). Never touch SysOpt / other dialogs.
int __fastcall rs_CreateDlg_hook(CWnd* pThis, void* /*edx*/,
    int l, int t, int w, int h, int z, int bScreenCoord, void* pData) {
    if (RsIsCUISysOptWnd(pThis)) {
        return rs_OrigCreateDlg(pThis, l, t, w, h, z, bScreenCoord, pData);
    }
    const bool isMenu = rs_width > 800 && pThis
        && (RsIsCUIMenuWnd(pThis) || RsLooksLikeUIMenuSize(w, h));
    if (!isMenu) {
        return rs_OrigCreateDlg(pThis, l, t, w, h, z, bScreenCoord, pData);
    }

    const int mw = (w > 0) ? w : 100;
    const int mh = (h > 0) ? h : 90;
    int nl = 0, nt = 0, btnL = -1, btnT = -1, btnH = 0, btnOff = -1;
    RsCalcUIMenuPlace(mw, mh, l, t, nl, nt, &btnL, &btnT, &btnH, &btnOff);
    char buf[288];
    sprintf_s(buf,
        "[RS] CreateDlg CUIMenu FIRE seed=%d,%d place=%d,%d size=%dx%d btnAbs=%d,%d "
        "(vanilla AbsTop+30 + real mh; LT after)",
        l, t, nl, nt, mw, mh, btnL, btnT);
    RsLogFlush(buf);

    const int r = rs_OrigCreateDlg(pThis, nl, nt, mw, mh, z, bScreenCoord, pData);
    // CreateWnd_hook may still bind CC — re-assert LT with screen-abs place.
    const bool ok = RsForceLtAbs(pThis, nl, nt);
    sprintf_s(buf,
        "[RS] CreateDlg CUIMenu LT -> %d,%d abs=%d,%d force=%d",
        nl, nt, RsWndAbsLeft(pThis), RsWndAbsTop(pThis), ok ? 1 : 0);
    RsLogFlush(buf);
    return r;
}

static const char* RsStatusBarBtnName(unsigned int id) {
    switch (id) {
    case 1000: return "商城/Shop";
    case 1001: return "MTS/BtMenu";
    case 1002: return "界面/GameMenu";
    case 1005: return "KeySet";
    case 1006: return "QuickSlot";
    case 1007: return "目录/ShortCut";
    case 1009: return "Claim";
    default: return "?";
    }
}

void __fastcall rs_StatusBarBtn_hook(void* pThis, void* /*edx*/, unsigned int a2) {
    char buf[160];
    sprintf_s(buf, "[RS] StatusBar btn FIRE id=%u (%s) screen=%dx%d",
              a2, RsStatusBarBtnName(a2), rs_width, rs_height);
    RsLogFlush(buf);
    rs_OrigStatusBarBtn(pThis, a2);
}

static void RsInstallCreateDlgDetour() {
    if (rs_createDlgDetoured) return;
    if (Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigCreateDlg),
                        CastHook(&rs_CreateDlg_hook))) {
        rs_createDlgDetoured = true;
        RsLogFlush("[RS] CreateDlg Detour attached @0x4EDA94 (CUIMenu LT gate)");
    } else {
        RsLogFlush("[RS] CreateDlg Detour FAILED");
    }
}

static void RsInstallStatusBarBtnDetour() {
    if (rs_statusBarBtnDetoured) return;
    if (Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigStatusBarBtn),
                        CastHook(&rs_StatusBarBtn_hook))) {
        rs_statusBarBtnDetoured = true;
        RsLogFlush("[RS] StatusBar btn Detour attached @0x8D423D (real OnButton)");
    } else {
        RsLogFlush("[RS] StatusBar btn Detour FAILED @0x8D423D");
    }
}

// CWnd::CreateWnd hook — kaentake Origin binding (resolution.cpp CreateWnd_hook).
//
// 目录/界面 are NOT CUIMenu. They are CFadeWnd subclasses:
//   GameMenu 界面 @0x849DE2 → CreateFadeWnd (666,423) size 93×140
//   ShortCut 目录 @0x84A560 → CreateFadeWnd (707,296) size 93×271
// Kaentake binds CreateFadeWnd ret 0x51FA03 → ms_pOrgStatusBar (LB @ abs 0,H-600).
// Vanilla RelMove kept → same Y-from-bottom; only X differs (666 vs 707). No invented Y.
void __fastcall rs_CreateWnd_hook(CWnd* pThis, void* edx, int l, int t, int w, int h, int z, int bScreenCoord, void* pData, int bSetFocus) {
    rs_OrigCreateWnd(pThis, l, t, w, h, z, bScreenCoord, pData, bSetFocus);
    if (!bScreenCoord || !pThis || !pThis->m_pLayer) return;

    const auto ret = reinterpret_cast<uintptr_t>(_ReturnAddress());

    // MiniMap only: always top-left on HD (ignore saved mid/right coords).
    if (ret == kRsMiniMapCreateWndRet && rs_width > 800) {
        const int nx = 8;
        const int ny = 8;
        if (RsForceLtAbs(pThis, nx, ny)) {
            std::cout << "[RS] MiniMap ForceLtAbs " << nx << "," << ny
                      << " screen=" << rs_width << "x" << rs_height
                      << " (args " << l << "," << t << ")" << std::endl;
        }
        return;
    }

    const bool createDlgRet =
        (ret == 0x004EDAB3 || ret == 0x004EDAEB || ret == 0x004EDB9A);
    // Megaphone / cash CUIMenu only — never SysOpt (stack scan removed: false positives).
    if (rs_width > 800 && createDlgRet && !RsIsCUISysOptWnd(pThis)) {
        const bool byVt = RsIsCUIMenuWnd(pThis);
        const bool bySize = RsLooksLikeUIMenuSize(w, h);
        if (byVt || bySize) {
            const char* why = byVt ? "vtable" : "size100x15k";
            if (RsTryPlaceCUIMenuLt(pThis, l, t, w, h, ret, why))
                return;
        }
    }

    // CUIKeyConfig — gold-align (E:\pro\BeiDou-ijl15): only ForceLtAbs CENTER when
    // originActive. When originActive=0 (1280 follow_login), pass-through — never
    // MoveWndAbs (ORIGIN_KEEP/ALWAYS_PROTECT skewed backgrnd vs s_aptKeyPos).
    if (ret == 0x008327D8) {
        const int ww = (w > 0) ? w : 629;
        const int hh = (h > 0) ? h : 373;
        char buf[320];
        if (rs_originActive) {
            const int cx = (rs_width - ww) / 2;
            const int cy = (rs_height - hh) / 2;
            const bool ok = RsForceLtAbs(pThis, cx, cy);
            sprintf_s(buf,
                      "[RS] CUIKeyConfig CreateWnd ForceLtAbs CENTER place=%d,%d "
                      "args=%d,%d abs=%d,%d force=%d size=%dx%d screen=%dx%d "
                      "originActive=1 stamp=KEYCONFIG_BE27_FIX_20260816",
                      cx, cy, l, t, RsWndAbsLeft(pThis), RsWndAbsTop(pThis),
                      ok ? 1 : 0, w, h, rs_width, rs_height);
        } else {
            sprintf_s(buf,
                      "[RS] CUIKeyConfig CreateWnd pass-through (no MoveWndAbs) "
                      "args=%d,%d abs=%d,%d size=%dx%d screen=%dx%d "
                      "originActive=0 stamp=KEYCONFIG_BE27_FIX_20260816",
                      l, t, RsWndAbsLeft(pThis), RsWndAbsTop(pThis),
                      w, h, rs_width, rs_height);
        }
        RsLogFlush(buf);
        return;
    }

    if (!rs_originActive) return;

    switch (ret) {
    case 0x005362BC: case 0x0053638B: case 0x00545D24: case 0x0056042E:
    case 0x00578B30: case 0x00A24D15:
        pThis->m_pLayer->origin = static_cast<IUnknown*>(rs_orgEx[1]); return; // CT: clocks
    case 0x0045A5EF:
        pThis->m_pLayer->origin = static_cast<IUnknown*>(rs_orgEx[2]); return; // RT: megaphone
    case 0x004EDAEB: case 0x004EDB9A: case 0x004EDAB3:
    case 0x007F202C: case 0x00897BD8: case 0x00994CB8:
        // WorldMap 666x524: LT + screen center (codecave already pushes LT coords).
        // Never CC here — and never call CWnd::GetAbsLeft virtual (unsafe / can hang).
        if (w == 666 && h == 524) {
            const int cx = (rs_width - w) / 2;
            const int cy = (rs_height - h) / 2;
            if (rs_orgEx[0])
                pThis->m_pLayer->origin = static_cast<IUnknown*>(rs_orgEx[0]); // LT
            const bool ok = RsForceLtAbs(pThis, cx, cy);
            char buf[192];
            sprintf_s(buf,
                "[RS] WorldMap CreateWnd LT center %d,%d args=%d,%d abs=%d,%d force=%d screen=%dx%d",
                cx, cy, l, t, RsWndAbsLeft(pThis), RsWndAbsTop(pThis), ok ? 1 : 0,
                rs_width, rs_height);
            RsLogFlush(buf);
            return;
        }
        // Other CreateDlg / centered dialogs → CC (CUIMenu already returned above).
        pThis->m_pLayer->origin = static_cast<IUnknown*>(rs_orgEx[4]); // CC: dialogs
        return;
    // Kaentake: CFadeWnd::CreateFadeWnd + CUIStatusBar → ms_pOrgStatusBar.
    // Covers GameMenu(界面)/ShortCut(目录) and other FadeWnd popups.
    // Do NOT add (H-600) to RelMove — org already embeds it.
    case 0x0051FA03:
    case 0x008CFD65: {
        if (rs_orgStatusBar) {
            pThis->m_pLayer->origin = static_cast<IUnknown*>(rs_orgStatusBar);
        }
        if (ret == 0x0051FA03) {
            const uintptr_t vt = *reinterpret_cast<uintptr_t*>(pThis);
            const bool isGameMenu = (vt == kRsGameMenuVtable) || (w == 93 && h == 140);
            const bool isShortCut = (vt == kRsShortCutVtable) || (w == 93 && h == 271);
            int placeL = l, placeT = t;
            const char* tag = "FadeWnd";
            if (isGameMenu) {
                placeL = 666;
                placeT = 423;
                tag = "GameMenu/界面";
            } else if (isShortCut) {
                placeL = 707;
                placeT = 296;
                tag = "ShortCut/目录";
            }
            // Force vanilla RelMove after origin bind (heal UpdateResolution H-177/H-281).
            if ((isGameMenu || isShortCut) && pThis->m_pLayer) {
                pThis->m_pLayer->RelMove(placeL, placeT);
            }
            char buf[320];
            sprintf_s(buf,
                "[RS] %s CreateWnd FIRE kaentake StatusBar-origin "
                "rel=%d,%d seed=%d,%d size=%dx%d abs=%d,%d screen=%dx%d "
                "(vanilla GameMenu 666,423 / ShortCut 707,296)",
                tag, placeL, placeT, l, t, w, h,
                RsWndAbsLeft(pThis), RsWndAbsTop(pThis), rs_width, rs_height);
            RsLogFlush(buf);
        }
        return;
    }
    // CUIEquip CreateWnd — leave natural GetUIWndPos (UIType=1). No ForceLtAbs.
    }
}

// ===== Attach =====
static bool rs_done = false;

// Gr2D_DX8 AdjustCenterY letterbox
static uintptr_t rs_adj_jmp = 0;
static uintptr_t rs_adj_ret = 0;
__declspec(naked) void rs_adj_center_hook() {
    __asm {
        pushfd
        sub ecx, rs_adjust_cy
        mov [ebp - 0x14], ecx
        lea edx, [esi + 0xC4]
        popfd
        jmp [rs_adj_ret]
    }
}

// Origin + RS layout hooks — ONLY after successful ScreenResolution to a non-login size.
// Installing these while still on UpdateResolution login dims misplaces UI without
// changing the window (StatusBar width→1920 was the main offender).
static void rs_installLayoutHooks() {
    if (rs_layoutHooksInstalled) return;
    rs_layoutHooksInstalled = true;
    std::cout << "[RS] installing Origin/layout hooks for " << rs_width << "x" << rs_height << std::endl;

    // Undo UpdateResolution flush-bottom StatusBar/QuickSlot patches — Origin owns layout.
    RsApplyOriginStatusBarMetrics();

    if (!rs_originsReady) {
        rs_originsReady = true;
        for (int i = 0; i < 9; ++i)
            PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgEx[i], nullptr);
        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgStatusBar, nullptr);
        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgScreenMsg, nullptr);
        PcCreateObject<IWzVector2DPtr>(L"Shape2D#Vector2D", rs_orgQuickSlot, nullptr);
    }
    RsResetOrgWindow();

    Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigCtor), CastHook(&rs_Ctor_hook));
    Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigDtor), CastHook(&rs_Dtor_hook));
    Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigGetOrg), CastHook(&rs_GetOrg_hook));
    Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigCreateWnd), CastHook(&rs_CreateWnd_hook));
    Memory::SetHook(true, reinterpret_cast<void**>(&rs_OrigOnMoveWnd), CastHook(&rs_OnMoveWnd_hook));

    // StatusBar may already exist under LT/UpdateResolution — recreate under Origin.
    RsReanchorExistingHud();

    ATTACH_HOOK(RsInput::SetCursorVectorPos, RsInput::SetCursorVectorPos_hook);
    ATTACH_HOOK(RsInput::SetCursorPos, RsInput::SetCursorPos_hook);
    ATTACH_HOOK(CUIToolTip::MakeLayer, CUIToolTip::MakeLayer_hook);
    Patch1(0x008EBC3C, 0xA1);
    Patch4(0x008EBC3C + 1, reinterpret_cast<uintptr_t>(&rs_width));
    Patch1(0x008EBC58, 0xA1);
    Patch4(0x008EBC58 + 1, reinterpret_cast<uintptr_t>(&rs_height));

    ATTACH_HOOK(CTemporaryStatView::AdjustPosition, CTemporaryStatView::AdjustPosition_hook);
    ATTACH_HOOK(CTemporaryStatView::ShowToolTip, CTemporaryStatView::ShowToolTip_hook);
    ATTACH_HOOK(CTemporaryStatView::FindIcon, CTemporaryStatView::FindIcon_hook);
    ATTACH_HOOK(CUIStatusBar::GetShortCutIndexByPos, CUIStatusBar::GetShortCutIndexByPos_hook);
    ATTACH_HOOK(CMapLoadable::RestoreViewRange, CMapLoadable::RestoreViewRange_hook);
    PatchJmp(rs_grid_jmp, &rs_grid_hook);

    // ScreenMsg RelMove (pickup/exp msgs) — kaentake LayoutScrMsg / MoveScrMsg
    PatchCall(0x0089B6FE, &rs_ScreenMsg_raw_RelMove_hook, 6);
    PatchCall(0x0089BA13, &rs_ScreenMsg_RelMove_hook);

    // Boss HP bar nLeft vs minimap at 800 (does not touch BossHP % draw hook)
    PatchCall(0x00533705, &rs_ShowMobHPTag_nLeft_hook, 15);

    // CUIEquip::IsMyAddon — fix equip window stutter when dragging (kaentake)
    Patch4(0x007FDF30 + 2, 0x5B4); // offsetof(CUIEquip, m_pUIPetEquip)

    // Cursor menus: bypass CreateDlg→CC (kaentake ContextMenu + BeiDou UIMenu/popup).
    ATTACH_HOOK(CUtilDlgEx::CreateUtilDlgEx, CUtilDlgEx::CreateUtilDlgEx_hook);
    RsInstallCursorDlgPatches();

    RsApplyFieldEffectMetrics();

    rs_adj_jmp = reinterpret_cast<uintptr_t>(FindPattern("GR2D_DX8.DLL", "8D 96 C4 00 00 00"));
    if (rs_adj_jmp) {
        rs_adj_ret = rs_adj_jmp + 6;
        PatchJmp(rs_adj_jmp, &rs_adj_center_hook);
    } else {
        std::cout << "[RS] AdjustCenterY pattern not found" << std::endl;
    }
}

void rs_attach() {
    if (rs_done) return;
    rs_done = true;

    // Backbuffer capacity only — do NOT widen StatusBar here (that broke login layout).
    Patch4(0x009F7078 + 1, RS_SCREEN_HEIGHT_MAX);
    Patch4(0x009F707D + 1, RS_SCREEN_WIDTH_MAX);

    ATTACH_HOOK(s_set_stage, rs_set_stage_hook);
    ATTACH_HOOK(CConfig::LoadGlobal, CConfig::LoadGlobal_hook);
    ATTACH_HOOK(CConfig::SaveGlobal, CConfig::SaveGlobal_hook);
    ATTACH_HOOK(CConfig::ApplySysOpt, CConfig::ApplySysOpt_hook);
    ATTACH_HOOK(CConfig::GetUIWndPos, CConfig::GetUIWndPos_hook);
    ATTACH_HOOK(CConfig::LoadCharacter, CConfig::LoadCharacter_hook);

    ATTACH_HOOK(CUISysOpt::OnCreate, CUISysOpt::OnCreate_hook);
    ATTACH_HOOK(CUISysOpt::Destructor, CUISysOpt::Destructor_hook);
    ATTACH_HOOK(CWvsApp::CallUpdate, CWvsApp::CallUpdate_hook);
    // Gold / kaentake: OK/Cancel Y=372 under combo @338.
    Patch4(0x009945BC + 1, 372);

    // Equip IsMyAddon — safe at boot (does not depend on Origin)
    Patch4(0x007FDF30 + 2, 0x5B4);

    char boot[192];
    sprintf_s(boot, "[RS] attached tier=%d (%dx%d) follow_login=%d (layout/Origin deferred until successful HD switch)",
              rs_tier, rs_width, rs_height, rs_field_follow_login ? 1 : 0);
    RsLogFlush(boot);
}

// ===== ModRegistry auto-registration =====
// DISABLED: static init order issues. Called manually from LazyCompatInit instead.
// namespace { ... } g_rsAutoRegister;
void rs_register() {
    static bool done = false;
    if (done) return; done = true;
    CompatModule m{};
    m.name = "Resolution";
    m.onAttach = []() { rs_attach(); };
    ModRegistry::RegisterModule(std::move(m));
}
