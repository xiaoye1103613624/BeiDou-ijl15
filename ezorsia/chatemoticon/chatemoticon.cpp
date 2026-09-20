// chatemoticon.cpp — Kaentake-style overhead chat emoticon, ported to the ezorsia
// (ijl15) framework.
//
// Full closed-loop client side of the "头顶动态聊天表情" feature:
//   * a persistent bottom-right "聊天表情" button (CWnd, screen-fixed) opens a
//     picker panel (4 category tabs / 4x4 grid / scroll / close).
//   * clicking an emoticon sends COutPacket(0x11C).Encode4(id) to the server.
//   * the server broadcasts SendOpcode.CHAT_EMOTICON(0x17F) = cid + emoticonId
//     back to the same-map clients; PacketDispatcher routes it here.
//   * on receive we spawn a native wz animation layer (create_anim_layer @
//     0x0043EA3E) anchored to the target character's head vector (get_vec_ctrl
//     @ 0x004AD42B): a code-drawn speech bubble + the looping emoticon sticker.
//
// Engine addresses are GMS083 / BeiDou-client @ 0x400000 (Kaentake README
// "BeiDou client @ 0x400000", IDA-verified 2026-09-16) and match ezorsia's own
// combatpower / dailycheckin / sidetoolbar offsets.

#include "stdafx.h"
#include "ChatEmoticonApi.h"
#include "compat/ClientAddresses.h"
#include "compat/ModRegistry.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/packet_legacy.h"
#include "wvs/Packet.h"
#include "wvs/wnd.h"
#include "wvs/util.h"
#include "ztl/ztl.h"
#include "WzLib/IWzGr2DLayer.h"
#include "WzLib/IWzCanvas.h"
#include "WzLib/IWzProperty.h"
#include "WzLib/IWzVector2D.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>
#include <unordered_map>

