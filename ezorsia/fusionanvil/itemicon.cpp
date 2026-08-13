#include "stdafx.h"
#include "compat/hook.h"
#include "compat/wvs/iteminfo.h"
#include "compat/ztl/ztl.h"
#include <intrin.h>

// ===========================================================================
// Potential grade overlay on inventory / equip icons (095 DrawGradeFrame style):
//   1) Colored rectangular border around equip icon
//   2) Small letter badge (C/B/A/S/SS) at top-right
//
// Stamp (grep DLL): HYPER_STARS_ROW_20260804
static const char kPotentialGradeUiStamp[] = "HYPER_STARS_ROW_20260804";
volatile const char* PotentialGradeUiStampRef() { return kPotentialGradeUiStamp; }
//
// IDA v083 (image base 0x400000):
//   CUIItem::Draw           0x0081DC20 .. 0x0081E255
//   call DrawItemIcon       0x0081DEE9  (esi = GW_ItemSlotEquip*)
//   CUIEquip::Draw          0x0083451B
//   call DrawItemIcon       0x008346F3  (esi = GW_ItemSlotEquip*)
//   DrawItemIconForSlot     0x005D6458
//
// Hook via PatchCall (NOT PatchJmp): keeps call/ret stack correct. The older
// PatchJmp naked path crashed on backpack open (bisect 2026-07-10).
// ===========================================================================

struct GW_ItemSlotBase : public ZRefCounted {
    virtual ~GW_ItemSlotBase() = 0;
    virtual int IsProtectedItem() = 0;
    virtual int IsPreventSlipItem() = 0;
    virtual int IsSupportWarmItem() = 0;
    virtual int IsBindedItem() = 0;
    virtual int IsPossibleTradingItem() = 0;
    virtual int GetType() = 0; // 1 = Equip
};

// Matches fusionanvil PacketCreator / tooltip Decode tail (size 0x140).
struct GW_ItemSlotEquipPot {
    MEMBER_AT(unsigned char, 0x10D, nEnhance)
    MEMBER_AT(unsigned char, 0x10E, nPotentialGrade)
    MEMBER_AT(int, 0x110, nPotential1)
    MEMBER_AT(int, 0x114, nPotential2)
    MEMBER_AT(int, 0x118, nPotential3)
};

static constexpr uintptr_t kAddr_DrawItemIconForSlot = 0x005D6458;
static constexpr uintptr_t kAddr_CUIItem_Call = 0x0081DEE9;
static constexpr uintptr_t kAddr_CUIEquip_Call = 0x008346F3;
static constexpr uintptr_t kAddr_WzFontCreate = 0x0046341A;

static constexpr int kIconSize = 32;

// 095 DrawGradeFrame ARGB (+ SS green). Index by BeiDou grade 1..5; [0]=hidden.
static constexpr unsigned long kGradeFrameArgb[6] = {
    0xFFFF4444u, // hidden / unrevealed
    0xFFB0B0B0u, // C
    0xFF5CA1FFu, // B ≈ Rare
    0xFFC24BFFu, // A ≈ Epic
    0xFFFFCC00u, // S ≈ Unique
    0xFF6ED86Eu, // SS ≈ Legendary
};

static GW_ItemSlotBase* g_ovItem = nullptr;
static IWzCanvas* g_ovCanvas = nullptr;
static int g_ovX = 0;
static int g_ovY = 0;

static IWzFontPtr g_badgeFonts[6];

