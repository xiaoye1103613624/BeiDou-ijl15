// ============================================================
// miraclecube.cpp -- Phase10 MiracleCube / HyperMiracleCube result UI.
// LP 0x17A. ALWAYS draws an opaque filled panel (WZ backgrnd optional).
// ============================================================

#include "stdafx.h"
#include "MiracleCubeApi.h"
#include "ClientAddresses.h"
#include "wvs/packet_legacy.h"
#include "wvs/Packet.h"
#include "wvs/wnd.h"
#include "wvs/util.h"
#include "wvs/iteminfo.h"
#include "ztl/ztl.h"
#include "../fusionanvil/ItemOptionTipApi.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace MiracleCube {

static constexpr uintptr_t kAddr_play_ui_sound = 0x00989588;
static constexpr uintptr_t kAddr_get_basic_font = 0x0098A707;
static constexpr uintptr_t kAddr_ProcessBasicUIKey = 0x00A07431;
static constexpr uintptr_t kAddr_CWvsContext_Instance = 0x00BE7918;

static auto play_ui_sound =
    reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto get_basic_font =
    reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

static void* GetWvsContext() {
    return *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
}

static constexpr int kWndW = 280;
static constexpr int kWndH = 250;

static constexpr unsigned int kPanelBorder = 0xFFB4C8FFu;
static constexpr unsigned int kPanelFill = 0xF0141A24u;
static constexpr unsigned int kPanelInner = 0xF01C2848u;
static constexpr unsigned int kTitleBar = 0xF0283C6Eu;

struct CubeResultState {
    unsigned char success = 0;
    int cubeItemId = 0;
    unsigned char uiKind = 0;
    unsigned char grade = 0;
    int pot1 = 0, pot2 = 0, pot3 = 0;
    int equipItemId = 0;
};
static CubeResultState g_state{};

static bool PopupDisabledByConfig() {
    // Default DisablePopup=1: no UI; cube result is server dropMessage (self only).
    // Set [miraclecube] DisablePopup=0 in config.ini to re-enable popup for debug.
    char buf[16];
    ZeroMemory(buf, sizeof(buf));
    GetPrivateProfileStringA("miraclecube", "DisablePopup", "1", buf, sizeof(buf), "config.ini");
    return buf[0] == '1' || buf[0] == '\0'
            || _stricmp(buf, "true") == 0 || _stricmp(buf, "yes") == 0;
}

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[512] = {};
    if (!sGbk || !*sGbk) {
        return Ztl_bstr_t(L"");
    }
    if (MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static int SafeEquipReqLevel(int itemId) {
    if (itemId <= 0) {
        return 1;
    }
    try {
        IWzPropertyPtr pItem = CItemInfo::GetInstance()->GetItemInfo(itemId);
        if (!pItem) {
            return 1;
        }
        Ztl_variant_t vInfo;
        if (FAILED(pItem->get_item(const_cast<wchar_t*>(L"info"), &vInfo))) {
            return 1;
        }
        IWzPropertyPtr pInfo(vInfo.GetUnknown(false, false));
        if (!pInfo) {
            return 1;
        }
        Ztl_variant_t vReq;
        if (FAILED(pInfo->get_item(const_cast<wchar_t*>(L"reqLevel"), &vReq))) {
            return 1;
        }
        int req = get_int32(vReq, 1);
        return req < 1 ? 1 : req;
    } catch (...) {
        return 1;
    }
}

class CUIMiracleCube : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIMiracleCube* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    IWzCanvasPtr m_pBg;
    IWzCanvasPtr m_pBtOkN;
    IWzCanvasPtr m_pBtOkP;
    IWzFontPtr m_pFont;
    RECT m_rcOk{};
    int m_nPressed = 0;
    int m_nWndW = kWndW;
    int m_nWndH = kWndH;
    int m_bHyperUi = 0;
    int m_bBgUsable = 0;

    CUIMiracleCube(int bHyperUi);
    virtual ~CUIMiracleCube() override {
        if (ms_pInstance == this) {
            ms_pInstance = nullptr;
        }
    }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int OnMouseMove(int /*rx*/, int /*ry*/) override { return 0; }
    virtual int OnMouseWheel(int, int, int) override { return 1; }
    virtual void OnDestroy() override;
    virtual void Update() override { InvalidateRect(nullptr); }
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

    static IWzCanvasPtr LoadSprite(const wchar_t* p) {
        IWzCanvasPtr c;
        try {
            c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p)));
        } catch (...) {
        }
        return c;
    }

    static int CanvasW(IWzCanvasPtr src) {
        if (!src) {
            return 0;
        }
        try {
            UINT uw = 0;
            src->get_width(&uw);
            return static_cast<int>(uw);
        } catch (...) {
            return 0;
        }
    }
    static int CanvasH(IWzCanvasPtr src) {
        if (!src) {
            return 0;
        }
        try {
            UINT uh = 0;
            src->get_height(&uh);
            return static_cast<int>(uh);
        } catch (...) {
            return 0;
        }
    }

    static void BlitAt(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src) {
            try {
                dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0);
            } catch (...) {
            }
        }
    }

    static void DrawSolidPanel(IWzCanvasPtr dst, int w, int h) {
        if (!dst) {
            return;
        }
        try {
            dst->DrawRectangle(0, 0, w, h, kPanelBorder);
            dst->DrawRectangle(2, 2, w - 4, h - 4, kPanelFill);
            dst->DrawRectangle(6, 6, w - 12, h - 12, kPanelInner);
            dst->DrawRectangle(10, 10, w - 20, 30, kTitleBar);
            dst->DrawRectangle(10, 40, w - 20, 1, 0x80B4C8FFu);
        } catch (...) {
        }
    }
};

