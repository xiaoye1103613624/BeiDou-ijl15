// coloringprism.cpp - Coloring Prism UI + protocol + drag equip

#include "stdafx.h"
#include "ColoringPrismApi.h"
#include "EquipDye.h"
#include "ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/util.h"
#include "compat/wvs/iteminfo.h"
#include "ztl/ztl.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern bool g_coloringPrismWndOpen;

namespace {

constexpr uintptr_t kAddr_play_ui_sound = 0x00989588;
constexpr uintptr_t kAddr_CUtilDlg_Notice = 0x009929DD;
constexpr uintptr_t kAddr_ProcessBasicUIKey = 0x00A07431;
constexpr uintptr_t kAddr_CWvsContext = 0x00BE7918;
constexpr uintptr_t kAddr_DraggableOnDropped = 0x004EF140;
constexpr uintptr_t kAddr_CharacterData_GetItem = 0x004282F7;
constexpr uintptr_t kAddr_TSecType_long_GetData = 0x0042873D;
constexpr uintptr_t kOffset_CharacterData_InContext = 0x20B8;

static auto play_ui_sound = reinterpret_cast<void(__cdecl*)(const wchar_t*)>(kAddr_play_ui_sound);
static auto ClientSocket_SendPacket =
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(ClientAddresses::kSendPacket);

typedef int(__cdecl* t_CUtilDlg_Notice)(ZXString<char>, const wchar_t*, void*, int, int);
static auto CUtilDlg_Notice = reinterpret_cast<t_CUtilDlg_Notice>(kAddr_CUtilDlg_Notice);

static void* GetWvsContext() { return *reinterpret_cast<void**>(kAddr_CWvsContext); }

static void SendPacket(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (sock) ClientSocket_SendPacket(sock, o);
}

static constexpr int kWndW = 300;
static constexpr int kWndH = 398;
static constexpr int kSliderMin = -100;
static constexpr int kSliderMax = 100;

static DyeHsl FromSliders(int h, int s, int l) {
    DyeHsl d;
    d.hue = h * 1.8f;
    d.sat = s * 0.007f;
    d.light = l * 0.0075f;
    return d;
}

static void ToSliders(const DyeHsl& d, int& h, int& s, int& l) {
    h = (int)std::lround(d.hue / 1.8f);
    s = (int)std::lround(d.sat / 0.007f);
    l = (int)std::lround(d.light / 0.0075f);
    if (h < kSliderMin) h = kSliderMin; if (h > kSliderMax) h = kSliderMax;
    if (s < kSliderMin) s = kSliderMin; if (s > kSliderMax) s = kSliderMax;
    if (l < kSliderMin) l = kSliderMin; if (l > kSliderMax) l = kSliderMax;
}

static bool HitRect(const RECT* rc, int x, int y) {
    return x >= rc->left && x < rc->right && y >= rc->top && y < rc->bottom;
}

struct CDraggableItem {
    MEMBER_AT(int, 0x18, m_nItemTI)
    MEMBER_AT(int, 0x1C, m_nSlotPosition)
};

using t_OnDropped = int(__thiscall*)(CDraggableItem*, IUIMsgHandler*, IUIMsgHandler*, int, int);
static auto Orig_OnDropped = reinterpret_cast<t_OnDropped>(kAddr_DraggableOnDropped);

static int GetItemIdFromSlot(int ti, int slot) {
    void* ctx = GetWvsContext();
    if (!ctx) return 0;
    void* charData = *reinterpret_cast<void**>(reinterpret_cast<char*>(ctx) + kOffset_CharacterData_InContext);
    if (!charData) return 0;
    void* item = reinterpret_cast<void*(__thiscall*)(void*, int, int)>(kAddr_CharacterData_GetItem)(charData, ti, slot);
    if (!item) return 0;
    try {
        return reinterpret_cast<int(__thiscall*)(void*)>(kAddr_TSecType_long_GetData)(
            reinterpret_cast<char*>(item) + 0xC);
    } catch (...) {
        return 0;
    }
}

static unsigned To8(float v) {
    int i = (int)(v * 255.f);
    if (i < 0) i = 0;
    if (i > 255) i = 255;
    return (unsigned)i;
}

static void DrawTrack(IWzCanvasPtr c, const RECT& rc, int val, unsigned color) {
    if (!c) return;
    try {
        c->DrawRectangle(rc.left, rc.top + 4, rc.right - rc.left, 6, 0xFF404050);
        float t = (float)(val - kSliderMin) / (float)(kSliderMax - kSliderMin);
        int x = rc.left + (int)(t * (rc.right - rc.left));
        c->DrawRectangle(x - 4, rc.top, 8, 14, color);
    } catch (...) {
    }
}

class CUIColoringPrism : public CWnd {
public:
    ZALLOC_GLOBAL
    inline static CUIColoringPrism* ms_pInstance = nullptr;
    inline static CRTTI ms_RTTI{nullptr};

