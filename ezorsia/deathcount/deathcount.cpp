// deathcount.cpp — expedition death-count HUD (GMS DEATH COUNT panel)
//
// IDA (this v83 EXE, image base 0x400000):
//   CClientSocket::ProcessPacket   0x004965F1  already hooked by PacketDispatcher
//   SendOpcode.SET_FIELD           0x7D        observe only — never consume
//   CWndMan singleton              0x00BEC20C  TSingleton; m_pOrgWindow +0xDC (LT origin)
//   IWzGr2D* get_gr                0x00BF14EC
//   IWzResMan* get_rm              0x00BF14E8
// No game-function Detour. Packet record + main-thread tick only.

#include "stdafx.h"
#include "DeathCountApi.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wndman.h"
#include "compat/wvs/Packet.h"
#include "ztl/ztl.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

constexpr int kPanelW = 89;
constexpr int kPanelH = 56;
constexpr double kMoldTensCX = 59.5;
constexpr double kMoldOnesCX = 69.5;
constexpr double kMoldCY = 37.0;
constexpr double kDigitCX[10] = {5.5, 8.5, 5.5, 4.5, 5.5, 5.5, 5.5, 4.5, 5.5, 5.5};
constexpr double kDigitCY[10] = {8.0, 8.0, 8.0, 8.0, 7.0, 8.0, 8.0, 7.5, 8.0, 8.0};
constexpr int kPanelXOffset = -217;
constexpr int kPanelY = 30;
constexpr int kPanelZ = 1400;
constexpr int kMaxValue = 99;
constexpr unsigned int kCanvasClear = 0x00FFFFFFu;
constexpr uintptr_t kWzFontCreate = 0x0046341A;
constexpr int kOff_CWndManOrgWindow = 0xDC;

int g_nValue = -1;
bool g_bDirty = false;
bool g_bDisabled = false;
int g_nArtState = -1; // -1 unprobed, 0 pending/absent, 1 loaded
int g_nDrawn = -2;
int g_nLayerX = -1;
int g_nLayerY = -1;

IWzCanvasPtr g_pPanel;
IWzCanvasPtr g_pSprBackgrd;
IWzCanvasPtr g_apSprDigit[10];
IWzGr2DLayerPtr g_pLayer;
IWzFontPtr g_fontDigit;
IWzFontPtr g_fontLabel;

IWzCanvasPtr LoadSprite(const wchar_t* path) {
    IWzCanvasPtr c;
    try {
        c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(path), vtEmpty, vtEmpty));
    } catch (...) {
    }
    return c;
}

bool CreateFontFace(IWzFontPtr& font, const wchar_t* face, unsigned long color, unsigned long size) {
    if (font) {
        return true;
    }
    PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
    if (!font) {
        return false;
    }
    Ztl_variant_t vStyle = L"";
    auto fnCreate = reinterpret_cast<HRESULT(__thiscall*)(
            IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&)>(kWzFontCreate);
    if (SUCCEEDED(fnCreate(font, Ztl_bstr_t(face), size, color, vStyle))) {
        return true;
    }
    font = nullptr;
    return false;
}

void Blit(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
    if (!dst || !src) {
        return;
    }
    try {
        dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, vtEmpty);
    } catch (...) {
    }
}

IWzVector2DPtr ScreenOriginLT() {
    CWndMan* man = CWndMan::GetInstance();
    if (!man) {
        return nullptr;
    }
    try {
        return *reinterpret_cast<IWzVector2DPtr*>(reinterpret_cast<char*>(man) + kOff_CWndManOrgWindow);
    } catch (...) {
        return nullptr;
    }
}

void PanelPos(int* x, int* y) {
    *x = get_screen_width() / 2 + kPanelXOffset;
    *y = kPanelY;
}

bool LoadArt() {
    if (g_nArtState == 0) {
        return false;
    }
    if (g_pSprBackgrd) {
        return true;
    }
    IWzCanvasPtr pBg = LoadSprite(L"UI/DeathCount.img/backgrd");
    if (!pBg) {
        g_nArtState = 0;
        std::cout << "[deathcount] art pending (UI/DeathCount.img/backgrd absent) — fallback HUD"
                  << std::endl;
        return false;
    }
    for (int i = 0; i < 10; ++i) {
        wchar_t path[96];
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"UI/DeathCount.img/number/%d", i);
        g_apSprDigit[i] = LoadSprite(path);
    }
    g_pSprBackgrd = pBg;
    g_nArtState = 1;
    return true;
}

void BlitDigit(int nDigit, double dWellCX, double dWellCY) {
    if (nDigit < 0 || nDigit > 9 || !g_apSprDigit[nDigit]) {
        return;
    }
    const int x = static_cast<int>(std::lround(dWellCX - kDigitCX[nDigit]));
    const int y = static_cast<int>(std::lround(dWellCY - kDigitCY[nDigit]));
    Blit(g_pPanel, g_apSprDigit[nDigit], x, y);
}

