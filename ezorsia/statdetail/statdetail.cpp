// Stat detail: always show UI/UIWindow.img/Stat/backgrnd4 + classic + combat attrs.
//
// PE (BeiDou.exe @ 0x400000) — CUIStatDetail vtable 0x00B3B990:
//   Draw            0x008C2870
//   OnButtonClicked 0x008C477F  (close id 1000)
//   OnCreate        0x008C265B  (SetBackgrnd; close CreateCtrl patched to 215,218)
//   ctor            0x008C50B4  CreateWnd size patched to 237x239
//   dtor path clears *(CUIStatDetail**)0x00BF10C8
// CUIStat::ToggleDetail 0x008C545D — slide open/close; open flag at CUIStat+0x5A4.
//   Close keeps detail singleton alive until slide finishes — do NOT re-layout on close.
// CUIStat CreateWnd: w=0xB0=176, h=0x15C=348. Native detail place:
//   X = StatLeft + 0xAA (170), Y = StatTop + 0x90 (144 = ~348-203).
// backgrnd4 237x239 => patch place/sync to X=+176, Y=+(348-239)=109 so flush + bottom-align.
//   (ctor top-36 alone is overwritten by sub_8C5531 on open/drag — must patch sync sites.)

#include "stdafx.h"
#include "StatDetailApi.h"
#include "../setitem/SetItemData.h"
#include "Memory.h"
#include "compat/hook.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wnd.h"
#include "compat/ztl/zcom.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uintptr_t kAddr_Draw = 0x008C2870;
constexpr uintptr_t kAddr_OnCreate = 0x008C265B;
constexpr uintptr_t kAddr_OnButtonClicked_Detail = 0x008C477F;
constexpr uintptr_t kAddr_OnButtonClicked_Stat = 0x008C522F;
constexpr uintptr_t kAddr_Ctor = 0x008C50B4;
constexpr uintptr_t kCUIStatDetailSingleton = 0x00BF10C8;
constexpr uintptr_t kCUIStatSingleton = 0x00BF10C4;
constexpr uintptr_t kOffStatDetailOpenFlag = 0x5A4; // CUIStat: 1 while detail open
constexpr uintptr_t kPatch_CtorCreateWndH = 0x008C5105 + 1; // push imm32 height
constexpr uintptr_t kPatch_CtorCreateWndW = 0x008C510A + 1; // push imm32 width
constexpr uintptr_t kPatch_OnCreateCloseY = 0x008C274F + 1; // push imm32 close Y
constexpr uintptr_t kPatch_OnCreateCloseX = 0x008C2754 + 1; // push imm32 close X
// add eax, imm32 — open ctor Y and sync Y (was 0x90=144)
constexpr uintptr_t kPatch_OpenPlaceY = 0x008C54A9 + 1;
constexpr uintptr_t kPatch_SyncPlaceY = 0x008C57BF + 1;
// sync open X: and eax, -off / add eax, off (was 0xAA=170)
constexpr uintptr_t kPatch_SyncOpenXAnd = 0x008C575B + 1;
constexpr uintptr_t kPatch_SyncOpenXAdd = 0x008C5760 + 1;
constexpr uintptr_t kCWvsContextPtr = 0x00BE7918;
constexpr uintptr_t kOffSecondaryStat = 0x2134;
constexpr uintptr_t kZtlSecureFuseLong = 0x00416563;
constexpr uintptr_t kAddr_GetPAD = 0x0077DF48;
constexpr uintptr_t kAddr_GetPDD = 0x0077E067;
constexpr uintptr_t kAddr_GetMDD = 0x0077E141;
constexpr uintptr_t kAddr_GetACC = 0x0077DE1C;
constexpr uintptr_t kAddr_GetEVA = 0x0077DEB2;
constexpr uintptr_t kAddr_GetSpeed = 0x008C457C;
constexpr uintptr_t kAddr_GetJump = 0x008C45E8;
constexpr uintptr_t kGetBasicFont = 0x0098A707;
constexpr uintptr_t kWzFontCreate = 0x0046341A;
constexpr uintptr_t kAddr_SetBackgrnd = 0x009E0AB2;

constexpr unsigned kBtnClose = 1000;
constexpr unsigned kBtnDetail = 2006;