    IWzCanvasPtr m_bg;
    IWzCanvasPtr m_btOk[3], m_btCancel[3], m_btClose[3], m_btOff[3];

    int m_itemId = 0;
    short m_slot = 0;
    signed char m_invType = 0;
    int m_slideH = 0, m_slideS = 0, m_slideL = 0;
    DyeHsl m_enterHsl{};
    int m_dragSlider = -1;
    int m_hoverBtn = -1;

    RECT m_rcOk{}, m_rcCancel{}, m_rcClose{}, m_rcOff{};
    RECT m_rcTrackH{}, m_rcTrackS{}, m_rcTrackL{};

    CUIColoringPrism();
    virtual ~CUIColoringPrism() override {
        if (ms_pInstance == this) ms_pInstance = nullptr;
        g_coloringPrismWndOpen = false;
    }

    virtual void Draw(const RECT* pRect) override;
    virtual void OnMouseButton(unsigned int msg, unsigned int wParam, int rx, int ry) override;
    virtual int OnMouseMove(int rx, int ry) override;
    virtual void OnDestroy() override;
    virtual void Update() override { InvalidateRect(nullptr); }
    virtual const CRTTI* GetRTTI() const override { return &ms_RTTI; }
    virtual int IsKindOf(const CRTTI* pRTTI) const override { return ms_RTTI.IsKindOf(pRTTI); }
    virtual int OnSetFocus(int) override { return 0; }
    virtual void OnKey(unsigned int wParam, unsigned int lParam) override {
        if (!(lParam & 0x80000000) && wParam == VK_ESCAPE) { CancelAndClose(); return; }
        void* ctx = GetWvsContext();
        if (ctx) {
            reinterpret_cast<int(__thiscall*)(void*, unsigned int, unsigned int)>(
                kAddr_ProcessBasicUIKey)(ctx, wParam, lParam);
        }
    }

