// ============================================================
// sidetoolbar.cpp — 4-category sidebar with horizontal sub-icons.
//
// Layout: GalaxyStar hub + 4 vertical categories; click category
// to expand sub-feature icons horizontally. Hover shows GBK tips.
// Icons: UI/MainNotice.img/{Prefix}/{Default|Appear}/{frame}
// ============================================================

#include "stdafx.h"
#include "SideToolbarApi.h"
#include "sidetoolbar_config.h"
#include "Client.h"
#include "beautyshop/BeautyShopApi.h"
#include "dailycheckin/DailyCheckinApi.h"
#include "storagebag/StorageBagApi.h"
#include "damagerank/uiDamageRank.h"
#include "partybuffs/PartyBuffsApi.h"
#include "cashshop/CashShopApi.h"
#include "compat/ClientAddresses.h"
#include "compat/rs/rs.h"
#include "wvs/packet_legacy.h"
#include "wvs/wnd.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace {

static constexpr uintptr_t kAddr_play_ui_sound = 0x00989588;
static constexpr uintptr_t kAddr_get_basic_font = 0x0098A707;
static constexpr uintptr_t kAddr_SetFont = 0x0046341A;
static constexpr uintptr_t kAddr_ProcessBasicUIKey = 0x00A07431;
static constexpr uintptr_t kAddr_CWvsContext_Instance = 0x00BE7918;

static auto play_ui_sound = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto get_basic_font = reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

// Slot = hit-rect only. MainNotice Default frames are ~32–47px — keep slots
// large enough so 1:1 blit is never clipped / never scaled to fill.
static constexpr int kHubW = 48;
static constexpr int kHubH = 48;
static constexpr int kCatW = 48;
static constexpr int kCatH = 48;
static constexpr int kSubW = 48;
static constexpr int kSubH = 48;
static constexpr int kPad = 4;
static constexpr int kLeftPad = 6;
static constexpr int kTopPad = 4;
static constexpr int kAnimIntervalMs = 250;
static constexpr int kMaxAnimFrames = 12;
static constexpr int kTipDelayMs = 300;
static constexpr int kLongPressMs = 800;
// Canvas room to the right of icons so tip text is not clipped by the CWnd layer.
static constexpr int kTipReserveW = 280;
static constexpr int kTipPadX = 2;
static constexpr int kTipLineH = 16;
static constexpr unsigned short kOpcode_SidebarTool = 0xC6;

static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

// Runtime overrides from server SIDEBAR_CONFIG_SYNC (0x3733). Default: all visible.
static bool g_toolVisible[SidebarConfig::kServerToolCount];
static bool g_tipOverride[SidebarConfig::kServerToolCount];
static char g_tipTitle[SidebarConfig::kServerToolCount][96];
static char g_tipDesc[SidebarConfig::kServerToolCount][192];
static bool g_runtimeInited = false;

static void EnsureRuntimeDefaults() {
    if (g_runtimeInited) {
        return;
    }
    for (int i = 0; i < SidebarConfig::kServerToolCount; ++i) {
        g_toolVisible[i] = true;
        g_tipOverride[i] = false;
        g_tipTitle[i][0] = '\0';
        g_tipDesc[i][0] = '\0';
    }
    g_runtimeInited = true;
}

static bool IsServerToolVisible(int toolIndex) {
    EnsureRuntimeDefaults();
    if (toolIndex < 0 || toolIndex >= SidebarConfig::kServerToolCount) {
        return false;
    }
    return g_toolVisible[toolIndex];
}

static bool IsSubVisible(const SidebarConfig::SubFeatureDef& sub) {
    if (sub.actionKind == SidebarConfig::kActionClientUi) {
        return true;
    }
    return IsServerToolVisible(sub.actionId);
}

static SidebarConfig::TipText ResolveSubTip(const SidebarConfig::SubFeatureDef& sub) {
    if (sub.actionKind == SidebarConfig::kActionServerTool &&
        sub.actionId >= 0 &&
        sub.actionId < SidebarConfig::kServerToolCount) {
        EnsureRuntimeDefaults();
        if (g_tipOverride[sub.actionId] && g_tipTitle[sub.actionId][0]) {
            return SidebarConfig::TipText{g_tipTitle[sub.actionId], g_tipDesc[sub.actionId]};
        }
    }
    return sub.tip;
}

static int CountVisibleSubs(int catIndex) {
    if (catIndex < 0 || catIndex >= SidebarConfig::kCategoryCount) {
        return 0;
    }
    const auto& cat = SidebarConfig::kCategories[catIndex];
    int n = 0;
    for (int i = 0; i < cat.subCount; ++i) {
        if (IsSubVisible(cat.subs[i])) {
            ++n;
        }
    }
    return n;
}