void ComposeFallback(int nShown) {
    if (!g_pPanel) {
        return;
    }
    try {
        g_pPanel->DrawRectangle(0, 0, kPanelW, kPanelH, kCanvasClear);
        g_pPanel->DrawRectangle(0, 0, kPanelW, kPanelH, 0xCC101018);
        g_pPanel->DrawRectangle(1, 1, kPanelW - 2, 1, 0xFFE8C840);
        g_pPanel->DrawRectangle(1, kPanelH - 2, kPanelW - 2, 1, 0xFFE8C840);
    } catch (...) {
    }
    CreateFontFace(g_fontLabel, L"Arial", 0xFFE8C840, 11);
    CreateFontFace(g_fontDigit, L"Arial", 0xFFFFFFFF, 16);
    if (g_fontLabel) {
        try {
            g_pPanel->DrawTextA(8, 6, Ztl_bstr_t(L"DEATH"), g_fontLabel, vtEmpty, vtEmpty);
        } catch (...) {
        }
    }
    if (g_fontDigit && nShown >= 0) {
        wchar_t buf[8];
        _snwprintf_s(buf, _TRUNCATE, L"%02d", nShown);
        try {
            g_pPanel->DrawTextA(52, 28, Ztl_bstr_t(buf), g_fontDigit, vtEmpty, vtEmpty);
        } catch (...) {
        }
    }
}

void Compose(int nShown) {
    if (!g_pPanel) {
        return;
    }
    if (g_nArtState != 1) {
        ComposeFallback(nShown);
        return;
    }
    try {
        g_pPanel->DrawRectangle(0, 0, kPanelW, kPanelH, kCanvasClear);
    } catch (...) {
    }
    Blit(g_pPanel, g_pSprBackgrd, 0, 0);
    if (nShown < 0) {
        return;
    }
    BlitDigit((nShown / 10) % 10, kMoldTensCX, kMoldCY);
    BlitDigit(nShown % 10, kMoldOnesCX, kMoldCY);
}

void Release(const char* why) {
    (void)why;
    if (g_pLayer) {
        try {
            g_pLayer->visible = 0;
        } catch (...) {
        }
        g_pLayer = nullptr;
    }
    g_pPanel = nullptr;
    g_nDrawn = -2;
    g_nLayerX = -1;
    g_nLayerY = -1;
}

bool Build() {
    if (g_pLayer) {
        return true;
    }
    LoadArt(); // absence is supported — fallback canvas

    IWzVector2DPtr pOrigin = ScreenOriginLT();
    if (!pOrigin) {
        return false;
    }
    auto gr = get_gr();
    if (!gr) {
        return false;
    }

    IWzCanvasPtr pPanel;
    PcCreateObject<IWzCanvasPtr>(L"Canvas", pPanel, nullptr);
    if (!pPanel) {
        return false;
    }
    try {
        pPanel->Create(kPanelW, kPanelH, vtMissing, vtMissing);
        pPanel->DrawRectangle(0, 0, kPanelW, kPanelH, kCanvasClear);
    } catch (...) {
        return false;
    }

    int x = 0, y = 0;
    PanelPos(&x, &y);
    IWzGr2DLayerPtr pLayer;
    try {
        pLayer = gr->CreateLayer(
                x, y,
                static_cast<unsigned>(kPanelW),
                static_cast<unsigned>(kPanelH),
                kPanelZ,
                static_cast<IUnknown*>(pPanel.GetInterfacePtr()),
                vtMissing);
    } catch (...) {
        return false;
    }
    if (!pLayer) {
        return false;
    }
    try {
        pLayer->origin = static_cast<IUnknown*>(pOrigin.GetInterfacePtr());
        pLayer->color = 0xFFFFFFFF;
        pLayer->visible = 0;
        pLayer->RelMove(x, y);
    } catch (...) {
    }
    g_pPanel = pPanel;
    g_pLayer = pLayer;
    g_nDrawn = -2;
    g_nLayerX = x;
    g_nLayerY = y;
    return true;
}

void Tick() {
    if (!g_bDirty && !g_pLayer) {
        return;
    }
    if (g_nValue < 0) {
        Release("value < 0");
        g_bDirty = false;
        return;
    }
    if (!Build()) {
        return;
    }
    int x = 0, y = 0;
    PanelPos(&x, &y);
    if (x != g_nLayerX || y != g_nLayerY) {
        try {
            g_pLayer->RelMove(x, y);
        } catch (...) {
        }
        g_nLayerX = x;
        g_nLayerY = y;
    }
    const int nShown = (g_nValue > kMaxValue) ? kMaxValue : g_nValue;
    if (nShown != g_nDrawn) {
        Compose(nShown);
        g_nDrawn = nShown;
    }
    try {
        g_pLayer->visible = 1;
    } catch (...) {
    }
    g_bDirty = false;
}

} // namespace

void DeathCount_OnPacket(CompatInPacket* packet) {
    if (g_bDisabled || !packet) {
        return;
    }
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != kExpedDeathCountOpcode) {
        return;
    }
    if (!packet->CanRead(2 + 4)) {
        return;
    }
    packet->Decode<uint16_t>();
    const int nValue = packet->Decode<int32_t>();
    if (nValue == g_nValue) {
        return;
    }
    g_nValue = nValue;
    g_bDirty = true;
}

void DeathCount_OnFieldChange() {
    if (g_nValue < 0 && !g_pLayer) {
        return;
    }
    g_nValue = -1;
    g_bDirty = true;
    if (g_pLayer) {
        try {
            g_pLayer->visible = 0;
        } catch (...) {
        }
    }
}

void DeathCount_OnClientTick() {
    if (g_bDisabled) {
        return;
    }
    try {
        Tick();
    } catch (...) {
        g_bDisabled = true;
        Release("fault");
    }
}