typedef HRESULT(__thiscall* WzFontCreate_t)(
    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(kAddr_WzFontCreate);

static int SafeGetItemType(GW_ItemSlotBase* pItem) {
    if (!pItem) {
        return 0;
    }
    int nType = 0;
    __try {
        nType = pItem->GetType();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return nType;
}

static bool SafeReadPotential(
    GW_ItemSlotBase* pItem,
    unsigned char* outGrade,
    int* outP1,
    int* outP2,
    int* outP3)
{
    if (outGrade) {
        *outGrade = 0;
    }
    if (outP1) {
        *outP1 = 0;
    }
    if (outP2) {
        *outP2 = 0;
    }
    if (outP3) {
        *outP3 = 0;
    }
    if (!pItem || SafeGetItemType(pItem) != 1) {
        return false;
    }
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    __try {
        auto* pe = reinterpret_cast<GW_ItemSlotEquipPot*>(pItem);
        grade = pe->nPotentialGrade;
        p1 = pe->nPotential1;
        p2 = pe->nPotential2;
        p3 = pe->nPotential3;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // Guard CS/heap garbage (same idea as tooltip SafeGetHyperPotential).
    if (grade > 5) {
        grade = 0;
    }
    if (p1 < 0 || p1 > 70000) {
        p1 = 0;
    }
    if (p2 < 0 || p2 > 70000) {
        p2 = 0;
    }
    if (p3 < 0 || p3 > 70000) {
        p3 = 0;
    }
    if (outGrade) {
        *outGrade = grade;
    }
    if (outP1) {
        *outP1 = p1;
    }
    if (outP2) {
        *outP2 = p2;
    }
    if (outP3) {
        *outP3 = p3;
    }
    return grade > 0 || p1 > 0 || p2 > 0 || p3 > 0;
}

static IWzFontPtr EnsureBadgeFont(unsigned char grade) {
    unsigned char g = grade;
    if (g > 5) {
        g = 0;
    }
    if (g_badgeFonts[g]) {
        return g_badgeFonts[g];
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", g_badgeFonts[g], nullptr);
        if (!g_badgeFonts[g]) {
            return nullptr;
        }
        const Ztl_variant_t style(L"");
        // White letter on colored badge plate.
        if (FAILED(WzFontCreate(g_badgeFonts[g], L"Dotum", 11, 0xFFFFFFFFu, style))) {
            g_badgeFonts[g] = nullptr;
            return nullptr;
        }
        return g_badgeFonts[g];
    } catch (...) {
        g_badgeFonts[g] = nullptr;
        return nullptr;
    }
}

static const wchar_t* GradeLetterW(unsigned char grade) {
    switch (grade) {
    case 1: return L"C";
    case 2: return L"B";
    case 3: return L"A";
    case 4: return L"S";
    case 5: return L"SS";
    default: return L"?";
    }
}

static void DrawGradeFrameRects(IWzCanvas* canvas, int left, int top, int right, int bottom, unsigned long frameColor) {
    const int w = right - left;
    const int h = bottom - top;
    if (!canvas || w <= 0 || h <= 0) {
        return;
    }
    __try {
        canvas->DrawRectangle(left, top, static_cast<unsigned>(w), 1u, frameColor);
        canvas->DrawRectangle(left, bottom - 1, static_cast<unsigned>(w), 1u, frameColor);
        canvas->DrawRectangle(left, top, 1u, static_cast<unsigned>(h), frameColor);
        canvas->DrawRectangle(right - 1, top, 1u, static_cast<unsigned>(h), frameColor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void DrawHiddenChip(IWzCanvas* canvas, int right, int top, unsigned long frameColor) {
    if (!canvas) {
        return;
    }
    __try {
        canvas->DrawRectangle(right - 10, top + 1, 9u, 9u, frameColor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void DrawBadgePlate(IWzCanvas* canvas, int bx, int by, int badgeW, int badgeH, unsigned long frameColor) {
    if (!canvas) {
        return;
    }
    __try {
        canvas->DrawRectangle(
            bx, by, static_cast<unsigned>(badgeW), static_cast<unsigned>(badgeH), frameColor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void DrawBadgeLetter(IWzCanvas* canvas, int bx, int by, unsigned char grade) {
    if (!canvas || grade < 1 || grade > 5) {
        return;
    }
    IWzFontPtr font = EnsureBadgeFont(grade);
    if (!font) {
        return;
    }
    try {
        canvas->DrawTextA(
            bx + 1, by - 1, Ztl_bstr_t(GradeLetterW(grade)), font,
            Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static void DrawGradeFrameAndBadge(IWzCanvas* canvas, int x, int y, unsigned char grade, bool hidden) {
    if (!canvas) {
        return;
    }
    const unsigned long frameColor =
        hidden ? kGradeFrameArgb[0]
               : kGradeFrameArgb[(grade >= 1 && grade <= 5) ? grade : 1];

    // DrawItemIconForSlot: bottom-left baseline → icon covers [x, y-kIconSize]..[x+kIconSize, y].
    const int left = x;
    const int top = y - kIconSize;
    const int right = x + kIconSize;
    const int bottom = y;
    DrawGradeFrameRects(canvas, left, top, right, bottom, frameColor);

    if (hidden || grade < 1 || grade > 5) {
        DrawHiddenChip(canvas, right, top, frameColor);
        return;
    }

    const int badgeW = (grade == 5) ? 16 : 11;
    const int badgeH = 11;
    const int bx = right - badgeW - 1;
    const int by = top + 1;
    DrawBadgePlate(canvas, bx, by, badgeW, badgeH, frameColor);
    DrawBadgeLetter(canvas, bx, by, grade);
}

// Called after vanilla icon draw; uses thread-local-ish globals set by naked hook.
static void __cdecl AfterDrawIconGradeOverlay() {
    GW_ItemSlotBase* item = g_ovItem;
    IWzCanvas* canvas = g_ovCanvas;
    const int x = g_ovX;
    const int y = g_ovY;
    g_ovItem = nullptr;
    g_ovCanvas = nullptr;

    if (!item || !canvas) {
        return;
    }
    unsigned char grade = 0;
    int p1 = 0, p2 = 0, p3 = 0;
    if (!SafeReadPotential(item, &grade, &p1, &p2, &p3)) {
        return;
    }
    const bool hidden = (grade > 0 && p1 <= 0 && p2 <= 0 && p3 <= 0);
    unsigned char showGrade = grade;
    if (showGrade == 0) {
        // Revealed lines but grade byte missing — treat as B for color.
        showGrade = 2;
    }
    DrawGradeFrameAndBadge(canvas, x, y, showGrade, hidden);
}

// PatchCall entry: stack already has ret to next insn; ecx=CItemInfo*; esi=item*.
void __declspec(naked) DrawItemIconForSlot_CallSiteHook() {
    __asm {
        // [esp]=ret, [esp+4]=canvas, [esp+8]=itemId, [esp+0C]=x, [esp+10]=y, ...
        mov     g_ovItem, esi
        mov     eax, [esp+4]
        mov     g_ovCanvas, eax
        mov     eax, [esp+0Ch]
        mov     g_ovX, eax
        mov     eax, [esp+10h]
        mov     g_ovY, eax

        call    dword ptr [kAddr_DrawItemIconForSlot]
        // thiscall popped its stack args; [esp] is still return to Draw caller

        call    AfterDrawIconGradeOverlay
        ret
    }
}

namespace {
bool g_itemIconHooksAttached = false;
} // namespace

void AttachFusionAnvilItemIconHooks() {
    if (g_itemIconHooksAttached) {
        return;
    }
    g_itemIconHooksAttached = true;
    // Keep stamp string alive under /OPT:REF.
    (void)PotentialGradeUiStampRef();
    // PatchCall preserves call semantics (unlike the old PatchJmp that crashed).
    PatchCall(kAddr_CUIItem_Call, &DrawItemIconForSlot_CallSiteHook);
    PatchCall(kAddr_CUIEquip_Call, &DrawItemIconForSlot_CallSiteHook);
}