static int LogicalSubFromVisible(int catIndex, int visibleIndex) {
    if (catIndex < 0 || catIndex >= SidebarConfig::kCategoryCount || visibleIndex < 0) {
        return -1;
    }
    const auto& cat = SidebarConfig::kCategories[catIndex];
    int seen = 0;
    for (int i = 0; i < cat.subCount; ++i) {
        if (!IsSubVisible(cat.subs[i])) {
            continue;
        }
        if (seen == visibleIndex) {
            return i;
        }
        ++seen;
    }
    return -1;
}

enum HoverKind : int {
    kHoverNone = 0,
    kHoverHub,
    kHoverCategory,
    kHoverSub,
};

struct HoverTarget {
    HoverKind kind = kHoverNone;
    int catIndex = -1;
    int subIndex = -1;
};

struct IconAnim {
    IWzCanvasPtr frames[kMaxAnimFrames];
    int frameCount = 0;
    int currentFrame = 0;
    int direction = 1;
    DWORD lastTick = 0;
    bool loaded = false;
};

static void SendSidebarTool(int toolIndex) {
    if (!IsServerToolVisible(toolIndex)) {
        return;
    }
    if (toolIndex < 0 || toolIndex >= SidebarConfig::kServerToolCount) {
        return;
    }
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (!sock) {
        return;
    }
    COutPacket o(kOpcode_SidebarTool);
    o.Encode1(static_cast<unsigned char>(toolIndex));
    ClientSocket_SendPacket(sock, o);
}

static IWzCanvasPtr LoadSpritePath(const wchar_t* path) {
    IWzCanvasPtr canvas;
    if (!path || !*path || !get_rm()) {
        return canvas;
    }
    try {
        canvas = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(path)));
    } catch (...) {
    }
    return canvas;
}

// Only small quest/notice icons — never full window backdrops (those blow past the hit-rect).
static const wchar_t* FallbackPathForPrefix(const wchar_t* prefix) {
    if (!prefix) {
        return L"UI/UIWindow.img/Quest/icon3/6";
    }
    if (wcscmp(prefix, L"GalaxyStar") == 0) return L"UI/UIWindow.img/Quest/icon3/6";
    if (wcscmp(prefix, L"Daily") == 0) return L"UI/UIWindow.img/Quest/icon0/0";
    if (wcscmp(prefix, L"attendance") == 0) return L"UI/UIWindow.img/Quest/icon8/0";
    if (wcscmp(prefix, L"dailyGift") == 0) return L"UI/UIWindow.img/Quest/icon7/0";
    if (wcscmp(prefix, L"mapleHelper") == 0) return L"UI/UIWindow.img/Quest/icon3/6";
    if (wcscmp(prefix, L"mapleStyle") == 0) return L"UI/UIWindow.img/Quest/icon2/0";
    if (wcscmp(prefix, L"itemCollection") == 0) return L"UI/UIWindow.img/Bag/BtOreBag/normal/0";
    if (wcscmp(prefix, L"Event") == 0) return L"UI/UIWindow.img/Quest/icon6/0";
    if (wcscmp(prefix, L"mapleAlarm") == 0) return L"UI/UIWindow.img/Quest/icon1/0";
    if (wcscmp(prefix, L"Content") == 0) return L"UI/UIWindow.img/Quest/icon4/0";
    if (wcscmp(prefix, L"advRemaster") == 0) return L"UI/UIWindow.img/Quest/icon5/0";
    if (wcscmp(prefix, L"collection") == 0) return L"UI/UIWindow.img/Quest/icon5/0";
    if (wcscmp(prefix, L"Achieve") == 0) return L"UI/UIWindow.img/UserInfo/bossPetCrown";
    if (wcscmp(prefix, L"EventList") == 0) return L"UI/UIWindow.img/Quest/icon3/6";
    if (wcscmp(prefix, L"Absolute") == 0) return L"UI/UIWindow.img/Quest/icon9/0";
    return L"UI/UIWindow.img/Quest/icon3/6";
}

static void LoadIconAnim(IconAnim& anim, const wchar_t* prefix, const wchar_t* state) {
    if (anim.loaded) {
        return;
    }
    anim.loaded = true;
    anim.frameCount = 0;

    wchar_t path[160];
    for (int i = 0; i < kMaxAnimFrames; ++i) {
        swprintf_s(path, L"UI/MainNotice.img/%s/%s/%d", prefix, state, i);
        anim.frames[i] = LoadSpritePath(path);
        if (anim.frames[i]) {
            anim.frameCount = i + 1;
        } else if (i == 0) {
            anim.frames[0] = LoadSpritePath(FallbackPathForPrefix(prefix));
            if (anim.frames[0]) {
                anim.frameCount = 1;
            }
            break;
        } else {
            break;
        }
    }
    anim.lastTick = GetTickCount();
}

