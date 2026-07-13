#include "stdafx.h"
#include "SetItemApi.h"
#include "SetItemData.h"
#include "equiptooltip_style.h"
#include "../fusionanvil/FusionAnvilApi.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include <comdef.h>
#include <algorithm>
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
    short mhp = 0;
    short mmp = 0;
    short speed = 0;
    short jump = 0;
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
    Title,         // set name — bold green, centered
    ActiveHeader,  // "N件效果（已激活）" — bold green
    White,         // equipped piece / active stat
    Grey,          // inactive piece / stat / unmet tier
    Orange,        // hover piece / final damage emphasis
};

struct SetTipSegment {
    std::string text;
    SetTipStyle style = SetTipStyle::White;
};

struct SetTipLine {
    std::vector<SetTipSegment> segments;
    bool separatorBefore = false;
    bool centered = false;
    bool tierGapAfter = false;
};

static constexpr int kSetTipPadX = 8;
static constexpr int kSetTipPadY = 10;
static constexpr int kSetTipLineH = 18;
static constexpr int kSetTipTitleExtraH = 4;
static constexpr unsigned int kSetTipSepColor = 0x80FFFFFF;
static constexpr size_t kSetTooltipBufSize = 0x600;
static constexpr unsigned long kColTitleGreen = 0xFF00CC00;
static constexpr unsigned long kColActiveGreen = 0xFF33DD33;
static constexpr unsigned long kColWhite = 0xFFFFFFFF;
static constexpr unsigned long kColGrey = 0xFFBBBBBB;
static constexpr unsigned long kColOrange = 0xFFFF9900;

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
    CreateSetFont(g_setTipFonts.title, kColTitleGreen, 12, true);
    CreateSetFont(g_setTipFonts.activeHeader, kColActiveGreen, 12, true);
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

