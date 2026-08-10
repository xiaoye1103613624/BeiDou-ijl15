// ============================================================
// sidetoolbar.cpp — floating side toolbar with animated icon.
//
// Icon frames (v083 live UIWindow.img — avoid UIWindow2 merges that drop Item):
//   UI/UIWindow.img/Minigame/Omok/stone/3/black/{0,1,2}
// Layout: [icon | feature list]. Icon has no chrome/background.
// Click icon to expand/collapse. Open features keep selected style.
// ============================================================

#include "stdafx.h"
#include "SideToolbarApi.h"
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

static void* GetWvsContext() {
    return *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
}

// Layout — QDeliveryIcon frames are 24x32; icon on the LEFT of the list.
static constexpr int kIconW = 32;
static constexpr int kIconH = 36;
static constexpr int kIconPad = 2;
static constexpr int kGap = 4;
static constexpr int kPanelW = 120;
static constexpr int kItemH = 22;
static constexpr int kItemPadX = 6;
static constexpr int kItemPadY = 3;
static constexpr int kAnimIntervalMs = 250;
static constexpr int kAnimFrameCount = 3; // Omok black 0..2

// 0..8 match SidebarToolHandler.java (SIDEBAR_TOOL 0xC6).
// 9..13 are local client UI toggles (no server packet).
enum FeatureId : int {
    kToolConvenient = 0, // 便民工具
    kToolEquipCenter,    // 装备中心
    kToolExchange,       // 兑换中心
    kToolVip,            // VIP会员
    kToolGrowth,         // 成长系统
    kToolDaily,          // 每日任务
    kToolSocial,         // 社交系统
    kToolCollect,        // 收集系统
    kToolGm,             // GM工具
    kServerToolCount = 9,
    kFeatStorageBag = 9, // 收纳背包
    kFeatDamageRank,     // 伤害统计
    kFeatPartyTracker,   // 队伍追踪
    kFeatBeauty,         // 美容美发
    kFeatCheckin,        // 每日签到
    kFeatCashShop,       // 现金商城
    kFeatCount
};

static constexpr unsigned short kOpcode_SidebarTool = 0xC6;

static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