static IWzCanvasPtr CurrentIconFrame(IconAnim& anim, const wchar_t* prefix) {
    LoadIconAnim(anim, prefix, L"Default");
    if (anim.frameCount <= 0) {
        return nullptr;
    }
    if (anim.frameCount == 1) {
        return anim.frames[0];
    }
    const int idx = anim.currentFrame;
    return anim.frames[(idx >= 0 && idx < anim.frameCount) ? idx : 0];
}

static void AdvanceIconAnim(IconAnim& anim) {
    if (anim.frameCount <= 1) {
        return;
    }
    const DWORD now = GetTickCount();
    if (anim.lastTick == 0) {
        anim.lastTick = now;
        return;
    }
    if (now - anim.lastTick < static_cast<DWORD>(kAnimIntervalMs)) {
        return;
    }
    anim.lastTick = now;
    anim.currentFrame += anim.direction;
    if (anim.currentFrame >= anim.frameCount - 1) {
        anim.currentFrame = anim.frameCount - 1;
        anim.direction = -1;
    } else if (anim.currentFrame <= 0) {
        anim.currentFrame = 0;
        anim.direction = 1;
    }
}

static bool IsClientUiOpen(SidebarConfig::ClientUiId id) {
    switch (id) {
    case SidebarConfig::kUiCheckin:
        return DailyCheckin_IsOpen();
    case SidebarConfig::kUiBeauty:
        return BeautyShop_IsOpen();
    case SidebarConfig::kUiStorageBag:
        return BagWindow_IsOpen();
    case SidebarConfig::kUiCashShop:
        return CashShopWnd_IsOpen();
    case SidebarConfig::kUiPartyTracker:
        return PartyBuffs_IsTrackerVisible();
    case SidebarConfig::kUiDamageRank:
        return CUIDamageRank::IsPanelOpen();
    default:
        return false;
    }
}

static void CloseClientUi(SidebarConfig::ClientUiId id) {
    switch (id) {
    case SidebarConfig::kUiCheckin:
        DailyCheckin_Close();
        break;
    case SidebarConfig::kUiBeauty:
        BeautyShop_CloseWindow();
        break;
    case SidebarConfig::kUiStorageBag:
        BagWindow_Close();
        break;
    case SidebarConfig::kUiCashShop:
        CashShopWnd_Close();
        break;
    case SidebarConfig::kUiPartyTracker:
        PartyBuffs_SetTrackerVisible(false);
        break;
    case SidebarConfig::kUiDamageRank:
        CUIDamageRank::ClosePanel();
        break;
    default:
        break;
    }
}

static void OpenClientUi(SidebarConfig::ClientUiId id) {
    switch (id) {
    case SidebarConfig::kUiCheckin:
        DailyCheckin_RequestOpen();
        break;
    case SidebarConfig::kUiBeauty:
        BeautyShop_OpenWindow();
        break;
    case SidebarConfig::kUiStorageBag:
        BagWindow_Toggle();
        break;
    case SidebarConfig::kUiCashShop:
        CashShopWnd_RequestOpen();
        break;
    case SidebarConfig::kUiPartyTracker:
        PartyBuffs_SetTrackerVisible(true);
        break;
    case SidebarConfig::kUiDamageRank:
        CUIDamageRank::ToggleBySidebar();
        break;
    default:
        break;
    }
}

static bool IsSubOpen(const SidebarConfig::SubFeatureDef& sub) {
    if (sub.actionKind == SidebarConfig::kActionClientUi) {
        return IsClientUiOpen(static_cast<SidebarConfig::ClientUiId>(sub.actionId));
    }
    return false;
}

static void InvokeSub(const SidebarConfig::SubFeatureDef& sub) {
    if (sub.actionKind == SidebarConfig::kActionClientUi) {
        const auto id = static_cast<SidebarConfig::ClientUiId>(sub.actionId);
        if (IsClientUiOpen(id)) {
            CloseClientUi(id);
        } else {
            OpenClientUi(id);
        }
        return;
    }
    SendSidebarTool(sub.actionId);
}

