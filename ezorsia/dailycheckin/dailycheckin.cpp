// ============================================================
// dailycheckin.cpp  —  custom Daily Check-In window (28-day login-streak grid).
//
// A single CWnd subclass (CUIDailyCheckin), one instance, showing a 7x4 grid of day
// slots over your custom art (UI/UIWindow.img/DailyCheckin/backgrnd, 513x346). Each
// consecutive day (server-gated to one claim / 24h) unlocks the next day's reward;
// clicking an unlocked, unclaimed slot sends a CLAIM request and (on the reply) pops a
// native confirmation dialog.
//
// The server is the source of truth (Character.checkin* + DailyCheckinHandler). The
// window is opened by the server PUSHING a snapshot — on login (auto-open when a reward
// is waiting) or in response to the @checkin / @daily command. The client only draws the
// state it is sent and requests claims; it never decides unlock/claim itself.
//
// Per-day state comes from the snapshot: currentDay (unlocked-through), claimedMask
// (28-bit), 28 reward icon item-ids, and 28 reward-breakdown tooltip strings.
//
// ---- Engine bindings this file expects (a v83 Kaentake-style injector DLL) ----
//   * pch.h / hook.h / wvs/*.h / ztl/*.h — the base framework headers (CWnd, COutPacket/
//     CInPacket, get_rm(), CItemInfo, ZXString, ATTACH_HOOK, etc.). Match these to your
//     own client framework; they are NOT included in this package.
//   * The hard-coded addresses below are for v83, image base 0x400000. A different client
//     build needs them re-found (see the README "Client addresses" table).
//   * AttachDailyCheckinMod() is the single install entry — call it from your hook installer.
// ============================================================

// BeiDou v083 client addresses - IDA MCP verified 2026-07-09 (image base 0x400000):
//   ProcessPacket 0x004965F1 = sub_4965F1  (ClientAddresses::kProcessPacket)
//   SendPacket    0x0049637B = sub_49637B  (ClientAddresses::kSendPacket)
//   CWvsContext   0x00BE7918 (global pointer, not a function)
//   CClientSocket 0x00BE7914 (ClientAddresses::kClientSocketPtr)
// Recv uses PacketDispatcher::RegisterHandler(0x17C); do NOT hook ProcessPacket.

#include "stdafx.h"
#include "DailyCheckinApi.h"
#include "ClientAddresses.h"
#include "wvs/packet_legacy.h"
#include "wvs/Packet.h"
#include "wvs/wnd.h"
#include "wvs/iteminfo.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace DailyCheckin {

// ---------------------------------------------------------------------------
// v83 client addresses (image base 0x400000).
// ---------------------------------------------------------------------------
static constexpr uintptr_t kAddr_play_ui_sound           = 0x00989588;
static constexpr uintptr_t kAddr_ProcessBasicUIKey       = 0x00A07431;
static constexpr uintptr_t kAddr_CWvsContext_Instance    = 0x00BE7918;
static constexpr uintptr_t kAddr_CUtilDlg_Notice         = 0x009929DD;
static constexpr uintptr_t kAddr_get_basic_font          = 0x0098A707;
static constexpr uintptr_t kAddr_SetFont                 = 0x0046341A;

// Styled-tooltip palette.
static constexpr unsigned long TT_COL_BORDER  = 0xFF20283A;  // steel frame
static constexpr unsigned long TT_COL_BG      = 0xF0141A24;  // near-opaque dark navy body
static constexpr unsigned long TT_COL_TITLE   = 0xCC4A3414;  // amber title bar
static constexpr unsigned long TT_COL_SEP     = 0xFFE0C070;  // gold separator

static auto play_ui_sound = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto get_basic_font = reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);
static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

// Engine's native single-button Notice dialog.
typedef int(__cdecl* t_CUtilDlg_Notice)(ZXString<char>, const wchar_t*, void*, int, int);
static auto CUtilDlg_Notice = reinterpret_cast<t_CUtilDlg_Notice>(kAddr_CUtilDlg_Notice);

static void* GetWvsContext() { return *reinterpret_cast<void**>(kAddr_CWvsContext_Instance); }