// GBK grade names: putong / xiyou / shishi / dute / chuanshuo / wu
static const char* GradeNameGbk(unsigned char g) {
    switch (g) {
    case 1: return "\xC6\xD5\xCD\xA8";
    case 2: return "\xCF\xA1\xD3\xD0";
    case 3: return "\xCA\xB7\xCA\xAB";
    case 4: return "\xB6\xC0\xCC\xD8";
    case 5: return "\xB4\xAB\xCB\xB5";
    default: return "\xCE\xDE";
    }
}

static unsigned char ResolveGrade() {
    unsigned char g = g_state.grade;
    if (g >= 1 && g <= 5) {
        return g;
    }
    g = ItemOptionTip_InferGrade(g_state.pot1, g_state.pot2, g_state.pot3);
    return g >= 1 ? g : 0;
}

static std::string FirstLineOnly(const std::string& s) {
    const size_t n = s.find('\n');
    if (n == std::string::npos) {
        return s;
    }
    return s.substr(0, n);
}

static std::string FormatPotLineGbk(int slot, int optionId, int potLevel) {
    char head[64];
    // qianneng = C7 B1 C4 DC  (NOT qianneng-typo qian=C7 B0)
    sprintf_s(head, "\xC7\xB1\xC4\xDC%d : ", slot);
    if (optionId <= 0) {
        return std::string(head) + "\xA3\xA8\xCE\xDE\xA3\xA9";
    }
    std::string tip = ItemOptionTip_FormatGbk(optionId, potLevel);
    tip = FirstLineOnly(tip);
    if (!tip.empty()) {
        return std::string(head) + tip;
    }
    char idbuf[48];
    sprintf_s(idbuf, "#%d", optionId);
    return std::string(head) + idbuf;
}