// Blit at original pixel size — never scale/stretch to the slot.
// CopyEx dstW/H must equal srcW/H (invresize rule) or WZ scales.
// If art is larger than the hit-rect, clip to the rect (still 1:1 pixels, no upsample).
static void BlitCentered(IWzCanvasPtr dst, IWzCanvasPtr src, const RECT& rc) {
    if (!dst || !src) {
        return;
    }
    int sw = 0;
    int sh = 0;
    try {
        sw = src->width;
        sh = src->height;
    } catch (...) {
    }
    if (sw <= 0 || sh <= 0) {
        return;
    }
    const int dw = rc.right - rc.left;
    const int dh = rc.bottom - rc.top;
    int copyW = sw;
    int copyH = sh;
    int srcX = 0;
    int srcY = 0;
    if (copyW > dw) {
        srcX = (copyW - dw) / 2;
        copyW = dw;
    }
    if (copyH > dh) {
        srcY = (copyH - dh) / 2;
        copyH = dh;
    }
    const int dx = rc.left + (dw - copyW) / 2;
    const int dy = rc.top + (dh - copyH) / 2;
    try {
        dst->CopyEx(dx, dy, src, CANVAS_ALPHATYPE::CA_OVERWRITE, copyW, copyH, srcX, srcY, copyW, copyH);
    } catch (...) {
    }
}

static void FillRect(IWzCanvasPtr canvas, const RECT& rc, unsigned int color) {
    if (!canvas) {
        return;
    }
    try {
        canvas->DrawRectangle(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, color);
    } catch (...) {
    }
}