static void SendPacket(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (sock) ClientSocket_SendPacket(sock, o);
}

// ---------------------------------------------------------------------------
// Protocol (must match the server's RecvOpcode/SendOpcode.DAILY_CHECKIN).
// ---------------------------------------------------------------------------
static constexpr int kOpcode_Send   = 0x11A;   // CP_DailyCheckin (client -> server)
static constexpr int kOpcode_Recv   = 0x17C;   // LP_DailyCheckin (server -> client)
static constexpr int kReq_Open      = 0;
static constexpr int kReq_Claim     = 1;
static constexpr int kResp_Snapshot = 1;
static constexpr int kDays          = 28;

static void SendReq_Open() {
    COutPacket o(kOpcode_Send);
    o.Encode1((unsigned char)kReq_Open);
    SendPacket(o);
}

static void SendReq_Claim(int day) {
    COutPacket o(kOpcode_Send);
    o.Encode1((unsigned char)kReq_Claim);
    o.Encode1((unsigned char)day);
    SendPacket(o);
}

// ---------------------------------------------------------------------------
// Geometry — measured off the sample art (513x346). 7 columns x 4 rows. A "DAY N"
// label tops each cell; the reward icon sits in the box below it. If your art uses
// different origins, adjust these constants.
// ---------------------------------------------------------------------------
static constexpr int kWndW      = 513;
static constexpr int kWndH      = 346;
static constexpr int kTitleH    = 18;   // draggable title bar height

static constexpr int kCols      = 7;
static constexpr int kRows      = 4;
static constexpr int kCol0      = 6;    // first column left
static constexpr int kColPitch  = 72;   // column spacing
static constexpr int kRowLabel0 = 56;   // first row's label top
static constexpr int kRowPitch  = 73;   // row spacing
static constexpr int kCellW     = 65;   // cell width (label/clickable span)
static constexpr int kCellH     = 71;   // cell height (label + content box)
static constexpr int kIconDX    = 16;   // (kCellW-32)/2 -> horizontally centre a 32px icon
static constexpr int kIconBY    = 62;   // bottom anchor for DrawItemIconForSlot, below the label

// Close button (top-right). Reuses the vanilla bag-window 12x12 close art
// (UI/UIWindow.img/Bag/BtClose) — point this at any close button in your UI.wz. Seated inside the
// header band (sample art's white strip is y5..18) so it doesn't clip the title-bar gradient/frame.
static constexpr int kBtCloseX = 490, kBtCloseY = 6, kBtCloseW = 12, kBtCloseH = 12;

// ---------------------------------------------------------------------------
// Shared snapshot state (the window is a thin view over this).
// ---------------------------------------------------------------------------
struct State {
    int         currentDay;     // highest day unlocked this cycle (0..28)
    int         claimedMask;    // 28-bit mask of claimed days
    int         iconId[kDays];  // reward icon per day (index 0 = day 1)
    std::string tip[kDays];     // per-day reward breakdown (hover tooltip text)
    bool        ready;
};
static State g_state;

// Remembered window placement (reopen lands where it was last closed).
static bool s_bSavedPos = false;
static int  s_savedX = 0, s_savedY = 0;

static bool DayUnlocked(int d)  { return d >= 1 && d <= g_state.currentDay; }
static bool DayClaimed(int d)   { return d >= 1 && d <= kDays && ((g_state.claimedMask >> (d - 1)) & 1) != 0; }
static bool DayClaimable(int d) { return DayUnlocked(d) && !DayClaimed(d); }