CUIMiracleCube::CUIMiracleCube(int bHyperUi)
    : m_bHyperUi(bHyperUi ? 1 : 0),
      m_nWndW(kWndW),
      m_nWndH(kWndH) {
    ms_pInstance = this;
    const int sw = get_screen_width();
    const int sh = get_screen_height();
    int x = (sw - m_nWndW) / 2;
    int y = (sh - m_nWndH) / 2;
    if (y < 48) {
        y = 48;
    }
    if (x < 8) {
        x = 8;
    }
    CreateWnd(x, y, m_nWndW, m_nWndH, 14, 1, nullptr, 1);
    play_ui_sound(L"MenuUp");

    if (bHyperUi) {
        m_pBg = LoadSprite(L"UI/UIWindow2.img/HyperMiracleCube/backgrnd");
        if (!m_pBg) {
            m_pBg = LoadSprite(L"UI/UIWindow2.img/HyperMiracleCube/backgrnd/0");
        }
        m_pBtOkN = LoadSprite(L"UI/UIWindow2.img/HyperMiracleCube/BtOk/normal/0");
        if (!m_pBtOkN) {
            m_pBtOkN = LoadSprite(L"UI/UIWindow2.img/HyperMiracleCube/BtOk/0");
        }
        m_pBtOkP = LoadSprite(L"UI/UIWindow2.img/HyperMiracleCube/BtOk/pressed/0");
    } else {
        m_pBg = LoadSprite(L"UI/UIWindow2.img/MiracleCube/backgrnd");
        if (!m_pBg) {
            m_pBg = LoadSprite(L"UI/UIWindow2.img/MiracleCube/backgrnd/0");
        }
        m_pBtOkN = LoadSprite(L"UI/UIWindow2.img/MiracleCube/BtOk/normal/0");
        if (!m_pBtOkN) {
            m_pBtOkN = LoadSprite(L"UI/UIWindow2.img/MiracleCube/BtOk/0");
        }
        m_pBtOkP = LoadSprite(L"UI/UIWindow2.img/MiracleCube/BtOk/pressed/0");
    }
    m_bBgUsable = (CanvasW(m_pBg) >= 50 && CanvasH(m_pBg) >= 50) ? 1 : 0;
    if (!m_bBgUsable) {
        m_pBg = nullptr;
    }

    if (!m_pBtOkN) {
        m_pBtOkN = LoadSprite(L"UI/UIWindow.img/UtilDlgEx/BtOK/normal/0");
        m_pBtOkP = LoadSprite(L"UI/UIWindow.img/UtilDlgEx/BtOK/pressed/0");
    }

    m_rcOk = {m_nWndW / 2 - 40, m_nWndH - 42, m_nWndW / 2 + 40, m_nWndH - 14};
    if (m_pBtOkN) {
        const int bw = CanvasW(m_pBtOkN);
        const int bh = CanvasH(m_pBtOkN);
        if (bw > 0 && bh > 0) {
            const int lx = m_nWndW / 2 - bw / 2;
            const int ty = m_nWndH - bh - 12;
            m_rcOk = {lx, ty, lx + bw, ty + bh};
        }
    }

    m_pFont = nullptr;
    try {
        get_basic_font(std::addressof(m_pFont), 0);
    } catch (...) {
    }
}

void CUIMiracleCube::OnDestroy() {
    m_pBg = nullptr;
    m_pBtOkN = nullptr;
    m_pBtOkP = nullptr;
    m_pFont = nullptr;
    if (ms_pInstance == this) {
        ms_pInstance = nullptr;
    }
    CWnd::OnDestroy();
}

