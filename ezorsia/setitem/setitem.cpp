#include "stdafx.h"
#include "SetItemApi.h"
#include "SetItemData.h"
#include "equiptooltip_style.h"
#include "../fusionanvil/FusionAnvilApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../equipgrowth/EquipGrowthApi.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include <comdef.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace SetItemMod {
bool g_hooksAttached = false;
std::map<int, int> g_itemToSet;

struct LocalSetBonus {
    int reqCount = 0;
    short str = 0;
    short dex = 0;
    short int_ = 0;
    short luk = 0;
    short pad = 0;
    short mad = 0;
    short pdd = 0;
    short mdd = 0;
    short acc = 0;
    short eva = 0;
    short mhp = 0;
    short mmp = 0;
    short speed = 0;
    short jump = 0;
    short mhpR = 0;
    short mmpR = 0;
    // combat % (from Effect keys or Option fallback)
    short damR = 0;
    short bdR = 0;
    short nbdR = 0;
    short fdR = 0;
    short ignorePdr = 0;
    short ignoreMdr = 0;
};

struct LocalSetDef {
    std::string name;
    int completeCount = 0;
    std::vector<int> itemIds;
    std::map<int, LocalSetBonus> tiers;
};

static std::map<int, LocalSetDef> g_setDefs;

static constexpr uintptr_t kAddr_CWvsContext_Instance = 0x00BE7918;
static constexpr uintptr_t kOffset_CharacterData_InContext = 0x20B8;
static constexpr uintptr_t kAddr_CharacterData_GetItem = 0x004282F7;
static constexpr uintptr_t kAddr_TSecTypeGetData = 0x0042873D;
static constexpr uintptr_t kAddr_GetItemName = 0x005CF63E;

// Draw on canvas with custom IWzFont colors (SetToolTip_String2 does not parse rich-text).
enum class SetTipStyle {
    Title,         // set name ? bold lime, centered
    ActiveHeader,  // "N????" ? same lime as title
    White,         // equipped piece / active stat
    Grey,          // inactive piece / stat / unmet tier
    Orange,        // hover piece / attack labels
};

struct SetTipSegment {
    std::string text;
    SetTipStyle style = SetTipStyle::White;
};

enum class SetTipLayout {
    Left,           // default flow left (title headers / N????)
    Center,         // whole line horizontally centered (set name)
    IndentLeft,     // left + 1 CJK char (tier attribute lines)
    ItemSlotRight,  // name left, slot label right-aligned
};

struct SetTipLine {
    std::vector<SetTipSegment> segments;
    bool separatorBefore = false;
    bool tierGapAfter = false;
    SetTipLayout layout = SetTipLayout::Left;
};

// When true, equal-value pairs merge into one line (???/??, ????/????, ????).
// Set false to always list each attribute separately.
static constexpr bool kMergeEqualSetStats = true;

static constexpr int kSetTipPadX = 8;
static constexpr int kSetTipPadY = 6;          // was 10
static constexpr int kSetTipLineH = 16;         // was 14 ? slight gap between attribute rows
static constexpr int kSetTipTitleExtraH = 2;    // was 4
static constexpr int kSetTipSepGapY = 3;        // was +6 after separator
static constexpr int kSetTipTierGapY = 3;       // was +8 after tierGapAfter
static constexpr int kSetTipIndentChars = 1;    // attribute lines: indent 1 CJK
static constexpr unsigned int kSetTipSepColor = 0x80FFFFFF;
// ASCII " : " (space + ':' + space) ? never fullwidth U+FF1A.
static constexpr char kSetTipColonSep[] = "\x20\x3A\x20";
static constexpr size_t kSetTooltipBufSize = 0x600;
// Match reference tip: lime / yellow-green for title + "N????".
static constexpr unsigned long kColTitleLime = 0xFFCCFF00;
static constexpr unsigned long kColWhite = 0xFFFFFFFF;
static constexpr unsigned long kColGrey = 0xFFBBBBBB;
static constexpr unsigned long kColOrange = 0xFFFFCC00; // hover / slot gold-yellow

typedef void(__cdecl* GetBasicFont_t)(IWzFontPtr*, int);
typedef HRESULT(__thiscall* WzFontCreate_t)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto get_basic_font = reinterpret_cast<GetBasicFont_t>(0x0098A707);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(0x0046341A);

struct SetTipFonts {
    IWzFontPtr title;
    IWzFontPtr activeHeader;
    IWzFontPtr white;
    IWzFontPtr grey;
    IWzFontPtr orange;
    bool ready = false;
};

static SetTipFonts g_setTipFonts;

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static bool CreateSetFont(IWzFontPtr& out, unsigned long color, int size, bool bold) {
    if (out) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
        if (!out) {
            return false;
        }
        const Ztl_variant_t style(bold ? L"B" : L"");
        return SUCCEEDED(WzFontCreate(out, L"Dotum", size, color, style));
    } catch (...) {
        out = nullptr;
        return false;
    }
}

static void EnsureSetTipFonts(CUIToolTip* /*fontTip*/) {
    if (g_setTipFonts.ready) {
        return;
    }
    // Title and tier headers share the same lime color (reference tip).
    CreateSetFont(g_setTipFonts.title, kColTitleLime, 12, true);
    CreateSetFont(g_setTipFonts.activeHeader, kColTitleLime, 12, true);
    CreateSetFont(g_setTipFonts.white, kColWhite, 12, false);
    CreateSetFont(g_setTipFonts.grey, kColGrey, 12, false);
    CreateSetFont(g_setTipFonts.orange, kColOrange, 12, false);

    if (!g_setTipFonts.white) {
        try {
            get_basic_font(std::addressof(g_setTipFonts.white), 0);
            get_basic_font(std::addressof(g_setTipFonts.grey), 0);
            g_setTipFonts.title = g_setTipFonts.white;
            g_setTipFonts.activeHeader = g_setTipFonts.white;
            g_setTipFonts.orange = g_setTipFonts.white;
        } catch (...) {
        }
    }
    g_setTipFonts.ready = g_setTipFonts.white != nullptr;
}

static IWzFontPtr GetSetTipFont(SetTipStyle style) {
    switch (style) {
    case SetTipStyle::Title:
        return g_setTipFonts.title ? g_setTipFonts.title : g_setTipFonts.white;
    case SetTipStyle::ActiveHeader:
        return g_setTipFonts.activeHeader ? g_setTipFonts.activeHeader : g_setTipFonts.white;
    case SetTipStyle::Grey:
        return g_setTipFonts.grey ? g_setTipFonts.grey : g_setTipFonts.white;
    case SetTipStyle::Orange:
        return g_setTipFonts.orange ? g_setTipFonts.orange : g_setTipFonts.white;
    case SetTipStyle::White:
    default:
        return g_setTipFonts.white;
    }
}

static int MeasureGbkTextWidth(const char* text) {
    if (!text) {
        return 0;
    }
    int w = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        w += (*p & 0x80) ? 12 : 7;
    }
    return w;
}

// Prefer real IWzFont metrics ? byte estimates mis-size ASCII (HP/MP) vs CJK and
// make label right-edges look like the colon axis jumped.
static int MeasureFontTextWidth(IWzFontPtr font, const char* gbk) {
    if (!gbk || !gbk[0]) {
        return 0;
    }
    if (font) {
        try {
            return static_cast<int>(font->CalcTextWidth(GbkToBstr(gbk), Ztl_variant_t()));
        } catch (...) {
        }
    }
    return MeasureGbkTextWidth(gbk);
}

static int MeasureLineWidth(const SetTipLine& line) {
    if (line.layout == SetTipLayout::ItemSlotRight && line.segments.size() >= 2) {
        IWzFontPtr nameFont = GetSetTipFont(line.segments[0].style);
        IWzFontPtr slotFont = GetSetTipFont(line.segments[1].style);
        const int nameW = MeasureFontTextWidth(nameFont, line.segments[0].text.c_str());
        const int slotW = MeasureFontTextWidth(slotFont, line.segments[1].text.c_str());
        return nameW + 8 + slotW;
    }
    int w = 0;
    if (line.layout == SetTipLayout::IndentLeft) {
        w += MeasureFontTextWidth(GetSetTipFont(SetTipStyle::White), "\xA1\xA1")
                * kSetTipIndentChars;
    }
    for (const SetTipSegment& seg : line.segments) {
        w += MeasureFontTextWidth(GetSetTipFont(seg.style), seg.text.c_str());
    }
    return w;
}

