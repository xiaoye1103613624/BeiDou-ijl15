#include "stdafx.h"

#include "StatDetailExtApi.h"

#include "../setitem/SetItemData.h"

#include "compat/hook.h"

#include "compat/wvs/wnd.h"

#include "compat/wvs/util.h"

#include "compat/ztl/zcom.h"

#include "compat/WzLib/IWzCanvas.h"

#include <cstdio>

namespace StatDetailExt {

namespace {

constexpr unsigned long kCUIStatDetailDraw = 0x008C2870;

// CUIStatDetail CreateWnd (sub_8C50B4): push height @0x8C5105, width @0x8C510A
// IDA: push 0xCB (203), push 0xB1 (177)
constexpr unsigned long kCreateWndHeightImm = 0x008C5105 + 1;
constexpr unsigned long kCreateWndWidthImm = 0x008C510A + 1;

// Clean extended backgrnd2 (stat_detail_backgrnd2_ext.png):
//   rows 0-8 = vanilla labels (攻击力..跳跃力), values drawn by orig Draw @ Y=8+18*i
//   rows 9-17 = custom extras only (plugin DrawExtraRows)
constexpr int kExtendedWidth = 298;
constexpr int kExtendedHeight = 365;
constexpr int kVanillaWidth = 177;
constexpr int kVanillaHeight = 203;
constexpr int kVanillaRowCount = 9;
constexpr int kRowStep = 18;
constexpr int kFirstValueY = 8;
constexpr int kValueX = 77;
constexpr int kExtraFirstIndex = kVanillaRowCount; // never stomp vanilla 0..8

// CUIStat main window — match live Stat/backgrnd (213x347).
constexpr int kMainStatBackgrndWidth = 213;
constexpr int kMainStatWidthVanilla = 176;
constexpr int kMainStatWidth = kMainStatBackgrndWidth;
constexpr int kMainStatWidthDelta = kMainStatWidth - kMainStatWidthVanilla;
constexpr int kMainStatHeight = 347;
constexpr int kBtDetailWidth = 46;

constexpr unsigned long kMainStatCreateWndWidthImm = 0x008C4AB3 + 1; // push imm32 width
constexpr unsigned long kMainStatCloseBtnXImm = 0x008C485A + 1;
constexpr unsigned long kMainStatApBtnXImm = 0x008C7AD9 + 1; // mov edi, imm32
// BtDetail CreateCtrl (PE/IDA): push Y imm32 @8C4E1B, push X imm8 @8C4E20.
// X>127 MUST use CodeCave — WriteByte on imm8 sign-extends.
constexpr unsigned long kBtDetailPushY = 0x008C4E1B;
constexpr unsigned long kBtDetailPushId = 0x008C4E22;
constexpr int kBtDetailOverwriteBytes = 7;

// Detail panel close/collapse btn CreateCtrl: push Y @8C274F, X @8C2754 (both imm32).
// Vanilla Y=0xB6 (182) on height 203 → sits near bottom; must move with extended height.
constexpr unsigned long kDetailCloseBtnYImm = 0x008C274F + 1;
constexpr unsigned long kDetailCloseBtnXImm = 0x008C2754 + 1;
constexpr int kDetailCloseBtnYVanilla = 0xB6;
constexpr int kDetailCloseBtnXVanilla = 0x9B;

constexpr int kMainStatCloseBtnXVanilla = 0x9C;
constexpr int kMainStatApBtnXVanilla = 0x99;
constexpr int kMainStatDetailBtnXVanilla = 0x7C;

DWORD g_btDetailBtnX = kMainStatDetailBtnXVanilla;
DWORD g_btDetailCaveRtn = kBtDetailPushId;

__declspec(naked) void BtDetailBtnPosCave() {
    __asm {
        push 0x144
        push dword ptr[g_btDetailBtnX]
        jmp dword ptr[g_btDetailCaveRtn]
    }
}

// Detail attach offsets (add eax, imm32).
// Vanilla: Y += 0x90 (144 = 347-203 bottom-align), X += 0xAA (170).
constexpr unsigned long kDetailAttachYImm1 = 0x008C4E91 + 1;
constexpr unsigned long kDetailAttachYImm2 = 0x008C6C65 + 1;
constexpr unsigned long kDetailAttachXImm1 = 0x008C4EA2 + 1;
constexpr unsigned long kDetailAttachXImm2 = 0x008C5760 + 1;
constexpr unsigned long kDetailAttachXImm3 = 0x008C6C72 + 1;

using Draw_t = int(__thiscall*)(void*, const RECT*);
Draw_t g_origDraw = reinterpret_cast<Draw_t>(kCUIStatDetailDraw);

typedef void(__cdecl* GetBasicFont_t)(IWzFontPtr*, int);
typedef HRESULT(__thiscall* WzFontCreate_t)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);

static auto get_basic_font = reinterpret_cast<GetBasicFont_t>(0x0098A707);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(0x0046341A);

IWzFontPtr g_font;
bool g_hooksAttached = false;

void LogPatch(const char* msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "statdetail_patch.txt", "a") != 0 || !f) {
        return;
    }
    fprintf(f, "%s\n", msg);
    fflush(f);
    fclose(f);
}

Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[128] = {};
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
            if (SUCCEEDED(WzFontCreate(g_font, L"Arial", 12, 0xFF000000, style))) {
                return true;
            }
            g_font = nullptr;
        }
    } catch (...) {
        g_font = nullptr;
    }
    try {
        get_basic_font(std::addressof(g_font), 25);
    } catch (...) {
        g_font = nullptr;
    }
    return g_font != nullptr;
}