// ---------------------------------------------------------------------------
// CUIDailyCheckin
// ---------------------------------------------------------------------------
class CUIDailyCheckin : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIDailyCheckin* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{ nullptr };

    int  m_screenX, m_screenY;
    int  m_nCloseHover;
    int  m_bDragging, m_nDragAnchorX, m_nDragAnchorY;

    IWzCanvasPtr m_pBg;
    IWzCanvasPtr m_pBtClose[2];   // normal, mouseOver
    IWzFontPtr   m_pFont;         // basic font (tooltip fallback)
    IWzFontPtr   m_pFontTitle;    // gold bold  — tooltip header
    IWzFontPtr   m_pFontBody;     // white bold — tooltip body lines
    IWzFontPtr   m_pFontAccent;   // amber bold — tooltip meso line
    IWzFontPtr   m_pFontShadow;   // black bold — 1px text shadow
    int          m_hoverDay;      // day (1..28) currently hovered, or -1

    RECT m_rcClose;

    CUIDailyCheckin(int nLeft, int nTop);
    virtual ~CUIDailyCheckin() override { if (ms_pInstance == this) ms_pInstance = nullptr; }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int  OnMouseMove(int rx, int ry) override;
    virtual int  OnMouseWheel(int /*rx*/, int /*ry*/, int /*nWheel*/) override { return 1; }
    virtual void OnMouseEnter(int bEnter) override;
    virtual void OnDestroy() override;
    virtual void Update() override { InvalidateRect(nullptr); }
    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }
    virtual int OnSetFocus(int /*bFocus*/) override { return 0; }   // let the player still move
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        const bool isKeyUp = (lParam & 0x80000000) != 0;
        if (!isKeyUp && wParam == VK_ESCAPE) {
            Destroy();
            return;
        }
        void* ctx = GetWvsContext();
        if (ctx) reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                     kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
    }

    static IWzCanvasPtr LoadSprite(const wchar_t* p) {
        IWzCanvasPtr c;
        try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p))); } catch (...) {}
        return c;
    }
    static void BlitAt(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    static void BlitA(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src)
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0); } catch (...) {}
    }
    static void DrawBorder(IWzCanvasPtr c, const RECT& rc, int t, unsigned int color) {
        if (!c) return;
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        try {
            c->DrawRectangle(rc.left, rc.top, w, t, color);
            c->DrawRectangle(rc.left, rc.bottom - t, w, t, color);
            c->DrawRectangle(rc.left, rc.top, t, h, color);
            c->DrawRectangle(rc.right - t, rc.top, t, h, color);
        } catch (...) {}
    }
    static void Fill(IWzCanvasPtr c, const RECT& rc, unsigned int color) {
        if (!c) return;
        try { c->DrawRectangle(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, color); } catch (...) {}
    }
    // Filled rounded rect — per-row spans give a 45deg corner bevel. Rows never overlap, so it
    // stays correct with a translucent color.
    static void FillRounded(IWzCanvasPtr c, const RECT& rc, unsigned int color, int r) {
        if (!c) return;
        int x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return;
        if (r < 1 || w < 2 * r || h < 2 * r) { Fill(c, rc, color); return; }
        try {
            for (int i = 0; i < r; ++i) { int in = r - 1 - i; c->DrawRectangle(x + in, y + i,         w - 2 * in, 1, color); }
            c->DrawRectangle(x, y + r, w, h - 2 * r, color);
            for (int i = 0; i < r; ++i) { int in = r - 1 - i; c->DrawRectangle(x + in, y + h - 1 - i, w - 2 * in, 1, color); }
        } catch (...) {}
    }
    // Rounded-rect outline (t px thick). Use OPAQUE colors — edge/corner overlaps are harmless then.
    static void StrokeRounded(IWzCanvasPtr c, const RECT& rc, unsigned int color, int r, int t) {
        if (!c) return;
        int x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return;
        if (r < 1 || w < 2 * r || h < 2 * r) { DrawBorder(c, rc, t, color); return; }
        try {
            c->DrawRectangle(x + r, y,           w - 2 * r, t, color);          // top
            c->DrawRectangle(x + r, y + h - t,   w - 2 * r, t, color);          // bottom
            c->DrawRectangle(x,     y + r,       t, h - 2 * r, color);          // left
            c->DrawRectangle(x + w - t, y + r,   t, h - 2 * r, color);          // right
            for (int i = 0; i < r; ++i) {
                int in = r - 1 - i;
                c->DrawRectangle(x + in,         y + i,         t, t, color);    // top-left
                c->DrawRectangle(x + w - t - in, y + i,         t, t, color);    // top-right
                c->DrawRectangle(x + in,         y + h - t - i, t, t, color);    // bottom-left
                c->DrawRectangle(x + w - t - in, y + h - t - i, t, t, color);    // bottom-right
            }
        } catch (...) {}
    }
    // Small green check mark (a V with a longer right arm) at top-left (x,y).
    static void DrawCheck(IWzCanvasPtr c, int x, int y) {
        if (!c) return;
        const unsigned int g = 0xFF2FBF3F;
        try {
            c->DrawRectangle(x,     y + 3, 2, 2, g);
            c->DrawRectangle(x + 2, y + 5, 2, 2, g);
            c->DrawRectangle(x + 4, y + 3, 2, 2, g);
            c->DrawRectangle(x + 6, y + 1, 2, 2, g);
            c->DrawRectangle(x + 8, y - 1, 2, 2, g);
        } catch (...) {}
    }

    void LoadSprites() {
        m_pBg          = LoadSprite(L"UI/UIWindow.img/DailyCheckin/backgrnd");
        m_pBtClose[0]  = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/normal/0");
        m_pBtClose[1]  = LoadSprite(L"UI/UIWindow.img/Bag/BtClose/mouseOver/0");
    }

    // Create a bold Dotum font of the given colour/size.
    static bool MakeFont(IWzFontPtr& out, unsigned long color, int size) {
        if (out) return true;
        try {
            PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
            if (!out) return false;
            auto fn = reinterpret_cast<HRESULT(__thiscall*)(
                IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kAddr_SetFont);
            return SUCCEEDED(fn(out, L"Dotum", size, color, Ztl_variant_t(L"B")));
        } catch (...) { return false; }
    }
    void EnsureTooltipFonts() {
        MakeFont(m_pFontTitle,  0xFFFFD45A, 12);   // gold
        MakeFont(m_pFontBody,   0xFFFFFFFF, 12);   // white
        MakeFont(m_pFontAccent, 0xFFFFE08A, 12);   // light gold (meso line)
        MakeFont(m_pFontShadow, 0xFF000000, 12);   // black shadow
    }
    // Text with a 1px black drop shadow.
    void ShadowText(IWzCanvasPtr c, int x, int y, const char* s, IWzFontPtr font) const {
        if (!c || !font || !s || !*s) return;
        try {
            if (m_pFontShadow) c->DrawTextA(x + 1, y + 1, s, m_pFontShadow);
            c->DrawTextA(x, y, s, font);
        } catch (...) {}
    }

    // Per-day reward breakdown tooltip: dark navy panel, amber title bar, gold separator,
    // gold (shadowed) header + white/amber body lines. Clamped on-screen.
    void DrawTooltip(IWzCanvasPtr c, int day) {
        if (!c || day < 1 || day > kDays) return;
        const std::string& s = g_state.tip[day - 1];
        if (s.empty()) return;
        IWzFontPtr titleF = m_pFontTitle ? m_pFontTitle : m_pFont;
        IWzFontPtr bodyF  = m_pFontBody  ? m_pFontBody  : m_pFont;
        if (!titleF || !bodyF) return;

        std::vector<std::string> lines;
        size_t st = 0;
        while (st <= s.size()) {
            size_t nl = s.find('\n', st);
            if (nl == std::string::npos) { lines.push_back(s.substr(st)); break; }
            lines.push_back(s.substr(st, nl - st));
            st = nl + 1;
        }
        if (lines.empty()) return;

        const int charW = 7, lineH = 16, padX = 9, titleH = 18;
        int maxc = 0;
        for (const auto& l : lines) if ((int)l.size() > maxc) maxc = (int)l.size();
        int boxW = maxc * charW + padX * 2;
        if (boxW < 96) boxW = 96;
        int bodyLines = (int)lines.size() - 1;
        if (bodyLines < 0) bodyLines = 0;
        int boxH = titleH + 6 + bodyLines * lineH + 6;

        RECT cell; CellRect(day, cell);
        int x = cell.right + 6;
        if (x + boxW > kWndW - 2) x = cell.left - boxW - 6;   // flip to the left if it would overflow
        if (x < 2) x = 2;
        int y = cell.top;
        if (y + boxH > kWndH - 2) y = kWndH - 2 - boxH;
        if (y < 2) y = 2;

        try {
            c->DrawRectangle(x, y, boxW, boxH, TT_COL_BORDER);                       // 1px steel frame
            c->DrawRectangle(x + 1, y + 1, boxW - 2, boxH - 2, TT_COL_BG);           // navy body
            c->DrawRectangle(x + 3, y + 3, boxW - 6, titleH - 2, TT_COL_TITLE);      // amber title bar
            c->DrawRectangle(x + 5, y + titleH + 2, boxW - 10, 1, TT_COL_SEP);       // gold separator
            int hw = (int)lines[0].size() * charW;
            int hx = x + (boxW - hw) / 2;
            if (hx < x + padX) hx = x + padX;
            ShadowText(c, hx, y + 4, lines[0].c_str(), titleF);                      // gold header, centred
            int ty = y + titleH + 6;
            for (size_t i = 1; i < lines.size(); ++i) {
                IWzFontPtr f = (lines[i].find("mesos") != std::string::npos && m_pFontAccent) ? m_pFontAccent : bodyF;
                ShadowText(c, x + padX, ty, lines[i].c_str(), f);
                ty += lineH;
            }
        } catch (...) {}
    }

    // Full clickable cell (label + box).
    static void CellRect(int day, RECT& rc) {
        int idx = day - 1, row = idx / kCols, col = idx % kCols;
        rc.left = kCol0 + col * kColPitch;
        rc.top  = kRowLabel0 + row * kRowPitch;
        rc.right = rc.left + kCellW;
        rc.bottom = rc.top + kCellH;
    }
    // The content box under the "DAY N" label (measured off the art): x = cell left, width 66;
    // y = label top + 21 .. + 71. State overlays hug THIS box so the label stays vivid.
    static void BoxRect(int day, RECT& rc) {
        int idx = day - 1, row = idx / kCols, col = idx % kCols;
        rc.left = kCol0 + col * kColPitch;
        rc.top  = kRowLabel0 + row * kRowPitch + 21;
        rc.right = rc.left + 66;
        rc.bottom = rc.top + 50;
        // The bottom row's box is shorter in the art (ends just above the window's bottom frame).
        if (rc.bottom > kWndH - 4) rc.bottom = kWndH - 4;
    }
    int HitDay(int rx, int ry) const {
        POINT pt{ rx, ry };
        for (int d = 1; d <= kDays; ++d) {
            RECT rc; CellRect(d, rc);
            if (PtInRect(&rc, pt)) return d;
        }
        return -1;
    }
};