class CUISideToolbar : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUISideToolbar* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    bool m_barExpanded = false;
    int m_openCategory = -1;
    int m_hoverCat = -1;
    int m_hoverSub = -1;
    bool m_hubHover = false;
    HoverTarget m_tipTarget{};
    HoverTarget m_tipShown{};
    DWORD m_tipHoverStart = 0;
    DWORD m_lastClickTick = 0;
    DWORD m_catPressTick = 0;
    int m_catPressIndex = -1;
    bool m_longPressGmSent = false;

    IconAnim m_hubAnim{};
    IconAnim m_catAnim[SidebarConfig::kCategoryCount]{};
    IconAnim m_subAnim[SidebarConfig::kCategoryCount][8]{};

    IWzFontPtr m_font;
    IWzFontPtr m_fontTitle;
    IWzFontPtr m_fontShadow;

    CUISideToolbar();
    virtual ~CUISideToolbar() override {
        if (ms_pInstance == this) {
            ms_pInstance = nullptr;
        }
    }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int OnMouseMove(int rx, int ry) override;
    virtual void OnMouseEnter(int bEnter) override;
    virtual void OnDestroy() override;
    virtual void Update() override;
    virtual int HitTest(int rx, int ry, CCtrlWnd** ppCtrl) override;
    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }
    virtual int OnSetFocus(int /*bFocus*/) override { return 0; }
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        void* ctx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
        if (ctx) {
            reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
        }
    }

    int SubCountForOpenCategory() const {
        return CountVisibleSubs(m_openCategory);
    }

    // Fixed max canvas size — NEVER resize CWnd layer on expand/collapse.
    // Changing m_pLayer width/height stretches the existing canvas ("zoom"),
    // and clips category icons that were drawn past the old canvas bounds.
    static int MaxSubCount() {
        int maxSubs = 0;
        for (int i = 0; i < SidebarConfig::kCategoryCount; ++i) {
            if (SidebarConfig::kCategories[i].subCount > maxSubs) {
                maxSubs = SidebarConfig::kCategories[i].subCount;
            }
        }
        return maxSubs;
    }

    static int WndW() {
        // hub/cat column + widest subcategory row + tip text reserve (no clip)
        const int subs = MaxSubCount();
        int w = kLeftPad + kCatW + kPad;
        if (subs > 0) {
            w += subs * (kSubW + kPad) + kPad;
        }
        w += kTipReserveW;
        return w + kLeftPad;
    }

    static int MeasureTipLineWidth(IWzFontPtr font, const char* gbk) {
        if (!gbk || !gbk[0]) {
            return 0;
        }
        if (font) {
            try {
                return static_cast<int>(font->CalcTextWidth(Ztl_bstr_t(gbk), Ztl_variant_t()));
            } catch (...) {
            }
        }
        // Fallback: ~6px ASCII / ~12px CJK (GBK) — better than a fixed narrow box.
        int w = 0;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(gbk); *p; ++p) {
            if (*p >= 0x80 && *(p + 1)) {
                w += 12;
                ++p;
            } else {
                w += 6;
            }
        }
        return w;
    }

    static int WndH() {
        const int catBlock = SidebarConfig::kCategoryCount * (kCatH + kPad);
        const int hubBlock = kHubH + kPad;
        return kTopPad + hubBlock + catBlock + kTopPad;
    }

    int HubY() const { return kTopPad; }
    int CatStartY() const { return kTopPad + kHubH + kPad; }

    RECT HubRect() const {
        const int x = kLeftPad;
        const int y = HubY();
        return {x, y, x + kHubW, y + kHubH};
    }

    RECT CategoryRect(int catIndex) const {
        const int x = kLeftPad;
        const int y = CatStartY() + catIndex * (kCatH + kPad);
        return {x, y, x + kCatW, y + kCatH};
    }

    RECT SubRect(int subIndex) const {
        const int catY = CatStartY() + m_openCategory * (kCatH + kPad);
        const int x = kLeftPad + kCatW + kPad + subIndex * (kSubW + kPad);
        return {x, catY, x + kSubW, catY + kSubH};
    }

    int HitCategory(int rx, int ry) const {
        if (!m_barExpanded) {
            return -1;
        }
        POINT pt{rx, ry};
        for (int i = 0; i < SidebarConfig::kCategoryCount; ++i) {
            RECT rc = CategoryRect(i);
            if (PtInRect(&rc, pt)) {
                return i;
            }
        }
        return -1;
    }

    int HitSub(int rx, int ry) const {
        if (!m_barExpanded || m_openCategory < 0) {
            return -1;
        }
        POINT pt{rx, ry};
        const int count = SubCountForOpenCategory();
        for (int i = 0; i < count; ++i) {
            RECT rc = SubRect(i);
            if (PtInRect(&rc, pt)) {
                return i;
            }
        }
        return -1;
    }

    HoverTarget ResolveHover(int rx, int ry) const {
        HoverTarget t;
        POINT pt{rx, ry};
        RECT hubRc = HubRect();
        if (PtInRect(&hubRc, pt)) {
            t.kind = kHoverHub;
            return t;
        }
        const int cat = HitCategory(rx, ry);
        if (cat >= 0) {
            t.kind = kHoverCategory;
            t.catIndex = cat;
            return t;
        }
        const int sub = HitSub(rx, ry);
        if (sub >= 0) {
            t.kind = kHoverSub;
            t.catIndex = m_openCategory;
            t.subIndex = sub;
        }
        return t;
    }

    void LoadFonts() {
        m_font = nullptr;
        m_fontTitle = nullptr;
        m_fontShadow = nullptr;
        try {
            get_basic_font(std::addressof(m_font), 0);
        } catch (...) {
        }
        try {
            PcCreateObject<IWzFontPtr>(L"Canvas#Font", m_fontTitle, nullptr);
            if (m_fontTitle) {
                auto fn = reinterpret_cast<HRESULT(__thiscall*)(
                    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kAddr_SetFont);
                fn(m_fontTitle, L"Dotum", 12, 0xFFFFD070, Ztl_variant_t(L""));
            }
        } catch (...) {
        }
        try {
            PcCreateObject<IWzFontPtr>(L"Canvas#Font", m_fontShadow, nullptr);
            if (m_fontShadow) {
                auto fn = reinterpret_cast<HRESULT(__thiscall*)(
                    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kAddr_SetFont);
                fn(m_fontShadow, L"Dotum", 11, 0xFF000000, Ztl_variant_t(L""));
            }
        } catch (...) {
        }
    }

    bool AcceptClick() {
        const DWORD now = GetTickCount();
        if (m_lastClickTick != 0 && (now - m_lastClickTick) < 180) {
            return false;
        }
        m_lastClickTick = now;
        return true;
    }

    void ToggleBar() {
        m_barExpanded = !m_barExpanded;
        if (!m_barExpanded) {
            m_openCategory = -1;
        }
        m_hoverCat = -1;
        m_hoverSub = -1;
        m_hubHover = false;
        m_tipTarget = {};
        m_tipShown = {};
        m_tipHoverStart = 0;
        try {
            play_ui_sound(m_barExpanded ? L"MenuUp" : L"MenuDown");
        } catch (...) {
        }
        InvalidateRect(nullptr);
    }

    void ToggleCategory(int catIndex) {
        if (catIndex < 0 || catIndex >= SidebarConfig::kCategoryCount) {
            return;
        }
        if (m_openCategory == catIndex) {
            m_openCategory = -1;
        } else {
            m_openCategory = catIndex;
        }
        m_hoverSub = -1;
        m_tipTarget = {};
        m_tipShown = {};
        m_tipHoverStart = 0;
        try {
            play_ui_sound(L"BtMouseClick");
        } catch (...) {
        }
        InvalidateRect(nullptr);
    }

    void DrawIconSlot(IWzCanvasPtr canvas, const RECT& rc, IconAnim& anim, const wchar_t* prefix,
                      bool /*hover*/, bool /*selected*/) {
        // No slot chrome — icons render at original art size (doc: no border / no scale-up).
        IWzCanvasPtr icon = CurrentIconFrame(anim, prefix);
        BlitCentered(canvas, icon, rc);
    }

    void DrawOutlinedText(IWzCanvasPtr canvas, int x, int y, Ztl_bstr_t& text, IWzFontPtr font,
                          IWzFontPtr shadow) {
        if (!canvas || !font) {
            return;
        }
        // 8-neighbor outline (no fill / no border box) for readability on game bg.
        if (shadow) {
            static constexpr int kOx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
            static constexpr int kOy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
            for (int i = 0; i < 8; ++i) {
                try {
                    canvas->DrawTextA(x + kOx[i], y + kOy[i], text, shadow);
                } catch (...) {
                }
            }
        }
        try {
            canvas->DrawTextA(x, y, text, font);
        } catch (...) {
        }
    }

    void DrawTip(IWzCanvasPtr canvas, const SidebarConfig::TipText& tip, int anchorX, int anchorY) {
        if (!tip.title || !tip.desc) {
            return;
        }
        const int titleW = MeasureTipLineWidth(m_fontTitle ? m_fontTitle : m_font, tip.title);
        const int descW = MeasureTipLineWidth(m_font, tip.desc);
        const int tipW = (std::max)(titleW, descW) + kTipPadX * 2;
        const int tipH = kTipLineH * 2 + 6;
        int tx = anchorX + 8;
        int ty = anchorY - tipH / 2;
        if (ty < 0) {
            ty = 0;
        }
        const int maxBottom = WndH() - tipH;
        if (ty > maxBottom) {
            ty = (std::max)(0, maxBottom);
        }
        // Keep tip inside canvas so long lines are never clipped by the layer.
        const int maxRight = WndW() - 2;
        if (tx + tipW > maxRight) {
            tx = maxRight - tipW;
        }
        if (tx < 0) {
            tx = 0;
        }
        // Text-only tip: no background fill, no gold border frame.
        try {
            Ztl_bstr_t title(tip.title);
            Ztl_bstr_t desc(tip.desc);
            const int textX = tx + kTipPadX;
            DrawOutlinedText(canvas, textX, ty + 2, title, m_fontTitle ? m_fontTitle : m_font,
                             m_fontShadow);
            DrawOutlinedText(canvas, textX, ty + 2 + kTipLineH + 2, desc, m_font, m_fontShadow);
        } catch (...) {
        }
    }

    void UpdateTipState(int rx, int ry) {
        const HoverTarget next = ResolveHover(rx, ry);
        if (next.kind != m_tipTarget.kind ||
            next.catIndex != m_tipTarget.catIndex ||
            next.subIndex != m_tipTarget.subIndex) {
            m_tipTarget = next;
            m_tipHoverStart = (next.kind == kHoverNone) ? 0 : GetTickCount();
            m_tipShown = {};
        }
        if (m_tipTarget.kind != kHoverNone && m_tipHoverStart != 0) {
            const DWORD elapsed = GetTickCount() - m_tipHoverStart;
            if (elapsed >= static_cast<DWORD>(kTipDelayMs)) {
                m_tipShown = m_tipTarget;
            }
        }
    }

    void CheckLongPress() {
        if (m_catPressIndex < 0 || m_longPressGmSent || !m_barExpanded) {
            return;
        }
        if (m_catPressIndex != 3) {
            return;
        }
        const DWORD now = GetTickCount();
        if (m_catPressTick == 0 || (now - m_catPressTick) < static_cast<DWORD>(kLongPressMs)) {
            return;
        }
        m_longPressGmSent = true;
        if (IsServerToolVisible(SidebarConfig::kToolGm)) {
            SendSidebarTool(SidebarConfig::kToolGm);
        }
        try {
            play_ui_sound(L"BtMouseClick");
        } catch (...) {
        }
    }

};