IWzCanvas* GetCanvasRaw(CWnd* wnd) noexcept {
    using Fn = IWzCanvas**(__thiscall*)(CWnd*, IWzCanvas**);
    static const Fn fn = reinterpret_cast<Fn>(0x00425C4C);
    IWzCanvas* raw = nullptr;
    __try {
        fn(wnd, &raw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw = nullptr;
    }
    return raw;
}

void FormatPercent(char* buf, size_t bufSize, int value) {
    if (value > 0) {
        sprintf_s(buf, bufSize, "%d%%", value);
    } else {
        sprintf_s(buf, bufSize, "0%%");
    }
}

void DrawValue(IWzCanvas* canvas, IWzFont* font, int rowIndex, const char* value) {
    if (!canvas || !font || !value || rowIndex < kExtraFirstIndex) {
        return;
    }
    const int y = kFirstValueY + rowIndex * kRowStep;
    try {
        canvas->DrawTextA(kValueX, y, GbkToBstr(value), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

void DrawExtraRows(CWnd* wnd) {
    if (!wnd || !EnsureFont()) {
        return;
    }
    IWzCanvas* raw = GetCanvasRaw(wnd);
    if (!raw) {
        return;
    }
    IWzCanvasPtr canvas(raw, false);

    // Rows 0-8: leave entirely to vanilla Draw (ATK..Jump). Extras start at index 9.
    char value[32] = {};
    int row = kExtraFirstIndex;

    FormatPercent(value, sizeof(value), SetItemData::g_itemDropProp);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_mesoDropProp);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_critRate);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_critDam);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_normalDamR);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_bossDamR);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_finalDamagePercent);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_ignorePDR);
    DrawValue(canvas, g_font, row++, value);

    FormatPercent(value, sizeof(value), SetItemData::g_damageReduce);
    DrawValue(canvas, g_font, row++, value);
}