// Keep a restored window fully on-screen.
static void ClampToScreen(int& x, int& y) {
    int sw = get_screen_width(), sh = get_screen_height();
    if (x < 8) x = 8;
    if (sw > kWndW && x > sw - kWndW) x = sw - kWndW;
    if (y < 0) y = 0;
    if (sh > kWndH && y > sh - kWndH) y = sh - kWndH;
}

static CUIDailyCheckin* EnsureWindow() {
    if (CUIDailyCheckin::ms_pInstance) return CUIDailyCheckin::ms_pInstance;
    int x, y;
    if (s_bSavedPos) { x = s_savedX; y = s_savedY; }
    else { x = (get_screen_width() - kWndW) / 2; y = 70; }
    ClampToScreen(x, y);
    return new CUIDailyCheckin(x, y);
}

CUIDailyCheckin::CUIDailyCheckin(int nLeft, int nTop)
    : m_screenX(nLeft), m_screenY(nTop), m_nCloseHover(0),
      m_bDragging(0), m_nDragAnchorX(0), m_nDragAnchorY(0), m_hoverDay(-1) {
    m_rcClose = { kBtCloseX, kBtCloseY, kBtCloseX + kBtCloseW, kBtCloseY + kBtCloseH };
    ms_pInstance = this;
    LoadSprites();
    CreateWnd(nLeft, nTop, kWndW, kWndH, 10, 1, nullptr, 0);
    play_ui_sound(L"MenuUp");
    m_pFont = nullptr;
    try { get_basic_font(std::addressof(m_pFont), 0); } catch (...) {}
    EnsureTooltipFonts();
}