CUISideToolbar::CUISideToolbar() {
    LoadFonts();
    // Four categories always visible on enter; GalaxyStar still toggles collapse.
    m_barExpanded = true;
    m_openCategory = -1;

    const int w = WndW();
    const int h = WndH();
    const int screenH = get_screen_height();
    const int left = 6;
    const int top = (std::max)(0, (screenH - h) / 2);
    CreateWnd(left, top, w, h, 12, 1, nullptr, 0);
    (void)rs_force_wnd_lt_abs(this, left, top);
    ms_pInstance = this;
}

void CUISideToolbar::OnDestroy() {
    m_font = nullptr;
    m_fontTitle = nullptr;
    m_fontShadow = nullptr;
    if (ms_pInstance == this) {
        ms_pInstance = nullptr;
    }
    CWnd::OnDestroy();
}

void CUISideToolbar::Update() {
    AdvanceIconAnim(m_hubAnim);
    for (int i = 0; i < SidebarConfig::kCategoryCount; ++i) {
        AdvanceIconAnim(m_catAnim[i]);
        const int subCount = SidebarConfig::kCategories[i].subCount;
        for (int j = 0; j < subCount; ++j) {
            AdvanceIconAnim(m_subAnim[i][j]);
        }
    }
    CheckLongPress();
    InvalidateRect(nullptr);
}