namespace ChatEmoticon {
namespace {

// ---- BeiDou v083 client addresses (image base 0x400000) --------------------
constexpr std::uintptr_t kStatusBarInstance   = 0x00BEC208;  // shown only in-game
constexpr std::uintptr_t kUserPoolInstance     = 0x00BEBFA8;
constexpr std::uintptr_t kUserPoolGetUser      = 0x009716ED;
constexpr std::uintptr_t kClientSocketSendPacket = 0x0049637B;
constexpr std::uintptr_t kPlayUiSound          = 0x00989588;
constexpr std::uintptr_t kSetFont              = 0x0046341A;
constexpr std::uintptr_t kUserGetVecCtrl       = 0x004AD42B;
constexpr std::uintptr_t kCreateAnimLayer      = 0x0043EA3E;

constexpr unsigned short kSendOpcode = 0x11C;  // CHAT_EMOTICON (client -> server)
constexpr unsigned short kRecvOpcode = 0x17F;  // CHAT_EMOTICON (server -> client)

// ---- Picker geometry (measured off Kaentake's ChatExpression art) ----------
constexpr int kButtonSize    = 14;
constexpr int kWindowWidth   = 384;
constexpr int kWindowHeight  = 314;
constexpr int kGridLeft      = 9;
constexpr int kGridTop       = 51;
constexpr int kCellWidth     = 85;
constexpr int kCellHeight    = 60;
constexpr int kColumns       = 4;
constexpr int kVisibleRows   = 4;
constexpr int kTabLeft       = 4;
constexpr int kTabTop        = 24;
constexpr int kTabHeight     = 19;
constexpr int kTabWidth      = 34;
constexpr int kScrollLeft    = 364;
constexpr int kScrollTop     = 47;
constexpr int kScrollLength  = 253;
constexpr int kCloseLeft     = 366;
constexpr int kCloseTop      = 5;
constexpr int kCloseSize     = 14;
constexpr int kEmoteDurationMs = 3000;

using SendPacketFn = void(__thiscall*)(void*, const COutPacket&);
using PlaySoundFn  = void(__cdecl*)(const wchar_t*);

// Narrow GBK (CP936) → BSTR. Never put UTF-8 Chinese in L"..." source literals.
static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || !*sGbk) {
        return Ztl_bstr_t(L"");
    }
    if (MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static std::wstring GbkToWstring(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || !*sGbk) {
        return L"";
    }
    if (MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return L"";
    }
    return wbuf;
}
using SetFontFn    = HRESULT(__thiscall*)(
    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
using GetUserFn    = void*(__thiscall*)(void*, unsigned int);
using GetVecCtrlFn = IWzVector2DPtr*(__thiscall*)(void*, IWzVector2DPtr*);
using CreateAnimLayerFn = void**(__cdecl*)(
    void**, void*, int, void*, int, int, void*, int, int, int);

const auto send_packet = reinterpret_cast<SendPacketFn>(kClientSocketSendPacket);
const auto play_sound  = reinterpret_cast<PlaySoundFn>(kPlayUiSound);
const auto set_font    = reinterpret_cast<SetFontFn>(kSetFont);
const auto get_user    = reinterpret_cast<GetUserFn>(kUserPoolGetUser);
const auto get_vec_ctrl = reinterpret_cast<GetVecCtrlFn>(kUserGetVecCtrl);
const auto create_anim_layer = reinterpret_cast<CreateAnimLayerFn>(kCreateAnimLayer);

// ---- Emoticon id tables (must stay in sync with the server whitelist) ------
constexpr std::array<int, 100> kDynamic0{
    100001, 100002, 100003, 100004, 100005, 100006, 100007, 100008,
    100009, 100010, 101001, 101002, 101003, 101004, 101005, 101006,
    101007, 101008, 101009, 101010, 102001, 102002, 102003, 102004,
    102005, 102006, 102007, 102008, 102009, 102010, 102011, 102012,
    102013, 102014, 102015, 102016, 102017, 102018, 102019, 102020,
    102021, 102022, 102023, 102024, 103001, 103002, 103003, 103004,
    103005, 103006, 103007, 104001, 104002, 104003, 104004, 104005,
    105001, 105002, 105003, 105004, 106001, 106002, 106003, 106004,
    106005, 106006, 107001, 107002, 107003, 107004, 107005, 107006,
    108000, 108001, 108002, 108003, 108004, 108005, 108006, 108007,
    108008, 108009, 108010, 108011, 108012, 108013, 108014, 108015,
    108016, 108017, 108018, 108019, 108020, 108021, 108022, 108023,
    108024, 108027, 108025, 108026,
};

constexpr std::array<int, 47> kDynamic1{
    200001, 200002, 200003, 200004, 200005, 201001, 201002, 201003,
    201004, 202001, 202002, 202003, 202004, 203001, 203002, 203003,
    203004, 203005, 203006, 204001, 204002, 204003, 204004, 204005,
    204006, 205001, 205002, 205003, 205004, 205005, 205006, 206001,
    206002, 206003, 206004, 206005, 206006, 207001, 207002, 207003,
    207004, 207005, 207006, 208000, 208001, 208002, 208003,
};

constexpr std::array<int, 120> kDynamic2{
    300120, 300001, 300002, 300003, 300004, 300005, 300006, 300007,
    300008, 300009, 300010, 300011, 300012, 300013, 300014, 300015,
    300016, 300017, 300018, 300019, 300020, 300021, 300022, 300023,
    300024, 300025, 300026, 300027, 300028, 300029, 300030, 300031,
    300032, 300033, 300034, 300035, 300036, 300037, 300038, 300039,
    300040, 300041, 300042, 300043, 300044, 300045, 300046, 300047,
    300048, 300049, 300050, 300051, 300052, 300053, 300054, 300055,
    300056, 300057, 300058, 300059, 300060, 300061, 300062, 300063,
    300064, 300065, 300066, 300067, 300068, 300069, 300070, 300071,
    300072, 300073, 300074, 300075, 300076, 300077, 300078, 300079,
    300080, 300081, 300082, 300083, 300084, 300085, 300086, 300087,
    300088, 300089, 300090, 300091, 300092, 300093, 300094, 300095,
    300096, 300097, 300098, 300099, 300100, 300101, 300102, 300103,
    300104, 300105, 300106, 300107, 300108, 300109, 300110, 300111,
    300112, 300113, 300114, 300115, 300116, 300117, 300118, 300119,
};

constexpr std::array<int, 3> kDynamic3{301000, 301001, 301002};

struct IdView {
    const int* data = nullptr;
    int size = 0;
};

IdView CategoryIds(int category) {
    switch (category) {
        case 0: return {kDynamic0.data(), static_cast<int>(kDynamic0.size())};
        case 1: return {kDynamic1.data(), static_cast<int>(kDynamic1.size())};
        case 2: return {kDynamic2.data(), static_cast<int>(kDynamic2.size())};
        case 3: return {kDynamic3.data(), static_cast<int>(kDynamic3.size())};
        default: return {};
    }
}

int CategoryForId(int id) {
    if (id >= 100000 && id < 200000) return 0;
    if (id >= 200000 && id < 300000) return 1;
    if (id >= 300001 && id <= 300120) return 2;
    if (id >= 301000 && id <= 301002) return 3;
    return -1;
}

bool IsKnownId(int id) {
    const int category = CategoryForId(id);
    const IdView ids = CategoryIds(category);
    for (int i = 0; i < ids.size; ++i) {
        if (ids.data[i] == id) return true;
    }
    return false;
}

void SendRequest(int id);
void TogglePicker();

// ---- Small drawing helpers (mirrors dailycheckin's rounded-rect toolkit) ---
IWzCanvasPtr LoadCanvas(int category, int id, int frame = 0) {
    IWzCanvasPtr canvas;
    if (category < 0 || category > 3 || !IsKnownId(id)) return canvas;
    wchar_t path[160]{};
    std::swprintf(path, _countof(path),
        L"Effect/ChatEmoticon.img/Dynamic%d/%d/effect/%d",
        category, id, frame);
    try {
        canvas = get_unknown(get_rm()->GetObjectA(path));
    } catch (...) {
    }
    return canvas;
}

IWzCanvasPtr LoadUiCanvas(const wchar_t* path) {
    IWzCanvasPtr canvas;
    if (!path || !get_rm()) return canvas;
    try {
        canvas = get_unknown(get_rm()->GetObjectA(path));
    } catch (...) {
    }
    return canvas;
}

void BlitAt(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
    if (dst && src) {
        try {
            dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
        } catch (...) {
        }
    }
}

void BlitCentered(IWzCanvasPtr dst, IWzCanvasPtr src, int left, int top,
                  int width, int height) {
    if (dst && src) {
        try {
            const int x = left + (width - static_cast<int>(src->width)) / 2;
            const int y = top + (height - static_cast<int>(src->height)) / 2;
            dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
        } catch (...) {
        }
    }
}

static void Fill(IWzCanvasPtr c, const RECT& rc, unsigned int color) {
    if (!c) return;
    try {
        c->DrawRectangle(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, color);
    } catch (...) {
    }
}

static void FillRounded(IWzCanvasPtr c, const RECT& rc, unsigned int color, int r) {
    if (!c) return;
    int x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    if (r < 1 || w < 2 * r || h < 2 * r) {
        Fill(c, rc, color);
        return;
    }
    try {
        for (int i = 0; i < r; ++i) {
            int in = r - 1 - i;
            c->DrawRectangle(x + in, y + i, w - 2 * in, 1, color);
        }
        c->DrawRectangle(x, y + r, w, h - 2 * r, color);
        for (int i = 0; i < r; ++i) {
            int in = r - 1 - i;
            c->DrawRectangle(x + in, y + h - 1 - i, w - 2 * in, 1, color);
        }
    } catch (...) {
    }
}

static void StrokeRounded(IWzCanvasPtr c, const RECT& rc, unsigned int color,
                          int r, int t) {
    if (!c) return;
    int x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    if (r < 1 || w < 2 * r || h < 2 * r) {
        Fill(c, rc, color);
        return;
    }
    try {
        c->DrawRectangle(x + r, y, w - 2 * r, t, color);
        c->DrawRectangle(x + r, y + h - t, w - 2 * r, t, color);
        c->DrawRectangle(x, y + r, t, h - 2 * r, color);
        c->DrawRectangle(x + w - t, y + r, t, h - 2 * r, color);
        for (int i = 0; i < r; ++i) {
            int in = r - 1 - i;
            c->DrawRectangle(x + in, y + i, t, t, color);
            c->DrawRectangle(x + w - t - in, y + i, t, t, color);
            c->DrawRectangle(x + in, y + h - t - i, t, t, color);
            c->DrawRectangle(x + w - t - in, y + h - t - i, t, t, color);
        }
    } catch (...) {
    }
}

bool MakeFont(IWzFontPtr& result, unsigned long color, int size) {
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", result, nullptr);
        if (!result) return false;
        return SUCCEEDED(set_font(result, L"Dotum", size, color, Ztl_variant_t(L"B")));
    } catch (...) {
        return false;
    }
}

// ===========================================================================
// Persistent bottom-right launcher button.
// ===========================================================================
class CUIChatEmoticonButton final : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIChatEmoticonButton* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    CUIChatEmoticonButton(int left, int top) {
        ms_pInstance = this;
        MakeFont(m_font, 0xFFFFFFFF, 11);
        CWnd::CreateWnd(left, top, kButtonSize, kButtonSize, 31, 1, nullptr, 0);
    }