void CUIDailyCheckin::OnDestroy() {
    s_savedX = m_screenX; s_savedY = m_screenY; s_bSavedPos = true;
    m_pBg = nullptr; m_pBtClose[0] = nullptr; m_pBtClose[1] = nullptr;
    m_pFont = nullptr; m_pFontTitle = nullptr; m_pFontBody = nullptr;
    m_pFontAccent = nullptr; m_pFontShadow = nullptr;
    if (ms_pInstance == this) ms_pInstance = nullptr;
    CWnd::OnDestroy();
}

void CUIDailyCheckin::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr pCanvas = GetCanvas();
    if (!pCanvas) return;
    auto pItemInfo = CItemInfo::GetInstance();

    // (1) Custom background (title, banner, 28 labelled wells are baked into the art).
    BlitAt(pCanvas, m_pBg, 0, 0);

    // (2) Each day: reward icon + state overlay. Icons stay vibrant so the grid reads as a full
    //     reward preview; only state is signalled by tint/border/check.
    bool blinkOn = ((GetTickCount() / 400) & 1) == 0;
    for (int d = 1; d <= kDays; ++d) {
        int idx = d - 1, row = idx / kCols, col = idx % kCols;
        int cl = kCol0 + col * kColPitch, ct = kRowLabel0 + row * kRowPitch;

        int icon = g_state.iconId[idx];
        if (pItemInfo && icon) {
            pItemInfo->DrawItemIconForSlot(pCanvas, icon, cl + kIconDX, ct + kIconBY, 0, 0, 0, 0, 0, 0);
        }

        // State overlays hug the content box (inset 1px); the "DAY N" label is left untouched.
        RECT box; BoxRect(d, box);
        RECT inner = { box.left + 1, box.top + 1, box.right - 1, box.bottom - 1 };
        const int kRad = 3;   // corner radius matching the grid box's rounded corners
        if (!DayUnlocked(d)) {
            FillRounded(pCanvas, inner, 0x73141A24, kRad);            // locked -> subtle navy veil
        } else if (DayClaimed(d)) {
            FillRounded(pCanvas, inner, 0x55208A3A, kRad);            // claimed -> soft green wash
            StrokeRounded(pCanvas, box, 0xFF3FB24A, kRad, 1);
            DrawCheck(pCanvas, box.right - 13, box.top + 3);
        } else {                                                      // claimable -> blinking gold glow
            StrokeRounded(pCanvas, box, blinkOn ? 0xFFFFD000 : 0xFFFFE873, kRad, 2);
        }
    }

    // (3) Close button (mouseOver art when hovered).
    BlitA(pCanvas, m_nCloseHover ? m_pBtClose[1] : m_pBtClose[0], m_rcClose.left, m_rcClose.top);

    // (4) Hover tooltip — drawn last, on top.
    if (m_hoverDay >= 1) DrawTooltip(pCanvas, m_hoverDay);
}