void CUISideToolbar::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr canvas = GetCanvas();
    if (!canvas) {
        return;
    }

    RECT clearRc{0, 0, WndW(), WndH()};
    FillRect(canvas, clearRc, 0x00FFFFFF);

    const SidebarConfig::TipText hubTip = {"\xB1\xB1\xB6\xB7\xD6\xFA\xCA\xD6", "\xB5\xE3\xBB\xF7\xD5\xB9\xBF\xAA\x2F\xCA\xD5\xC6\xF0\xB2\xE0\xB1\xDF\xC0\xB8"};

    DrawIconSlot(canvas, HubRect(), m_hubAnim, SidebarConfig::kHubIconPrefix, m_hubHover, false);

    if (m_barExpanded) {
        for (int i = 0; i < SidebarConfig::kCategoryCount; ++i) {
            const auto& cat = SidebarConfig::kCategories[i];
            const bool hover = (i == m_hoverCat);
            const bool selected = (i == m_openCategory);
            DrawIconSlot(canvas, CategoryRect(i), m_catAnim[i], cat.iconPrefix, hover, selected);

            if (selected) {
                int visibleSlot = 0;
                for (int j = 0; j < cat.subCount; ++j) {
                    const auto& sub = cat.subs[j];
                    if (!IsSubVisible(sub)) {
                        continue;
                    }
                    const bool subHover = (m_hoverSub == visibleSlot);
                    const bool subOpen = IsSubOpen(sub);
                    DrawIconSlot(canvas, SubRect(visibleSlot), m_subAnim[i][j], sub.iconPrefix, subHover, subOpen);
                    ++visibleSlot;
                }
            }
        }
    }

    if (m_tipShown.kind == kHoverHub) {
        RECT rc = HubRect();
        DrawTip(canvas, hubTip, rc.right, (rc.top + rc.bottom) / 2);
    } else if (m_tipShown.kind == kHoverCategory &&
               m_tipShown.catIndex >= 0 &&
               m_tipShown.catIndex < SidebarConfig::kCategoryCount) {
        const auto& cat = SidebarConfig::kCategories[m_tipShown.catIndex];
        RECT rc = CategoryRect(m_tipShown.catIndex);
        DrawTip(canvas, cat.tip, rc.right, (rc.top + rc.bottom) / 2);
    } else if (m_tipShown.kind == kHoverSub &&
               m_tipShown.catIndex >= 0 &&
               m_tipShown.catIndex < SidebarConfig::kCategoryCount) {
        const auto& cat = SidebarConfig::kCategories[m_tipShown.catIndex];
        const int logical = LogicalSubFromVisible(m_tipShown.catIndex, m_tipShown.subIndex);
        if (logical >= 0 && logical < cat.subCount) {
            RECT rc = SubRect(m_tipShown.subIndex);
            DrawTip(canvas, ResolveSubTip(cat.subs[logical]), rc.right, (rc.top + rc.bottom) / 2);
        }
    }
}

int CUISideToolbar::HitTest(int rx, int ry, CCtrlWnd** ppCtrl) {
    if (ppCtrl) {
        *ppCtrl = nullptr;
    }
    POINT pt{rx, ry};
    RECT hubRc = HubRect();
    if (PtInRect(&hubRc, pt)) {
        return 1;
    }
    if (!m_barExpanded) {
        return 0;
    }
    if (HitCategory(rx, ry) >= 0) {
        return 1;
    }
    if (HitSub(rx, ry) >= 0) {
        return 1;
    }
    return 0;
}