    static IWzCanvasPtr LoadSprite(const wchar_t* p) {
        IWzCanvasPtr c;
        try { c = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(p), vtEmpty, vtEmpty)); } catch (...) {}
        return c;
    }
    static void Blit(IWzCanvasPtr dst, IWzCanvasPtr src, int x, int y) {
        if (dst && src) {
            try { dst->CopyEx(x, y, src, CANVAS_ALPHATYPE::CA_REMOVEALPHA, 0, 0, 0, 0, 0, 0, vtEmpty); } catch (...) {}
        }
    }

    void SoftRefresh() {
        if (m_itemId <= 0) return;
        EquipDye::SoftRefreshPreview(m_itemId, FromSliders(m_slideH, m_slideS, m_slideL));
    }

    void Hint(const char* gbk) {
        ZXString<char> s;
        s = gbk;
        CUtilDlg_Notice(s, nullptr, nullptr, 0, 0);
    }

    bool AcceptItem(int itemId, int invType, short slot) {
        if (!EquipDye::IsDyeableCategory(itemId)) {
            Hint("\xBD\xF6\xD6\xA7\xB3\xD6\xCF\xD4\xC9\xAB\xC9\xCF\xD2\xC2/\xCC\xD7\xB7\xFE/\xBF\xE3/\xC5\xFB\xB7\xE7/\xCF\xD6\xBD\xF0\xCE\xE4\xC6\xF7");
            return false;
        }
        if (slot >= 0) {
            Hint("\xC7\xEB\xB4\xD3\xD2\xD1\xB4\xA9\xB4\xF8\xB5\xC4\xCF\xD6\xBD\xF0\xD7\xB0\xB1\xB8\xCD\xCF\xC8\xEB");
            return false;
        }
        if (m_itemId > 0) {
            EquipDye::RestoreSharedWzForItem(m_itemId);
            EquipDye::SoftRefreshPreview(m_itemId, m_enterHsl);
        }
        m_itemId = itemId;
        m_slot = slot;
        m_invType = (signed char)((slot < 0) ? 0xFF : invType);
        m_enterHsl = EquipDye::GetOwnedHsl(itemId);
        ToSliders(m_enterHsl, m_slideH, m_slideS, m_slideL);
        SoftRefresh();
        play_ui_sound(L"DlgNotice");
        return true;
    }

    void ConfirmAndClose() {
        if (m_itemId <= 0) { Destroy(); return; }
        DyeHsl hsl = FromSliders(m_slideH, m_slideS, m_slideL);
        EquipDye::SetOwnedHsl(m_itemId, hsl);
        EquipDye::ApplyOwnedItemDyeOnly();

        COutPacket o(kColoringPrismSendOpcode);
        o.Encode1(1);
        o.Encode1((unsigned char)m_invType);
        o.Encode2(m_slot);
        o.Encode4(m_itemId);
        uint32_t hb = 0, sb = 0, lb = 0;
        memcpy(&hb, &hsl.hue, 4);
        memcpy(&sb, &hsl.sat, 4);
        memcpy(&lb, &hsl.light, 4);
        o.Encode4(hb); o.Encode4(sb); o.Encode4(lb);
        SendPacket(o);

        Destroy();
        ZXString<char> msg;
        msg = "\xC8\xBE\xC9\xAB\xB3\xC9\xB9\xA6\xA3\xAC\xC7\xEB\xB8\xFC\xBB\xBB\xB5\xD8\xCD\xBC\xCB\xA2\xD0\xC2\xA1\xA3";
        CUtilDlg_Notice(msg, nullptr, nullptr, 0, 0);
    }

    void ClearAndRestore() {
        if (m_itemId > 0) {
            COutPacket o(kColoringPrismSendOpcode);
            o.Encode1(2);
            o.Encode1((unsigned char)m_invType);
            o.Encode2(m_slot);
            o.Encode4(m_itemId);
            SendPacket(o);
            EquipDye::ClearOwnedHsl(m_itemId);
            EquipDye::RestoreSharedWzForItem(m_itemId);
            m_slideH = m_slideS = m_slideL = 0;
            m_enterHsl = DyeHsl();
        }
    }

    void CancelAndClose() {
        if (m_itemId > 0) {
            EquipDye::RestoreSharedWzForItem(m_itemId);
            if (!m_enterHsl.nearZero()) {
                EquipDye::SoftRefreshPreview(m_itemId, m_enterHsl);
            }
        }
        Destroy();
    }

    int HitSlider(int rx, int ry) {
        if (HitRect(&m_rcTrackH, rx, ry)) return 0;
        if (HitRect(&m_rcTrackS, rx, ry)) return 1;
        if (HitRect(&m_rcTrackL, rx, ry)) return 2;
        return -1;
    }

    void SetSliderFromX(int which, int rx) {
        RECT* rc = which == 0 ? &m_rcTrackH : (which == 1 ? &m_rcTrackS : &m_rcTrackL);
        int w = rc->right - rc->left;
        if (w <= 0) return;
        float t = (float)(rx - rc->left) / (float)w;
        if (t < 0) t = 0; if (t > 1) t = 1;
        int v = kSliderMin + (int)std::lround(t * (kSliderMax - kSliderMin));
        if (which == 0) m_slideH = v;
        else if (which == 1) m_slideS = v;
        else m_slideL = v;
        SoftRefresh();
    }

    void BlitBtn(IWzCanvasPtr c, IWzCanvasPtr* bt, const RECT& rc, int id) {
        int st = (m_hoverBtn == id) ? 1 : 0;
        if (bt[st]) Blit(c, bt[st], rc.left, rc.top);
        else if (bt[0]) Blit(c, bt[0], rc.left, rc.top);
        else {
            try { c->DrawRectangle(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0xFF606070); } catch (...) {}
        }
    }
};