void CUIDailyCheckin::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    POINT pt{ rx, ry };
    if (msg == WM_LBUTTONDOWN) {
        if (PtInRect(&m_rcClose, pt)) { Destroy(); return; }
        int d = HitDay(rx, ry);
        if (d >= 1) {
            if (DayClaimable(d)) {
                SendReq_Claim(d);
                play_ui_sound(L"BtMouseClick");
            } else {
                play_ui_sound(L"BtMouseOver");
            }
            return;
        }
        if (ry < kTitleH) { m_bDragging = 1; m_nDragAnchorX = rx; m_nDragAnchorY = ry; }
    } else if (msg == WM_LBUTTONUP) {
        m_bDragging = 0;
    }
    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

int CUIDailyCheckin::OnMouseMove(int rx, int ry) {
    if (m_bDragging) {
        int dx = rx - m_nDragAnchorX, dy = ry - m_nDragAnchorY;
        if ((dx || dy) && m_pLayer) {
            m_pLayer->RelOffset(dx, dy, Ztl_variant_t(), Ztl_variant_t());
            m_screenX += dx; m_screenY += dy;
        }
        return 1;
    }
    POINT pt{ rx, ry };
    int hov = PtInRect(&m_rcClose, pt) ? 1 : 0;
    if (hov != m_nCloseHover) { m_nCloseHover = hov; InvalidateRect(nullptr); }
    int hd = HitDay(rx, ry);
    if (hd != m_hoverDay) { m_hoverDay = hd; InvalidateRect(nullptr); }
    return 1;
}