// IDA-verified DrawText anchors for classic column; right column of backgrnd4.
constexpr int kValueXLeft = 0x4D;   // 77
constexpr int kValueXRight = 189;
constexpr int kRow0Y = 8;
constexpr int kRowH = 18;
constexpr unsigned long kColValue = 0xFF000000;

constexpr int kBg4W = 237;
constexpr int kBg4H = 239;
constexpr int kNativeDetailW = 177;
constexpr int kNativeDetailH = 203;
constexpr int kNativeCloseX = 155;
constexpr int kNativeCloseY = 182;
constexpr int kCloseX = kBg4W - (kNativeDetailW - kNativeCloseX); // 215
constexpr int kCloseY = kBg4H - (kNativeDetailH - kNativeCloseY); // 218
// CUIStat CreateWnd (IDA): w=0xB0=176, h=0x15C=348
constexpr int kStatWndW = 176;
constexpr int kStatWndH = 348;
// Native Y offset 144 ~= StatH - nativeDetailH; new bottom-align offset:
constexpr int kPlaceOffsetY = kStatWndH - kBg4H; // 109
// Flush to Stat right edge (native used 170, left a small gap).
constexpr int kPlaceOffsetX = kStatWndW; // 176
constexpr unsigned kPlaceOffsetXNeg = static_cast<unsigned>(-kPlaceOffsetX); // 0xFFFFFF50

using Draw_t = void(__thiscall*)(void* self, const RECT* rc);
using OnCreate_t = void(__thiscall*)(void* self);
using OnBtn_t = void(__thiscall*)(void* self, unsigned int id);
using Ctor_t = void*(__fastcall*)(void* self, void* edx, int left, int top);
using FuseLong_t = int(__cdecl*)(void* tear, unsigned int checksum);
using GetCD_t = void*(__thiscall*)(void* ctx, void* zrefOut);
using StatInt1_t = int(__thiscall*)(void* ss, void* charData);
using StatInt0_t = int(__thiscall*)(void* ss);
using GetBasicFont_t = void(__cdecl*)(IWzFontPtr*, int);
using WzFontCreate_t = HRESULT(__thiscall*)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);

Draw_t g_drawOrig = reinterpret_cast<Draw_t>(kAddr_Draw);
OnCreate_t g_onCreateOrig = reinterpret_cast<OnCreate_t>(kAddr_OnCreate);
OnBtn_t g_detailBtnOrig = reinterpret_cast<OnBtn_t>(kAddr_OnButtonClicked_Detail);
OnBtn_t g_statBtnOrig = reinterpret_cast<OnBtn_t>(kAddr_OnButtonClicked_Stat);
Ctor_t g_ctorOrig = reinterpret_cast<Ctor_t>(kAddr_Ctor);
FuseLong_t g_fuseLong = reinterpret_cast<FuseLong_t>(kZtlSecureFuseLong);
StatInt1_t g_getPdd = reinterpret_cast<StatInt1_t>(kAddr_GetPDD);
StatInt1_t g_getMdd = reinterpret_cast<StatInt1_t>(kAddr_GetMDD);
StatInt1_t g_getAcc = reinterpret_cast<StatInt1_t>(kAddr_GetACC);
StatInt1_t g_getEva = reinterpret_cast<StatInt1_t>(kAddr_GetEVA);
StatInt0_t g_getSpeed = reinterpret_cast<StatInt0_t>(kAddr_GetSpeed);
StatInt0_t g_getJump = reinterpret_cast<StatInt0_t>(kAddr_GetJump);
GetBasicFont_t g_getBasicFont = reinterpret_cast<GetBasicFont_t>(kGetBasicFont);
WzFontCreate_t g_wzFontCreate = reinterpret_cast<WzFontCreate_t>(kWzFontCreate);

bool g_detailOpen = false;
bool g_layoutReady = false;
void* g_activeDetail = nullptr;
bool g_hooksAttached = false;
IWzFontPtr g_font;
IWzCanvasPtr g_bg4;

// GetCharacterData call site inside Draw uses 0x00428??? — resolve from known Draw insn.
// Draw @ 0x8C28E8: call GetCharacterData — read target at runtime once.
GetCD_t ResolveGetCharacterData() {
    static GetCD_t fn = nullptr;
    if (fn) {
        return fn;
    }
    // call imm32 at 0x8C28E8
    const uintptr_t callSite = 0x008C28E8;
    const int rel = *reinterpret_cast<const int*>(callSite + 1);
    fn = reinterpret_cast<GetCD_t>(callSite + 5 + rel);
    return fn;
}