static void DrawSetTipText(IWzCanvasPtr canvas, int x, int y, const char* text, IWzFontPtr font) {
    if (!canvas || !text || !text[0] || !font) {
        return;
    }
    try {
        canvas->DrawTextA(x, y, GbkToBstr(text), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static void DrawSetTipLine(IWzCanvasPtr canvas, int width, int y, const SetTipLine& line) {
    if (line.layout == SetTipLayout::ItemSlotRight && !line.segments.empty()) {
        const SetTipSegment& name = line.segments[0];
        IWzFontPtr nameFont = GetSetTipFont(name.style);
        DrawSetTipText(canvas, kSetTipPadX, y, name.text.c_str(), nameFont);
        if (line.segments.size() >= 2) {
            const SetTipSegment& slot = line.segments[1];
            IWzFontPtr slotFont = GetSetTipFont(slot.style);
            const int slotW = MeasureFontTextWidth(slotFont, slot.text.c_str());
            const int slotX = width - kSetTipPadX - slotW;
            DrawSetTipText(canvas, slotX, y, slot.text.c_str(), slotFont);
        }
        return;
    }

    const int lineW = MeasureLineWidth(line);
    int x = kSetTipPadX;
    if (line.layout == SetTipLayout::Center) {
        x = (std::max)(kSetTipPadX, (width - lineW) / 2);
    } else if (line.layout == SetTipLayout::IndentLeft) {
        // One CJK character indent for attribute lines.
        const int indent = MeasureFontTextWidth(GetSetTipFont(SetTipStyle::White), "\xA1\xA1")
                * kSetTipIndentChars;
        x = kSetTipPadX + indent;
    }
    for (const SetTipSegment& seg : line.segments) {
        if (seg.text.empty()) {
            continue;
        }
        IWzFontPtr font = GetSetTipFont(seg.style);
        DrawSetTipText(canvas, x, y, seg.text.c_str(), font);
        x += MeasureFontTextWidth(font, seg.text.c_str());
    }
}

static void DrawSetTipSeparator(IWzCanvasPtr canvas, int width, int y) {
    if (!canvas || width <= 2 * kSetTipPadX + 1) {
        return;
    }
    try {
        canvas->DrawRectangle(kSetTipPadX, y, width - 2 * kSetTipPadX, 1, kSetTipSepColor);
    } catch (...) {
    }
}

class GW_ItemSlotEquip {
public:
    MEMBER_AT(TSecType<int>, 0xC, nItemID)
};

void InitSetItemDatabase();

CUIToolTip* g_activeMainTooltip = nullptr;
GW_ItemSlotEquip* g_activeEquip = nullptr;
static alignas(8) char g_setTooltipBuf[kSetTooltipBufSize];
static bool g_setTooltipInited = false;
// Second buffer: set tip for the equipped item in the compare column.
static alignas(8) char g_cmpSetTooltipBuf[kSetTooltipBufSize];
static bool g_cmpSetTooltipInited = false;
static int g_cmpLastSetId = 0;
static int g_cmpLastItemId = 0;
static int g_cmpLastEquippedCount = 0;
static int g_cmpLastDockX = 0;
static int g_cmpLastDockY = 0;
// Third buffer: opposite-band set for the same slot family as hover (cash↔normal).
static alignas(8) char g_altSetTooltipBuf[kSetTooltipBufSize];
static bool g_altSetTooltipInited = false;
static int g_altLastSetId = 0;
static int g_altLastItemId = 0;
static int g_altLastEquippedCount = 0;
static int g_altLastDockX = 0;
static int g_altLastDockY = 0;
int g_lastLayoutX = 0;
int g_lastLayoutY = 0;
int g_lastSetId = 0;
int g_lastHoverItemId = 0;
int g_lastEquippedCount = 0;
// ShowItemToolTip primary hover — AfterEquipTipDrawn must not switch to other set pieces.
int g_primaryShowItemId = 0;
bool g_inTooltipUpdate = false;
bool g_uiHooksAttached = false;
static thread_local bool g_inShowItemToolTip = false;

struct ZRefOut {
    void* m_unused;
    void* m_pItem;
};

typedef void(__thiscall* CharacterData_GetItem_t)(void*, ZRefOut*, int, int);
typedef int(__thiscall* TSecTypeGetData_t)(const void*);
typedef void(__thiscall* GetItemName_t)(CItemInfo*, ZXString<char>*, int);

static auto CharacterData_GetItem =
        reinterpret_cast<CharacterData_GetItem_t>(kAddr_CharacterData_GetItem);
static auto TSecTypeGetData = reinterpret_cast<TSecTypeGetData_t>(kAddr_TSecTypeGetData);
static auto GetItemName = reinterpret_cast<GetItemName_t>(kAddr_GetItemName);

static bool IsEquipItemId(int itemId) {
    return itemId >= 1000000 && itemId < 2000000;
}

// TSecTypeGetData throws ZException on checksum failure ? must use C++ try/catch.
static int DecodeItemIdAt(const void* base, int offset) {
    if (!base) {
        return 0;
    }
    try {
        return TSecTypeGetData(reinterpret_cast<const char*>(base) + offset);
    } catch (...) {
        return 0;
    }
}

static int SafeGetItemId(GW_ItemSlotEquip* pe) {
    return DecodeItemIdAt(pe, 0xC);
}

// IDA 0x8F5C26: ShowItemToolTip reads item id from a4 + 0xC (all paths incl. backpack).
// Equip-only: used by set/growth companions — do not widen to non-equip.
static int ReadShowItemToolTipItemId(int* a4) {
    if (!a4) {
        return 0;
    }
    const int id = DecodeItemIdAt(a4, 0xC);
    return IsEquipItemId(id) ? id : 0;
}

// Any positive item id (equip / consume / etc.) for tip bottom "ID: n" line.
// Must stay separate from ReadShowItemToolTipItemId (equip filter).
static int ReadAnyShowItemToolTipItemId(int* a4) {
    if (!a4) {
        return 0;
    }
    const int id = DecodeItemIdAt(a4, 0xC);
    return id > 0 ? id : 0;
}

// Pending for Pet/Bundle early-MakeLayer canvas draw (those paths never PrintLines).
// Equip/Ring go through SetToolTip_Equip_Basic instead — CUIEquip/CUIPetEquip call
// SetToolTip_Equip directly and bypass ShowItemToolTip, so Basic is the reliable entry.
// Tip pointer match prevents main/compare cross-talk; thread_local for UI thread.
static thread_local CUIToolTip* g_pendingIdTip = nullptr;
static thread_local int g_pendingItemId = 0;
static thread_local bool g_pendingIdConsumed = false;

// AddInfoEx advances m_nHeight by fontH+4 (~13+4). Pet/Bundle canvas band matches that.
static constexpr int kItemTipIdLineAdvance = 17;
static constexpr int kItemTipIdDrawX = 12;

static void ClearPendingItemTipId() {
    g_pendingIdTip = nullptr;
    g_pendingItemId = 0;
    g_pendingIdConsumed = false;
}

static void SetPendingItemTipId(CUIToolTip* tip, int itemId) {
    if (!Client::showItemTipId || !tip || itemId <= 0) {
        ClearPendingItemTipId();
        return;
    }
    g_pendingIdTip = tip;
    g_pendingItemId = itemId;
    g_pendingIdConsumed = false;
}

static void MarkPendingItemTipIdConsumed(CUIToolTip* tip) {
    if (tip && tip == g_pendingIdTip) {
        g_pendingIdConsumed = true;
        g_pendingIdTip = nullptr;
    }
}

static int TipInfoLineCount(CUIToolTip* tip) {
    if (!tip) {
        return 0;
    }
    // CUIToolTip info-line count at +0x1C (IDA AddInfoEx / PrintLines).
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(tip) + 0x1C);
}

// Must run before MakeLayer bakes the canvas. After Original_ShowItemToolTip returns,
// AddInfoEx is a no-op for display (canvas already created).
// Split SEH vs C++ unwind: ZXString dtor cannot live in a __try function (C2712).
static void AppendItemIdLineImpl(CUIToolTip* tip, int itemId) {
    ZXString<char> sLine;
    sLine.Format("ID: %d", itemId);
    // Font 14/15: same AddInfoEx slot pair as equipcompare / fusionanvil tip lines.
    tip->AddInfoEx(14, 15, sLine, ZXString<char>(), 1, 1001);
}

static void AppendItemIdLine(CUIToolTip* tip, int itemId) {
    if (!tip || itemId <= 0 || !Client::showItemTipId) {
        return;
    }
    __try {
        AppendItemIdLineImpl(tip, itemId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static int FetchEquippedItemAt(void* pCharData, int pos) {
    ZRefOut out = {};
    try {
        CharacterData_GetItem(pCharData, &out, 1, pos);
        if (!out.m_pItem) {
            return 0;
        }
        return DecodeItemIdAt(out.m_pItem, 0xC);
    } catch (...) {
        return 0;
    }
}

static std::set<int> GetLocalEquippedItemIds() {
    std::set<int> ids;
    void* pCtx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
    if (!pCtx) {
        return ids;
    }
    void* pCharData = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(pCtx) + kOffset_CharacterData_InContext);
    if (!pCharData) {
        return ids;
    }
    // Include −62 (aux) / −61 (heart) + addon BP; prior −60..−1 missed body seats.
    // Cash: only vanilla fashion −101..−119. Sidecar cash −154..−162 is emptied by
    // EquipAddon GetItem hook (same ZRef as −bp) — scanning them double-counts set
    // pieces and wrongly activates unmet tiers (white/lime + bonus preview).
    for (int pos = -62; pos <= -1; ++pos) {
        const int itemId = FetchEquippedItemAt(pCharData, pos);
        if (itemId > 0) {
            ids.insert(itemId);
        }
    }
    for (int pos = -119; pos <= -101; ++pos) {
        const int itemId = FetchEquippedItemAt(pCharData, pos);
        if (itemId > 0) {
            ids.insert(itemId);
        }
    }
    return ids;
}

// Split normal vs cash equipped ids (for dual set tips + hover band detection).
static void CollectEquippedByBand(std::set<int>& outNormal, std::set<int>& outCash) {
    outNormal.clear();
    outCash.clear();
    void* pCtx = *reinterpret_cast<void**>(kAddr_CWvsContext_Instance);
    if (!pCtx) {
        return;
    }
    void* pCharData = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(pCtx) + kOffset_CharacterData_InContext);
    if (!pCharData) {
        return;
    }
    for (int pos = -62; pos <= -1; ++pos) {
        const int itemId = FetchEquippedItemAt(pCharData, pos);
        if (itemId > 0) {
            outNormal.insert(itemId);
        }
    }
    for (int pos = -119; pos <= -101; ++pos) {
        const int itemId = FetchEquippedItemAt(pCharData, pos);
        if (itemId > 0) {
            outCash.insert(itemId);
        }
    }
}

static std::string GetItemDisplayName(int itemId) {
    if (itemId <= 0) {
        return "";
    }
    try {
        ZXString<char> sName;
        GetItemName(CItemInfo::GetInstance(), &sName, itemId);
        const char* psz = static_cast<const char*>(sName);
        if (psz && psz[0] != '\0') {
            return std::string(psz);
        }
    } catch (...) {
    }
    return "";
}

static const char* GetEquipSlotLabel(int itemId) {
    switch (itemId / 10000) {
    case 100:
        return "\xC3\xB1\xD7\xD3";
    case 101:
        return "\xC1\xB3\xCA\xCE";
    case 102:
        return "\xD1\xDB\xCA\xCE";
    case 103:
        return "\xB6\xFA\xCA\xCE";
    case 104:
        return "\xC9\xCF\xD2\xC2";
    case 105:
        return "\xCC\xD7\xB7\xFE"; // 套服
    case 106:
        return "\xBF\xE3/\xC8\xB9";
    case 107:
        return "\xD0\xAC\xD7\xD3";
    case 108:
        return "\xCA\xD6\xCC\xD7";
    case 109:
        return "\xB6\xDC\xC5\xC6"; // 盾牌
    case 110:
        return "\xC5\xFB\xB7\xE7"; // 披风
    case 111:
        return "\xBD\xE4\xD6\xB8"; // 戒指
    case 112:
        return "\xCF\xEE\xC1\xB4"; // 项链
    case 113:
        return "\xD1\xFC\xB4\xF8"; // 腰带
    case 114:
        return "\xD1\xAB\xD5\xC2"; // 勋章
    case 115:
        return "\xBC\xE7\xCA\xCE"; // 肩饰
    case 116:
        return "\xBF\xDA\xB4\xFC\xB5\xC0\xBE\xDF"; // 口袋道具
    case 118:
        return "\xBB\xD5\xD5\xC2"; // 徽章
    case 119:
        return "\xCE\xC6\xD5\xC2"; // 纹章
    case 120:
        return "\xCD\xBC\xCC\xDA"; // 图腾
    case 134:
        return "\xCB\xAB\xB5\xB6"; // 双刀
    case 135:
        return "\xB8\xA8\xD6\xFA\xCE\xE4\xC6\xF7"; // 辅助武器
    case 166:
        return "\xD6\xC7\xC4\xDC\xBB\xFA\xC6\xF7\xC8\xCB"; // 智能机器人
    case 167:
        return "\xBB\xFA\xD0\xB5\xD0\xC4\xD4\xE0"; // 机械心脏
    default:
        if (itemId >= 1300000 && itemId < 1500000) {
            return "\xCE\xE4\xC6\xF7";
        }
        if (itemId / 10000 == 170) {
            return "\xCE\xE4\xC6\xF7";
        }
        return "";
    }
}

// Head-to-toe then accessories; weapons last. Same key ? stable_sort keeps order.
static int GetEquipSortKey(int itemId) {
    const int cat = itemId / 10000;
    if ((cat >= 130 && cat <= 149) || cat == 170) {
        return 1000 + cat;
    }
    switch (cat) {
    case 100: return 10;  // hat
    case 101: return 20;  // face
    case 102: return 30;  // eye
    case 103: return 40;  // ear
    case 104: return 50;  // top
    case 105: return 55;  // overall
    case 106: return 60;  // pants
    case 107: return 70;  // shoes
    case 108: return 80;  // gloves
    case 109: return 85;  // shield
    case 110: return 90;  // cape
    case 113: return 95;  // belt
    case 115: return 96;  // shoulder
    case 112: return 97;  // pendant
    case 111: return 98;  // ring
    case 114: return 99;  // medal
    default: return 200 + cat;
    }
}

static std::map<int, std::string> g_itemNameCache;

static std::string GetCachedItemDisplayName(int itemId) {
    const auto it = g_itemNameCache.find(itemId);
    if (it != g_itemNameCache.end()) {
        return it->second;
    }
    const std::string name = GetItemDisplayName(itemId);
    g_itemNameCache[itemId] = name;
    return name;
}

static const LocalSetDef* GetSetDef(int setId);
static bool TierHasStats(const LocalSetBonus& bonus);
static std::string GetLocalSetName(int setId);

static void AppendTierStatLine(std::vector<SetTipLine>& lines, SetTipStyle labelStyle,
        SetTipStyle valueStyle, const char* label, int value, const char* suffix = "") {
    if (value == 0) {
        return;
    }
    char left[80];
    char right[32];
    _snprintf_s(left, _countof(left), _TRUNCATE, "%s%s", label, kSetTipColonSep);
    _snprintf_s(right, _countof(right), _TRUNCATE, "+%d%s", value, suffix);
    SetTipLine line;
    line.layout = SetTipLayout::IndentLeft; // 1 CJK indent, left-aligned (not colon-center)
    line.segments.push_back({left, labelStyle});
    line.segments.push_back({right, valueStyle});
    lines.push_back(std::move(line));
}

static void AppendTierStats(std::vector<SetTipLine>& lines, const LocalSetBonus& bonus,
        bool active) {
    const SetTipStyle valueStyle = active ? SetTipStyle::White : SetTipStyle::Grey;
    const SetTipStyle nameStyle = active ? SetTipStyle::White : SetTipStyle::Grey;
    // ??? / ?? ? orange name when active
    const SetTipStyle atkStyle = active ? SetTipStyle::Orange : SetTipStyle::Grey;

    // ???equal ? ?????????
    if (kMergeEqualSetStats && bonus.str != 0 && bonus.str == bonus.dex && bonus.str == bonus.int_
            && bonus.str == bonus.luk) {
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xCB\xF9\xD3\xD0\xCA\xF4\xD0\xD4",
                bonus.str);
    } else {
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xC1\xA6\xC1\xBF", bonus.str);
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xC3\xF4\xBD\xDD", bonus.dex);
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xD6\xC7\xC1\xA6", bonus.int_);
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xD4\xCB\xC6\xF8", bonus.luk);
    }

    // ???/?? merge when equal
    if (kMergeEqualSetStats && bonus.pad != 0 && bonus.pad == bonus.mad) {
        AppendTierStatLine(lines, atkStyle, valueStyle,
                "\xB9\xA5\xBB\xF7\xC1\xA6/\xC4\xA7\xC1\xA6", bonus.pad);
    } else {
        AppendTierStatLine(lines, atkStyle, valueStyle, "\xB9\xA5\xBB\xF7\xC1\xA6", bonus.pad);
        AppendTierStatLine(lines, atkStyle, valueStyle, "\xC4\xA7\xC1\xA6", bonus.mad); // ??
    }

    // ????/???? merge when equal
    if (kMergeEqualSetStats && bonus.mhp != 0 && bonus.mhp == bonus.mmp) {
        AppendTierStatLine(lines, nameStyle, valueStyle,
                "\xD7\xEE\xB4\xF3\xD1\xAA\xC1\xBF/\xD7\xEE\xB4\xF3\xC4\xA7\xC1\xBF",
                bonus.mhp);
    } else {
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xD7\xEE\xB4\xF3\xD1\xAA\xC1\xBF",
                bonus.mhp);
        AppendTierStatLine(lines, nameStyle, valueStyle, "\xD7\xEE\xB4\xF3\xC4\xA7\xC1\xBF",
                bonus.mmp);
    }

    AppendTierStatLine(lines, nameStyle, valueStyle, "\xB7\xC0\xD3\xF9\xC1\xA6", bonus.pdd); // ???
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xC4\xA7\xB7\xC0", bonus.mdd);       // ??
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xC3\xFC\xD6\xD0\xC2\xCA", bonus.acc); // ???
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xBB\xD8\xB1\xDC\xC2\xCA", bonus.eva); // ???

    AppendTierStatLine(lines, nameStyle, valueStyle, "\xD2\xC6\xB6\xAF\xCB\xD9\xB6\xC8",
            bonus.speed);
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xCC\xF8\xD4\xBE\xC1\xA6", bonus.jump);

    // % HP/MP
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xD7\xEE\xB4\xF3\xD1\xAA\xC1\xBF",
            bonus.mhpR, "%");
    AppendTierStatLine(lines, nameStyle, valueStyle, "\xD7\xEE\xB4\xF3\xC4\xA7\xC1\xBF",
            bonus.mmpR, "%");

    // combat % ? GBK labels matching reference tip style
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xC9\xCB\xBA\xA6", bonus.damR, "%"); // ??
    // bdR: boss damage % (GBK: 攻击首领怪物时的伤害 — 怪=B9D6 not 关=B9D8)
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xB9\xA5\xBB\xF7\xCA\xD7\xC1\xEC\xB9\xD6\xCE\xEF\xCA\xB1\xB5\xC4\xC9\xCB\xBA\xA6",
            bonus.bdR, "%");
    // nbdR: normal-mob damage %
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xB6\xD4\xC6\xD5\xCD\xA8\xB9\xD6\xCE\xEF\xB5\xC4\xC9\xCB\xBA\xA6",
            bonus.nbdR, "%");
    // ignorePdr
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xCE\xDE\xCA\xD3\xB9\xD6\xCE\xEF\xB7\xC0\xD3\xF9\xC2\xCA",
            bonus.ignorePdr, "%");
    // ignoreMdr
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xCE\xDE\xCA\xD3\xB9\xD6\xCE\xEF\xC4\xA7\xB7\xA8\xB7\xC0\xD3\xF9\xC2\xCA",
            bonus.ignoreMdr, "%");
    // ????
    AppendTierStatLine(lines, atkStyle, valueStyle,
            "\xD7\xEE\xD6\xD5\xC9\xCB\xBA\xA6", bonus.fdR, "%");
}