    ~CUIChatEmoticonButton() override {
        if (ms_pInstance == this) ms_pInstance = nullptr;
    }

    void Draw(const RECT* rect) override {
        CWnd::Draw(rect);
        IWzCanvasPtr canvas = GetCanvas();
        if (!canvas) return;
        const unsigned int bg = m_pressed ? 0xFF33507C
                                : (m_hover ? 0xFF4A6FA8 : 0xFF2E4A78);
        RECT rc{0, 0, kButtonSize, kButtonSize};
        FillRounded(canvas, rc, bg, 4);
        StrokeRounded(canvas, rc, 0xFF9CC0E8, 4, 1);
        if (m_font) {
            try {
                // GBK: 表
                canvas->DrawTextA(1, 1, GbkToBstr("\xB1\xED"), m_font);
            } catch (...) {
            }
        }
    }

    void OnMouseButton(unsigned int message, unsigned int, int rx, int ry) override;

    int OnMouseMove(int rx, int ry) override {
        const bool hover = rx >= 0 && ry >= 0 && rx < kButtonSize && ry < kButtonSize;
        if (hover != m_hover) {
            m_hover = hover;
            InvalidateRect(nullptr);
        }
        return 1;
    }

    void OnMouseEnter(int enter) override {
        CWnd::OnMouseEnter(enter);
        if (!enter && (m_hover || m_pressed)) {
            m_hover = false;
            m_pressed = false;
            InvalidateRect(nullptr);
        }
    }