int __fastcall Hook_CUIStatDetail_Draw(void* pThis, void* /*edx*/, const RECT* pRect) {
    const int result = g_origDraw(pThis, pRect);
    __try {
        DrawExtraRows(reinterpret_cast<CWnd*>(pThis));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}

unsigned int ReadImm32(unsigned long addr) {
    return *reinterpret_cast<unsigned int*>(addr);
}

unsigned char ReadImm8(unsigned long addr) {
    return *reinterpret_cast<unsigned char*>(addr);
}

void PatchWindowSize() {
    Memory::WriteInt(kCreateWndHeightImm, static_cast<unsigned int>(kExtendedHeight));
    Memory::WriteInt(kCreateWndWidthImm, static_cast<unsigned int>(kExtendedWidth));

    Memory::WriteInt(kDetailAttachXImm1, static_cast<unsigned int>(kMainStatWidth));
    Memory::WriteInt(kDetailAttachXImm2, static_cast<unsigned int>(kMainStatWidth));
    Memory::WriteInt(kDetailAttachXImm3, static_cast<unsigned int>(kMainStatWidth));

    // Top-align when detail is taller than main (365 > 347); bottom-align would go negative.
    int detailY = kMainStatHeight - kExtendedHeight;
    if (detailY < 0) {
        detailY = 0;
    }
    Memory::WriteInt(kDetailAttachYImm1, static_cast<unsigned int>(detailY));
    Memory::WriteInt(kDetailAttachYImm2, static_cast<unsigned int>(detailY));

    // Keep close btn on the bottom-right of the extended detail panel.
    const int closeY =
            kDetailCloseBtnYVanilla + (kExtendedHeight - kVanillaHeight);
    const int closeX =
            kDetailCloseBtnXVanilla + (kExtendedWidth - kVanillaWidth);
    Memory::WriteInt(kDetailCloseBtnYImm, static_cast<unsigned int>(closeY));
    Memory::WriteInt(kDetailCloseBtnXImm, static_cast<unsigned int>(closeX));
}

void PatchMainStatWindow() {
    const int detailBtnX = kMainStatWidth - kBtDetailWidth - 5;
    const int apBtnX = kMainStatApBtnXVanilla + kMainStatWidthDelta;
    const int closeBtnX = kMainStatCloseBtnXVanilla + kMainStatWidthDelta;

    Memory::WriteInt(kMainStatCreateWndWidthImm, static_cast<unsigned int>(kMainStatWidth));
    Memory::WriteInt(kMainStatCloseBtnXImm, static_cast<unsigned int>(closeBtnX));
    Memory::WriteInt(kMainStatApBtnXImm, static_cast<unsigned int>(apBtnX));

    g_btDetailBtnX = static_cast<DWORD>(detailBtnX);
    g_btDetailCaveRtn = kBtDetailPushId;
    Memory::CodeCave(reinterpret_cast<void*>(BtDetailBtnPosCave), kBtDetailPushY,
            kBtDetailOverwriteBytes);

    char buf[256];
    sprintf_s(buf,
            "PatchMainStat: width=%d (mem=%u) closeX=%d apX=%d btDetailX=%d cave@8C4E1B "
            "op=%02X%02X%02X%02X%02X nop=%02X%02X idPush=%02X%02X%02X%02X%02X",
            kMainStatWidth, ReadImm32(kMainStatCreateWndWidthImm), closeBtnX, apBtnX, detailBtnX,
            ReadImm8(kBtDetailPushY), ReadImm8(kBtDetailPushY + 1), ReadImm8(kBtDetailPushY + 2),
            ReadImm8(kBtDetailPushY + 3), ReadImm8(kBtDetailPushY + 4), ReadImm8(kBtDetailPushY + 5),
            ReadImm8(kBtDetailPushY + 6), ReadImm8(kBtDetailPushId), ReadImm8(kBtDetailPushId + 1),
            ReadImm8(kBtDetailPushId + 2), ReadImm8(kBtDetailPushId + 3),
            ReadImm8(kBtDetailPushId + 4));
    LogPatch(buf);
}

} // namespace

void EnsureHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    LogPatch("EnsureHooks: begin (vanilla rows intact, extras @9+)");
    PatchMainStatWindow();
    PatchWindowSize();
    ATTACH_HOOK(g_origDraw, Hook_CUIStatDetail_Draw);
    LogPatch("EnsureHooks: done");
}

} // namespace StatDetailExt
