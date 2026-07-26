#include "stdafx.h"
#include "EquipCompareApi.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include "../setitem/SetItemApi.h"
#include "../setitem/equiptooltip_style.h"

#include <algorithm>
#include <cstring>

// Side-by-side equipped compare + delta rendering on the hovered tip.
//
// Delta rules (hover vs currently equipped):
//   both have attr  -> same line append (+N)/( -N)
//   only hover has  -> treat equipped as 0, show (+N) as gain
//   only equipped   -> PrintValue skipped (hover=0); append bottom line "0 (-N)"
//   anvil-only-eq   -> append appearance line: "(none) -> name"

namespace {

constexpr uintptr_t kAddrShowItemToolTip = ClientAddresses::SetItem::kShowItemToolTip;
constexpr uintptr_t kAddrToolTipClear = ClientAddresses::kToolTipClear;
constexpr uintptr_t kAddrToolTipCtor = ClientAddresses::kToolTipCtor;
constexpr uintptr_t kAddrItemToolTipParamCtor = 0x00483EED;
constexpr uintptr_t kAddrItemToolTipParamDtor = 0x00483F18;
constexpr uintptr_t kAddrPrintValue = 0x008E7836;
constexpr uintptr_t kAddrSetToolTipEquipBasic = 0x008ECA0C;
constexpr uintptr_t kAddrCWvsContext = 0x00BE7918;
constexpr uintptr_t kOffsetCharacterDataInContext = 0x20B8;
constexpr uintptr_t kAddrCharacterDataGetItem = 0x004282F7;
constexpr uintptr_t kAddrGetBodyPartFromItem = 0x004606A0;
constexpr uintptr_t kAddrTSecTypeGetData = 0x0042873D;
constexpr uintptr_t kAddrStringPoolGet = 0x0079E993;
constexpr uintptr_t kAddrStringPoolInstance = 0x0079E805;
constexpr uintptr_t kAddrGetItemName = 0x005CF63E;

constexpr size_t kToolTipBufSize = 0x520;
constexpr int kEquipInventoryType = 1;
constexpr int kCompareTooltipGap = 8;
constexpr int kShoulderItemCategory = 115;
// BP20 / inventory -20 (Sh). Was wrongly 49 (medal) — conflicts with 114.
constexpr int kShoulderBodyPart = 20;

// 外观 : / (无) / ->  / (装备独有)  in GBK
static const char kLabelAppearance[] = "\xCD\xE2\xB9\xD3 :";
static const char kWordNone[] = "(\xCE\xDE)";
static const char kArrowTo[] = " -> ";
static const char kSuffixEqOnly[] = " (\xD7\xB0\xB1\xB8\xB6\xC0\xD3\xD0)";

enum class EquipStat : int {
    STR = 0,
    DEX,
    INT,
    LUK,
    MaxHP,
    MaxMP,
    PAD,
    MAD,
    PDD,
    MDD,
    ACC,
    EVA,
    Craft,
    Speed,
    Jump,
    Count
};

// StringPool ids used by CUIToolTip::SetToolTip_Equip_Basic before PrintValue.
static constexpr unsigned kPoolIds[static_cast<int>(EquipStat::Count)] = {
    2042, 2043, 2044, 2045, 2046, 2047,
    657, 658, 659, 660, 661, 662, 663, 664, 665
};

struct GW_ItemSlotBase {
};

class GW_ItemSlotEquip : public GW_ItemSlotBase {
public:
    MEMBER_AT(TSecType<int>, 0xC, nItemID)
    MEMBER_AT(ZtlSecure<short>, 0x34, niSTR)
    MEMBER_AT(ZtlSecure<short>, 0x3C, niDEX)
    MEMBER_AT(ZtlSecure<short>, 0x44, niINT)
    MEMBER_AT(ZtlSecure<short>, 0x4C, niLUK)
    MEMBER_AT(ZtlSecure<short>, 0x54, niMaxHP)
    MEMBER_AT(ZtlSecure<short>, 0x5C, niMaxMP)
    MEMBER_AT(ZtlSecure<short>, 0x64, niPAD)
    MEMBER_AT(ZtlSecure<short>, 0x6C, niMAD)
    MEMBER_AT(ZtlSecure<short>, 0x74, niPDD)
    MEMBER_AT(ZtlSecure<short>, 0x7C, niMDD)
    MEMBER_AT(ZtlSecure<short>, 0x84, niACC)
    MEMBER_AT(ZtlSecure<short>, 0x8C, niEVA)
    MEMBER_AT(ZtlSecure<short>, 0x94, niCraft)
    MEMBER_AT(ZtlSecure<short>, 0x9C, niSpeed)
    MEMBER_AT(ZtlSecure<short>, 0xA4, niJump)
    MEMBER_AT(int, 0xF9, nAnvilItemID)
};

struct ZRefOut {
    void* unused;
    GW_ItemSlotBase* item;
};

using ShowItemToolTipFn = int(__thiscall*)(
    CUIToolTip*, int, int, GW_ItemSlotBase*, void*, int, int, int, unsigned int);
using ClearToolTipFn = void(__thiscall*)(CUIToolTip*);
using PrintValueFn = void(__thiscall*)(CUIToolTip*, int, int, ZXString<char>, int);
using SetToolTipEquipBasicFn = void(__thiscall*)(CUIToolTip*, GW_ItemSlotEquip*);
using TSecTypeGetDataFn = int(__thiscall*)(const void*);
using GetBodyPartFn = int(__cdecl*)(int, int, int*, int);
using GetItemFn = void(__thiscall*)(void*, ZRefOut*, int, int);
using StringPoolGetStringFn = ZXString<char>*(__fastcall*)(
    void* pThis, void* edx, ZXString<char>* result, unsigned int nIdx, char formal);

static auto Original_ShowItemToolTip =
    reinterpret_cast<ShowItemToolTipFn>(kAddrShowItemToolTip);
static auto Original_ClearToolTip =
    reinterpret_cast<ClearToolTipFn>(kAddrToolTipClear);
static auto Original_PrintValue =
    reinterpret_cast<PrintValueFn>(kAddrPrintValue);
static auto Original_SetToolTipEquipBasic =
    reinterpret_cast<SetToolTipEquipBasicFn>(kAddrSetToolTipEquipBasic);
static auto TSecTypeGetData =
    reinterpret_cast<TSecTypeGetDataFn>(kAddrTSecTypeGetData);

static alignas(8) char g_compareTooltipMem[kToolTipBufSize] = {};
static bool g_compareTooltipInit = false;
static bool g_showingCompareTooltip = false;
static bool g_compareTooltipActive = false;
static bool g_inPrimaryToolTipShow = false;
static bool g_inEquipBasicCompare = false;
static bool g_labelsReady = false;

static CUIToolTip* g_activeSourceTooltip = nullptr;
static CUIToolTip* g_deltaTooltip = nullptr;
static GW_ItemSlotEquip* g_deltaEquipped = nullptr;
static GW_ItemSlotEquip* g_deltaHover = nullptr;
static int g_activeSourceItemId = 0;
static int g_activeCompareItemId = 0;

static char g_labelBuf[static_cast<int>(EquipStat::Count)][64] = {};
static size_t g_labelLen[static_cast<int>(EquipStat::Count)] = {};

static CUIToolTip* CompareToolTip() {
    return reinterpret_cast<CUIToolTip*>(g_compareTooltipMem);
}

static int DecodeItemId(GW_ItemSlotBase* item) {
    if (!item) {
        return 0;
    }
    try {
        return TSecTypeGetData(reinterpret_cast<const char*>(item) + 0xC);
    } catch (...) {
        return 0;
    }
}

static short SafeShort(ZtlSecure<short>& v) {
    try {
        return static_cast<short>(v);
    } catch (...) {
        return 0;
    }
}

static int SafeAnvil(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    int id = 0;
    __try {
        id = pe->nAnvilItemID;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return id;
}

static int ReadStat(GW_ItemSlotEquip* pe, EquipStat st) {
    if (!pe) {
        return 0;
    }
    try {
        switch (st) {
        case EquipStat::STR: return SafeShort(pe->niSTR);
        case EquipStat::DEX: return SafeShort(pe->niDEX);
        case EquipStat::INT: return SafeShort(pe->niINT);
        case EquipStat::LUK: return SafeShort(pe->niLUK);
        case EquipStat::MaxHP: return SafeShort(pe->niMaxHP);
        case EquipStat::MaxMP: return SafeShort(pe->niMaxMP);
        case EquipStat::PAD: return SafeShort(pe->niPAD);
        case EquipStat::MAD: return SafeShort(pe->niMAD);
        case EquipStat::PDD: return SafeShort(pe->niPDD);
        case EquipStat::MDD: return SafeShort(pe->niMDD);
        case EquipStat::ACC: return SafeShort(pe->niACC);
        case EquipStat::EVA: return SafeShort(pe->niEVA);
        case EquipStat::Craft: return SafeShort(pe->niCraft);
        case EquipStat::Speed: return SafeShort(pe->niSpeed);
        case EquipStat::Jump: return SafeShort(pe->niJump);
        default: return 0;
        }
    } catch (...) {
        return 0;
    }
}

static void EnsureLabels() {
    if (g_labelsReady) {
        return;
    }
    auto* getPool = reinterpret_cast<void*(__cdecl*)()>(kAddrStringPoolInstance);
    auto getString = reinterpret_cast<StringPoolGetStringFn>(kAddrStringPoolGet);
    void* pool = getPool();
    if (!pool) {
        return;
    }
    for (int i = 0; i < static_cast<int>(EquipStat::Count); ++i) {
        ZXString<char> s;
        getString(pool, nullptr, &s, kPoolIds[i], 0);
        const char* p = static_cast<const char*>(s);
        if (!p) {
            continue;
        }
        size_t n = std::strlen(p);
        if (n >= sizeof(g_labelBuf[i])) {
            n = sizeof(g_labelBuf[i]) - 1;
        }
        std::memcpy(g_labelBuf[i], p, n);
        g_labelBuf[i][n] = 0;
        g_labelLen[i] = n;
    }
    g_labelsReady = true;
}

static int MatchStatByLabel(const char* prop) {
    if (!prop || !*prop) {
        return -1;
    }
    EnsureLabels();
    for (int i = 0; i < static_cast<int>(EquipStat::Count); ++i) {
        if (g_labelLen[i] == 0) {
            continue;
        }
        // Pool label is a prefix of PrintValue property (trailing spaces / colon ok).
        if (std::strncmp(prop, g_labelBuf[i], g_labelLen[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static bool IsCashEquipItem(int itemId) {
    if (itemId <= 0) {
        return false;
    }
    try {
        IWzPropertyPtr pItem = CItemInfo::GetInstance()->GetItemInfo(itemId);
        if (!pItem) {
            return false;
        }
        Ztl_variant_t vInfo;
        if (FAILED(pItem->get_item(const_cast<wchar_t*>(L"info"), &vInfo))) {
            return false;
        }
        IWzPropertyPtr pInfo(vInfo.GetUnknown(false, false));
        if (!pInfo) {
            return false;
        }
        Ztl_variant_t vCash;
        if (FAILED(pInfo->get_item(const_cast<wchar_t*>(L"cash"), &vCash))) {
            return false;
        }
        return get_int32(vCash, 0) != 0;
    } catch (...) {
        return false;
    }
}

static GW_ItemSlotBase* FetchInventoryItem(int inventoryType, int slot) {
    void* ctx = *reinterpret_cast<void**>(kAddrCWvsContext);
    if (!ctx) {
        return nullptr;
    }
    void* characterData = *reinterpret_cast<void**>(
        reinterpret_cast<char*>(ctx) + kOffsetCharacterDataInContext);
    if (!characterData) {
        return nullptr;
    }
    auto getItem = reinterpret_cast<GetItemFn>(kAddrCharacterDataGetItem);
    ZRefOut out = {};
    getItem(characterData, &out, inventoryType, slot);
    return out.item;
}

static bool TryGetBodyPartsForItem(int itemId, int* bodyParts, int* count) {
    *count = 0;
    if (itemId / 10000 == kShoulderItemCategory) {
        bodyParts[0] = kShoulderBodyPart;
        *count = 1;
        return true;
    }
    auto getBodyPart = reinterpret_cast<GetBodyPartFn>(kAddrGetBodyPartFromItem);
    __try {
        *count = getBodyPart(itemId, 0, bodyParts, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *count = 0;
    }
    if (*count > 0) {
        return true;
    }
    __try {
        *count = getBodyPart(itemId, 1, bodyParts, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *count = 0;
    }
    return *count > 0;
}

static GW_ItemSlotBase* FindEquippedCompareItem(int itemId, GW_ItemSlotBase* sourceItem) {
    if (itemId / 1000000 != 1) {
        return nullptr;
    }
    int bodyParts[8] = {};
    int count = 0;
    if (!TryGetBodyPartsForItem(itemId, bodyParts, &count)) {
        return nullptr;
    }
    const bool cash = IsCashEquipItem(itemId);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const int bodyPart = bodyParts[i];
            if (bodyPart <= 0) {
                continue;
            }
            const bool useCash = (pass == 0) ? cash : !cash;
            const int slot = useCash ? -(100 + bodyPart) : -bodyPart;
            GW_ItemSlotBase* equipped = FetchInventoryItem(kEquipInventoryType, slot);
            if (equipped && equipped != sourceItem) {
                return equipped;
            }
        }
    }
    return nullptr;
}

static void EnsureCompareToolTip() {
    if (g_compareTooltipInit) {
        return;
    }
    memset(g_compareTooltipMem, 0, sizeof(g_compareTooltipMem));
    reinterpret_cast<void(__thiscall*)(void*)>(kAddrToolTipCtor)(g_compareTooltipMem);
    g_compareTooltipInit = true;
}

static void ClearEquippedCompareToolTip() {
    if (!g_compareTooltipActive) {
        return;
    }
    if (g_compareTooltipInit) {
        Original_ClearToolTip(CompareToolTip());
    }
    g_compareTooltipActive = false;
    g_activeSourceTooltip = nullptr;
    g_activeSourceItemId = 0;
    g_activeCompareItemId = 0;
}

static bool IsEquippedCompareToolTip(CUIToolTip* tooltip) {
    return g_compareTooltipInit && tooltip == CompareToolTip();
}

static bool ShouldClearEquippedCompareOnToolTipClear(CUIToolTip* tooltip) {
    return g_compareTooltipActive && tooltip == g_activeSourceTooltip;
}

// Tip font slots (CUIToolTip ctor): green at +0x434 is orphaned (not in GetFont
// switch). Case2 at +0x428 is yellow — borrow green into that slot for +.
// Case10 at +0x44C is red (selectable) for -.
// IMPORTANT: AddInfoEx only STORES the subtype id; Draw re-resolves the font
// later. Remap must stay until the tip is cleared (not restored right after Add).
constexpr int kFontSubtypeWhite = 16;
constexpr int kFontSubtypeGreen = 2; // yellow slot remapped to green tip font
constexpr int kFontSubtypeRed = 10;
constexpr uintptr_t kOffFontGreen = 0x434;
constexpr uintptr_t kOffFontYellow = 0x428;

static CUIToolTip* g_greenRemapTip = nullptr;
static void* g_savedYellowFont = nullptr;

static void RestoreGreenFontRemap() {
    if (!g_greenRemapTip) {
        return;
    }
    *reinterpret_cast<void**>(reinterpret_cast<char*>(g_greenRemapTip) + kOffFontYellow) =
        g_savedYellowFont;
    g_greenRemapTip = nullptr;
    g_savedYellowFont = nullptr;
}

static void EnsureGreenFontRemap(CUIToolTip* tip) {
    if (!tip) {
        return;
    }
    if (g_greenRemapTip == tip) {
        return;
    }
    RestoreGreenFontRemap();
    char* base = reinterpret_cast<char*>(tip);
    void** yellow = reinterpret_cast<void**>(base + kOffFontYellow);
    g_savedYellowFont = *yellow;
    *yellow = *reinterpret_cast<void**>(base + kOffFontGreen);
    g_greenRemapTip = tip;
}

static void EmitCompareValueLine(
    CUIToolTip* tip,
    int nType,
    int hoverVal,
    int eqVal,
    ZXString<char> sProperty) {
    const int delta = hoverVal - eqVal;

    ZXString<char> sBase;
    if (nType == CUIToolTip::PT_INC) {
        if (hoverVal >= 0) {
            sBase.Format(" +%d", hoverVal);
        } else {
            sBase.Format(" %d", hoverVal);
        }
    } else {
        sBase.Format(" %d", hoverVal);
    }

    if (delta == 0) {
        tip->AddInfoEx(14, kFontSubtypeWhite, sProperty, sBase, 1, 1001);
        return;
    }

    ZXString<char> sDelta;
    if (delta > 0) {
        EnsureGreenFontRemap(tip);
        sDelta.Format(" (+%d)", delta);
    } else {
        sDelta.Format(" (%d)", delta);
    }

    ZXString<char> sLeft;
    sLeft.Format("%s%s",
                 static_cast<const char*>(sProperty),
                 static_cast<const char*>(sBase));
    const int colorFont = (delta > 0) ? kFontSubtypeGreen : kFontSubtypeRed;
    tip->AddInfoEx(14, colorFont, sLeft, sDelta, 1, 1001);
}

static void AppendEquippedOnlyStats(CUIToolTip* tip, GW_ItemSlotEquip* hover, GW_ItemSlotEquip* eq) {
    EnsureLabels();
    for (int i = 0; i < static_cast<int>(EquipStat::Count); ++i) {
        const int h = ReadStat(hover, static_cast<EquipStat>(i));
        const int e = ReadStat(eq, static_cast<EquipStat>(i));
        if (h != 0 || e <= 0) {
            continue;
        }
        // Hover has no line (PrintValue skipped); show as loss vs equipped.
        ZXString<char> prop;
        prop.Format("%s", g_labelBuf[i]);
        const int nType =
            (i >= static_cast<int>(EquipStat::PAD) && i <= static_cast<int>(EquipStat::MDD))
                ? CUIToolTip::PT_VALUE
                : CUIToolTip::PT_INC;
        EmitCompareValueLine(tip, nType, 0, e, prop);
    }

    const int hAnvil = SafeAnvil(hover);
    const int eAnvil = SafeAnvil(eq);
    if (hAnvil == 0 && eAnvil != 0) {
        ZXString<char> sName;
        reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
            kAddrGetItemName)(CItemInfo::GetInstance(), &sName, eAnvil);
        ZXString<char> sLine;
        sLine.Format("%s%s%s%s", kWordNone, kArrowTo,
                     static_cast<const char*>(sName), kSuffixEqOnly);
        ZXString<char> sLabel;
        sLabel.Format("%s", kLabelAppearance);
        tip->AddInfoEx(14, 15, sLabel, sLine, 1, 1001);
    }
}

static void ComputeCompareDockLeft(
    CUIToolTip* sourceTooltip,
    int sourceLeft,
    int sourceTop,
    int& outLeft,
    int& outTop) {
    outLeft = sourceLeft;
    outTop = sourceTop;
    if (sourceTooltip && sourceTooltip->m_nWidth > 0) {
        outLeft = sourceLeft + sourceTooltip->m_nWidth + kCompareTooltipGap;
    }
    int setX = 0;
    int setY = 0;
    int setW = 0;
    int setH = 0;
    if (SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0) {
        outLeft = setX + setW + kCompareTooltipGap;
        outTop = setY;
    }
    const int screenW = get_screen_width();
    int cmpW = 0;
    if (g_compareTooltipInit) {
        cmpW = CompareToolTip()->m_nWidth;
    }
    if (screenW > 0 && cmpW > 0 && outLeft + cmpW > screenW) {
        outLeft = (std::max)(0, screenW - cmpW);
    }
}

static void RelayoutActiveCompareTipImpl() {
    if (!g_compareTooltipActive || !g_compareTooltipInit || !g_activeSourceTooltip) {
        return;
    }
    CUIToolTip* cmp = CompareToolTip();
    if (!cmp || !cmp->m_pLayer) {
        return;
    }
    int sourceLeft = 0;
    int sourceTop = 0;
    __try {
        if (g_activeSourceTooltip->m_pLayer) {
            sourceLeft = g_activeSourceTooltip->m_pLayer->rx;
            sourceTop = g_activeSourceTooltip->m_pLayer->ry;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    int left = 0;
    int top = 0;
    ComputeCompareDockLeft(g_activeSourceTooltip, sourceLeft, sourceTop, left, top);
    __try {
        cmp->m_pLayer->RelMove(left, top);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ShowEquippedCompareToolTip(
    CUIToolTip* sourceTooltip,
    int sourceLeft,
    int sourceTop,
    GW_ItemSlotBase* sourceItem,
    int a6,
    int a7,
    int a8,
    unsigned int a9) {
    if (g_showingCompareTooltip || !sourceTooltip || !sourceItem) {
        return;
    }

    const int itemId = DecodeItemId(sourceItem);
    if (itemId / 1000000 != 1) {
        ClearEquippedCompareToolTip();
        return;
    }

    GW_ItemSlotBase* equippedItem = FindEquippedCompareItem(itemId, sourceItem);
    if (!equippedItem) {
        ClearEquippedCompareToolTip();
        return;
    }

    const int equippedItemId = DecodeItemId(equippedItem);
    int left = sourceLeft;
    int top = sourceTop;
    ComputeCompareDockLeft(sourceTooltip, sourceLeft, sourceTop, left, top);

    if (g_compareTooltipActive &&
        g_activeSourceItemId == itemId &&
        g_activeCompareItemId == equippedItemId &&
        CompareToolTip()->m_pLayer) {
        CompareToolTip()->m_pLayer->RelMove(left, top);
        g_activeSourceTooltip = sourceTooltip;
        return;
    }

    EnsureCompareToolTip();
    ClearEquippedCompareToolTip();

    unsigned char paramBuf[0x2C] = {};
    reinterpret_cast<void(__thiscall*)(void*)>(kAddrItemToolTipParamCtor)(paramBuf);

    g_showingCompareTooltip = true;
    __try {
        Original_ShowItemToolTip(
            CompareToolTip(),
            left,
            top,
            equippedItem,
            paramBuf,
            a6,
            a7,
            a8,
            a9);
        g_compareTooltipActive = true;
        g_activeSourceTooltip = sourceTooltip;
        g_activeSourceItemId = itemId;
        g_activeCompareItemId = equippedItemId;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_compareTooltipActive = false;
        g_activeSourceTooltip = nullptr;
        g_activeSourceItemId = 0;
        g_activeCompareItemId = 0;
    }
    g_showingCompareTooltip = false;
    reinterpret_cast<void(__thiscall*)(void*)>(kAddrItemToolTipParamDtor)(paramBuf);

    // Set tip may appear after this call — SetItem will Relayout; also try now.
    RelayoutActiveCompareTipImpl();
}

void __fastcall Hook_PrintValue(
    CUIToolTip* tip,
    void* /*edx*/,
    int nType,
    int nValue,
    ZXString<char> sProperty,
    int bShowAlways) {
    if (g_inEquipBasicCompare &&
        tip == g_deltaTooltip &&
        g_deltaEquipped &&
        (nType == CUIToolTip::PT_INC || nType == CUIToolTip::PT_VALUE)) {
        const char* prop = static_cast<const char*>(sProperty);
        const int idx = MatchStatByLabel(prop);
        if (idx >= 0) {
            const int eqVal = ReadStat(g_deltaEquipped, static_cast<EquipStat>(idx));
            if (!bShowAlways && nValue <= 0 && eqVal <= 0) {
                return;
            }
            if (!bShowAlways && nValue <= 0) {
                return;
            }
            EmitCompareValueLine(tip, nType, nValue, eqVal, sProperty);
            return;
        }
    }
    Original_PrintValue(tip, nType, nValue, sProperty, bShowAlways);
}

void __fastcall Hook_SetToolTipEquipBasic(
    CUIToolTip* tip,
    void* /*edx*/,
    GW_ItemSlotEquip* pe) {
    const bool doDelta =
        tip &&
        tip == g_deltaTooltip &&
        g_deltaEquipped &&
        pe == g_deltaHover &&
        !g_showingCompareTooltip;

    if (doDelta) {
        g_inEquipBasicCompare = true;
    }
    Original_SetToolTipEquipBasic(tip, pe);
    if (doDelta) {
        g_inEquipBasicCompare = false;
        AppendEquippedOnlyStats(tip, pe, g_deltaEquipped);
    }
}

int __fastcall Hook_ShowItemToolTip(
    CUIToolTip* tooltip,
    void* /*edx*/,
    int nLeft,
    int nTop,
    GW_ItemSlotBase* item,
    void* param,
    int a6,
    int a7,
    int a8,
    unsigned int a9) {
    if (IsEquippedCompareToolTip(tooltip) || g_showingCompareTooltip) {
        return Original_ShowItemToolTip(tooltip, nLeft, nTop, item, param, a6, a7, a8, a9);
    }

    GW_ItemSlotEquip* equipped = nullptr;
    if (item && DecodeItemId(item) / 1000000 == 1) {
        equipped = reinterpret_cast<GW_ItemSlotEquip*>(
            FindEquippedCompareItem(DecodeItemId(item), item));
    }

    g_deltaTooltip = tooltip;
    g_deltaHover = reinterpret_cast<GW_ItemSlotEquip*>(item);
    g_deltaEquipped = equipped;

    if (item && DecodeItemId(item) / 1000000 == 1) {
        EquipTooltipStyle_NoteHoverPos(tooltip, nLeft, nTop);
    }

    g_inPrimaryToolTipShow = true;
    const int result =
        Original_ShowItemToolTip(tooltip, nLeft, nTop, item, param, a6, a7, a8, a9);
    g_inPrimaryToolTipShow = false;

    g_deltaTooltip = nullptr;
    g_deltaHover = nullptr;
    g_deltaEquipped = nullptr;

    if (item && equipped) {
        ShowEquippedCompareToolTip(tooltip, nLeft, nTop, item, a6, a7, a8, a9);
    } else {
        ClearEquippedCompareToolTip();
    }
    return result;
}

void __fastcall Hook_ClearToolTip(CUIToolTip* tooltip, void* /*edx*/) {
    if (tooltip == g_greenRemapTip) {
        RestoreGreenFontRemap();
    }
    Original_ClearToolTip(tooltip);
    if (!g_inPrimaryToolTipShow &&
        !IsEquippedCompareToolTip(tooltip) &&
        ShouldClearEquippedCompareOnToolTipClear(tooltip)) {
        ClearEquippedCompareToolTip();
    }
}

} // namespace

void AttachEquipCompareMod() {
    EnsureLabels();
    ATTACH_HOOK(Original_ShowItemToolTip, Hook_ShowItemToolTip);
    ATTACH_HOOK(Original_ClearToolTip, Hook_ClearToolTip);
    ATTACH_HOOK(Original_PrintValue, Hook_PrintValue);
    // Chain after FusionAnvil's SetToolTip_Equip_Basic (transmog line stays).
    ATTACH_HOOK(Original_SetToolTipEquipBasic, Hook_SetToolTipEquipBasic);
}

namespace EquipCompare {
void RelayoutActiveCompareTip() {
    RelayoutActiveCompareTipImpl();
}
bool IsDeltaPrintValueActive() {
    return g_inEquipBasicCompare;
}
} // namespace EquipCompare