static std::vector<SetTipLine> BuildSetTooltipLayout(int setId, int hoverItemId) {
    std::vector<SetTipLine> lines;
    const LocalSetDef* def = GetSetDef(setId);
    if (!def || def->itemIds.empty()) {
        return lines;
    }

    const std::set<int> equipped = GetLocalEquippedItemIds();
    int equippedCount = 0;
    for (int id : def->itemIds) {
        if (equipped.count(id) > 0) {
            ++equippedCount;
        }
    }

    {
        SetTipLine title;
        title.layout = SetTipLayout::Center;
        title.segments.push_back({def->name, SetTipStyle::Title});
        lines.push_back(std::move(title));
    }

    // Blank row above the equipment name list.
    lines.push_back(SetTipLine{});

    std::vector<int> sortedIds = def->itemIds;
    // Avoid std::stable_sort → ___std_rotate link flake across MSVC toolsets.
    std::sort(sortedIds.begin(), sortedIds.end(),
            [](int a, int b) { return GetEquipSortKey(a) < GetEquipSortKey(b); });

    for (int id : sortedIds) {
        const std::string name = GetCachedItemDisplayName(id);
        const char* slot = GetEquipSlotLabel(id);
        const bool isEquipped = equipped.count(id) > 0;
        const bool isHover = id == hoverItemId;
        // Reference tip: hover = gold name+slot; equipped = white; unequipped = grey.
        const SetTipStyle rowStyle = isHover
                ? SetTipStyle::Orange
                : (isEquipped ? SetTipStyle::White : SetTipStyle::Grey);

        SetTipLine itemLine;
        itemLine.layout = SetTipLayout::ItemSlotRight;
        itemLine.segments.push_back({name.empty() ? "?" : name, rowStyle});
        if (slot && slot[0] != '\0') {
            std::string slotText = "(";
            slotText += slot;
            slotText += ")";
            itemLine.segments.push_back({slotText, rowStyle});
        }
        lines.push_back(std::move(itemLine));
    }

    // Blank row below the equipment name list (before tier effects).
    lines.push_back(SetTipLine{});

    if (!def->tiers.empty()) {
        for (const auto& tierEntry : def->tiers) {
            const int req = tierEntry.first;
            const LocalSetBonus& bonus = tierEntry.second;
            if (!TierHasStats(bonus)) {
                continue;
            }
            // Off-by-one: "3件套效果" needs exactly equippedCount >= 3 (not > 2 / not > 3).
            // Unmet tiers stay Grey; met tiers use lime header + white/orange stats.
            const bool active = equippedCount >= req;
            // "N件套效果" — no indent; same lime as title when active.
            char header[48];
            _snprintf_s(header, _countof(header), _TRUNCATE,
                    "%d\xCC\xD7\xD7\xB0\xD0\xA7\xB9\xFB", req);
            SetTipLine headerLine;
            headerLine.separatorBefore = true;
            headerLine.layout = SetTipLayout::Left;
            headerLine.segments.push_back(
                    {header, active ? SetTipStyle::ActiveHeader : SetTipStyle::Grey});
            lines.push_back(std::move(headerLine));
            const size_t statsStart = lines.size();
            AppendTierStats(lines, bonus, active);
            if (lines.size() > statsStart) {
                lines.back().tierGapAfter = true;
            }
        }
    }

    if (SetItemData::g_finalDamagePercent > 0) {
        char buf[64];
        _snprintf_s(buf, _countof(buf), _TRUNCATE,
                "\xD7\xEE\xD6\xD5\xC9\xCB\xBA\xA6 +%d%%",
                SetItemData::g_finalDamagePercent);
        SetTipLine fdLine;
        fdLine.separatorBefore = true;
        fdLine.segments.push_back({buf, SetTipStyle::Orange});
        lines.push_back(std::move(fdLine));
    }

    return lines;
}

static std::vector<SetTipLine> BuildServerFallbackLayout(int setId) {
    std::vector<SetTipLine> lines;
    const char* serverText = SetItem::GetSetItemSkillBonusText(setId);
    if (serverText && serverText[0] != '\0') {
        const char* p = serverText;
        while (*p) {
            const char* eol = p;
            while (*eol && *eol != '\r' && *eol != '\n') {
                ++eol;
            }
            if (eol > p) {
                SetTipLine line;
                line.segments.push_back({std::string(p, eol), SetTipStyle::White});
                lines.push_back(std::move(line));
            }
            p = eol;
            while (*p == '\r' || *p == '\n') {
                ++p;
            }
        }
    } else {
        const std::string localName = GetLocalSetName(setId);
        if (!localName.empty()) {
            SetTipLine line;
            line.segments.push_back({localName, SetTipStyle::Title});
            line.layout = SetTipLayout::Center;
            lines.push_back(std::move(line));
        }
    }
    if (SetItemData::g_finalDamagePercent > 0) {
        char buf[64];
        _snprintf_s(buf, _countof(buf), _TRUNCATE,
                "\xD7\xEE\xD6\xD5\xC9\xCB\xBA\xA6 +%d%%",
                SetItemData::g_finalDamagePercent);
        SetTipLine fdLine;
        fdLine.separatorBefore = !lines.empty();
        fdLine.segments.push_back({buf, SetTipStyle::Orange});
        lines.push_back(std::move(fdLine));
    }
    return lines;
}

static int EstimateLayoutWidth(const std::vector<SetTipLine>& lines) {
    int maxLine = 0;
    for (const SetTipLine& line : lines) {
        maxLine = (std::max)(maxLine, MeasureLineWidth(line));
    }
    // Fallback only when hover tip width is unavailable.
    return (std::max)(180, (std::min)(320, maxLine + 2 * kSetTipPadX));
}

static int ResolveSetTipWidth(CUIToolTip* mainTip, const std::vector<SetTipLine>& lines) {
    int mainW = 0;
    if (mainTip) {
        __try {
            mainW = mainTip->m_nWidth;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            mainW = 0;
        }
    }
    if (mainW > 40) {
        return mainW;
    }
    return EstimateLayoutWidth(lines);
}

// Same Y walk as RenderSetTooltipCanvas + bottom pad ? exact content height.
static int ComputeSetTipContentHeight(const std::vector<SetTipLine>& lines) {
    int y = kSetTipPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const SetTipLine& line = lines[i];
        if (line.separatorBefore) {
            y += kSetTipSepGapY;
        }
        y += kSetTipLineH;
        if (i == 0 && line.layout == SetTipLayout::Center) {
            y += kSetTipTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kSetTipTierGapY;
        }
    }
    return y + kSetTipPadY;
}

static constexpr int kSetTipHeightSlack = 3;

// Calibrate String2 newline probe to shrink-fit targetH (same approach as FusionAnvil marker tip).
static int CreateSetTipLayerAtHeight(CUIToolTip* tip, int tipX, int tipY, int tipW, int targetH) {
    if (!tip || tipW <= 0 || targetH <= 0) {
        return 0;
    }

    ZXString<char> zTitle("");
    auto setLines = [&](int n) -> int {
        std::string s((std::max)(0, n), '\n');
        ZXString<char> zDesc(s.c_str());
        tip->ClearToolTip();
        tip->SetToolTip_String2(tipX, tipY, zTitle, zDesc, 0, 0, 0, tipW, 1, 0);
        return tip->m_nHeight;
    };

    const int h2 = setLines(2);
    const int h12 = setLines(12);
    const int perLine = (h12 > h2) ? (std::max)(1, (h12 - h2) / 10) : 14;
    const int base = h2 - 2 * perLine;
    int need = (std::max)(1, (targetH - base + perLine - 1) / perLine);
    setLines(need);

    for (int i = 0; i < 6 && tip->m_nHeight < targetH; ++i) {
        setLines(++need);
    }
    for (int i = 0; i < 8 && need > 1 && tip->m_nHeight > targetH + kSetTipHeightSlack; ++i) {
        const int prevH = tip->m_nHeight;
        setLines(--need);
        if (tip->m_nHeight < targetH) {
            setLines(++need);
            break;
        }
        if (tip->m_nHeight >= prevH) {
            break;
        }
    }
    return tip->m_nHeight > 0 ? tip->m_nHeight : targetH;
}