void CUIMiracleCube::Draw(const RECT* pRect) {
    CWnd::Draw(pRect);
    IWzCanvasPtr pCanvas = GetCanvas();
    if (!pCanvas) {
        return;
    }

    DrawSolidPanel(pCanvas, m_nWndW, m_nWndH);
    if (m_bBgUsable && m_pBg) {
        BlitAt(pCanvas, m_pBg, 0, 0);
    }

    const int potLevel = ItemOptionTip_PotLevelFromReq(SafeEquipReqLevel(g_state.equipItemId));
    std::vector<std::string> lines;
    if (g_state.success) {
        // mofang chong sui chenggong
        lines.emplace_back("\xC4\xA7\xB7\xBD\xD6\xD8\xCB\xE6\xB3\xC9\xB9\xA6");
        char gradeLine[96];
        // pinjie :
        sprintf_s(gradeLine, "\xC6\xB7\xBD\xD7 : %s", GradeNameGbk(ResolveGrade()));
        lines.emplace_back(gradeLine);
        lines.push_back(FormatPotLineGbk(1, g_state.pot1, potLevel));
        lines.push_back(FormatPotLineGbk(2, g_state.pot2, potLevel));
        if (g_state.pot3 != 0) {
            lines.push_back(FormatPotLineGbk(3, g_state.pot3, potLevel));
        }
        char idLine[64];
        sprintf_s(idLine, "Cube %d", g_state.cubeItemId);
        lines.emplace_back(idLine);
    } else {
        lines.emplace_back("\xC4\xA7\xB7\xBD\xCA\xB1\xD3\xC3\xCA\xA7\xB0\xDC");
        char idLine[64];
        sprintf_s(idLine, "Cube %d", g_state.cubeItemId);
        lines.emplace_back(idLine);
    }

    if (m_pFont) {
        try {
            int y = 16;
            for (const auto& line : lines) {
                if (line.empty()) {
                    continue;
                }
                pCanvas->DrawTextA(16, y, GbkToBstr(line.c_str()), m_pFont, Ztl_variant_t(),
                                   Ztl_variant_t());
                y += 20;
                if (y > m_nWndH - 56) {
                    break;
                }
            }
            pCanvas->DrawTextA(m_nWndW - 28, m_nWndH - 18, Ztl_bstr_t(L"v2"), m_pFont,
                               Ztl_variant_t(), Ztl_variant_t());
        } catch (...) {
        }
    }

    IWzCanvasPtr btn = (m_nPressed && m_pBtOkP) ? m_pBtOkP : m_pBtOkN;
    if (btn) {
        BlitAt(pCanvas, btn, m_rcOk.left, m_rcOk.top);
    } else {
        try {
            const int bw = m_rcOk.right - m_rcOk.left;
            const int bh = m_rcOk.bottom - m_rcOk.top;
            pCanvas->DrawRectangle(m_rcOk.left, m_rcOk.top, bw, bh, 0xFF3A5080u);
            pCanvas->DrawRectangle(m_rcOk.left + 1, m_rcOk.top + 1, bw - 2, bh - 2, 0xFF243050u);
            if (m_pFont) {
                pCanvas->DrawTextA(m_rcOk.left + 10, m_rcOk.top + 4,
                                   GbkToBstr("\xC8\xB7\xB6\xA8"), m_pFont, Ztl_variant_t(),
                                   Ztl_variant_t());
            }
        } catch (...) {
        }
    }
}

void CUIMiracleCube::OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) {
    POINT pt{rx, ry};
    if (msg == WM_LBUTTONDOWN) {
        if (PtInRect(&m_rcOk, pt)) {
            m_nPressed = 1;
            InvalidateRect(nullptr);
        }
    } else if (msg == WM_LBUTTONUP) {
        if (m_nPressed && PtInRect(&m_rcOk, pt)) {
            play_ui_sound(L"BtMouseClick");
            Destroy();
            return;
        }
        m_nPressed = 0;
        InvalidateRect(nullptr);
    }
    CWnd::OnMouseButton(msg, wParam, rx, ry);
}

static void OpenOrRefresh() {
    if (PopupDisabledByConfig()) {
        return;
    }
    const int bHyperUi = (g_state.uiKind != 0) ? 1 : 0;
    if (CUIMiracleCube::ms_pInstance) {
        CUIMiracleCube::ms_pInstance->Destroy();
        CUIMiracleCube::ms_pInstance = nullptr;
    }
    new CUIMiracleCube(bHyperUi);
}

void HandleServerPacket(CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    // Consume legacy 0x17A; do not open UI when DisablePopup=1 (default).
    packet->Decode<uint16_t>();
    g_state.success = packet->Decode<uint8_t>();
    g_state.cubeItemId = packet->Decode<int32_t>();
    g_state.uiKind = packet->Decode<uint8_t>();
    g_state.grade = packet->Decode<uint8_t>();
    g_state.pot1 = packet->Decode<int32_t>();
    g_state.pot2 = packet->Decode<int32_t>();
    g_state.pot3 = packet->Decode<int32_t>();
    g_state.equipItemId = 0;
    if (packet->CanRead(sizeof(int32_t))) {
        g_state.equipItemId = packet->Decode<int32_t>();
    }
    if (PopupDisabledByConfig()) {
        return;
    }
    if (g_state.grade < 1 || g_state.grade > 5) {
        const unsigned char inferred =
                ItemOptionTip_InferGrade(g_state.pot1, g_state.pot2, g_state.pot3);
        if (inferred >= 1) {
            g_state.grade = inferred;
        }
    }
    OpenOrRefresh();
}

void AttachMiracleCubeMod() {
}

} // namespace MiracleCube