static void SendSidebarTool(int toolIndex) {
    if (toolIndex < 0 || toolIndex >= kServerToolCount) {
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

// GBK labels (client Dotum font expects ANSI/GBK on CN client).
static const char* FeatureLabel(int id) {
    switch (id) {
    case kToolConvenient:   return "\xB1\xE3\xC3\xF1\xB9\xA4\xBE\xDF"; // 便民工具
    case kToolEquipCenter:  return "\xD7\xB0\xB1\xB8\xD6\xD0\xD0\xC4"; // 装备中心
    case kToolExchange:     return "\xB6\xD2\xBB\xBB\xD6\xD0\xD0\xC4"; // 兑换中心
    case kToolVip:          return "VIP\xBB\xE1\xD4\xB1";             // VIP会员
    case kToolGrowth:       return "\xB3\xC9\xB3\xA4\xCF\xB5\xCD\xB3"; // 成长系统
    case kToolDaily:        return "\xC3\xBF\xC8\xD5\xC8\xCE\xCE\xF1"; // 每日任务
    case kToolSocial:       return "\xC9\xE7\xBD\xBB\xCF\xB5\xCD\xB3"; // 社交系统
    case kToolCollect:      return "\xCA\xD5\xBC\xAF\xCF\xB5\xCD\xB3"; // 收集系统
    case kToolGm:           return "GM\xB9\xA4\xBE\xDF";               // GM工具
    case kFeatStorageBag:  return "\xCA\xD5\xC4\xC9\xB1\xB3\xB0\xFC"; // 收纳背包
    case kFeatDamageRank:   return "\xC9\xCB\xBA\xA6\xCD\xB3\xBC\xC6"; // 伤害统计
    case kFeatPartyTracker: return "\xB6\xD3\xCE\xE9\xD7\xB7\xD7\xD9"; // 队伍追踪
    case kFeatBeauty:       return "\xC3\xC0\xC8\xDD\xC3\xC0\xB7\xA2"; // 美容美发
    case kFeatCheckin:      return "\xC3\xBF\xC8\xD5\xC7\xA9\xB5\xBD"; // 每日签到
    case kFeatCashShop:     return "\xCF\xD6\xBD\xF0\xC9\xCC\xB3\xC7"; // 现金商城
    default:                return "?";
    }
}

static bool IsFeatureOpen(int id) {
    switch (id) {
    case kFeatBeauty:
        return BeautyShop_IsOpen();
    case kFeatCheckin:
        return DailyCheckin_IsOpen();
    case kFeatDamageRank:
        return CUIDamageRank::IsPanelOpen();
    case kFeatStorageBag:
        return BagWindow_IsOpen();
    case kFeatPartyTracker:
        return PartyBuffs_IsTrackerVisible();
    case kFeatCashShop:
        return CashShopWnd_IsOpen();
    default:
        return false;
    }
}

static unsigned int QueryOpenMask() {
    unsigned int mask = 0;
    for (int i = 0; i < kFeatCount; ++i) {
        if (IsFeatureOpen(i)) {
            mask |= (1u << i);
        }
    }
    return mask;
}

static void CloseFeature(int id) {
    switch (id) {
    case kFeatBeauty:
        BeautyShop_CloseWindow();
        break;
    case kFeatCheckin:
        DailyCheckin_Close();
        break;
    case kFeatDamageRank:
        CUIDamageRank::ClosePanel();
        break;
    case kFeatStorageBag:
        BagWindow_Close();
        break;
    case kFeatPartyTracker:
        PartyBuffs_SetTrackerVisible(false);
        break;
    case kFeatCashShop:
        CashShopWnd_Close();
        break;
    default:
        break;
    }
}

static void OpenFeature(int id) {
    if (id >= 0 && id < kServerToolCount) {
        SendSidebarTool(id);
        return;
    }
    switch (id) {
    case kFeatBeauty:
        BeautyShop_OpenWindow();
        break;
    case kFeatCheckin:
        DailyCheckin_RequestOpen();
        break;
    case kFeatDamageRank:
        CUIDamageRank::ToggleBySidebar();
        break;
    case kFeatStorageBag:
        BagWindow_Toggle();
        break;
    case kFeatPartyTracker:
        PartyBuffs_SetTrackerVisible(true);
        break;
    case kFeatCashShop:
        CashShopWnd_RequestOpen();
        break;
    default:
        break;
    }
}

static void InvokeFeature(int id) {
    if (id >= kServerToolCount && IsFeatureOpen(id)) {
        CloseFeature(id);
    } else {
        OpenFeature(id);
    }
}

class CUISideToolbar : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUISideToolbar* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    bool m_expanded = false;
    int m_animFrame = 0;   // 0..6
    int m_animDir = 1;     // +1 / -1
    DWORD m_lastAnimTick = 0;
    DWORD m_lastClickTick = 0; // debounce: one logical click
    int m_hoverItem = -1;
    bool m_iconHover = false;
    unsigned int m_openMask = 0;

    IWzCanvasPtr m_icon[kAnimFrameCount];
    IWzFontPtr m_font;
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
        void* ctx = GetWvsContext();
        if (ctx) {
            reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
        }
    }

    static int WndW() { return kIconPad + kIconW + kGap + kPanelW + kIconPad; }
    static int WndH() {
        const int listH = kItemPadY * 2 + kFeatCount * kItemH;
        return listH > kIconH ? listH : kIconH;
    }
    // Icon centered inside the tall layer (list expands around the same axis).
    static int IconLocalY() { return (WndH() - kIconH) / 2; }
    static int ListStartY() { return (WndH() - kFeatCount * kItemH) / 2; }

    RECT IconRect() const {
        const int x = kIconPad;
        const int y = IconLocalY();
        return {x, y, x + kIconW, y + kIconH};
    }

    // Feature list sits to the RIGHT of the icon.
    RECT PanelRect() const {
        const int x = kIconPad + kIconW + kGap;
        return {x, 0, x + kPanelW, WndH()};
    }

    RECT ItemRect(int index) const {
        RECT panel = PanelRect();
        const int y = ListStartY() + index * kItemH;
        return {panel.left + kItemPadX, y, panel.right - kItemPadX, y + kItemH - 2};
    }

    int HitItem(int rx, int ry) const {
        if (!m_expanded) {
            return -1;
        }
        POINT pt{rx, ry};
        for (int i = 0; i < kFeatCount; ++i) {
            RECT rc = ItemRect(i);
            if (PtInRect(&rc, pt)) {
                return i;
            }
        }
        return -1;
    }

    static IWzCanvasPtr LoadSprite(const wchar_t* path) {
        IWzCanvasPtr c;
        try {
            c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(path)));
        } catch (...) {
        }
        return c;
    }

    static void BlitA(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src) {
            try {
                dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0);
            } catch (...) {
            }
        }
    }

    static void Fill(IWzCanvasPtr c, const RECT& rc, unsigned int color) {
        if (!c) {
            return;
        }
        try {
            c->DrawRectangle(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, color);
        } catch (...) {
        }
    }

    static void Stroke(IWzCanvasPtr c, const RECT& rc, unsigned int color) {
        if (!c) {
            return;
        }
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        try {
            c->DrawRectangle(rc.left, rc.top, w, 1, color);
            c->DrawRectangle(rc.left, rc.bottom - 1, w, 1, color);
            c->DrawRectangle(rc.left, rc.top, 1, h, color);
            c->DrawRectangle(rc.right - 1, rc.top, 1, h, color);
        } catch (...) {
        }
    }

    void LoadAssets() {
        // Primary: Omok stones. Fallbacks: Quest marker / CashShop tab (always in v083 UI).
        static const wchar_t* kFrameSets[][kAnimFrameCount] = {
            {
                L"UI/UIWindow.img/Minigame/Omok/stone/3/black/0",
                L"UI/UIWindow.img/Minigame/Omok/stone/3/black/1",
                L"UI/UIWindow.img/Minigame/Omok/stone/3/black/2",
            },
            {
                L"UI/UIWindow.img/Quest/icon3/6",
                L"UI/UIWindow.img/Quest/icon3/6",
                L"UI/UIWindow.img/Quest/icon3/6",
            },
            {
                L"UI/Basic.img/Cursor/0/0",
                L"UI/Basic.img/Cursor/0/0",
                L"UI/Basic.img/Cursor/0/0",
            },
        };
        bool any = false;
        for (const auto& set : kFrameSets) {
            any = false;
            for (int i = 0; i < kAnimFrameCount; ++i) {
                m_icon[i] = LoadSprite(set[i]);
                if (m_icon[i]) {
                    any = true;
                }
            }
            if (any) {
                break;
            }
        }
        m_font = nullptr;
        m_fontShadow = nullptr;
        try {
            get_basic_font(std::addressof(m_font), 0);
        } catch (...) {
        }
        try {
            PcCreateObject<IWzFontPtr>(L"Canvas#Font", m_fontShadow, nullptr);
            if (m_fontShadow) {
                auto fn = reinterpret_cast<HRESULT(__thiscall*)(
                    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kAddr_SetFont);
                fn(m_fontShadow, L"Dotum", 12, 0xFF000000, Ztl_variant_t(L""));
            }
        } catch (...) {
        }
    }

    void AdvanceAnim() {
        const DWORD now = GetTickCount();
        if (m_lastAnimTick == 0) {
            m_lastAnimTick = now;
            return;
        }
        if (now - m_lastAnimTick < static_cast<DWORD>(kAnimIntervalMs)) {
            return;
        }
        m_lastAnimTick = now;
        m_animFrame += m_animDir;
        if (m_animFrame >= kAnimFrameCount - 1) {
            m_animFrame = kAnimFrameCount - 1;
            m_animDir = -1;
        } else if (m_animFrame <= 0) {
            m_animFrame = 0;
            m_animDir = 1;
        }
        InvalidateRect(nullptr);
    }

    void RefreshOpenMask() {
        const unsigned int next = QueryOpenMask();
        if (next != m_openMask) {
            m_openMask = next;
            if (m_expanded) {
                InvalidateRect(nullptr);
            }
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

    void ToggleExpanded() {
        m_expanded = !m_expanded;
        m_hoverItem = -1;
        if (m_expanded) {
            m_openMask = QueryOpenMask();
        }
        // Do NOT MoveWnd here — position is fixed; MoveWnd mid-click can eat the next click.
        try {
            play_ui_sound(m_expanded ? L"MenuUp" : L"MenuDown");
        } catch (...) {
        }
        InvalidateRect(nullptr);
    }
};

CUISideToolbar::CUISideToolbar() {
    LoadAssets();
    m_lastAnimTick = GetTickCount();
    m_openMask = QueryOpenMask();
    m_expanded = false;

    const int w = WndW();
    const int h = WndH();
    const int screenH = get_screen_height();
    // Icon centered in layer + window vertically centered → icon at screen mid.
    const int left = 6;
    const int top = (std::max)(0, (screenH - h) / 2);
    CreateWnd(left, top, w, h, 12, 1, nullptr, 0);
    const bool pinned = rs_force_wnd_lt_abs(this, left, top);
    (void)pinned; // tip-drawonly: no fopen(sidebar_debug) — absent from green 6D612F01
    ms_pInstance = this;
}

void CUISideToolbar::OnDestroy() {
    for (int i = 0; i < kAnimFrameCount; ++i) {
        m_icon[i] = nullptr;
    }
    m_font = nullptr;
    m_fontShadow = nullptr;
    if (ms_pInstance == this) {
        ms_pInstance = nullptr;
    }
    CWnd::OnDestroy();
}

void CUISideToolbar::Update() {
    AdvanceAnim();
    RefreshOpenMask();
}

void CUISideToolbar::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr canvas = GetCanvas();
    if (!canvas) {
        return;
    }

    // Wipe full window first. v083 canvas needs transparent WHITE (0x00FFFFFF),
    // same as damage-rank — 0x00000000 does NOT clear and leaves a ghost list.
    RECT clearRc{0, 0, WndW(), WndH()};
    Fill(canvas, clearRc, 0x00FFFFFF);

    if (m_expanded) {
        RECT panel = PanelRect();
        Fill(canvas, panel, 0xE0181E28);
        Stroke(canvas, panel, 0xFFD0A050);

        for (int i = 0; i < kFeatCount; ++i) {
            RECT irc = ItemRect(i);
            const bool hover = (i == m_hoverItem);
            const bool open = (m_openMask & (1u << i)) != 0;

            // Open features keep a selected (gold) look until closed.
            unsigned int fill = 0xFF242C38;
            unsigned int edge = 0xFF506070;
            if (open) {
                fill = hover ? 0xFF6A5018 : 0xFF4A3A10;
                edge = 0xFFFFD070;
            } else if (hover) {
                fill = 0xFF3A4A60;
                edge = 0xFF90A0B0;
            }
            Fill(canvas, irc, fill);
            Stroke(canvas, irc, edge);

            const char* label = FeatureLabel(i);
            const int tx = irc.left + 8;
            const int ty = irc.top + 5;
            try {
                Ztl_bstr_t text(label);
                if (m_fontShadow) {
                    canvas->DrawTextA(tx + 1, ty + 1, text, m_fontShadow);
                }
                if (m_font) {
                    canvas->DrawTextA(tx, ty, text, m_font);
                }
            } catch (...) {
            }
        }
    }

    // Always draw solid chrome first — sprite-only icons can load but blit as fully
    // transparent on some UIWindow.img formats, which looked like "toolbar missing".
    RECT irc = IconRect();
    Fill(canvas, irc, m_iconHover ? 0xFF6A5018 : 0xFF4A3A10);
    Stroke(canvas, irc, 0xFFFFD070);
    IWzCanvasPtr icon = m_icon[m_animFrame];
    if (icon) {
        int sw = 24, sh = 32;
        try {
            sw = icon->width;
            sh = icon->height;
        } catch (...) {
        }
        const int sx = irc.left + (kIconW - sw) / 2;
        const int sy = irc.top + (kIconH - sh) / 2;
        BlitA(canvas, icon, sx, sy);
    }
}