static int MeasureLineWidth(const SetTipLine& line) {
    int w = 0;
    for (const SetTipSegment& seg : line.segments) {
        w += MeasureGbkTextWidth(seg.text.c_str());
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
    const int lineW = MeasureLineWidth(line);
    int x = line.centered ? (std::max)(kSetTipPadX, (width - lineW) / 2) : kSetTipPadX;
    for (const SetTipSegment& seg : line.segments) {
        if (seg.text.empty()) {
            continue;
        }
        IWzFontPtr font = GetSetTipFont(seg.style);
        DrawSetTipText(canvas, x, y, seg.text.c_str(), font);
        x += MeasureGbkTextWidth(seg.text.c_str());
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
int g_lastLayoutX = 0;
int g_lastLayoutY = 0;
int g_lastSetId = 0;
int g_lastHoverItemId = 0;
int g_lastEquippedCount = 0;
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

// TSecTypeGetData throws ZException on checksum failure — must use C++ try/catch.
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
static int ReadShowItemToolTipItemId(int* a4) {
    if (!a4) {
        return 0;
    }
    const int id = DecodeItemIdAt(a4, 0xC);
    return IsEquipItemId(id) ? id : 0;
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
    for (int pos = -60; pos <= -1; ++pos) {
        const int itemId = FetchEquippedItemAt(pCharData, pos);
        if (itemId > 0) {
            ids.insert(itemId);
        }
    }
    return ids;
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
        return "\xC1\xAC\xD2\xC2\xB7\xFE";
    case 106:
        return "\xBF\xE3/\xC8\xB9";
    case 107:
        return "\xD0\xAC\xD7\xD3";
    case 108:
        return "\xCA\xD6\xCC\xD7";
    case 109:
        return "\xB6\xB7\xC5\xF0";
    case 110:
        return "\xB6\xDC\xC5\xC6";
    default:
        if (itemId >= 1300000 && itemId < 1500000) {
            return "\xCE\xE4\xC6\xF7";
        }
        return "";
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

static void AppendTierStatLine(std::vector<SetTipLine>& lines, SetTipStyle style,
        const char* label, int value, const char* suffix = "") {
    if (value == 0) {
        return;
    }
    char buf[96];
    _snprintf_s(buf, _countof(buf), _TRUNCATE, "%s : +%d%s", label, value, suffix);
    SetTipLine line;
    line.segments.push_back({buf, style});
    lines.push_back(std::move(line));
}

static void AppendTierStats(std::vector<SetTipLine>& lines, const LocalSetBonus& bonus,
        bool active) {
    const SetTipStyle statStyle = active ? SetTipStyle::White : SetTipStyle::Grey;
    AppendTierStatLine(lines, statStyle, "\xC1\xA6\xC1\xBF", bonus.str);
    AppendTierStatLine(lines, statStyle, "\xC3\xF4\xBD\xDD", bonus.dex);
    AppendTierStatLine(lines, statStyle, "\xD6\xC7\xC1\xA6", bonus.int_);
    AppendTierStatLine(lines, statStyle, "\xD4\xCB\xC6\xF8", bonus.luk);
    AppendTierStatLine(lines, statStyle, "\xB9\xA5\xBB\xF7\xC1\xA6", bonus.pad);
    AppendTierStatLine(lines, statStyle, "\xC4\xA7\xB7\xA8\xB9\xA5\xBB\xF7\xC1\xA6", bonus.mad);
    AppendTierStatLine(lines, statStyle, "HP", bonus.mhp);
    AppendTierStatLine(lines, statStyle, "MP", bonus.mmp);
    AppendTierStatLine(lines, statStyle, "\xD2\xC6\xB6\xAF\xCB\xD9\xB6\xC8", bonus.speed);
    AppendTierStatLine(lines, statStyle, "\xCC\xF8\xD4\xBE\xC1\xA6", bonus.jump);
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
        title.centered = true;
        title.segments.push_back({def->name, SetTipStyle::Title});
        lines.push_back(std::move(title));
    }

    for (int id : def->itemIds) {
        const std::string name = GetCachedItemDisplayName(id);
        const char* slot = GetEquipSlotLabel(id);
        const bool isEquipped = equipped.count(id) > 0;
        const bool isHover = id == hoverItemId;
        const SetTipStyle nameStyle =
                isHover ? SetTipStyle::Orange : (isEquipped ? SetTipStyle::White : SetTipStyle::Grey);
        const SetTipStyle slotStyle =
                isHover ? SetTipStyle::Grey : (isEquipped ? SetTipStyle::White : SetTipStyle::Grey);

        SetTipLine itemLine;
        itemLine.segments.push_back({name.empty() ? "?" : name, nameStyle});
        if (slot && slot[0] != '\0') {
            std::string slotText = " (";
            slotText += slot;
            slotText += ")";
            itemLine.segments.push_back({slotText, slotStyle});
        }
        lines.push_back(std::move(itemLine));
    }

    if (!def->tiers.empty()) {
        for (const auto& tierEntry : def->tiers) {
            const int req = tierEntry.first;
            const LocalSetBonus& bonus = tierEntry.second;
            if (!TierHasStats(bonus)) {
                continue;
            }
            const bool active = equippedCount >= req;
            char header[64];
            if (active) {
                _snprintf_s(header, _countof(header), _TRUNCATE,
                        "%d\xBC\xFE\xD0\xA7\xB9\xFB (\xD2\xD1\xBC\xA4\xBB\xEE)", req);
            } else {
                _snprintf_s(header, _countof(header), _TRUNCATE,
                        "%d\xBC\xFE\xD0\xA7\xB9\xFB (%d/%d)", req, equippedCount, req);
            }
            SetTipLine headerLine;
            headerLine.separatorBefore = true;
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
            line.centered = true;
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
    return (std::max)(180, (std::min)(320, maxLine + 2 * kSetTipPadX));
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
            DrawSetTipSeparator(canvas, width, y - 4);
            y += 6;
        }
        DrawSetTipLine(canvas, width, y, line);
        y += kSetTipLineH;
        if (i == 0 && line.centered) {
            y += kSetTipTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += 8;
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

static LocalSetBonus ParseEffectTier(IWzPropertyPtr tierNode, int reqCount) {
    LocalSetBonus bonus;
    bonus.reqCount = reqCount;
    bonus.str = static_cast<short>(ReadWzInt(tierNode, L"incSTR"));
    bonus.dex = static_cast<short>(ReadWzInt(tierNode, L"incDEX"));
    bonus.int_ = static_cast<short>(ReadWzInt(tierNode, L"incINT"));
    bonus.luk = static_cast<short>(ReadWzInt(tierNode, L"incLUK"));
    bonus.pad = static_cast<short>(ReadWzInt(tierNode, L"incPAD"));
    bonus.mad = static_cast<short>(ReadWzInt(tierNode, L"incMAD"));
    bonus.mhp = static_cast<short>(ReadWzInt(tierNode, L"incMHP"));
    bonus.mmp = static_cast<short>(ReadWzInt(tierNode, L"incMMP"));
    bonus.speed = static_cast<short>(ReadWzInt(tierNode, L"incSpeed"));
    bonus.jump = static_cast<short>(ReadWzInt(tierNode, L"incJump"));
    const int allStat = ReadWzInt(tierNode, L"incAllStat");
    if (allStat > 0) {
        bonus.str = static_cast<short>(bonus.str + allStat);
        bonus.dex = static_cast<short>(bonus.dex + allStat);
        bonus.int_ = static_cast<short>(bonus.int_ + allStat);
        bonus.luk = static_cast<short>(bonus.luk + allStat);
    }
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
            bonus.pad != 0 || bonus.mad != 0 || bonus.mhp != 0 || bonus.mmp != 0 ||
            bonus.speed != 0 || bonus.jump != 0;
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
        const int nNarrow = WideCharToMultiByte(CP_ACP, 0, vName.bstrVal, nWide,
                nullptr, 0, nullptr, nullptr);
        if (nNarrow <= 0) {
            return "";
        }
        std::string name(nNarrow, '\0');
        WideCharToMultiByte(CP_ACP, 0, vName.bstrVal, nWide, name.data(), nNarrow,
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

static void HideSetTooltip() {
    if (g_setTooltipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
                g_setTooltipBuf);
    }
    g_lastSetId = 0;
    g_lastHoverItemId = 0;
    g_lastEquippedCount = 0;
}

static void ShowSetTooltipAt(int x, int y, int setId, int hoverItemId, CUIToolTip* fontTip) {
    std::vector<SetTipLine> lines = BuildSetTooltipLayout(setId, hoverItemId);
    if (lines.empty()) {
        lines = BuildServerFallbackLayout(setId);
    }
    if (lines.empty()) {
        return;
    }

    EnsureSetTipFonts(fontTip);
    if (!g_setTipFonts.ready || !g_setTipFonts.white) {
        return;
    }

    CUIToolTip* setTip = EnsureSetTooltip();
    setTip->ClearToolTip();

    const int width = EstimateLayoutWidth(lines);
    const int lineCount = static_cast<int>(lines.size());
    const int targetHeight = kSetTipPadY * 2 + lineCount * kSetTipLineH + 8;

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    ZXString<char> zTitle("");
    std::string probe((std::max)(lineCount + 2, 4), '\n');
    ZXString<char> zDesc(probe.c_str());
    setTip->SetToolTip_String2(x, y, zTitle, zDesc, 0, 0, 0, width, 1, 0);
    if (setTip->m_nHeight < targetHeight) {
        probe.assign((std::max)(lineCount + 4, 6), '\n');
        zDesc = ZXString<char>(probe.c_str());
        setTip->ClearToolTip();
        setTip->SetToolTip_String2(x, y, zTitle, zDesc, 0, 0, 0, width, 1, 0);
    }

    IWzCanvasPtr canvas = GetTooltipCanvas(setTip);
    if (!canvas) {
        return;
    }
    RenderSetTooltipCanvas(canvas, lines, width);
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

static void ReadMainTooltipLayout(CUIToolTip* mainTip, int posX, int posY,
        int& outX, int& outY, int& mainW) {
    outX = posX;
    outY = posY;
    mainW = 0;
    if (!mainTip) {
        return;
    }
    __try {
        mainW = mainTip->m_nWidth;
        if (mainTip->m_pLayer) {
            outX = mainTip->m_pLayer->rx + mainW + 4;
            outY = mainTip->m_pLayer->ry;
        } else if (mainW > 0) {
            outX = posX + mainW + 4;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mainW = 0;
        outX = posX;
        outY = posY;
    }
}

static void UpdateSetTooltip(CUIToolTip* mainTip, int posX, int posY,
        int itemId, bool requestIfMissing) {
    if (g_inTooltipUpdate) {
        return;
    }
    g_inTooltipUpdate = true;

    try {
        g_activeMainTooltip = mainTip;
        g_lastLayoutX = posX;
        g_lastLayoutY = posY;

        EnsureSetItemDatabaseLoaded();
        if (!IsEquipItemId(itemId)) {
            HideSetTooltip();
            g_inTooltipUpdate = false;
            return;
        }
        const int setId = ResolveSetIdForItem(itemId);
        if (setId <= 0) {
            HideSetTooltip();
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

        if (setId == g_lastSetId && itemId == g_lastHoverItemId &&
                equippedCount == g_lastEquippedCount && HasSetTooltipLayer()) {
            g_inTooltipUpdate = false;
            return;
        }

        g_lastSetId = setId;
        g_lastHoverItemId = itemId;
        g_lastEquippedCount = equippedCount;

        int mainW = 0;
        int setX = 0;
        int setY = 0;
        ReadMainTooltipLayout(mainTip, 0, 0, setX, setY, mainW);
        if (setX <= 0 && mainW > 0) {
            setX = mainW + 4;
        }
        if (mainW <= 0) {
            mainW = 180;
        }
        if (setX < 0) {
            setX = 0;
        }
        if (setY < 0) {
            setY = 0;
        }

        ShowSetTooltipAt(setX, setY, setId, itemId, mainTip);
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
        Ztl_variant_t vRoot = get_rm()->GetObjectA(L"Etc/SetItemInfo.img");
        IWzPropertyPtr pRoot(get_unknown(vRoot));
        if (!pRoot) {
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
                            CP_ACP, 0, vName.bstrVal, nWide, nullptr, 0, nullptr, nullptr);
                    if (nNarrow > 0) {
                        def.name.assign(nNarrow, '\0');
                        WideCharToMultiByte(CP_ACP, 0, vName.bstrVal, nWide, def.name.data(),
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
    } catch (...) {
    }
}

// ---------------------------------------------------------------------------
// Skill window (+x) bonus display
// ---------------------------------------------------------------------------
static thread_local int g_skillFmtId = 0;

typedef int(__thiscall* SkillEntryCmp_t)(void* this_, unsigned int* skillEntry);
static auto Original_SkillEntryCmp =
        reinterpret_cast<SkillEntryCmp_t>(0x00A08E05);

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

void __fastcall CUIToolTip__DrawToolTip_Equip_SetItem_hook(
        CUIToolTip* pThis, void* /*edx*/, int layout, GW_ItemSlotEquip* pe) {
    const int itemId = pe ? SafeGetItemId(pe) : 0;
    CUIToolTip__DrawToolTip_Equip(pThis, layout, pe);
    if (g_inShowItemToolTip || !pThis || !pe) {
        return;
    }
    if (!IsEquipItemId(itemId) || ResolveSetIdForItem(itemId) <= 0) {
        return;
    }
    g_activeEquip = pe;
    UpdateSetTooltip(pThis, 0, 0, itemId, true);
}

auto CUIToolTip__ClearToolTip =
        reinterpret_cast<void(__thiscall*)(CUIToolTip*)>(
                ClientAddresses::kToolTipClear);

void __fastcall CUIToolTip__ClearToolTip_SetItem_hook(
        CUIToolTip* pThis, void* /*edx*/) {
    if (pThis == g_activeMainTooltip) {
        HideSetTooltip();
        g_activeMainTooltip = nullptr;
        g_activeEquip = nullptr;
    }
    CUIToolTip__ClearToolTip(pThis);
}

typedef int(__thiscall* ShowItemToolTip_t)(
        CUIToolTip* pThis,
        __int64* pos,
        int* a3,
        int* a4,
        int a5,
        int a6,
        int a7,
        int a8,
        unsigned int a9);
static auto Original_ShowItemToolTip = reinterpret_cast<ShowItemToolTip_t>(
        ClientAddresses::SetItem::kShowItemToolTip);

static void ReadTooltipPoint(__int64* pos, int& outX, int& outY) {
    outX = 0;
    outY = 0;
    if (!pos) {
        return;
    }
    const int* pt = reinterpret_cast<const int*>(pos);
    outX = pt[0];
    outY = pt[1];
}

int __fastcall Hook_ShowItemToolTip(
        CUIToolTip* pThis,
        void* /*edx*/,
        __int64* pos,
        int* a3,
        int* a4,
        int a5,
        int a6,
        int a7,
        int a8,
        unsigned int a9) {
    if (!a4) {
        if (pThis == g_activeMainTooltip) {
            HideSetTooltip();
            g_activeMainTooltip = nullptr;
            g_activeEquip = nullptr;
        }
        return Original_ShowItemToolTip(pThis, pos, a3, a4, a5, a6, a7, a8, a9);
    }

    struct ShowItemToolTipScope {
        ShowItemToolTipScope() {
            g_inShowItemToolTip = true;
        }
        ~ShowItemToolTipScope() {
            g_inShowItemToolTip = false;
        }
    } scope;

    const int hoverItemId = ReadShowItemToolTipItemId(a4);
    const int result = Original_ShowItemToolTip(
            pThis, pos, a3, a4, a5, a6, a7, a8, a9);

    try {
        if (hoverItemId > 0 && ResolveSetIdForItem(hoverItemId) > 0) {
            UpdateSetTooltip(pThis, 0, 0, hoverItemId, true);
        } else if (pThis == g_activeMainTooltip) {
            HideSetTooltip();
            g_activeMainTooltip = nullptr;
            g_activeEquip = nullptr;
        }
    } catch (...) {
        HideSetTooltip();
    }

    return result;
}

void RedrawActivePanel() {
    if (!g_activeMainTooltip || g_lastHoverItemId <= 0) {
        return;
    }
    g_lastSetId = 0;
    g_lastEquippedCount = 0;
    UpdateSetTooltip(g_activeMainTooltip, 0, 0, g_lastHoverItemId, false);
}
} // namespace SetItemMod

void SetItem_RedrawActivePanel() {
    SetItemMod::RedrawActivePanel();
}

void AttachSetItemMod() {
    if (SetItemMod::g_hooksAttached) {
        return;
    }
    SetItemMod::g_hooksAttached = true;
    SetItemMod::InitSetItemDatabase();
    SetItemMod::AttachSkillBonusHooks();
}

void AttachSetItemUiHooks() {
    if (SetItemMod::g_uiHooksAttached) {
        return;
    }
    SetItemMod::g_uiHooksAttached = true;
    AttachEquipTooltipStyleHooks();
    ATTACH_HOOK(SetItemMod::Original_ShowItemToolTip, SetItemMod::Hook_ShowItemToolTip);
    // Chain onto FusionAnvil's live DrawToolTip detour — do not re-patch raw 0x8ED0D2.
    FusionAnvil_BindDrawToolTipEquipTarget(
            reinterpret_cast<void**>(&SetItemMod::CUIToolTip__DrawToolTip_Equip));
    ATTACH_HOOK(SetItemMod::CUIToolTip__DrawToolTip_Equip,
                SetItemMod::CUIToolTip__DrawToolTip_Equip_SetItem_hook);
    ATTACH_HOOK(SetItemMod::CUIToolTip__ClearToolTip,
                SetItemMod::CUIToolTip__ClearToolTip_SetItem_hook);
}

int SetItem_GetSetIdForItem(int itemId) {
    SetItemMod::EnsureSetItemDatabaseLoaded();
    return SetItemMod::ResolveSetIdForItem(itemId);
}