int ReadSecureLong(void* base, int offset) {
    if (!base) {
        return 0;
    }
    __try {
        unsigned char* tear = reinterpret_cast<unsigned char*>(base) + offset;
        const unsigned int cs = *reinterpret_cast<unsigned int*>(tear + 8);
        return g_fuseLong(tear, cs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void* GetWvsContext() {
    __try {
        return *reinterpret_cast<void**>(kCWvsContextPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetSecondaryStat() {
    void* ctx = GetWvsContext();
    if (!ctx) {
        return nullptr;
    }
    return reinterpret_cast<unsigned char*>(ctx) + kOffSecondaryStat;
}

void* GetCharacterDataPtr() {
    void* ctx = GetWvsContext();
    if (!ctx) {
        return nullptr;
    }
    __try {
        unsigned char zref[16] = {};
        ResolveGetCharacterData()(ctx, zref);
        return *reinterpret_cast<void**>(zref + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetDetailSingleton() {
    __try {
        return *reinterpret_cast<void**>(kCUIStatDetailSingleton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* GetStatSingleton() {
    __try {
        return *reinterpret_cast<void**>(kCUIStatSingleton);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ToggleDetail sets CUIStat+0x5A4=1 on open and clears it at the start of close
// (singleton stays alive during the slide). Prefer this over singleton presence.
bool ReadStatDetailOpenFlag() {
    void* stat = GetStatSingleton();
    if (!stat) {
        return false;
    }
    __try {
        return *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(stat) + kOffStatDetailOpenFlag) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[64] = {};
    if (!sGbk || MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

bool EnsureFont() {
    if (g_font) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_font, nullptr);
        if (g_font) {
            const Ztl_variant_t style(L"");
            if (SUCCEEDED(g_wzFontCreate(g_font, L"DotumLight", 12, kColValue, style))) {
                return true;
            }
            g_font = nullptr;
        }
    } catch (...) {
        g_font = nullptr;
    }
    try {
        g_getBasicFont(std::addressof(g_font), 0);
    } catch (...) {
        g_font = nullptr;
    }
    return g_font != nullptr;
}

IWzCanvasPtr LoadSprite(const wchar_t* path) {
    IWzCanvasPtr canvas;
    try {
        canvas = get_unknown(get_rm()->GetObjectA(const_cast<wchar_t*>(path)));
    } catch (...) {
        canvas = nullptr;
    }
    return canvas;
}

IWzCanvasPtr EnsureBg4() {
    if (g_bg4) {
        return g_bg4;
    }
    g_bg4 = LoadSprite(L"UI/UIWindow.img/Stat/backgrnd4");
    return g_bg4;
}

void DrawValue(IWzCanvasPtr canvas, int x, int y, int value, bool percent) {
    if (!canvas || !EnsureFont()) {
        return;
    }
    char buf[32];
    if (percent) {
        _snprintf_s(buf, _TRUNCATE, "%d%%", value);
    } else {
        _snprintf_s(buf, _TRUNCATE, "%d", value);
    }
    try {
        canvas->DrawTextA(x, y, GbkToBstr(buf), g_font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

int SafeCall1(StatInt1_t fn, void* ss, void* cd) {
    if (!fn || !ss || !cd) {
        return 0;
    }
    __try {
        return fn(ss, cd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int SafeCall0(StatInt0_t fn, void* ss) {
    if (!fn || !ss) {
        return 0;
    }
    __try {
        return fn(ss);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int ReadPad(void* ss, void* cd) {
    if (!ss || !cd) {
        return 0;
    }
    __try {
        // Draw @ 0x8C2A35: push extra(0), push CharacterData*; thiscall GetPAD.
        using GetPad2_t = int(__thiscall*)(void* ss, void* charData, int extra);
        return reinterpret_cast<GetPad2_t>(kAddr_GetPAD)(ss, cd, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ReadSecureLong(ss, 0);
    }
}

int ReadMad(void* ss) {
    // MAD row fuses SecondaryStat+0x60 / +0x6C (IDA between DrawText y=26 and y=44).
    if (!ss) {
        return 0;
    }
    return ReadSecureLong(ss, 0x60) + ReadSecureLong(ss, 0x6C);
}

int ReadCraft(void* ss) {
    return ReadSecureLong(ss, 0x120);
}

void EnsureDetailLayout(void* self) {
    if (!self || g_layoutReady) {
        return;
    }
    auto* wnd = reinterpret_cast<CWnd*>(self);
    wnd->m_width = kBg4W;
    wnd->m_height = kBg4H;
    // SetBackgrnd uses Ztl_bstr_t (C++ unwind) — no SEH in this function.
    try {
        using SetBg2_t = void(__thiscall*)(void* self, Ztl_bstr_t uol, int a, int b);
        reinterpret_cast<SetBg2_t>(kAddr_SetBackgrnd)(
                self, Ztl_bstr_t(L"UI/UIWindow.img/Stat/backgrnd4"), 0, 0);
    } catch (...) {
    }
    g_layoutReady = true;
    std::cout << "[StatDetail] layout " << kBg4W << "x" << kBg4H
              << " (place X+" << kPlaceOffsetX << " Y+" << kPlaceOffsetY << ")"
              << std::endl;
}

void DrawOverlay(void* self) {
    auto* wnd = reinterpret_cast<CWnd*>(self);
    IWzCanvasPtr canvas = wnd->GetCanvas();
    if (!canvas) {
        return;
    }

    IWzCanvasPtr bg = EnsureBg4();
    if (bg) {
        try {
            canvas->CopyEx(0, 0, bg, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0);
        } catch (...) {
            bg = nullptr;
        }
    }

    void* ss = GetSecondaryStat();
    void* cd = GetCharacterDataPtr();

    const int pad = ReadPad(ss, cd);
    const int pdd = SafeCall1(g_getPdd, ss, cd);
    const int mad = ReadMad(ss);
    const int mdd = SafeCall1(g_getMdd, ss, cd);
    const int acc = SafeCall1(g_getAcc, ss, cd);
    const int eva = SafeCall1(g_getEva, ss, cd);
    const int craft = ReadCraft(ss);
    const int speed = SafeCall0(g_getSpeed, ss);
    const int jump = SafeCall0(g_getJump, ss);

    const SetItemData::CombatPanelStats& s = SetItemData::g_combatPanel;

    // Left: 9 classic (IDA y=8..152) + crit rows on backgrnd4.
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 0, pad, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 1, pdd, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 2, mad, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 3, mdd, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 4, acc, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 5, eva, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 6, craft, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 7, speed, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 8, jump, false);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 9, s.critRate, true);
    DrawValue(canvas, kValueXLeft, kRow0Y + kRowH * 10, s.critDam, true);

    // Right: 9 combat attrs aligned to top row (伤害 included).
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 0, s.damR, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 1, s.finalDamagePercent, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 2, s.bossDamR, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 3, s.ignorePDR, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 4, s.asrR, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 5, s.buffTimeR, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 6, s.stanceProp, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 7, s.itemDropProp, true);
    DrawValue(canvas, kValueXRight, kRow0Y + kRowH * 8, s.mesoDropProp, true);
}

void InvalidateDetail(void* self) {
    if (!self) {
        return;
    }
    __try {
        reinterpret_cast<CWnd*>(self)->InvalidateRect(nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void MarkDetailClosed() {
    g_detailOpen = false;
    g_activeDetail = nullptr;
    // Keep g_layoutReady / g_bg4: Draw may still run during slide-close.
}

void MarkDetailOpened(void* detail) {
    g_detailOpen = true;
    g_activeDetail = detail;
    g_bg4 = nullptr;
    g_layoutReady = false;
    EnsureDetailLayout(detail);
    InvalidateDetail(detail);
    std::cout << "[StatDetail] opened backgrnd4 panel" << std::endl;
}

void __fastcall Draw_Hook(void* self, void* /*edx*/, const RECT* rc) {
    g_activeDetail = self;
    // Still painting during slide-close; open flag may already be 0.
    if (ReadStatDetailOpenFlag()) {
        g_detailOpen = true;
    }
    EnsureDetailLayout(self);
    // Native draw keeps window chrome; overlay paints backgrnd4 + all numbers.
    g_drawOrig(self, rc);
    DrawOverlay(self);
}

void __fastcall OnCreate_Hook(void* self, void* /*edx*/) {
    g_detailOpen = true;
    g_activeDetail = self;
    g_bg4 = nullptr;
    g_layoutReady = false;
    g_onCreateOrig(self);
    EnsureDetailLayout(self);
    InvalidateDetail(self);
    std::cout << "[StatDetail] OnCreate -> backgrnd4 layout" << std::endl;
}

void* __fastcall Ctor_Hook(void* self, void* edx, int left, int top) {
    // CreateWnd size + place Y are patched in AttachHooks; pass top through unchanged.
    void* r = g_ctorOrig(self, edx, left, top);
    if (self) {
        auto* wnd = reinterpret_cast<CWnd*>(self);
        __try {
            wnd->m_width = kBg4W;
            wnd->m_height = kBg4H;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_layoutReady = false;
    }
    return r;
}

void __fastcall DetailBtn_Hook(void* self, void* /*edx*/, unsigned int id) {
    if (id == kBtnClose) {
        // Closing: clear plugin open state only — do not reset layout (slide still draws).
        MarkDetailClosed();
    }
    g_detailBtnOrig(self, id);
}

void __fastcall StatBtn_Hook(void* self, void* /*edx*/, unsigned int id) {
    if (id == kBtnDetail) {
        const bool wasOpen = ReadStatDetailOpenFlag() || GetDetailSingleton() != nullptr;
        g_statBtnOrig(self, id);
        const bool flagOpen = ReadStatDetailOpenFlag();
        void* detail = GetDetailSingleton();
        if (!wasOpen && flagOpen && detail) {
            // Opened: apply backgrnd4 layout once.
            MarkDetailOpened(detail);
        } else if (wasOpen && !flagOpen) {
            // Close started (singleton may still exist during slide) — never re-layout.
            MarkDetailClosed();
            std::cout << "[StatDetail] closing (slide) - skip layout" << std::endl;
        } else if (flagOpen && detail) {
            // Already open / no-op toggle edge: keep handle, do not force re-layout.
            g_detailOpen = true;
            g_activeDetail = detail;
        } else {
            MarkDetailClosed();
        }
        return;
    }
    g_statBtnOrig(self, id);
}

void PatchDetailWindowSizes() {
    // push imm32 immediates (IDA-verified).
    Memory::WriteInt(static_cast<DWORD>(kPatch_CtorCreateWndW), static_cast<unsigned int>(kBg4W));
    Memory::WriteInt(static_cast<DWORD>(kPatch_CtorCreateWndH), static_cast<unsigned int>(kBg4H));
    Memory::WriteInt(static_cast<DWORD>(kPatch_OnCreateCloseX), static_cast<unsigned int>(kCloseX));
    Memory::WriteInt(static_cast<DWORD>(kPatch_OnCreateCloseY), static_cast<unsigned int>(kCloseY));
    // Place/sync offsets so open + drag stay flush and bottom-aligned with Stat.
    Memory::WriteInt(static_cast<DWORD>(kPatch_OpenPlaceY), static_cast<unsigned int>(kPlaceOffsetY));
    Memory::WriteInt(static_cast<DWORD>(kPatch_SyncPlaceY), static_cast<unsigned int>(kPlaceOffsetY));
    Memory::WriteInt(static_cast<DWORD>(kPatch_SyncOpenXAdd), static_cast<unsigned int>(kPlaceOffsetX));
    Memory::WriteInt(static_cast<DWORD>(kPatch_SyncOpenXAnd), kPlaceOffsetXNeg);
    std::cout << "[StatDetail] patched CreateWnd " << kBg4W << "x" << kBg4H
              << " close@(" << kCloseX << "," << kCloseY << ")"
              << " place X+" << kPlaceOffsetX << " Y+" << kPlaceOffsetY << std::endl;
}

} // namespace

namespace StatDetailExt {

void AttachHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    PatchDetailWindowSizes();
    ATTACH_HOOK(g_drawOrig, Draw_Hook);
    ATTACH_HOOK(g_onCreateOrig, OnCreate_Hook);
    ATTACH_HOOK(g_detailBtnOrig, DetailBtn_Hook);
    ATTACH_HOOK(g_statBtnOrig, StatBtn_Hook);
    ATTACH_HOOK(g_ctorOrig, Ctor_Hook);
    std::cout << "[StatDetail] hooks ready: backgrnd4 + toggle fix + align sync"
              << std::endl;
}

void OnCombatPanelUpdated() {
    void* detail = GetDetailSingleton();
    if (!detail || !g_detailOpen) {
        return;
    }
    InvalidateDetail(detail);
}

} // namespace StatDetailExt