int CUISideToolbar::HitTest(int rx, int ry, CCtrlWnd** ppCtrl) {
    // Rect hit only — do NOT delegate to CWnd::HitTest (it alpha-tests the canvas
    // and can miss the icon sprite / treat uncleared ghost pixels oddly).
    if (ppCtrl) {
        *ppCtrl = nullptr;
    }
    POINT pt{rx, ry};
    RECT icon = IconRect();
    if (PtInRect(&icon, pt)) {
        return 1;
    }
    if (m_expanded) {
        RECT panel = PanelRect();
        if (PtInRect(&panel, pt)) {
            return 1;
        }
    }
    return 0;
}

void CUISideToolbar::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    POINT pt{rx, ry};
    // Match other custom UIs (dailycheckin/bag): act on WM_LBUTTONDOWN.
    // LBUTTONUP is not reliably delivered to CWnd on this client.
    if (msg == WM_LBUTTONDOWN) {
        RECT icon = IconRect();
        if (PtInRect(&icon, pt)) {
            if (AcceptClick()) {
                ToggleExpanded();
            }
            return;
        }
        const int item = HitItem(rx, ry);
        if (item >= 0) {
            if (AcceptClick()) {
                try {
                    play_ui_sound(L"BtMouseClick");
                } catch (...) {
                }
                InvokeFeature(item);
                m_openMask = QueryOpenMask();
                InvalidateRect(nullptr);
            }
            return;
        }
    }
    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

int CUISideToolbar::OnMouseMove(int rx, int ry) {
    POINT pt{rx, ry};
    RECT icon = IconRect();
    const bool ih = PtInRect(&icon, pt) ? true : false;
    const int hi = HitItem(rx, ry);
    if (ih != m_iconHover || hi != m_hoverItem) {
        m_iconHover = ih;
        m_hoverItem = hi;
        InvalidateRect(nullptr);
    }
    return 1;
}

void CUISideToolbar::OnMouseEnter(int bEnter) {
    CWnd::OnMouseEnter(bEnter);
    if (!bEnter) {
        m_iconHover = false;
        m_hoverItem = -1;
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
    // Destroy removes Gr2D/layers. Skip `delete` — ZALLOC + multi-inherit (A041FF uses
    // ZRefCounted release on registered singletons; this toolbar is not in that list).
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
}

// SideToolbar::EnsureHooks / OnTick live in SideToolbarBridge.cpp
// (deferred create after enter-game ticks — do not create UI at CField).