void CUIDailyCheckin::OnMouseEnter(int bEnter) {
    CWnd::OnMouseEnter(bEnter);
    if (!bEnter) { m_nCloseHover = 0; m_hoverDay = -1; InvalidateRect(nullptr); }
}

static std::string DecodePacketString(CompatInPacket* packet) {
    uint16_t len = packet->Decode<uint16_t>();
    std::string s;
    if (len == 0 || !packet->CanRead(len)) {
        return s;
    }
    const unsigned char* p = packet->Current();
    if (p) {
        s.assign(reinterpret_cast<const char*>(p), len);
    }
    packet->SetOffset(packet->GetOffset() + len);
    return s;
}

static void HandleSnapshotCompat(CompatInPacket* packet) {
    int currentDay  = packet->Decode<uint8_t>();
    int claimedMask = packet->Decode<int32_t>();
    int justClaimed = packet->Decode<uint8_t>();
    int count       = packet->Decode<uint8_t>();
    if (count < 0) count = 0;
    if (count > kDays) count = kDays;

    g_state.currentDay  = currentDay;
    g_state.claimedMask = claimedMask;
    for (int i = 0; i < kDays; ++i) g_state.iconId[i] = 0;
    for (int i = 0; i < count; ++i) {
        int id = packet->Decode<int32_t>();
        if (i < kDays) g_state.iconId[i] = id;
    }
    for (int i = 0; i < kDays; ++i) g_state.tip[i].clear();
    for (int i = 0; i < count; ++i) {
        std::string s = DecodePacketString(packet);
        if (i < kDays) g_state.tip[i] = std::move(s);
    }
    g_state.ready = true;

    CUIDailyCheckin* w = EnsureWindow();
    if (w) w->InvalidateRect(nullptr);

    if (justClaimed >= 1 && justClaimed <= kDays) {
        play_ui_sound(L"BtMouseClick");
        char buf[160];
        _snprintf(buf, sizeof(buf),
                  "Daily Check-In\r\n\r\nDay %d reward claimed!\r\nCheck your inventory.",
                  justClaimed);
        buf[sizeof(buf) - 1] = 0;
        try {
            ZXString<char> zmsg(buf);
            CUtilDlg_Notice(zmsg, nullptr, nullptr, 0, 0);
        } catch (...) {}
    }
}

void HandleServerPacket(CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != kDailyCheckinOpcode) {
        return;
    }
    packet->Decode<uint16_t>();
    const uint8_t respType = packet->Decode<uint8_t>();
    if (respType == kResp_Snapshot) {
        HandleSnapshotCompat(packet);
    }
}

} // namespace DailyCheckin

void AttachDailyCheckinMod() {
    // No extra hooks; PacketDispatcher handles recv opcode 0x17C.
}

void DailyCheckin_RequestOpen() {
    // Same TU as SendReq_Open (namespace DailyCheckin).
    COutPacket o(0x11A);
    o.Encode1(0); // kReq_Open
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (sock) {
        reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket)(sock, o);
    }
}

void DailyCheckin_Close() {
    if (DailyCheckin::CUIDailyCheckin::ms_pInstance) {
        DailyCheckin::CUIDailyCheckin::ms_pInstance->Destroy();
    }
}

void DailyCheckin_Toggle() {
    if (DailyCheckin_IsOpen()) {
        DailyCheckin_Close();
    } else {
        DailyCheckin_RequestOpen();
    }
}

bool DailyCheckin_IsOpen() {
    return DailyCheckin::CUIDailyCheckin::ms_pInstance != nullptr;
}