void CUISideToolbar::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    POINT pt{rx, ry};
    if (msg == WM_LBUTTONDOWN) {
        RECT hubRc = HubRect();
        if (PtInRect(&hubRc, pt)) {
            if (AcceptClick()) {
                ToggleBar();
            }
            return;
        }
        if (!m_barExpanded) {
            return;
        }
        const int cat = HitCategory(rx, ry);
        if (cat >= 0) {
            // Defer toggle to mouse-up so long-press GM on 会员中心 does not also flip the category.
            m_catPressIndex = cat;
            m_catPressTick = GetTickCount();
            m_longPressGmSent = false;
            return;
        }
        const int sub = HitSub(rx, ry);
        if (sub >= 0 && m_openCategory >= 0) {
            m_catPressIndex = -1;
            if (AcceptClick()) {
                try {
                    play_ui_sound(L"BtMouseClick");
                } catch (...) {
                }
                const auto& catDef = SidebarConfig::kCategories[m_openCategory];
                const int logical = LogicalSubFromVisible(m_openCategory, sub);
                if (logical >= 0 && logical < catDef.subCount) {
                    InvokeSub(catDef.subs[logical]);
                }
                InvalidateRect(nullptr);
            }
            return;
        }
    }
    if (msg == WM_LBUTTONUP) {
        if (m_catPressIndex >= 0 && !m_longPressGmSent && m_barExpanded) {
            const int cat = m_catPressIndex;
            if (HitCategory(rx, ry) == cat && AcceptClick()) {
                ToggleCategory(cat);
            }
        }
        m_catPressIndex = -1;
        m_catPressTick = 0;
        m_longPressGmSent = false;
    }
    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

int CUISideToolbar::OnMouseMove(int rx, int ry) {
    POINT hoverPt{rx, ry};
    RECT hubRc = HubRect();
    const bool hubHover = PtInRect(&hubRc, hoverPt) ? true : false;
    const int cat = HitCategory(rx, ry);
    const int sub = HitSub(rx, ry);
    if (hubHover != m_hubHover || cat != m_hoverCat || sub != m_hoverSub) {
        m_hubHover = hubHover;
        m_hoverCat = cat;
        m_hoverSub = sub;
        UpdateTipState(rx, ry);
        InvalidateRect(nullptr);
    } else {
        UpdateTipState(rx, ry);
        if (m_tipShown.kind != m_tipTarget.kind ||
            m_tipShown.catIndex != m_tipTarget.catIndex ||
            m_tipShown.subIndex != m_tipTarget.subIndex) {
            InvalidateRect(nullptr);
        }
    }
    return 1;
}

void CUISideToolbar::OnMouseEnter(int bEnter) {
    CWnd::OnMouseEnter(bEnter);
    if (!bEnter) {
        m_hubHover = false;
        m_hoverCat = -1;
        m_hoverSub = -1;
        m_tipTarget = {};
        m_tipShown = {};
        m_tipHoverStart = 0;
        m_catPressIndex = -1;
        InvalidateRect(nullptr);
    }
}

void EnsureInstance() {
    if (CUISideToolbar::ms_pInstance) {
        return;
    }
    try {
        if (!get_rm()) {
            return;
        }
        new CUISideToolbar();
    } catch (...) {
        CUISideToolbar::ms_pInstance = nullptr;
    }
}

void DestroySideToolbarForLogout() {
    CUISideToolbar* p = CUISideToolbar::ms_pInstance;
    if (!p) {
        return;
    }
    CUISideToolbar::ms_pInstance = nullptr;
    p->Destroy();
}

} // namespace

void AttachSideToolbarMod() {
    EnsureInstance();
}

namespace SideToolbar {
void DestroyForLogout() {
    DestroySideToolbarForLogout();
}

void ResetServerToolConfigDefaults() {
    g_runtimeInited = false;
    EnsureRuntimeDefaults();
}

void ApplyServerToolConfig(int toolIndex, bool visible, const char* tipTitleGbk, const char* tipDescGbk) {
    EnsureRuntimeDefaults();
    if (toolIndex < 0 || toolIndex >= SidebarConfig::kServerToolCount) {
        return;
    }
    g_toolVisible[toolIndex] = visible;
    if (tipTitleGbk && tipTitleGbk[0]) {
        strncpy_s(g_tipTitle[toolIndex], tipTitleGbk, _TRUNCATE);
        if (tipDescGbk) {
            strncpy_s(g_tipDesc[toolIndex], tipDescGbk, _TRUNCATE);
        } else {
            g_tipDesc[toolIndex][0] = '\0';
        }
        g_tipOverride[toolIndex] = true;
    }
}

void InvalidateUi() {
    if (CUISideToolbar::ms_pInstance) {
        CUISideToolbar::ms_pInstance->InvalidateRect(nullptr);
    }
}
}