static void RenderSetTooltipCanvas(IWzCanvasPtr canvas, const std::vector<SetTipLine>& lines,
        int width) {
    if (!canvas || lines.empty()) {
        return;
    }
    int y = kSetTipPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const SetTipLine& line = lines[i];
        if (line.separatorBefore) {
            DrawSetTipSeparator(canvas, width, y - 2);
            y += kSetTipSepGapY;
        }
        DrawSetTipLine(canvas, width, y, line);
        y += kSetTipLineH;
        if (i == 0 && line.layout == SetTipLayout::Center) {
            y += kSetTipTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kSetTipTierGapY;
        }
    }
}

static IWzCanvasPtr GetTooltipCanvas(CUIToolTip* tip) {
    if (!tip || !tip->m_pLayer) {
        return nullptr;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        return tip->m_pLayer->Getcanvas(vIdx);
    } catch (...) {
        return nullptr;
    }
}

static int ReadWzInt(IWzPropertyPtr node, const wchar_t* key) {
    if (!node) {
        return 0;
    }
    try {
        Ztl_variant_t v;
        if (FAILED(node->get_item(const_cast<wchar_t*>(key), &v))) {
            return 0;
        }
        return get_int32(v, 0);
    } catch (...) {
        return 0;
    }
}

static void ApplyKnownSetOptionFallback(LocalSetBonus& bonus, int optionId) {
    // ??/???? Option 60024??????????? +10%?083 ItemOption ?????
    if (optionId == 60024) {
        bonus.bdR = static_cast<short>(bonus.bdR + 10);
    }
}

static void ParseEffectOptions(IWzPropertyPtr tierNode, LocalSetBonus& bonus) {
    if (!tierNode) {
        return;
    }
    try {
        Ztl_variant_t vOpt;
        if (FAILED(tierNode->get_item(const_cast<wchar_t*>(L"Option"), &vOpt))) {
            return;
        }
        IWzPropertyPtr optRoot(vOpt.GetUnknown(false, false));
        if (!optRoot) {
            return;
        }
        IUnknownPtr pEnumUnknown;
        if (FAILED(optRoot->get__NewEnum(&pEnumUnknown))) {
            return;
        }
        IEnumVARIANTPtr pEnum(pEnumUnknown);
        if (!pEnum) {
            return;
        }
        while (true) {
            VARIANT rgVar[1];
            ULONG uFetched = 0;
            if (FAILED(pEnum->Next(1, rgVar, &uFetched)) || uFetched == 0) {
                break;
            }
            if (rgVar[0].vt != VT_BSTR || !rgVar[0].bstrVal) {
                VariantClear(&rgVar[0]);
                continue;
            }
            Ztl_variant_t vChild;
            if (FAILED(optRoot->get_item(rgVar[0].bstrVal, &vChild))) {
                VariantClear(&rgVar[0]);
                continue;
            }
            IWzPropertyPtr child(vChild.GetUnknown(false, false));
            if (child) {
                const int optionId = ReadWzInt(child, L"option");
                if (optionId > 0) {
                    ApplyKnownSetOptionFallback(bonus, optionId);
                }
            }
            VariantClear(&rgVar[0]);
        }
    } catch (...) {
    }
}

static LocalSetBonus ParseEffectTier(IWzPropertyPtr tierNode, int reqCount) {
    LocalSetBonus bonus;
    bonus.reqCount = reqCount;
    bonus.str = static_cast<short>(ReadWzInt(tierNode, L"incSTR"));
    bonus.dex = static_cast<short>(ReadWzInt(tierNode, L"incDEX"));
    bonus.int_ = static_cast<short>(ReadWzInt(tierNode, L"incINT"));
    bonus.luk = static_cast<short>(ReadWzInt(tierNode, L"incLUK"));
    bonus.pad = static_cast<short>(ReadWzInt(tierNode, L"incPAD"));
    bonus.mad = static_cast<short>(ReadWzInt(tierNode, L"incMAD"));
    bonus.pdd = static_cast<short>(ReadWzInt(tierNode, L"incPDD"));
    bonus.mdd = static_cast<short>(ReadWzInt(tierNode, L"incMDD"));
    bonus.acc = static_cast<short>(ReadWzInt(tierNode, L"incACC"));
    bonus.eva = static_cast<short>(ReadWzInt(tierNode, L"incEVA"));
    bonus.mhp = static_cast<short>(ReadWzInt(tierNode, L"incMHP"));
    bonus.mmp = static_cast<short>(ReadWzInt(tierNode, L"incMMP"));
    bonus.speed = static_cast<short>(ReadWzInt(tierNode, L"incSpeed"));
    bonus.jump = static_cast<short>(ReadWzInt(tierNode, L"incJump"));
    bonus.mhpR = static_cast<short>(ReadWzInt(tierNode, L"incMHPr"));
    bonus.mmpR = static_cast<short>(ReadWzInt(tierNode, L"incMMPr"));
    bonus.damR = static_cast<short>(ReadWzInt(tierNode, L"damR"));
    bonus.bdR = static_cast<short>(ReadWzInt(tierNode, L"bdR"));
    bonus.nbdR = static_cast<short>(ReadWzInt(tierNode, L"nbdR"));
    bonus.fdR = static_cast<short>(ReadWzInt(tierNode, L"fdR"));
    if (bonus.fdR == 0) {
        // incDamage may be numeric or "10%"
        bonus.fdR = static_cast<short>(ReadWzInt(tierNode, L"incDamage"));
    }
    bonus.ignorePdr = static_cast<short>(ReadWzInt(tierNode, L"ignoreMobpdpR"));
    if (bonus.ignorePdr == 0) {
        bonus.ignorePdr = static_cast<short>(ReadWzInt(tierNode, L"ignoreTargetDEF"));
    }
    bonus.ignoreMdr = static_cast<short>(ReadWzInt(tierNode, L"ignoreMobmdR"));
    const int allStat = ReadWzInt(tierNode, L"incAllStat");
    if (allStat > 0) {
        bonus.str = static_cast<short>(bonus.str + allStat);
        bonus.dex = static_cast<short>(bonus.dex + allStat);
        bonus.int_ = static_cast<short>(bonus.int_ + allStat);
        bonus.luk = static_cast<short>(bonus.luk + allStat);
    }
    ParseEffectOptions(tierNode, bonus);
    return bonus;
}

static void CollectOrderedItemIds(IWzPropertyPtr node, std::vector<int>& out) {
    if (!node) {
        return;
    }
    try {
        IUnknownPtr pEnumUnknown;
        if (FAILED(node->get__NewEnum(&pEnumUnknown))) {
            return;
        }
        IEnumVARIANTPtr pEnum(pEnumUnknown);
        if (!pEnum) {
            return;
        }
        while (true) {
            VARIANT rgVar[1];
            ULONG uFetched = 0;
            if (FAILED(pEnum->Next(1, rgVar, &uFetched)) || uFetched == 0) {
                break;
            }
            if (rgVar[0].vt != VT_BSTR || !rgVar[0].bstrVal) {
                VariantClear(&rgVar[0]);
                continue;
            }
            Ztl_variant_t vItem;
            if (FAILED(node->get_item(rgVar[0].bstrVal, &vItem))) {
                VariantClear(&rgVar[0]);
                continue;
            }
            IWzPropertyPtr child(vItem.GetUnknown(false, false));
            if (child) {
                CollectOrderedItemIds(child, out);
            } else {
                const int itemId = get_int32(vItem, 0);
                if (itemId > 0) {
                    out.push_back(itemId);
                }
            }
            VariantClear(&rgVar[0]);
        }
    } catch (...) {
    }
}

static const LocalSetDef* GetSetDef(int setId) {
    const auto it = g_setDefs.find(setId);
    return it == g_setDefs.end() ? nullptr : &it->second;
}

static bool TierHasStats(const LocalSetBonus& bonus) {
    return bonus.str != 0 || bonus.dex != 0 || bonus.int_ != 0 || bonus.luk != 0 ||
            bonus.pad != 0 || bonus.mad != 0 || bonus.pdd != 0 || bonus.mdd != 0 ||
            bonus.acc != 0 || bonus.eva != 0 || bonus.mhp != 0 || bonus.mmp != 0 ||
            bonus.speed != 0 || bonus.jump != 0 || bonus.mhpR != 0 || bonus.mmpR != 0 ||
            bonus.damR != 0 || bonus.bdR != 0 || bonus.nbdR != 0 || bonus.fdR != 0 ||
            bonus.ignorePdr != 0 || bonus.ignoreMdr != 0;
}

static int LookupSetIdFromItemWz(int itemId) {
    if (itemId <= 0) {
        return 0;
    }
    try {
        IWzPropertyPtr pItem = CItemInfo::GetInstance()->GetItemInfo(itemId);
        if (!pItem) {
            return 0;
        }
        Ztl_variant_t vInfo;
        if (FAILED(pItem->get_item(const_cast<wchar_t*>(L"info"), &vInfo))) {
            return 0;
        }
        IWzPropertyPtr pInfo(vInfo.GetUnknown(false, false));
        if (!pInfo) {
            return 0;
        }
        Ztl_variant_t vSetId;
        if (FAILED(pInfo->get_item(const_cast<wchar_t*>(L"setItemID"), &vSetId))) {
            return 0;
        }
        return get_int32(vSetId, 0);
    } catch (...) {
        return 0;
    }
}

static int ResolveSetIdForItem(int itemId) {
    if (itemId <= 0) {
        return 0;
    }
    const auto it = g_itemToSet.find(itemId);
    if (it != g_itemToSet.end()) {
        return it->second;
    }
    const int setId = LookupSetIdFromItemWz(itemId);
    if (setId > 0) {
        g_itemToSet[itemId] = setId;
    }
    return setId;
}

static std::string GetLocalSetName(int setId) {
    if (setId <= 0) {
        return "";
    }
    try {
        wchar_t key[16];
        _snwprintf_s(key, _countof(key), _TRUNCATE, L"%d", setId);
        Ztl_variant_t vRoot = get_rm()->GetObjectA(L"Etc/SetItemInfo.img");
        IWzPropertyPtr pRoot(get_unknown(vRoot));
        if (!pRoot) {
            return "";
        }
        Ztl_variant_t vSet;
        if (FAILED(pRoot->get_item(key, &vSet))) {
            return "";
        }
        IWzPropertyPtr pSet(vSet.GetUnknown(false, false));
        if (!pSet) {
            return "";
        }
        Ztl_variant_t vName;
        if (FAILED(pSet->get_item(const_cast<wchar_t*>(L"setItemName"), &vName))) {
            return "";
        }
        if (vName.vt != VT_BSTR || !vName.bstrVal) {
            return "";
        }
        const int nWide = static_cast<int>(SysStringLen(vName.bstrVal));
        if (nWide <= 0) {
            return "";
        }
        const int nNarrow = WideCharToMultiByte(936, 0, vName.bstrVal, nWide,
                nullptr, 0, nullptr, nullptr);
        if (nNarrow <= 0) {
            return "";
        }
        std::string name(nNarrow, '\0');
        WideCharToMultiByte(936, 0, vName.bstrVal, nWide, name.data(), nNarrow,
                nullptr, nullptr);
        return name;
    } catch (...) {
        return "";
    }
}

static void EnsureSetItemDatabaseLoaded() {
    if (!g_setDefs.empty()) {
        return;
    }
    InitSetItemDatabase();
}

static CUIToolTip* EnsureSetTooltip() {
    if (!g_setTooltipInited) {
        g_setTooltipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
                g_setTooltipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_setTooltipBuf);
}

static CUIToolTip* EnsureCmpSetTooltip() {
    if (!g_cmpSetTooltipInited) {
        g_cmpSetTooltipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
                g_cmpSetTooltipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_cmpSetTooltipBuf);
}

static void HideAltSetTooltip();

static void ClearSetTooltipLayer(char* buf, bool inited) {
    if (!inited) {
        return;
    }
    reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(buf);
}