CUIColoringPrism::CUIColoringPrism() {
    ms_pInstance = this;
    g_coloringPrismWndOpen = true;
    m_bg = LoadSprite(L"UI/UIWindow.img/ColoringPrism/backgrnd");
    m_btOk[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOK/normal");
    m_btOk[1] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOK/mouseOver");
    m_btOk[2] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOK/pressed");
    m_btCancel[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtCancel/normal");
    m_btCancel[1] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtCancel/mouseOver");
    m_btCancel[2] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtCancel/pressed");
    m_btClose[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtClose/normal");
    m_btClose[1] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtClose/mouseOver");
    m_btClose[2] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtClose/pressed");
    m_btOff[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOff/normal");
    m_btOff[1] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOff/mouseOver");
    m_btOff[2] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOff/pressed");
    if (!m_btOk[0]) m_btOk[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOK/0");
    if (!m_btCancel[0]) m_btCancel[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtCancel/0");
    if (!m_btClose[0]) m_btClose[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtClose/0");
    if (!m_btOff[0]) m_btOff[0] = LoadSprite(L"UI/UIWindow.img/ColoringPrism/BtOff/0");

    m_rcClose = {kWndW - 22, 4, kWndW - 6, 20};
    m_rcOk = {40, 350, 120, 380};
    m_rcCancel = {180, 350, 260, 380};
    m_rcOff = {110, 310, 190, 340};
    m_rcTrackH = {40, 260, 260, 274};
    m_rcTrackS = {40, 278, 260, 292};
    m_rcTrackL = {40, 296, 260, 310};

    int sx = (get_screen_width() - kWndW) / 2;
    int sy = (get_screen_height() - kWndH) / 2;
    CreateWnd(sx, sy, kWndW, kWndH, 10, 1, nullptr, 0);
}

void CUIColoringPrism::OnDestroy() {
    g_coloringPrismWndOpen = false;
    CWnd::OnDestroy();
}

void CUIColoringPrism::Draw(const RECT* /*pRect*/) {
    IWzCanvasPtr c = GetCanvas();
    if (!c) return;
    if (m_bg) Blit(c, m_bg, 0, 0);
    else {
        try { c->DrawRectangle(0, 0, kWndW, kWndH, 0xCC202028); } catch (...) {}
    }

    try {
        unsigned color = 0xFF808890;
        if (m_itemId > 0) {
            DyeHsl hsl = FromSliders(m_slideH, m_slideS, m_slideL);
            float hr = (hsl.hue + 180.f) / 360.f;
            float r = 0.4f + 0.5f * hr;
            float g = 0.4f + 0.5f * (hsl.sat + 1.f) * 0.5f;
            float b = 0.4f + 0.5f * (hsl.light + 1.f) * 0.5f;
            color = 0xFF000000u | (To8(r) << 16) | (To8(g) << 8) | To8(b);
        }
        c->DrawRectangle(90, 80, 120, 140, color);
    } catch (...) {
    }

    DrawTrack(c, m_rcTrackH, m_slideH, 0xFFE08080);
    DrawTrack(c, m_rcTrackS, m_slideS, 0xFF80E080);
    DrawTrack(c, m_rcTrackL, m_slideL, 0xFF8080E0);

    BlitBtn(c, m_btOk, m_rcOk, 1);
    BlitBtn(c, m_btCancel, m_rcCancel, 2);
    BlitBtn(c, m_btClose, m_rcClose, 3);
    BlitBtn(c, m_btOff, m_rcOff, 4);
}

void CUIColoringPrism::OnMouseButton(unsigned int msg, unsigned int /*wParam*/, int rx, int ry) {
    if (msg == WM_LBUTTONDOWN) {
        int s = HitSlider(rx, ry);
        if (s >= 0) { m_dragSlider = s; SetSliderFromX(s, rx); return; }
        if (HitRect(&m_rcOk, rx, ry)) { ConfirmAndClose(); return; }
        if (HitRect(&m_rcCancel, rx, ry) || HitRect(&m_rcClose, rx, ry)) { CancelAndClose(); return; }
        if (HitRect(&m_rcOff, rx, ry)) { ClearAndRestore(); SoftRefresh(); return; }
    } else if (msg == WM_LBUTTONUP) {
        m_dragSlider = -1;
    }
}

int CUIColoringPrism::OnMouseMove(int rx, int ry) {
    if (m_dragSlider >= 0) SetSliderFromX(m_dragSlider, rx);
    int h = -1;
    if (HitRect(&m_rcOk, rx, ry)) h = 1;
    else if (HitRect(&m_rcCancel, rx, ry)) h = 2;
    else if (HitRect(&m_rcClose, rx, ry)) h = 3;
    else if (HitRect(&m_rcOff, rx, ry)) h = 4;
    m_hoverBtn = h;
    return 0;
}

int __fastcall Hook_OnDropped(CDraggableItem* pThis, void*, IUIMsgHandler* pFrom, IUIMsgHandler* pTo, int rx, int ry) {
    if (pTo && CUIColoringPrism::ms_pInstance && pTo->IsKindOf(&CUIColoringPrism::ms_RTTI)) {
        CUIColoringPrism* dlg = static_cast<CUIColoringPrism*>(static_cast<CWnd*>(static_cast<IUIMsgHandler*>(pTo)));
        if (dlg != CUIColoringPrism::ms_pInstance) {
            CUIColoringPrism* alt = reinterpret_cast<CUIColoringPrism*>(reinterpret_cast<char*>(pTo) - 4);
            if (alt == CUIColoringPrism::ms_pInstance) dlg = alt;
            else return 0;
        }
        int ti = pThis->m_nItemTI;
        int slot = pThis->m_nSlotPosition;
        int itemId = GetItemIdFromSlot(ti, slot);
        if (itemId <= 0) return 0;
        dlg->AcceptItem(itemId, ti, (short)slot);
        return 1;
    }
    return Orig_OnDropped(pThis, pFrom, pTo, rx, ry);
}

static std::vector<DyeEntry> ReadEntries(CompatInPacket* p) {
    std::vector<DyeEntry> out;
    short count = p->Decode<int16_t>();
    if (count < 0) count = 0;
    if (count > 256) count = 256;
    out.reserve((size_t)count);
    for (int i = 0; i < count; ++i) {
        DyeEntry e;
        e.itemId = p->Decode<int32_t>();
        uint32_t hb = p->Decode<uint32_t>();
        uint32_t sb = p->Decode<uint32_t>();
        uint32_t lb = p->Decode<uint32_t>();
        memcpy(&e.hsl.hue, &hb, 4);
        memcpy(&e.hsl.sat, &sb, 4);
        memcpy(&e.hsl.light, &lb, 4);
        out.push_back(e);
    }
    return out;
}

} // namespace

void ColoringPrism_OpenWindow() {
    if (CUIColoringPrism::ms_pInstance) return;
    new CUIColoringPrism();
}

void ColoringPrism_CloseWindow() {
    if (CUIColoringPrism::ms_pInstance) CUIColoringPrism::ms_pInstance->Destroy();
}

bool ColoringPrism_IsOpen() {
    return CUIColoringPrism::ms_pInstance != nullptr;
}

void ColoringPrism_RequestOpen() {
    COutPacket o(kColoringPrismSendOpcode);
    o.Encode1(0);
    SendPacket(o);
}

void ColoringPrism_HandleServerPacket(CompatInPacket* packet) {
    if (!packet) return;
    unsigned short peeked = 0;
    if (!packet->TryPeekOpcode(peeked) || peeked != kColoringPrismRecvOpcode) return;
    packet->Decode<uint16_t>();
    uint8_t type = packet->Decode<uint8_t>();
    if (type == 0) {
        ColoringPrism_OpenWindow();
    } else if (type == 1) {
        std::vector<DyeEntry> entries = ReadEntries(packet);
        EquipDye::SyncItemHslFromList(entries);
        EquipDye::ScheduleDeferredOwnedApply(800);
    } else if (type == 2) {
        int charId = packet->Decode<int32_t>();
        std::vector<DyeEntry> entries = ReadEntries(packet);
        EquipDye::MergeItemHslFromList(charId, entries);
        EquipDye::RequestForeignAvatarRefresh(charId);
    }
}

void ColoringPrism_OnTick() {
    EquipDye::PollDeferredOwnedApply();
    int charId = 0;
    if (EquipDye::TryConsumeForeignAvatarRefresh(&charId)) {
        EquipDye::ApplyForeignVisibleDye(charId);
    }
}

void AttachColoringPrismMod() {
    EquipDye::Hook();
    ATTACH_HOOK(Orig_OnDropped, Hook_OnDropped);
}