    void OnDestroy() override {
        if (ms_pInstance == this) ms_pInstance = nullptr;
        CWnd::OnDestroy();
    }

    const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    int IsKindOf(const CRTTI* rtti) const override { return ms_RTTI.IsKindOf(rtti); }
    int OnSetFocus(int) override { return 0; }
    virtual void Update() override { InvalidateRect(nullptr); }

private:
    bool m_hover = false;
    bool m_pressed = false;
    IWzFontPtr m_font;
};

// ===========================================================================
// Picker panel.
// ===========================================================================
class CUIChatEmoticon final : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIChatEmoticon* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    CUIChatEmoticon(int left, int top) {
        ms_pInstance = this;
        m_close[0] = LoadUiCanvas(L"UI/UIWindow.img/Bag/BtClose/normal/0");
        m_close[1] = LoadUiCanvas(L"UI/UIWindow.img/Bag/BtClose/mouseOver/0");
        for (int tab = 0; tab < 4; ++tab) {
            // GBK: 表%d
            char nbuf[32]{};
            std::snprintf(nbuf, sizeof(nbuf), "\xB1\xED%d", tab + 1);
            m_tabLabel[tab] = GbkToWstring(nbuf);
        }
        MakeFont(m_font, 0xFFFFFFFF, 12);
        MakeFont(m_fontSmall, 0xFFD8E0EC, 11);
        CWnd::CreateWnd(left, top, kWindowWidth, kWindowHeight, 30, 1, nullptr, 0);
        play_sound(L"MenuUp");
    }

    ~CUIChatEmoticon() override {
        if (ms_pInstance == this) ms_pInstance = nullptr;
    }

    void Draw(const RECT* rect) override {
        CWnd::Draw(rect);
        IWzCanvasPtr canvas = GetCanvas();
        if (!canvas) return;

        // Body.
        RECT body{0, 0, kWindowWidth, kWindowHeight};
        FillRounded(canvas, body, 0xF01A2230, 8);
        StrokeRounded(canvas, body, 0xFF5A7FB5, 8, 2);

        // Title bar.
        RECT title{2, 2, kWindowWidth - 2, kTabTop - 2};
        FillRounded(canvas, title, 0xFF223047, 6);
        if (m_font) {
            try {
                // GBK: 聊天表情
                canvas->DrawTextA((kWindowWidth - 12 * 4) / 2, 4,
                    GbkToBstr("\xC1\xC4\xCC\xEC\xB1\xED\xC7\xE9"), m_font);
            } catch (...) {
            }
        }

        // Tabs.
        for (int tab = 0; tab < 4; ++tab) {
            RECT tr{kTabLeft + tab * kTabWidth, kTabTop,
                    kTabLeft + (tab + 1) * kTabWidth, kTabTop + kTabHeight};
            FillRounded(canvas, tr,
                tab == m_category ? 0xFF3C5680 : 0xFF26344A, 4);
            StrokeRounded(canvas, tr,
                tab == m_category ? 0xFF9CC0E8 : 0xFF4A6FA8, 4, 1);
            if (m_fontSmall) {
                try {
                    canvas->DrawTextA(tr.left + 10, tr.top + 3,
                        Ztl_bstr_t(m_tabLabel[tab].c_str()), m_fontSmall);
                } catch (...) {
                }
            }
        }

        // Close button.
        BlitAt(canvas, m_close[m_close_hover ? 1 : 0], kCloseLeft, kCloseTop);

        // Grid.
        const IdView ids = CategoryIds(m_category);
        const int first = m_scroll_row * kColumns;
        for (int visible = 0; visible < kColumns * kVisibleRows; ++visible) {
            const int index = first + visible;
            if (index >= ids.size) break;
            const int column = visible % kColumns;
            const int row = visible / kColumns;
            const int x = kGridLeft + column * kCellWidth;
            const int y = kGridTop + row * kCellHeight;
            if (index == m_hover_index) {
                RECT hr{x, y, x + kCellWidth, y + kCellHeight};
                StrokeRounded(canvas, hr, 0xFFB6D9EF, 3, 1);
            }
            BlitCentered(canvas, Thumbnail(m_category, ids.data[index]),
                x, y, kCellWidth, kCellHeight);
        }

        DrawScrollBar(canvas, ids.size);
    }

    void OnMouseButton(unsigned int message, unsigned int, int rx, int ry) override {
        if (message == WM_LBUTTONDOWN) {
            if (InClose(rx, ry)) {
                m_close_pressed = true;
                InvalidateRect(nullptr);
                return;
            }
            if (ry >= kTabTop && ry < kTabTop + kTabHeight) {
                const int tab = (rx - kTabLeft) / kTabWidth;
                if (rx >= kTabLeft && tab >= 0 && tab < 4) {
                    m_category = tab;
                    m_scroll_row = 0;
                    m_hover_index = -1;
                    play_sound(L"BtMouseClick");
                    InvalidateRect(nullptr);
                }
                return;
            }
            if (rx >= kScrollLeft && rx < kScrollLeft + 12 &&
                ry >= kScrollTop && ry < kScrollTop + kScrollLength) {
                const int max_row = MaxScrollRow();
                if (ry < kScrollTop + 12) {
                    m_scroll_row = (std::max)(0, m_scroll_row - 1);
                } else if (ry >= kScrollTop + kScrollLength - 12) {
                    m_scroll_row = (std::min)(max_row, m_scroll_row + 1);
                } else if (max_row > 0) {
                    const int position = ry - (kScrollTop + 12);
                    m_scroll_row = (std::max)(0, (std::min)(max_row,
                        position * max_row / (kScrollLength - 24)));
                }
                m_hover_index = -1;
                play_sound(L"BtMouseClick");
                InvalidateRect(nullptr);
                return;
            }
            const int index = HitGrid(rx, ry);
            if (index >= 0) {
                const IdView ids = CategoryIds(m_category);
                if (index < ids.size && LoadCanvas(m_category, ids.data[index])) {
                    SendRequest(ids.data[index]);
                    play_sound(L"BtMouseClick");
                    Destroy();
                }
                return;
            }
        } else if (message == WM_LBUTTONUP) {
            const bool close = m_close_pressed && InClose(rx, ry);
            m_close_pressed = false;
            if (close) {
                play_sound(L"MenuDown");
                Destroy();
                return;
            }
            InvalidateRect(nullptr);
        }
        CWnd::OnMouseButton(message, 0, rx, ry);
    }

    int OnMouseMove(int rx, int ry) override {
        const bool close = InClose(rx, ry);
        const int hover = HitGrid(rx, ry);
        if (close != m_close_hover || hover != m_hover_index) {
            m_close_hover = close;
            m_hover_index = hover;
            InvalidateRect(nullptr);
        }
        return 1;
    }

    int OnMouseWheel(int, int, int wheel) override {
        const int max_row = MaxScrollRow();
        if (wheel < 0) {
            m_scroll_row = (std::max)(0, m_scroll_row - 1);
        } else if (wheel > 0) {
            m_scroll_row = (std::min)(max_row, m_scroll_row + 1);
        }
        m_hover_index = -1;
        InvalidateRect(nullptr);
        return 1;
    }

    void OnMouseEnter(int enter) override {
        CWnd::OnMouseEnter(enter);
        if (!enter) {
            m_close_hover = false;
            m_close_pressed = false;
            m_hover_index = -1;
            InvalidateRect(nullptr);
        }
    }

    void OnKey(unsigned int key, unsigned int) override {
        if (key == VK_ESCAPE) {
            play_sound(L"MenuDown");
            Destroy();
        }
    }

    void OnDestroy() override {
        for (auto& s : m_close) s = nullptr;
        m_thumbnails.clear();
        if (ms_pInstance == this) ms_pInstance = nullptr;
        CWnd::OnDestroy();
    }

    const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    int IsKindOf(const CRTTI* rtti) const override { return ms_RTTI.IsKindOf(rtti); }
    int OnSetFocus(int) override { return 0; }
    virtual void Update() override { InvalidateRect(nullptr); }