static void HideSetTooltip() {
    ClearSetTooltipLayer(g_setTooltipBuf, g_setTooltipInited);
    g_lastSetId = 0;
    g_lastHoverItemId = 0;
    g_lastEquippedCount = 0;
    // Keep g_primaryShowItemId — cleared only on real tip dismiss (!g_inShowItemToolTip).
    HideAltSetTooltip();
    // Compare tip may have been parked to the right of set — pull back beside hover.
    EquipCompare::RelayoutActiveCompareTip();
}

static void HideCmpSetTooltip() {
    if (g_cmpSetTooltipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
                g_cmpSetTooltipBuf);
    }
    g_cmpLastSetId = 0;
    g_cmpLastItemId = 0;
    g_cmpLastEquippedCount = 0;
    g_cmpLastDockX = 0;
    g_cmpLastDockY = 0;
}

static CUIToolTip* EnsureAltSetTooltip() {
    if (!g_altSetTooltipInited) {
        g_altSetTooltipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
                g_altSetTooltipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_altSetTooltipBuf);
}

static void HideAltSetTooltip() {
    if (g_altSetTooltipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
                g_altSetTooltipBuf);
    }
    g_altLastSetId = 0;
    g_altLastItemId = 0;
    g_altLastEquippedCount = 0;
    g_altLastDockX = 0;
    g_altLastDockY = 0;
}