private:
    static bool InClose(int x, int y) {
        return x >= kCloseLeft && x < kCloseLeft + kCloseSize &&
               y >= kCloseTop && y < kCloseTop + kCloseSize;
    }

    int HitGrid(int x, int y) const {
        if (x < kGridLeft || x >= kGridLeft + kColumns * kCellWidth ||
            y < kGridTop || y >= kGridTop + kVisibleRows * kCellHeight) {
            return -1;
        }
        const int column = (x - kGridLeft) / kCellWidth;
        const int row = (y - kGridTop) / kCellHeight;
        const int index = (m_scroll_row + row) * kColumns + column;
        return index < CategoryIds(m_category).size ? index : -1;
    }

    int MaxScrollRow() const {
        const IdView ids = CategoryIds(m_category);
        return (std::max)(0,
            (ids.size + kColumns - 1) / kColumns - kVisibleRows);
    }

    IWzCanvasPtr Thumbnail(int category, int id) {
        const int key = category * 1000000 + id;
        const auto found = m_thumbnails.find(key);
        if (found != m_thumbnails.end()) return found->second;
        IWzCanvasPtr canvas = LoadCanvas(category, id);
        m_thumbnails.emplace(key, canvas);
        return canvas;
    }

    void DrawScrollBar(IWzCanvasPtr canvas, int item_count) const {
        if (!canvas) return;
        // Track.
        Fill(canvas, {kScrollLeft, kScrollTop + 12, kScrollLeft + 12,
                       kScrollTop + kScrollLength - 12},
             0x55202A3C);
        // Up / down arrow glyphs (code-drawn, no Basic.img dependency).
        DrawArrow(canvas, kScrollTop, true);
        DrawArrow(canvas, kScrollTop + kScrollLength - 12, false);
        const int total_rows = (std::max)(1,
            (item_count + kColumns - 1) / kColumns);
        const int max_row = (std::max)(0, total_rows - kVisibleRows);
        const int travel = kScrollLength - 36;
        const int thumb_y = kScrollTop + 12 +
            (max_row ? travel * m_scroll_row / max_row : 0);
        FillRounded(canvas, {kScrollLeft, thumb_y, kScrollLeft + 12, thumb_y + 24},
            0xFF5A7FB5, 4);
    }

    static void DrawArrow(IWzCanvasPtr canvas, int top, bool up) {
        const int cx = kScrollLeft + 6;
        if (up) {
            for (int i = 0; i < 6; ++i)
                canvas->DrawRectangle(cx - i, top + 2 + i, 1 + 2 * i, 1, 0xFFC8D6EA);
        } else {
            for (int i = 0; i < 6; ++i)
                canvas->DrawRectangle(cx - i, top + 4 - i, 1 + 2 * i, 1, 0xFFC8D6EA);
        }
    }

    int m_category = 0;
    int m_scroll_row = 0;
    int m_hover_index = -1;
    bool m_close_hover = false;
    bool m_close_pressed = false;
    IWzCanvasPtr m_close[2];
    IWzFontPtr m_font;
    IWzFontPtr m_fontSmall;
    std::wstring m_tabLabel[4];
    std::unordered_map<int, IWzCanvasPtr> m_thumbnails;
};