bool TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    outX = outY = outW = outH = 0;
    if (!g_setTooltipInited || g_lastSetId <= 0) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_setTooltipBuf);
        if (!setTip || !setTip->m_pLayer) {
            return false;
        }
        outW = setTip->m_nWidth;
        outH = setTip->m_nHeight;
        // Layer size ready before m_nHeight in some create paths.
        if (outW <= 0) {
            outW = setTip->m_pLayer->width;
        }
        if (outH <= 0) {
            outH = setTip->m_pLayer->height;
        }
        // Prefer MakeLayer-stored / forced dock (shop/storage often leave rx/ry=0).
        const int storedX = setTip->m_nLayerLeft;
        const int storedY = setTip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else {
            outX = setTip->m_pLayer->rx;
            outY = setTip->m_pLayer->ry;
        }
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryGetActiveCompareSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    outX = outY = outW = outH = 0;
    if (!g_cmpSetTooltipInited || g_cmpLastSetId <= 0) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_cmpSetTooltipBuf);
        if (!setTip || !setTip->m_pLayer) {
            return false;
        }
        outW = setTip->m_nWidth;
        outH = setTip->m_nHeight;
        if (outW <= 0) {
            outW = setTip->m_pLayer->width;
        }
        if (outH <= 0) {
            outH = setTip->m_pLayer->height;
        }
        const int storedX = setTip->m_nLayerLeft;
        const int storedY = setTip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else if (g_cmpLastDockX != 0 || g_cmpLastDockY != 0) {
            outX = g_cmpLastDockX;
            outY = g_cmpLastDockY;
        } else {
            outX = setTip->m_pLayer->rx;
            outY = setTip->m_pLayer->ry;
        }
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool HasCmpSetTooltipLayer() {
    if (!g_cmpSetTooltipInited) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_cmpSetTooltipBuf);
        return setTip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int SafeReadTipWidth(CUIToolTip* setTip, int fallback) {
    int drawW = fallback;
    __try {
        if (setTip && setTip->m_nWidth > 0) {
            drawW = setTip->m_nWidth;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return drawW;
}

static bool ShowSetTooltipAt(CUIToolTip* setTip, int x, int y, int setId, int hoverItemId,
        CUIToolTip* fontTip, int preferredWidth) {
    if (!setTip) {
        return false;
    }
    std::vector<SetTipLine> lines = BuildSetTooltipLayout(setId, hoverItemId);
    if (lines.empty()) {
        lines = BuildServerFallbackLayout(setId);
    }
    if (lines.empty()) {
        return false;
    }

    EnsureSetTipFonts(fontTip);
    if (!g_setTipFonts.ready || !g_setTipFonts.white) {
        return false;
    }
    // Unmet tiers must stay visibly grey — retry if first CreateSetFont failed
    // (fallback GetSetTipFont(Grey) would otherwise paint white = false "active").
    if (!g_setTipFonts.grey) {
        CreateSetFont(g_setTipFonts.grey, kColGrey, 12, false);
    }
    // Orange names for PAD/MAD — retry if first CreateSetFont failed.
    if (!g_setTipFonts.orange) {
        CreateSetFont(g_setTipFonts.orange, kColOrange, 12, false);
    }

    setTip->ClearToolTip();

    const int width = preferredWidth > 40 ? preferredWidth : ResolveSetTipWidth(fontTip, lines);
    const int targetHeight = ComputeSetTipContentHeight(lines);

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    CreateSetTipLayerAtHeight(setTip, x, y, width, targetHeight);

    IWzCanvasPtr canvas = GetTooltipCanvas(setTip);
    if (!canvas) {
        return false;
    }
    int drawW = SafeReadTipWidth(setTip, width);
    try {
        const int cw = static_cast<int>(canvas->width);
        if (cw > 0) {
            drawW = cw;
        }
    } catch (...) {
    }
    RenderSetTooltipCanvas(canvas, lines, drawW);
    return true;
}

static int ReadTooltipPosX(CUIToolTip* mainTip, int layout) {
    if (mainTip && mainTip->m_pLayer) {
        try {
            const int rx = mainTip->m_pLayer->rx;
            if (rx > 0) {
                return rx;
            }
        } catch (...) {
        }
    }
    if (layout > 0x10000) {
        return *reinterpret_cast<int*>(reinterpret_cast<char*>(layout) + 98);
    }
    return 0;
}

static int ReadTooltipPosY(CUIToolTip* mainTip, int layout) {
    if (mainTip && mainTip->m_pLayer) {
        try {
            const int ry = mainTip->m_pLayer->ry;
            if (ry > 0) {
                return ry;
            }
        } catch (...) {
        }
    }
    if (layout > 0x10000) {
        return *reinterpret_cast<int*>(reinterpret_cast<char*>(layout) + 2);
    }
    return 0;
}

static bool HasSetTooltipLayer() {
    if (!g_setTooltipInited) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_setTooltipBuf);
        return setTip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Dock set tip to the RIGHT of the hovered equip tip: [hover] [set] [compare].
static constexpr int kSetTipGap = 4;

// Read where the main equip tip was placed. Prefer MakeLayer-stored nLeft/nTop
// (+0x14/+0x18): shop/storage/cash often leave layer rx/ry at 0 even when the tip
// is visible, which used to park the set companion at screen (0,0).
static void ReadMainTooltipOrigin(CUIToolTip* mainTip, int fallbackX, int fallbackY,
        int& mainX, int& mainY, int& mainW) {
    mainX = fallbackX;
    mainY = fallbackY;
    mainW = 0;
    if (!mainTip) {
        return;
    }
    __try {
        mainW = mainTip->m_nWidth;
        const int storedX = mainTip->m_nLayerLeft;
        const int storedY = mainTip->m_nLayerTop;
        // Stored MakeLayer coords are the ShowItemToolTip screen anchor.
        if (storedX != 0 || storedY != 0) {
            mainX = storedX;
            mainY = storedY;
        } else if (fallbackX != 0 || fallbackY != 0) {
            mainX = fallbackX;
            mainY = fallbackY;
        } else if (mainTip->m_pLayer) {
            mainX = mainTip->m_pLayer->rx;
            mainY = mainTip->m_pLayer->ry;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mainW = 0;
        mainX = fallbackX;
        mainY = fallbackY;
    }
}

// Dock set tip to the RIGHT of the hovered equip tip: [hover] [set] [growth?] [compare].
// Never flip to the left of the equip tip — keep the right-hand companion chain.
static void ComputeSetTooltipRightOfMain(CUIToolTip* mainTip, int setW,
        int& outX, int& outY) {
    int mainX = 0;
    int mainY = 0;
    int mainW = 0;
    ReadMainTooltipOrigin(mainTip, g_lastLayoutX, g_lastLayoutY, mainX, mainY, mainW);
    if (mainW <= 0) {
        mainW = 180;
    }
    if (setW <= 0) {
        setW = 180;
    }
    outX = mainX + mainW + kSetTipGap;
    outY = mainY;
    if (outY < 0) {
        outY = 0;
    }
    // Soft clamp only: keep right of equip; may sit partly off-screen on narrow views.
    if (outX < 0) {
        outX = 0;
    }
}

static int SafeGetSetTipWidth(CUIToolTip* setTip) {
    int tipW = 0;
    __try {
        if (setTip && setTip->m_nWidth > 0) {
            tipW = setTip->m_nWidth;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        tipW = 0;
    }
    return tipW;
}

static void SafeRelMoveSetTip(CUIToolTip* setTip, int setX, int setY) {
    __try {
        if (!setTip || !setTip->m_pLayer) {
            return;
        }
        // RelMove + force rx/ry (same as FusionAnvil companion tips). MakeLayer_hook
        // screen-clamp can leave companions at (0,0) when rs_width/size disagree.
        setTip->m_pLayer->RelMove(setX, setY);
        setTip->m_pLayer->rx = setX;
        setTip->m_pLayer->ry = setY;
        setTip->m_nLayerLeft = setX;
        setTip->m_nLayerTop = setY;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void ReadTipOrigin(CUIToolTip* tip, int& outX, int& outY, int& outW) {
    outX = outY = outW = 0;
    if (!tip) {
        return;
    }
    __try {
        outW = tip->m_nWidth;
        const int storedX = tip->m_nLayerLeft;
        const int storedY = tip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else if (tip->m_pLayer) {
            outX = tip->m_pLayer->rx;
            outY = tip->m_pLayer->ry;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outW = 0;
    }
}

static void ComputeCmpSetDockRightOfCompare(CUIToolTip* compareTip, int setW, int& outX,
        int& outY) {
    int cx = 0, cy = 0, cw = 0;
    ReadTipOrigin(compareTip, cx, cy, cw);
    if (cw <= 0) {
        cw = 180;
    }
    if (setW <= 0) {
        setW = 180;
    }
    outX = cx + cw + kSetTipGap;
    outY = cy;
    if (outY < 0) {
        outY = 0;
    }
    if (outX < 0) {
        outX = 0;
    }
}

void RelayoutCompareCompanion(CUIToolTip* compareTip) {
    if (!compareTip || g_cmpLastSetId <= 0 || !HasCmpSetTooltipLayer()) {
        return;
    }
    CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_cmpSetTooltipBuf);
    int tipW = SafeGetSetTipWidth(setTip);
    if (tipW <= 0) {
        tipW = 180;
    }
    int setX = 0, setY = 0;
    ComputeCmpSetDockRightOfCompare(compareTip, tipW, setX, setY);
    SafeRelMoveSetTip(setTip, setX, setY);
    g_cmpLastDockX = setX;
    g_cmpLastDockY = setY;
}

void HideCompareCompanion() {
    HideCmpSetTooltip();
}

void UpdateCompareCompanion(CUIToolTip* compareTip, int itemId) {
    if (!compareTip || !IsEquipItemId(itemId)) {
        HideCmpSetTooltip();
        return;
    }
    EnsureSetItemDatabaseLoaded();
    const int setId = ResolveSetIdForItem(itemId);
    if (setId <= 0) {
        HideCmpSetTooltip();
        return;
    }
    // Same set already shown beside hover — do not duplicate on the compare side.
    if (setId == g_lastSetId && g_lastSetId > 0 && HasSetTooltipLayer()) {
        HideCmpSetTooltip();
        return;
    }

    std::vector<SetTipLine> layout = BuildSetTooltipLayout(setId, itemId);
    if (layout.empty()) {
        layout = BuildServerFallbackLayout(setId);
    }
    if (layout.empty()) {
        HideCmpSetTooltip();
        return;
    }

    const std::set<int> equipped = GetLocalEquippedItemIds();
    int equippedCount = 0;
    const LocalSetDef* def = GetSetDef(setId);
    if (def) {
        for (int id : def->itemIds) {
            if (equipped.count(id) > 0) {
                ++equippedCount;
            }
        }
    }

    const int setW = ResolveSetTipWidth(compareTip, layout);
    CUIToolTip* setTip = EnsureCmpSetTooltip();

    if (setId == g_cmpLastSetId && itemId == g_cmpLastItemId &&
            equippedCount == g_cmpLastEquippedCount && HasCmpSetTooltipLayer()) {
        RelayoutCompareCompanion(compareTip);
        return;
    }

    g_cmpLastSetId = setId;
    g_cmpLastItemId = itemId;
    g_cmpLastEquippedCount = equippedCount;

    int setX = 0, setY = 0;
    ComputeCmpSetDockRightOfCompare(compareTip, setW, setX, setY);
    ShowSetTooltipAt(setTip, setX, setY, setId, itemId, compareTip, setW);
    const int renderedW = SafeGetSetTipWidth(setTip);
    if (renderedW > 0) {
        ComputeCmpSetDockRightOfCompare(compareTip, renderedW, setX, setY);
        SafeRelMoveSetTip(setTip, setX, setY);
    }
    g_cmpLastDockX = setX;
    g_cmpLastDockY = setY;
}

static bool HasAltSetTooltipLayer() {
    if (!g_altSetTooltipInited) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_altSetTooltipBuf);
        return setTip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryGetActiveAltSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    outX = outY = outW = outH = 0;
    if (!g_altSetTooltipInited || g_altLastSetId <= 0) {
        return false;
    }
    __try {
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_altSetTooltipBuf);
        if (!setTip || !setTip->m_pLayer) {
            return false;
        }
        outW = setTip->m_nWidth;
        outH = setTip->m_nHeight;
        if (outW <= 0) {
            outW = setTip->m_pLayer->width;
        }
        if (outH <= 0) {
            outH = setTip->m_pLayer->height;
        }
        const int storedX = setTip->m_nLayerLeft;
        const int storedY = setTip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else if (g_altLastDockX != 0 || g_altLastDockY != 0) {
            outX = g_altLastDockX;
            outY = g_altLastDockY;
        } else {
            outX = setTip->m_pLayer->rx;
            outY = setTip->m_pLayer->ry;
        }
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Equip slot family (100=hat … 113=belt …). Same family = same body-slot class.
static int EquipSlotFamily(int itemId) {
    if (itemId < 1000000) {
        return 0;
    }
    return itemId / 10000;
}

// Best set among `band` pieces that share hover's slot family (≠ hoverSetId).
// Does NOT fall back to unrelated sets (e.g. hat set while inspecting a belt).
static int BestRelatedSetIdInBand(const std::set<int>& band, int hoverSetId, int hoverItemId) {
    const int family = EquipSlotFamily(hoverItemId);
    if (family <= 0) {
        return 0;
    }
    std::map<int, int> counts;
    for (int id : band) {
        if (EquipSlotFamily(id) != family) {
            continue;
        }
        const int sid = ResolveSetIdForItem(id);
        if (sid > 0 && sid != hoverSetId) {
            ++counts[sid];
        }
    }
    int best = 0;
    int bestC = 0;
    for (const auto& kv : counts) {
        if (kv.second > bestC) {
            best = kv.first;
            bestC = kv.second;
        }
    }
    return best;
}

// Opposite-band set tip only when the hover item is equipped on one band and the
// other band has a same-slot-family set (cash belt ↔ normal belt).
// Bag hover must NOT pick the equipped same-slot set here — that belongs to the
// compare companion (right of compare tip). Doing so injected a middle tip that
// highlighted an arbitrary equipped piece (e.g. hat while comparing staves).
static int FindAltSetId(int hoverSetId, int hoverItemId) {
    if (hoverItemId <= 0 || EquipSlotFamily(hoverItemId) <= 0) {
        return 0;
    }
    std::set<int> normal;
    std::set<int> cash;
    CollectEquippedByBand(normal, cash);
    const bool hoverInCash = cash.count(hoverItemId) > 0;
    const bool hoverInNormal = normal.count(hoverItemId) > 0;
    if (hoverInCash && !hoverInNormal) {
        return BestRelatedSetIdInBand(normal, hoverSetId, hoverItemId);
    }
    if (hoverInNormal && !hoverInCash) {
        return BestRelatedSetIdInBand(cash, hoverSetId, hoverItemId);
    }
    // Bag / not equipped / on both bands: no alt tip.
    return 0;
}

static void ComputeAltSetDock(CUIToolTip* mainTip, int setW, int& outX, int& outY) {
    int gx = 0, gy = 0, gw = 0, gh = 0;
    if (EquipGrowth::TryGetActiveGrowthTooltipRect(gx, gy, gw, gh) && gw > 0) {
        outX = gx + gw + kSetTipGap;
        outY = gy;
    } else {
        int sx = 0, sy = 0, sw = 0, sh = 0;
        if (TryGetActiveSetTooltipRect(sx, sy, sw, sh) && sw > 0) {
            outX = sx + sw + kSetTipGap;
            outY = sy;
        } else {
            ComputeSetTooltipRightOfMain(mainTip, setW, outX, outY);
            return;
        }
    }
    if (outY < 0) {
        outY = 0;
    }
    if (outX < 0) {
        outX = 0;
    }
}

static void UpdateAltSetTooltip(CUIToolTip* mainTip, int hoverItemId, int hoverSetId) {
    if (!mainTip) {
        HideAltSetTooltip();
        return;
    }
    EnsureSetItemDatabaseLoaded();
    const int altSetId = FindAltSetId(hoverSetId, hoverItemId);
    if (altSetId <= 0) {
        HideAltSetTooltip();
        return;
    }
    // Representative piece for orange hover highlight in alt tip:
    // prefer same slot family as hover, else first equipped in set.
    int repItemId = 0;
    std::set<int> normal;
    std::set<int> cash;
    CollectEquippedByBand(normal, cash);
    const LocalSetDef* def = GetSetDef(altSetId);
    const int hoverFamily = EquipSlotFamily(hoverItemId);
    if (def) {
        for (int id : def->itemIds) {
            if (normal.count(id) == 0 && cash.count(id) == 0) {
                continue;
            }
            if (hoverFamily > 0 && EquipSlotFamily(id) == hoverFamily) {
                repItemId = id;
                break;
            }
            if (repItemId <= 0) {
                repItemId = id;
            }
        }
    }
    if (repItemId <= 0) {
        HideAltSetTooltip();
        return;
    }

    std::vector<SetTipLine> layout = BuildSetTooltipLayout(altSetId, repItemId);
    if (layout.empty()) {
        layout = BuildServerFallbackLayout(altSetId);
    }
    if (layout.empty()) {
        HideAltSetTooltip();
        return;
    }

    const std::set<int> equipped = GetLocalEquippedItemIds();
    int equippedCount = 0;
    if (def) {
        for (int id : def->itemIds) {
            if (equipped.count(id) > 0) {
                ++equippedCount;
            }
        }
    }

    const int setW = ResolveSetTipWidth(mainTip, layout);
    CUIToolTip* setTip = EnsureAltSetTooltip();

    if (altSetId == g_altLastSetId && repItemId == g_altLastItemId &&
            equippedCount == g_altLastEquippedCount && HasAltSetTooltipLayer()) {
        int tipW = SafeGetSetTipWidth(setTip);
        if (tipW <= 0) {
            tipW = setW;
        }
        int setX = 0, setY = 0;
        ComputeAltSetDock(mainTip, tipW, setX, setY);
        SafeRelMoveSetTip(setTip, setX, setY);
        g_altLastDockX = setX;
        g_altLastDockY = setY;
        return;
    }

    g_altLastSetId = altSetId;
    g_altLastItemId = repItemId;
    g_altLastEquippedCount = equippedCount;

    int setX = 0, setY = 0;
    ComputeAltSetDock(mainTip, setW, setX, setY);
    ShowSetTooltipAt(setTip, setX, setY, altSetId, repItemId, mainTip, setW);
    const int renderedW = SafeGetSetTipWidth(setTip);
    if (renderedW > 0) {
        ComputeAltSetDock(mainTip, renderedW, setX, setY);
        SafeRelMoveSetTip(setTip, setX, setY);
    }
    g_altLastDockX = setX;
    g_altLastDockY = setY;
}

static void UpdateSetTooltip(CUIToolTip* mainTip, int posX, int posY,
        int itemId, bool requestIfMissing) {
    if (g_inTooltipUpdate) {
        return;
    }
    g_inTooltipUpdate = true;

    try {
        g_activeMainTooltip = mainTip;
        // Keep last ShowItemToolTip screen anchor when callers pass (0,0).
        if (posX != 0 || posY != 0) {
            g_lastLayoutX = posX;
            g_lastLayoutY = posY;
        }

        EnsureSetItemDatabaseLoaded();
        if (!IsEquipItemId(itemId)) {
            HideSetTooltip();
            g_inTooltipUpdate = false;
            return;
        }

        const int setId = ResolveSetIdForItem(itemId);
        const bool hoverChanged = itemId != g_lastHoverItemId;
        const bool setChanged = setId != g_lastSetId;
        // Always drop stale layers before painting a different hover/set.
        if (hoverChanged || setChanged) {
            ClearSetTooltipLayer(g_setTooltipBuf, g_setTooltipInited);
            g_lastSetId = 0;
            g_lastEquippedCount = 0;
            if (hoverChanged) {
                HideAltSetTooltip();
            }
        }

        if (setId <= 0) {
            HideSetTooltip();
            g_lastHoverItemId = itemId;
            UpdateAltSetTooltip(mainTip, itemId, 0);
            EquipCompare::RelayoutActiveCompareTip();
            g_inTooltipUpdate = false;
            return;
        }

        if (requestIfMissing) {
            const char* cached = SetItem::GetSetItemSkillBonusText(setId);
            if (!cached || cached[0] == '\0') {
                SetItem::RequestSetItemBonus(setId);
            }
        }

        std::vector<SetTipLine> layout = BuildSetTooltipLayout(setId, itemId);
        if (layout.empty()) {
            layout = BuildServerFallbackLayout(setId);
        }
        if (layout.empty()) {
            HideSetTooltip();
            g_lastHoverItemId = itemId;
            UpdateAltSetTooltip(mainTip, itemId, setId);
            EquipCompare::RelayoutActiveCompareTip();
            g_inTooltipUpdate = false;
            return;
        }

        const std::set<int> equipped = GetLocalEquippedItemIds();
        int equippedCount = 0;
        const LocalSetDef* def = GetSetDef(setId);
        if (def) {
            for (int id : def->itemIds) {
                if (equipped.count(id) > 0) {
                    ++equippedCount;
                }
            }
        }

        const int setW = ResolveSetTipWidth(mainTip, layout);

        if (setId == g_lastSetId && itemId == g_lastHoverItemId &&
                equippedCount == g_lastEquippedCount && HasSetTooltipLayer()) {
            int setX = 0;
            int setY = 0;
            CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_setTooltipBuf);
            int tipW = SafeGetSetTipWidth(setTip);
            if (tipW <= 0) {
                tipW = setW;
            }
            ComputeSetTooltipRightOfMain(mainTip, tipW, setX, setY);
            SafeRelMoveSetTip(setTip, setX, setY);
            UpdateAltSetTooltip(mainTip, itemId, setId);
            EquipCompare::RelayoutActiveCompareTip();
            g_inTooltipUpdate = false;
            return;
        }

        g_lastSetId = setId;
        g_lastHoverItemId = itemId;
        g_lastEquippedCount = equippedCount;

        int setX = 0;
        int setY = 0;
        ComputeSetTooltipRightOfMain(mainTip, setW, setX, setY);
        if (!ShowSetTooltipAt(EnsureSetTooltip(), setX, setY, setId, itemId, mainTip, setW)) {
            ClearSetTooltipLayer(g_setTooltipBuf, g_setTooltipInited);
            g_lastSetId = 0;
            g_lastEquippedCount = 0;
            UpdateAltSetTooltip(mainTip, itemId, setId);
            EquipCompare::RelayoutActiveCompareTip();
            g_inTooltipUpdate = false;
            return;
        }
        // Re-anchor with actual rendered width (estimate can differ slightly).
        CUIToolTip* setTip = reinterpret_cast<CUIToolTip*>(g_setTooltipBuf);
        const int renderedW = SafeGetSetTipWidth(setTip);
        if (renderedW > 0) {
            ComputeSetTooltipRightOfMain(mainTip, renderedW, setX, setY);
            SafeRelMoveSetTip(setTip, setX, setY);
        }
        UpdateAltSetTooltip(mainTip, itemId, setId);
        EquipCompare::RelayoutActiveCompareTip();
    } catch (...) {
        HideSetTooltip();
    }

    g_inTooltipUpdate = false;
}

void InitSetItemDatabase() {
    if (!g_itemToSet.empty() && !g_setDefs.empty()) {
        return;
    }
    try {
        IWzResManPtr rm = get_rm();
        if (!rm) {
            std::cout << "[SetItem] InitSetItemDatabase: ResMan null" << std::endl;
            return;
        }
        Ztl_variant_t vRoot = rm->GetObjectA(L"Etc/SetItemInfo.img");
        IWzPropertyPtr pRoot(get_unknown(vRoot));
        if (!pRoot) {
            std::cout << "[SetItem] InitSetItemDatabase: SetItemInfo.img missing" << std::endl;
            return;
        }

        IUnknownPtr pEnumUnknown;
        if (FAILED(pRoot->get__NewEnum(&pEnumUnknown))) {
            return;
        }
        IEnumVARIANTPtr pEnum(pEnumUnknown);
        if (!pEnum) {
            return;
        }

        while (true) {
            VARIANT rgVar[1];
            ULONG uFetched = 0;
            if (FAILED(pEnum->Next(1, rgVar, &uFetched)) || uFetched == 0) {
                break;
            }
            if (rgVar[0].vt != VT_BSTR || !rgVar[0].bstrVal) {
                VariantClear(&rgVar[0]);
                continue;
            }

            const int setId = _wtoi(rgVar[0].bstrVal);
            if (setId <= 0) {
                VariantClear(&rgVar[0]);
                continue;
            }

            Ztl_variant_t vSet;
            if (FAILED(pRoot->get_item(rgVar[0].bstrVal, &vSet))) {
                VariantClear(&rgVar[0]);
                continue;
            }
            IWzPropertyPtr pSet(vSet.GetUnknown(false, false));
            if (!pSet) {
                VariantClear(&rgVar[0]);
                continue;
            }

            LocalSetDef def;
            def.name = GetLocalSetName(setId);
            if (def.name.empty()) {
                Ztl_variant_t vName;
                if (SUCCEEDED(pSet->get_item(const_cast<wchar_t*>(L"setItemName"), &vName)) &&
                        vName.vt == VT_BSTR && vName.bstrVal) {
                    const int nWide = static_cast<int>(SysStringLen(vName.bstrVal));
                    const int nNarrow = WideCharToMultiByte(
                            936, 0, vName.bstrVal, nWide, nullptr, 0, nullptr, nullptr);
                    if (nNarrow > 0) {
                        def.name.assign(nNarrow, '\0');
                        WideCharToMultiByte(936, 0, vName.bstrVal, nWide, def.name.data(),
                                nNarrow, nullptr, nullptr);
                    }
                }
            }
            def.completeCount = ReadWzInt(pSet, L"completeCount");

            Ztl_variant_t vItems;
            if (SUCCEEDED(pSet->get_item(const_cast<wchar_t*>(L"ItemID"), &vItems))) {
                IWzPropertyPtr pItems(vItems.GetUnknown(false, false));
                CollectOrderedItemIds(pItems, def.itemIds);
                for (int itemId : def.itemIds) {
                    g_itemToSet[itemId] = setId;
                }
            }

            Ztl_variant_t vEffect;
            if (SUCCEEDED(pSet->get_item(const_cast<wchar_t*>(L"Effect"), &vEffect))) {
                IWzPropertyPtr pEffect(vEffect.GetUnknown(false, false));
                if (pEffect) {
                    IUnknownPtr pEffectEnumUnknown;
                    if (SUCCEEDED(pEffect->get__NewEnum(&pEffectEnumUnknown))) {
                        IEnumVARIANTPtr pEffectEnum(pEffectEnumUnknown);
                        while (pEffectEnum) {
                            VARIANT tierVar[1];
                            ULONG tierFetched = 0;
                            if (FAILED(pEffectEnum->Next(1, tierVar, &tierFetched)) ||
                                    tierFetched == 0) {
                                break;
                            }
                            if (tierVar[0].vt != VT_BSTR || !tierVar[0].bstrVal) {
                                VariantClear(&tierVar[0]);
                                continue;
                            }
                            const int reqCount = _wtoi(tierVar[0].bstrVal);
                            if (reqCount > 0) {
                                Ztl_variant_t vTier;
                                if (SUCCEEDED(pEffect->get_item(tierVar[0].bstrVal, &vTier))) {
                                    IWzPropertyPtr pTier(vTier.GetUnknown(false, false));
                                    if (pTier) {
                                        def.tiers[reqCount] = ParseEffectTier(pTier, reqCount);
                                    }
                                }
                            }
                            VariantClear(&tierVar[0]);
                        }
                    }
                }
            }

            if (!def.itemIds.empty()) {
                g_setDefs[setId] = std::move(def);
            }
            VariantClear(&rgVar[0]);
        }
        std::cout << "[SetItem] InitSetItemDatabase loaded sets=" << g_setDefs.size()
                  << " items=" << g_itemToSet.size() << std::endl;
    } catch (...) {
        std::cout << "[SetItem] InitSetItemDatabase exception" << std::endl;
    }
}

// ---------------------------------------------------------------------------
// Skill window (+x) bonus display
// ---------------------------------------------------------------------------
static thread_local int g_skillFmtId = 0;

typedef int(__thiscall* SkillEntryCmp_t)(void* this_, unsigned int* skillEntry);
static auto Original_SkillEntryCmp =
        reinterpret_cast<SkillEntryCmp_t>(0x00A08E05);

// SanitizeSkillReqList REMOVED (2026-07-24 #3):
// It ran on EVERY SkillEntryCmp call (global Detour), not only CUISkill, and
// mutated skillEntry[+0x50]. With Skill.wz == V16 and cleaned char skills the
// cyclic-list hang hypothesis is obsolete; the global walk was itself a hang /
// corruption risk on K-open. Set-item skill (+x) display only needs fmt id.

int __fastcall Hook_SkillEntryCmp(void* this_, void* /*edx*/, unsigned int* skillEntry) {
    void* ret = _ReturnAddress();
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ret);
    if (skillEntry && addr >= 0x008AC000 && addr <= 0x008AD500) {
        g_skillFmtId = static_cast<int>(*skillEntry);
    }
    return Original_SkillEntryCmp(this_, skillEntry);
}

typedef int(__cdecl* SkillBonusFormat_t)(int out, char* fmt, int bonusVal);
static auto Real_SkillBonusFormat = reinterpret_cast<SkillBonusFormat_t>(0x00445B4B);

int __cdecl Hook_SkillBonusFormat(int out, char* fmt, int bonusVal) {
    if (g_skillFmtId > 0) {
        bonusVal += SetItem::GetSkillBonusLevel(g_skillFmtId);
    }
    return Real_SkillBonusFormat(out, fmt, bonusVal);
}

static void AttachSkillBonusHooks() {
    ATTACH_HOOK(Original_SkillEntryCmp, Hook_SkillEntryCmp);
    PatchCall(0x008ACB3F, &Hook_SkillBonusFormat, 5);
}

auto CUIToolTip__DrawToolTip_Equip =
        reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, GW_ItemSlotEquip*)>(
                ClientAddresses::SetItem::kDrawToolTipEquip);

static void SyncLayoutFromTipOnly(CUIToolTip* tip) {
    if (!tip) {
        return;
    }
    __try {
        if (tip->m_nLayerLeft != 0 || tip->m_nLayerTop != 0) {
            g_lastLayoutX = tip->m_nLayerLeft;
            g_lastLayoutY = tip->m_nLayerTop;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static void AfterEquipTipDrawn(CUIToolTip* pThis, GW_ItemSlotEquip* pe) {
    // Must run even inside ShowItemToolTip: that is when MakeLayer has just written
    // m_nLayerLeft/Top. Skipping here left bag/equip with no set tip whenever the
    // outer ShowItemToolTip item-id read missed (shop still painted via other paths).
    if (!pThis || !pe) {
        return;
    }
    // Equipped compare tip must not replace hover set/growth companions.
    if (EquipCompare::IsEquippedCompareTip(pThis) || EquipCompare::IsShowingCompareTip()) {
        return;
    }
    const int itemId = SafeGetItemId(pe);
    if (!IsEquipItemId(itemId)) {
        return;
    }
    // Other set-piece DrawToolTip_Equip must not steal set/growth companions.
    if (g_primaryShowItemId > 0 && itemId != g_primaryShowItemId) {
        return;
    }
    SyncLayoutFromTipOnly(pThis);
    const int setId = ResolveSetIdForItem(itemId);
    if (setId > 0) {
        g_activeEquip = pe;
        UpdateSetTooltip(pThis, g_lastLayoutX, g_lastLayoutY, itemId, true);
    } else if (g_primaryShowItemId > 0 && itemId == g_primaryShowItemId) {
        HideSetTooltip();
    }
    // Growth: only from post-ShowItemToolTip path to avoid double work while drawing.
    if (!g_inShowItemToolTip) {
        EquipGrowth_OnEquipTipDrawn(pThis, reinterpret_cast<::GW_ItemSlotEquip*>(pe));
    }
}

void __fastcall CUIToolTip__DrawToolTip_Equip_SetItem_hook(
        CUIToolTip* pThis, void* /*edx*/, int layout, GW_ItemSlotEquip* pe) {
    CUIToolTip__DrawToolTip_Equip(pThis, layout, pe);
    AfterEquipTipDrawn(pThis, pe);
}

auto CUIToolTip__ClearToolTip =
        reinterpret_cast<void(__thiscall*)(CUIToolTip*)>(
                ClientAddresses::kToolTipClear);

void __fastcall CUIToolTip__ClearToolTip_SetItem_hook(
        CUIToolTip* pThis, void* /*edx*/) {
    if (pThis == g_activeMainTooltip) {
        // Mid-ShowItemToolTip Clear rebuilds the main tip canvas. Do NOT wipe
        // companion state / primary hover lock — that caused flaky set tips on
        // CUIEquip (AfterEquipTipDrawn lost g_primaryShowItemId).
        if (!g_inShowItemToolTip) {
            HideSetTooltip();
            HideAltSetTooltip();
            HideCmpSetTooltip();
            EquipGrowth_Hide();
            g_activeMainTooltip = nullptr;
            g_activeEquip = nullptr;
            g_primaryShowItemId = 0;
            EquipCompare::ClearPendingCompare();
        }
    }
    CUIToolTip__ClearToolTip(pThis);
}

// IDA 0x8F5B20 stack (retn 0x20): arg0=nLeft, arg1=nTop, arg2=item*, …
// Same layout as EquipCompare / cashshop / storagebag — not __int64* POINT.
typedef int(__thiscall* ShowItemToolTip_t)(
        CUIToolTip* pThis,
        int nLeft,
        int nTop,
        int* item,
        void* param,
        int a6,
        int a7,
        int a8,
        unsigned int a9);
static auto Original_ShowItemToolTip = reinterpret_cast<ShowItemToolTip_t>(
        ClientAddresses::SetItem::kShowItemToolTip);

// IDA 0x8F3141 CUIToolTip::MakeLayer — Pet/Bundle early-bake path only.
// Equip/Ring ID is appended in SetToolTip_Equip_Basic (before footer height reserve).
// Detours chain: this hook outermost → rs MakeLayer_hook (clamp) → game.
typedef IWzCanvasPtr*(__thiscall* MakeLayer_t)(
        CUIToolTip* pThis,
        IWzCanvasPtr* result,
        int nLeft,
        int nTop,
        int bDoubleOutline,
        int bLogin,
        int bCharToolTip,
        unsigned int uColor);
static auto Original_MakeLayer = reinterpret_cast<MakeLayer_t>(0x008F3141);

// Pet/Bundle: MakeLayer runs right after SetBasicInfo (no AddInfoEx / PrintLines).
// Reserve a bottom band before bake, then paint ASCII on the canvas.
static void DrawItemIdOnTipCanvas(CUIToolTip* tip, int itemId) {
    if (!tip || itemId <= 0 || !Client::showItemTipId) {
        return;
    }
    EnsureSetTipFonts(tip);
    char buf[48] = {};
    sprintf_s(buf, "ID: %d", itemId);
    IWzCanvasPtr canvas = GetTooltipCanvas(tip);
    IWzFontPtr font = GetSetTipFont(SetTipStyle::White);
    if (!canvas || !font) {
        return;
    }
    // Height was bumped by kItemTipIdLineAdvance before MakeLayer; paint inside
    // that reserved band (not above it — old formula overlapped prior content).
    const int y = tip->m_nHeight - kItemTipIdLineAdvance + 2;
    if (y < 0) {
        return;
    }
    DrawSetTipText(canvas, kItemTipIdDrawX, y, buf, font);
}

// IDA 0x8ECA0C — append ID at end of Basic, BEFORE SetToolTip_Equip footer reserve
// (sub_8F4535 measure + optional +30). MakeLayer-time AddInfoEx landed inside that
// reserve and overlapped orange 剪刀/交易文案 (or left a huge gap).
// Also covers CUIEquip/CUIPetEquip OnMouseMove → SetToolTip_Equip (bypasses ShowItemToolTip).
typedef void(__thiscall* SetToolTipEquipBasic_t)(CUIToolTip* tip, GW_ItemSlotEquip* pe);
static auto Original_SetToolTipEquipBasic =
        reinterpret_cast<SetToolTipEquipBasic_t>(0x008ECA0C);

void __fastcall Hook_SetToolTipEquipBasic(
        CUIToolTip* tip,
        void* /*edx*/,
        GW_ItemSlotEquip* pe) {
    Original_SetToolTipEquipBasic(tip, pe);
    if (!Client::showItemTipId || !tip || !pe) {
        return;
    }
    const int itemId = SafeGetItemId(pe);
    if (itemId <= 0) {
        return;
    }
    AppendItemIdLine(tip, itemId);
    MarkPendingItemTipIdConsumed(tip);
}

IWzCanvasPtr* __fastcall Hook_MakeLayer(
        CUIToolTip* pThis,
        void* /*edx*/,
        IWzCanvasPtr* result,
        int nLeft,
        int nTop,
        int bDoubleOutline,
        int bLogin,
        int bCharToolTip,
        unsigned int uColor) {
    int canvasDrawId = 0;
    // Equip/Ring: Basic hook already AppendItemIdLine + consumed before footer reserve.
    // Never AddInfoEx here when TipInfoLineCount>0 — SetToolTip_Equip has already
    // measured orange footer into m_nHeight; late AddInfoEx lands in that band and
    // overlaps 剪刀/制炼书文案 (the original bug). Pet/Bundle only: lineCount==0.
    if (pThis &&
            pThis == g_pendingIdTip &&
            !g_pendingIdConsumed &&
            g_pendingItemId > 0 &&
            Client::showItemTipId) {
        if (TipInfoLineCount(pThis) == 0) {
            pThis->m_nHeight += kItemTipIdLineAdvance;
            canvasDrawId = g_pendingItemId;
        }
        // else: equip path should have been handled in Hook_SetToolTipEquipBasic;
        // drop pending rather than late-append into footer reserve.
        g_pendingIdConsumed = true;
        g_pendingIdTip = nullptr;
    }
    IWzCanvasPtr* layer = Original_MakeLayer(
            pThis, result, nLeft, nTop, bDoubleOutline, bLogin, bCharToolTip, uColor);
    if (canvasDrawId > 0) {
        DrawItemIdOnTipCanvas(pThis, canvasDrawId);
    }
    return layer;
}

static void SyncLayoutFromTip(CUIToolTip* tip) {
    if (!tip) {
        return;
    }
    __try {
        if (tip->m_nLayerLeft != 0 || tip->m_nLayerTop != 0) {
            g_lastLayoutX = tip->m_nLayerLeft;
            g_lastLayoutY = tip->m_nLayerTop;
            EquipTooltipStyle_NoteHoverPos(tip, g_lastLayoutX, g_lastLayoutY);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int __fastcall Hook_ShowItemToolTip(
        CUIToolTip* pThis,
        void* /*edx*/,
        int nLeft,
        int nTop,
        int* item,
        void* param,
        int a6,
        int a7,
        int a8,
        unsigned int a9) {
    // Always clear pending on exit so a missed MakeLayer cannot leak into the next tip.
    struct PendingItemTipIdScope {
        ~PendingItemTipIdScope() {
            ClearPendingItemTipId();
        }
    } pendingScope;

    // Compare tip nested ShowItemToolTip must not steal hover set/growth companions.
    // Still arm pending so compare tip gets its own "ID: n" (tip pointer match).
    if (EquipCompare::IsEquippedCompareTip(pThis) || EquipCompare::IsShowingCompareTip()) {
        if (item) {
            SetPendingItemTipId(pThis, ReadAnyShowItemToolTipItemId(item));
        }
        return Original_ShowItemToolTip(pThis, nLeft, nTop, item, param, a6, a7, a8, a9);
    }

    if (!item) {
        if (pThis == g_activeMainTooltip) {
            HideSetTooltip();
            g_activeMainTooltip = nullptr;
            g_activeEquip = nullptr;
            g_primaryShowItemId = 0;
        }
        EquipCompare::ClearPendingCompare();
        return Original_ShowItemToolTip(pThis, nLeft, nTop, item, param, a6, a7, a8, a9);
    }

    struct ShowItemToolTipScope {
        ShowItemToolTipScope() {
            g_inShowItemToolTip = true;
        }
        ~ShowItemToolTipScope() {
            g_inShowItemToolTip = false;
        }
    } scope;

    // Arm before Original: SetToolTip_* → MakeLayer (our hook Appends then bakes).
    SetPendingItemTipId(pThis, ReadAnyShowItemToolTipItemId(item));

    const int hoverItemId = ReadShowItemToolTipItemId(item);
    g_primaryShowItemId = hoverItemId;
    // Remember screen anchor from the call site (NPC shop / storage / cash / bag).
    g_lastLayoutX = nLeft;
    g_lastLayoutY = nTop;
    EquipTooltipStyle_NoteHoverPos(pThis, nLeft, nTop);
    const int result = Original_ShowItemToolTip(
            pThis, nLeft, nTop, item, param, a6, a7, a8, a9);

    // After MakeLayer, tip+0x14/+0x18 hold the true screen anchor.
    SyncLayoutFromTip(pThis);

    try {
        if (hoverItemId > 0 && ResolveSetIdForItem(hoverItemId) > 0) {
            UpdateSetTooltip(pThis, g_lastLayoutX, g_lastLayoutY, hoverItemId, true);
        } else if (pThis == g_activeMainTooltip) {
            HideSetTooltip();
            g_activeMainTooltip = nullptr;
            g_activeEquip = nullptr;
        }
        if (hoverItemId > 0) {
            // Same object as set tip: pass pe from item (GW_ItemSlotEquip*).
            EquipGrowth_OnEquipTipDrawn(
                    pThis, reinterpret_cast<::GW_ItemSlotEquip*>(item));
        } else if (pThis == g_activeMainTooltip) {
            EquipGrowth_Hide();
            g_primaryShowItemId = 0;
        }
    } catch (...) {
        HideSetTooltip();
        EquipGrowth_Hide();
        g_primaryShowItemId = 0;
    }

    // After hover companions: show compare to the right of set/growth (or hover).
    EquipCompare::FlushPendingAfterCompanions(
            pThis,
            g_lastLayoutX,
            g_lastLayoutY,
            item,
            a6,
            a7,
            a8,
            a9);

    return result;
}

void RedrawActivePanel() {
    if (!g_activeMainTooltip || g_lastHoverItemId <= 0) {
        return;
    }
    g_lastSetId = 0;
    g_lastEquippedCount = 0;
    UpdateSetTooltip(g_activeMainTooltip, g_lastLayoutX, g_lastLayoutY, g_lastHoverItemId, false);
}

void DismissHoverCompanionsIfMain(CUIToolTip* tip) {
    if (!tip || tip != g_activeMainTooltip) {
        return;
    }
    if (g_inShowItemToolTip) {
        return;
    }
    HideSetTooltip();
    HideAltSetTooltip();
    HideCmpSetTooltip();
    EquipGrowth_Hide();
    g_activeMainTooltip = nullptr;
    g_activeEquip = nullptr;
    g_primaryShowItemId = 0;
    EquipCompare::ClearPendingCompare();
}
} // namespace SetItemMod

void SetItem_RedrawActivePanel() {
    SetItemMod::RedrawActivePanel();
}

void SetItem_OnEquipTipDrawn(CUIToolTip* tip, GW_ItemSlotEquip* pe) {
    SetItemMod::AfterEquipTipDrawn(
            tip, reinterpret_cast<SetItemMod::GW_ItemSlotEquip*>(pe));
}

void SetItem_ReleaseCustomMainTipWithoutHidingSet(CUIToolTip* tip) {
    if (tip && tip == SetItemMod::g_activeMainTooltip) {
        SetItemMod::g_activeMainTooltip = nullptr;
    }
}

void SetItem_DismissHoverCompanionsIfMain(CUIToolTip* tip) {
    SetItemMod::DismissHoverCompanionsIfMain(tip);
}

bool SetItem_TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    return SetItemMod::TryGetActiveSetTooltipRect(outX, outY, outW, outH);
}

bool SetItem_TryGetActiveCompareSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    return SetItemMod::TryGetActiveCompareSetTooltipRect(outX, outY, outW, outH);
}

bool SetItem_TryGetActiveAltSetTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    return SetItemMod::TryGetActiveAltSetTooltipRect(outX, outY, outW, outH);
}

void SetItem_UpdateCompareCompanion(CUIToolTip* compareTip, int itemId) {
    SetItemMod::UpdateCompareCompanion(compareTip, itemId);
}

void SetItem_HideCompareCompanion() {
    SetItemMod::HideCompareCompanion();
}

void SetItem_RelayoutCompareCompanion(CUIToolTip* compareTip) {
    SetItemMod::RelayoutCompareCompanion(compareTip);
}

void AttachSetItemMod() {
    if (SetItemMod::g_hooksAttached) {
        return;
    }
    SetItemMod::g_hooksAttached = true;
    SetItemMod::InitSetItemDatabase();
    // HARD-OFF 2026-07-24: SkillEntryCmp global detour was a K-open hang risk
    // (ran on every skill compare; SanitizeSkillReqList already removed).
    // Set-item skill (+x) display disabled until K is stable again.
    // SetItemMod::AttachSkillBonusHooks();
}

void AttachSetItemUiHooks() {
    if (SetItemMod::g_uiHooksAttached) {
        return;
    }
    SetItemMod::g_uiHooksAttached = true;
    AttachEquipTooltipStyleHooks();
    ATTACH_HOOK(SetItemMod::Original_ShowItemToolTip, SetItemMod::Hook_ShowItemToolTip);
    // After EquipCompare's SetToolTip_Equip_Basic: Detours makes us outermost so ID
    // appends after compare-only stats and before SetToolTip_Equip footer reserve.
    ATTACH_HOOK(SetItemMod::Original_SetToolTipEquipBasic, SetItemMod::Hook_SetToolTipEquipBasic);
    // After rs MakeLayer_hook: Detours makes us outermost so Pet/Bundle reserve+draw
    // runs around bake+clamp.
    ATTACH_HOOK(SetItemMod::Original_MakeLayer, SetItemMod::Hook_MakeLayer);
    // Re-hook game DrawToolTip_Equip address so Detours chains us outermost over
    // FusionAnvil (Bind-to-trampoline previously skipped FA or failed to fire).
    SetItemMod::CUIToolTip__DrawToolTip_Equip =
            reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, SetItemMod::GW_ItemSlotEquip*)>(
                    ClientAddresses::SetItem::kDrawToolTipEquip);
    ATTACH_HOOK(SetItemMod::CUIToolTip__DrawToolTip_Equip,
                SetItemMod::CUIToolTip__DrawToolTip_Equip_SetItem_hook);
    ATTACH_HOOK(SetItemMod::CUIToolTip__ClearToolTip,
                SetItemMod::CUIToolTip__ClearToolTip_SetItem_hook);
    SetItemMod::EnsureSetItemDatabaseLoaded();
    std::cout << "[SetItem] EnsureUiHooks OK sets=" << SetItemMod::g_setDefs.size()
              << " items=" << SetItemMod::g_itemToSet.size()
              << " showItemTipId=" << (Client::showItemTipId ? "on" : "off") << std::endl;
}

int SetItem_GetSetIdForItem(int itemId) {
    SetItemMod::EnsureSetItemDatabaseLoaded();
    return SetItemMod::ResolveSetIdForItem(itemId);
}