void CUIChatEmoticonButton::OnMouseButton(
    unsigned int message, unsigned int, int rx, int ry) {
    const bool inside = rx >= 0 && ry >= 0 && rx < kButtonSize && ry < kButtonSize;
    if (message == WM_LBUTTONDOWN && inside) {
        m_pressed = true;
        InvalidateRect(nullptr);
        return;
    }
    if (message == WM_LBUTTONUP) {
        const bool clicked = m_pressed && inside;
        m_pressed = false;
        InvalidateRect(nullptr);
        if (clicked) {
            play_sound(L"BtMouseClick");
            TogglePicker();
        }
        return;
    }
    CWnd::OnMouseButton(message, 0, rx, ry);
}

void TogglePicker() {
    if (CUIChatEmoticon::ms_pInstance) {
        play_sound(L"MenuDown");
        CUIChatEmoticon::ms_pInstance->Destroy();
        return;
    }
    int x = (get_screen_width() - kWindowWidth) / 2;
    int y = (get_screen_height() - kWindowHeight) / 2;
    x = (std::max)(0, (std::min)(x, get_screen_width() - kWindowWidth));
    y = (std::max)(0, (std::min)(y, get_screen_height() - kWindowHeight));
    new CUIChatEmoticon(x, y);
}

// ===========================================================================
// Outbound request.
// ===========================================================================
void SendRequest(int id) {
    if (!IsKnownId(id)) return;
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (!sock) return;
    COutPacket packet(kSendOpcode);
    packet.Encode4(static_cast<unsigned int>(id));
    send_packet(sock, packet);
}

// ===========================================================================
// Overhead effect — native wz animation layers anchored to the character head.
// ===========================================================================
struct LiveEmoticon {
    IWzGr2DLayerPtr bubble;
    IWzGr2DLayerPtr sticker;
    DWORD expires = 0;
};

std::unordered_map<unsigned int, LiveEmoticon> g_live_emoticons;

void* CallCreateAnimLayer(void* node, void* position, int x, int y, int z) {
    void* raw = nullptr;
    __try {
        create_anim_layer(&raw, node, 0, position, x, y, nullptr, z, 255, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw = nullptr;
    }
    return raw;
}

IWzGr2DLayerPtr CreateEmoticonLayer(const IWzPropertyPtr& node,
                                    const IWzVector2DPtr& position, int x, int y,
                                    int z, GR2D_ANITYPE animation) {
    IWzGr2DLayerPtr result;
    if (!node || !position) return result;
    node->AddRef();
    position->AddRef();
    void* raw = CallCreateAnimLayer(
        node.GetInterfacePtr(), position.GetInterfacePtr(), x, y, z);
    result.Attach(reinterpret_cast<IWzGr2DLayer*>(raw));
    if (!result) return result;
    try {
        result->Animate(animation);
        result->visible = 1;
    } catch (...) {
        result = nullptr;
    }
    return result;
}

// Code-drawn speech bubble (replaces Kaentake's UI/UIWindow.img/ChatExpression/
// DialogBox, which we deliberately do NOT merge into UIWindow.img).
IWzCanvasPtr MakeBubbleCanvas() {
    IWzCanvasPtr c;
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", c, nullptr);
        if (!c) return nullptr;
        const int w = 92, h = 60;
        c->Create(w, h, Ztl_variant_t(), Ztl_variant_t());
        RECT body{0, 0, w, h - 10};
        FillRounded(c, body, 0xFFFCFCFC, 9);
        StrokeRounded(c, body, 0xFF6E8FC0, 9, 2);
        // Tail pointing down toward the character.
        for (int i = 0; i < 10; ++i) {
            c->DrawRectangle(w / 2 - 5 + i, h - 10 + i, 10 - 2 * i, 1, 0xFFFCFCFC);
        }
        for (int i = 0; i < 10; ++i) {
            c->DrawRectangle(w / 2 - 5 + i, h - 10 + i, 1, 1, 0xFF6E8FC0);
            c->DrawRectangle(w / 2 + 4 - i, h - 10 + i, 1, 1, 0xFF6E8FC0);
        }
    } catch (...) {
    }
    return c;
}

void SweepOverheadEffects() {
    const DWORD now = GetTickCount();
    for (auto it = g_live_emoticons.begin(); it != g_live_emoticons.end();) {
        if (static_cast<LONG>(now - it->second.expires) < 0) {
            ++it;
            continue;
        }
        try {
            if (it->second.bubble) it->second.bubble->visible = 0;
            if (it->second.sticker) it->second.sticker->visible = 0;
        } catch (...) {
        }
        it = g_live_emoticons.erase(it);
    }
}

void ShowOverheadEffect(unsigned int character_id, int emoticon_id) {
    const int category = CategoryForId(emoticon_id);
    if (category < 0 || !IsKnownId(emoticon_id)) return;
    void* user_pool = *reinterpret_cast<void**>(kUserPoolInstance);
    if (!user_pool) return;
    void* user = get_user(user_pool, character_id);
    if (!user) return;

    IWzVector2DPtr position;
    get_vec_ctrl(user, std::addressof(position));
    if (!position) return;

    try {
        IWzCanvasPtr bubble_canvas = MakeBubbleCanvas();
        if (!bubble_canvas) return;
        IWzPropertyPtr bubble_node;
        PcCreateObject<IWzPropertyPtr>(L"Property", bubble_node, nullptr);
        if (!bubble_node) return;
        bubble_node->item[L"0"] = static_cast<IUnknown*>(bubble_canvas);

        wchar_t path[160]{};
        std::swprintf(path, _countof(path),
            L"Effect/ChatEmoticon.img/Dynamic%d/%d/effect", category, emoticon_id);
        IWzPropertyPtr sticker_node = get_unknown(get_rm()->GetObjectA(path));
        if (!sticker_node) return;

        LiveEmoticon live;
        live.bubble = CreateEmoticonLayer(bubble_node, position, 0, -62, 2, GA_STOP);
        live.sticker = CreateEmoticonLayer(sticker_node, position, 5, -113, 3, GA_REPEAT);
        if (!live.bubble || !live.sticker) return;
        live.expires = GetTickCount() + kEmoteDurationMs;
        g_live_emoticons[character_id] = live;
    } catch (...) {
    }
}

bool HandlePacket(CompatInPacket* packet, unsigned short opcode) {
    if (!packet || opcode != kRecvOpcode) return false;
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != kRecvOpcode) return false;
    packet->Decode<uint16_t>();
    const unsigned int character_id = packet->Decode<uint32_t>();
    const int emoticon_id = static_cast<int>(packet->Decode<uint32_t>());
    ShowOverheadEffect(character_id, emoticon_id);
    return true;
}

// Per-frame: expire old effects + keep the launcher button alive while in-game.
void Tick() {
    SweepOverheadEffects();

    void* status_bar = *reinterpret_cast<void**>(kStatusBarInstance);
    if (!status_bar) {
        if (CUIChatEmoticonButton::ms_pInstance)
            CUIChatEmoticonButton::ms_pInstance->Destroy();
        if (CUIChatEmoticon::ms_pInstance)
            CUIChatEmoticon::ms_pInstance->Destroy();
        return;
    }
    if (!CUIChatEmoticonButton::ms_pInstance) {
        const int x = get_screen_width() - kButtonSize - 6;
        const int y = get_screen_height() - kButtonSize - 6;
        new CUIChatEmoticonButton(x, y);
    }
}

} // namespace

void RegisterModule() {
    CompatModule module{};
    module.name = "ChatEmoticon";
    module.onAttach = []() {
        PacketDispatcher::RegisterHandler(
            kRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                return HandlePacket(packet, opcode);
            });
    };
    module.onTick = []() {
        Tick();
    };
    ModRegistry::RegisterModule(std::move(module));
}

bool IsOpen() {
    return CUIChatEmoticon::ms_pInstance != nullptr;
}

void Toggle() {
    TogglePicker();
}

void Close() {
    if (CUIChatEmoticon::ms_pInstance) {
        CUIChatEmoticon::ms_pInstance->Destroy();
    }
}

} // namespace ChatEmoticon
