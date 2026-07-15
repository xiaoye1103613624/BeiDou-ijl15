#include "stdafx.h"
#include "compat/ClientAddresses.h"
#include "compat/hook.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include "compat/ztl/zcom.h"
#include <algorithm>
#include <string>
#include <vector>

// Temporary equip-marker UI mock — disable when real marker packets exist.
#ifndef MOCK_EQUIP_MARKERS
#define MOCK_EQUIP_MARKERS 1
#endif

// v083 zh-CN client renders narrow strings as GBK/CP936. UTF-8 source literals
// (e.g. L"幻化" from a UTF-8 .cpp) display as mojibake (澶艳 / 骞诲寇).
namespace {
// 外观 : / (幻化) in GBK
static const char kLabelAppearance[] = "\xCD\xE2\xB9\xD3 :";
static const char kSuffixTransmog[]  = " (\xBB\xC3\xBB\xAF)";
static const char kWordTransmog[]    = "\xBB\xC3\xBB\xAF";

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[64] = {};
    if (MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}
} // namespace

// ===========================================================================
// Custom.wz cash-item string fallback
// CItemInfo eagerly caches String/<category>.img into a ZMap at +0x54 during
// startup. GetItemString @ 0x5CF6FE reads only that cache, so cash IDs added
// later under Custom.wz/String/Cash.img aren't visible. Hook falls back to a
// live GetObjectA lookup so tooltips resolve without mutating the cache.
// ===========================================================================
static constexpr uintptr_t kAddr_GetItemString = 0x005CF6FE;
static constexpr uintptr_t kAddr_ZXString_Cat  = 0x00428D30;

typedef ZXString<char>* (__thiscall* t_GetItemString)(
    void* pThis, ZXString<char>* result, int nItemID, const char* sPropName);
static auto Original_GetItemString =
    reinterpret_cast<t_GetItemString>(kAddr_GetItemString);

ZXString<char>* __fastcall Hook_GetItemString(
    void* pThis, void* /*edx*/, ZXString<char>* result, int nItemID, const char* sPropName)
{
    Original_GetItemString(pThis, result, nItemID, sPropName);

    if (!result->IsEmpty() || !sPropName) {
        return result;
    }
    if (nItemID < 5000000 || nItemID >= 6000000) {
        return result;
    }

    wchar_t wProp[64];
    if (MultiByteToWideChar(CP_ACP, 0, sPropName, -1, wProp, _countof(wProp)) <= 0) {
        return result;
    }
    wchar_t wPath[192];
    _snwprintf_s(wPath, _countof(wPath), _TRUNCATE, L"Custom/String/Cash.img/%d/%s", nItemID, wProp);

    Ztl_variant_t vObj = get_rm()->GetObjectA(wPath);
    if (vObj.vt != VT_BSTR || !V_BSTR(&vObj)) {
        return result;
    }

    BSTR bs = V_BSTR(&vObj);
    int nWide = static_cast<int>(SysStringLen(bs));
    if (nWide <= 0) {
        return result;
    }
    int nNarrow = WideCharToMultiByte(CP_ACP, 0, bs, nWide, nullptr, 0, nullptr, nullptr);
    if (nNarrow <= 0) {
        return result;
    }
    std::vector<char> buf(nNarrow + 1, 0);
    WideCharToMultiByte(CP_ACP, 0, bs, nWide, buf.data(), nNarrow, nullptr, nullptr);

    reinterpret_cast<void(__thiscall*)(void*, const char*, int)>(kAddr_ZXString_Cat)(
        result, buf.data(), nNarrow);
    return result;
}


class GW_ItemSlotEquip {
public:
    MEMBER_AT(TSecType<int>, 0xC, nItemID)
    MEMBER_AT(ZtlSecurePacked<unsigned char>, 0x28, nRUC)
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
    MEMBER_AT(ZtlSecure<short>, 0xAC, nAttribute)
    // Fusion Anvil transmog: int at offset 0xF9 holds the "skin" item id.
    MEMBER_AT(int, 0xF9, nAnvilItemID)
    // 灵韵觉醒 fields (after anvil extension).
    MEMBER_AT(int, 0xFD, nEquipSkillID)
    MEMBER_AT(int, 0x101, nEquipSkillLevel)
    MEMBER_AT(unsigned long long, 0x105, tEquipSkillExpire)
};


static int SafeGetAnvilItemId(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    int nAnvilItemID = 0;
    __try { nAnvilItemID = pe->nAnvilItemID; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return nAnvilItemID;
}

static void SafeGetEquipSkill(GW_ItemSlotEquip* pe, int* outId, int* outLv, unsigned long long* outExpire) {
    if (outId) *outId = 0;
    if (outLv) *outLv = 0;
    if (outExpire) *outExpire = 0;
    if (!pe) return;
    int sid = 0;
    int lv = 0;
    unsigned long long exp = 0;
    __try {
        sid = pe->nEquipSkillID;
        lv = pe->nEquipSkillLevel;
        exp = pe->tEquipSkillExpire;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (outId) *outId = sid;
    if (outLv) *outLv = lv;
    if (outExpire) *outExpire = exp;
}

static void SafeGetEquipSkill(GW_ItemSlotEquip* pe, int* outId, int* outLv) {
    SafeGetEquipSkill(pe, outId, outLv, nullptr);
}

// GBK: 灵韵
static const char kLabelSpirit[] = "\xC1\xE9\xD4\xCF :";

static std::string LookupSkillFieldGbk(int skillId, const wchar_t* field) {
    if (skillId <= 0 || !field) {
        return std::string();
    }
    try {
        wchar_t path[64] = {};
        swprintf_s(path, L"String/Skill.img/%07d", skillId);
        IWzPropertyPtr property = get_rm()->GetObjectA(path).GetUnknown();
        if (!property) {
            return std::string();
        }
        Ztl_variant_t value = property->item[field];
        if (value.vt == VT_BSTR) {
            return static_cast<const char*>(_bstr_t(value));
        }
    } catch (...) {
    }
    return std::string();
}

static Ztl_bstr_t LookupSkillFieldBstr(int skillId, const wchar_t* field) {
    if (skillId <= 0 || !field) {
        return Ztl_bstr_t(L"");
    }
    try {
        wchar_t path[64] = {};
        swprintf_s(path, L"String/Skill.img/%07d", skillId);
        IWzPropertyPtr property = get_rm()->GetObjectA(path).GetUnknown();
        if (!property) {
            return Ztl_bstr_t(L"");
        }
        Ztl_variant_t value = property->item[field];
        if (value.vt == VT_BSTR && V_BSTR(&value)) {
            return Ztl_bstr_t(V_BSTR(&value));
        }
    } catch (...) {
    }
    return Ztl_bstr_t(L"");
}

static std::string LookupSkillNameGbk(int skillId) {
    return LookupSkillFieldGbk(skillId, L"name");
}

static IWzCanvasPtr LoadSkillIconCanvas(int skillId) {
    IWzCanvasPtr out;
    if (skillId <= 0) {
        return out;
    }
    auto rm = get_rm();
    if (!rm) {
        return out;
    }
    try {
        char uol[128];
        sprintf_s(uol, "Skill/%d.img/skill/%d/icon", skillId / 10000, skillId);
        Ztl_variant_t v1(vtMissing);
        Ztl_variant_t v2(vtMissing);
        Ztl_variant_t obj = rm->GetObjectA(Ztl_bstr_t(uol), v1, v2);
        IUnknown* unk = obj.GetUnknown(false, false);
        if (!unk) {
            return out;
        }
        IWzCanvas* raw = nullptr;
        if (FAILED(unk->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw))) || !raw) {
            return out;
        }
        out = raw;
        raw->Release();
    } catch (...) {
    }
    return out;
}

// Estimate wrapped line count for GBK tip body (~28 chars / line @ tip width).
static int EstimateWrapLines(const std::string& s, int charsPerLine) {
    if (s.empty() || charsPerLine <= 0) {
        return 0;
    }
    int lines = 1;
    int col = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n') {
            ++lines;
            col = 0;
            continue;
        }
        int w = (c >= 0x80) ? 2 : 1; // rough GBK width
        if (i + 1 < s.size() && c >= 0x80) {
            // consume trail byte as same cell
        }
        col += (c >= 0x80) ? 1 : 1; // count chars loosely
        if (col >= charsPerLine) {
            ++lines;
            col = 0;
        }
        if (c >= 0x80 && i + 1 < s.size()) {
            ++i; // skip GBK trail
        }
    }
    return (std::max)(1, lines);
}

static int CalcSpiritTipExtraHeight(int skillId, int skillLv, unsigned long long expire) {
    if (skillId <= 0 || skillLv <= 0) {
        return 0;
    }
    int h = 0;
    h += 22; // skill name
    if (expire != 0) {
        h += 16; // expire line
    }
    h += 10; // separator
    std::string desc = LookupSkillFieldGbk(skillId, L"desc");
    if (desc.empty()) {
        wchar_t hl[16];
        swprintf_s(hl, L"h%d", skillLv);
        desc = LookupSkillFieldGbk(skillId, hl);
    }
    const int descLines = EstimateWrapLines(desc, 22);
    const int descBlock = (std::max)(68, descLines * 14 + 4);
    h += descBlock;
    h += 10; // separator
    h += 16; // [Current Level]
    wchar_t hl[16];
    swprintf_s(hl, L"h%d", skillLv);
    std::string lvDesc = LookupSkillFieldGbk(skillId, hl);
    h += EstimateWrapLines(lvDesc, 28) * 14 + 8;
    return h;
}

static void DrawWrappedGbk(
    IWzCanvasPtr canvas, IWzFontPtr font, int x, int y, int maxChars, const std::string& gbk, int lineH)
{
    if (!canvas || !font || gbk.empty()) {
        return;
    }
    std::string line;
    int col = 0;
    int cy = y;
    auto flush = [&]() {
        if (line.empty()) {
            return;
        }
        canvas->DrawTextA(x, cy, GbkToBstr(line.c_str()), font, Ztl_variant_t(), Ztl_variant_t());
        line.clear();
        col = 0;
        cy += lineH;
    };
    for (size_t i = 0; i < gbk.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(gbk[i]);
        if (c == '\n') {
            flush();
            continue;
        }
        if (c >= 0x80 && i + 1 < gbk.size()) {
            if (col + 1 >= maxChars) {
                flush();
            }
            line.push_back(static_cast<char>(c));
            line.push_back(gbk[i + 1]);
            ++i;
            col += 1;
        } else {
            if (col + 1 >= maxChars) {
                flush();
            }
            line.push_back(static_cast<char>(c));
            col += 1;
        }
    }
    flush();
}

static constexpr uintptr_t kAddr_GetItemName            = 0x005CF63E;
static constexpr uintptr_t kAddr_SetToolTip_Equip_Basic = 0x008ECA0C;
static constexpr uintptr_t kAddr_DrawToolTip_Equip      = 0x008ED0D2;
static constexpr uintptr_t kAddr_get_basic_font         = 0x0098A707;

static auto get_basic_font =
    reinterpret_cast<IWzFontPtr*(__cdecl*)(IWzFontPtr*, int)>(kAddr_get_basic_font);

static int g_spiritTipExtraH = 0;

static void DrawSpiritSkillBlock(CUIToolTip* tip, GW_ItemSlotEquip* pe) {
    if (!tip || !pe || g_spiritTipExtraH <= 0 || !tip->m_pLayer) {
        return;
    }
    int skillId = 0;
    int skillLv = 0;
    unsigned long long expire = 0;
    SafeGetEquipSkill(pe, &skillId, &skillLv, &expire);
    if (skillId <= 0 || skillLv <= 0) {
        return;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        IWzCanvasPtr canvas = tip->m_pLayer->Getcanvas(vIdx);
        if (!canvas) {
            return;
        }

        IWzFontPtr fontName;
        IWzFontPtr fontBody;
        // font 1 在部分 tip 上对中文名几乎不可见；统一用 body 字体画名称
        get_basic_font(std::addressof(fontBody), 0);
        get_basic_font(std::addressof(fontName), 0);
        if (!fontBody) {
            return;
        }
        if (!fontName) {
            fontName = fontBody;
        }

        int y = tip->m_nHeight - g_spiritTipExtraH + 6;
        const int tipW = tip->m_nWidth;

        // 技能名：WZ BSTR 直绘，避免 GBK 往返丢字（「灵韵 : 冒险岛勇士」）
        {
            Ztl_bstr_t label = GbkToBstr("\xC1\xE9\xD4\xCF : "); // 灵韵 :
            canvas->DrawTextA(12, y, label, fontName, Ztl_variant_t(), Ztl_variant_t());

            Ztl_bstr_t nameBstr = LookupSkillFieldBstr(skillId, L"name");
            const wchar_t* nameWide = nameBstr;
            if (!nameWide || !*nameWide) {
                char buf[48];
                sprintf_s(buf, "#%d", skillId);
                nameBstr = GbkToBstr(buf);
            }
            // 「灵韵 : 」约 48px，名称跟在后面
            canvas->DrawTextA(12 + 48, y, nameBstr, fontName, Ztl_variant_t(), Ztl_variant_t());
            y += 20;
        }

        if (expire != 0) {
            // Show raw expire hint (permanent uses 0)
            canvas->DrawTextA(
                12, y, GbkToBstr("\xB9\xFD\xC6\xDA"), // 过期
                fontBody, Ztl_variant_t(), Ztl_variant_t());
            y += 16;
        }

        // Separator
        canvas->raw_DrawRectangle(6, y, tipW - 12, 1, 0xFFFFFFFFu);
        y += 10;

        // Icon + desc
        IWzCanvasPtr icon = LoadSkillIconCanvas(skillId);
        const int iconX = 10;
        const int iconY = y;
        if (icon) {
            Ztl_variant_t empty;
            canvas->CopyEx(
                iconX, iconY, icon, CANVAS_ALPHATYPE::CA_OVERWRITE,
                32, 32, 0, 0, 32, 32, empty);
        }

        std::string desc = LookupSkillFieldGbk(skillId, L"desc");
        const int descX = icon ? 50 : 12;
        DrawWrappedGbk(canvas, fontBody, descX, y, icon ? 22 : 28, desc, 14);
        y += (std::max)(icon ? 36 : 0, EstimateWrapLines(desc, icon ? 22 : 28) * 14) + 6;

        // Separator
        canvas->raw_DrawRectangle(6, y, tipW - 12, 1, 0xFFFFFFFFu);
        y += 8;

        // Current level
        {
            char lvLine[64];
            // [当前等级: N]
            sprintf_s(lvLine, "[\xB5\xB1\xC7\xB0\xB5\xC8\xBC\xB6: %d]", skillLv);
            canvas->DrawTextA(12, y, GbkToBstr(lvLine), fontBody, Ztl_variant_t(), Ztl_variant_t());
            y += 16;
        }

        wchar_t hl[16];
        swprintf_s(hl, L"h%d", skillLv);
        std::string lvDesc = LookupSkillFieldGbk(skillId, hl);
        DrawWrappedGbk(canvas, fontBody, 12, y, 28, lvDesc, 14);
    } catch (...) {
    }
}


static auto CUIToolTip__SetToolTip_Equip_Basic =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, GW_ItemSlotEquip*)>(kAddr_SetToolTip_Equip_Basic);

void __fastcall CUIToolTip__SetToolTip_Equip_Basic_hook(
    CUIToolTip* pThis, void* /*edx*/, GW_ItemSlotEquip* pe)
{
    if (!pThis || !pe) {
        return;
    }

    // Keep vanilla zh-CN stat/type labels from String.wz/ToolTipHelp.img.
    CUIToolTip__SetToolTip_Equip_Basic(pThis, pe);

    // Fusion Anvil: append skin item name only; do not replace base tooltip text.
    const int nAnvilItemID = SafeGetAnvilItemId(pe);
    if (nAnvilItemID != 0) {
        ZXString<char> sSkinName;
        reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
            kAddr_GetItemName)(CItemInfo::GetInstance(), &sSkinName, nAnvilItemID);

        ZXString<char> sLine;
        sLine.Format("%s%s", static_cast<const char*>(sSkinName), kSuffixTransmog);
        pThis->AddInfoEx(14, 15, kLabelAppearance, sLine, 1, 1001);
    }

    // 灵韵：抬高 tip，留给 Draw 阶段画「图标+说明」区块（对齐参考装备加技能）
    g_spiritTipExtraH = 0;
    int skillId = 0;
    int skillLv = 0;
    unsigned long long expire = 0;
    SafeGetEquipSkill(pe, &skillId, &skillLv, &expire);
    if (skillId > 0 && skillLv > 0) {
        g_spiritTipExtraH = CalcSpiritTipExtraHeight(skillId, skillLv, expire);
        if (g_spiritTipExtraH > 0) {
            pThis->m_nHeight += g_spiritTipExtraH;
        }
    }
}


// ===========================================================================
// Transmog corner icon — draw the skin item's small icon in the top-right
// corner of the equip tooltip canvas after the engine has rendered it.
// ===========================================================================

static auto CUIToolTip__DrawToolTip_Equip =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*, int, GW_ItemSlotEquip*)>(kAddr_DrawToolTip_Equip);

#if MOCK_EQUIP_MARKERS
// Companion tip (套装属性-style) — do NOT paint into native equip canvas.
// String.wz/Eqp.img: 1332025 = maple blade (fengye blade)
static constexpr int kMockMapleBladeItemId = 1332025;
static constexpr unsigned long kMockStarColor = 0xFFFFCC33; // warm gold
static constexpr unsigned long kMockRowColor = 0xFFFFFFFF;
static constexpr size_t kMarkerTipBufSize = 0x600;
static constexpr int kMarkerTipPadY = 4;
static constexpr int kMarkerTipLineH = 16;
static constexpr int kMarkerTipLineGap = 2;
// String2 cannot hit an exact pixel height; keep final canvas within this slack.
static constexpr int kMarkerTipHeightSlack = 4;
// Lateral content margin (matches String2 wrap chrome tipW-15 ≈ 2*7.5).
static constexpr int kMarkerTipMarginX = 8;
// Flush dock leaves NO gap, but each tip draws its own MakeLayer white frame
// (bDoubleOutline). Vertical stack would show companion bottom + equip top as
// a double white seam. Scrub BOTH outlines + overlap 2px, then paint exactly
// one white join line (z: companion after main DrawToolTip_Equip).
static constexpr int kMarkerGapAbove = 0;
static constexpr int kMarkerBorderOverlap = 2;
// MakeLayer fill when String2 calls it (color = -1610612672 / 0xA0000000).
static constexpr unsigned long kMarkerTipFillColor = 0xA0000000u;
// MakeLayer outline / double-outline white stroke.
static constexpr unsigned long kMarkerTipOutlineColor = 0xFFFFFFFFu;
// MakeLayer top chrome: corner 1x1 at y=0 + inner line at y=1 → wipe 2px only.
// Owner/name text starts below this strip; do not paint deeper.
static constexpr int kMarkerEquipTopScrubH = 2;

// GBK labels for zh-CN Dotum path (UTF-8 source would mojibake).
static const char kMockForgeGbk[] = "\xB6\xCB\xD4\xEC Lv.5";           // 锻造 Lv.5
static const char kMockGemGbk[] = "\xB1\xA6\xCA\xAF x2";               // 宝石 x2
static const char kMockAnvilGbk[] = "[\xD2\xD1\xBB\xC3\xBB\xAF]";     // [已幻化]

typedef HRESULT(__thiscall* WzFontCreate_t)(
    IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(0x0046341A);

enum class MarkerLineKind : int {
    Stars = 0,
    GbkText = 1,
};

struct MarkerLine {
    MarkerLineKind kind;
    const char* gbkText; // valid when kind == GbkText
};

// One drawn row after wrap (Stars stays one row; text may expand to N centered rows).
struct MarkerVisualLine {
    MarkerLineKind kind;
    std::wstring text; // used when kind == GbkText
};

static alignas(8) char g_markerTipBuf[kMarkerTipBufSize];
static bool g_markerTipInited = false;
static CUIToolTip* g_markerMainTip = nullptr;
static int g_markerLastItemId = 0;
static int g_markerLastX = -1;
static int g_markerLastY = -1;
static int g_markerLastW = 0;
static int g_markerLastH = 0;
static int g_markerLastVisualCount = 0;

static auto Original_ClearToolTip =
    reinterpret_cast<void(__thiscall*)(CUIToolTip*)>(ClientAddresses::kToolTipClear);

static int SafeGetItemId(GW_ItemSlotEquip* pe) {
    if (!pe) {
        return 0;
    }
    try {
        return static_cast<int>(pe->nItemID);
    } catch (...) {
        return 0;
    }
}

static IWzCanvasPtr LoadWzCanvas(const wchar_t* uol) {
    IWzCanvasPtr canvas;
    if (!uol || !*uol || !get_rm()) {
        return canvas;
    }
    try {
        Ztl_variant_t value = get_rm()->GetObjectA(const_cast<wchar_t*>(uol));
        IUnknown* unknown = value.GetUnknown(false, false);
        if (unknown) {
            IWzCanvas* raw = nullptr;
            if (SUCCEEDED(unknown->QueryInterface(__uuidof(IWzCanvas), reinterpret_cast<void**>(&raw)))
                && raw) {
                canvas = raw;
                raw->Release();
            }
        }
    } catch (...) {
    }
    return canvas;
}

static IWzFontPtr EnsureMockFont(unsigned long color) {
    IWzFontPtr font;
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", font, nullptr);
        if (font) {
            const Ztl_variant_t style(L"");
            if (FAILED(WzFontCreate(font, L"Dotum", 12, color, style))) {
                font = nullptr;
            }
        }
    } catch (...) {
        font = nullptr;
    }
    if (!font) {
        try {
            get_basic_font(std::addressof(font), 0);
        } catch (...) {
        }
    }
    return font;
}

static void DrawCenteredWide(
    IWzCanvasPtr canvas, int tipWidth, int y, const wchar_t* text, IWzFontPtr font)
{
    if (!canvas || !text || !text[0] || !font || tipWidth <= 0) {
        return;
    }
    try {
        const Ztl_bstr_t bs(text);
        const unsigned tw = font->CalcTextWidth(bs, Ztl_variant_t());
        const int x = (tipWidth - static_cast<int>(tw)) / 2;
        canvas->DrawTextA(x, y, bs, font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

// IWzFont::CalcTextWidth — same COM Measure as IDA font width helpers.
static int MeasureTextWidth(IWzFontPtr font, const wchar_t* text) {
    if (!font || !text) {
        return 0;
    }
    try {
        return static_cast<int>(font->CalcTextWidth(Ztl_bstr_t(text), Ztl_variant_t()));
    } catch (...) {
        return 0;
    }
}

// Dotum 12 fullwidth Han ≈ 12px. Capacity at tipW:
//   maxHan = floor((tipW - 2*kMarkerTipMarginX) / hanW)
// tipW≈245 → floor((245-16)/12) = 19 汉字/行.
static int MaxHanCharsPerLine(IWzFontPtr font, int tipW) {
    const int avail = tipW - 2 * kMarkerTipMarginX;
    if (avail <= 0) {
        return 1;
    }
    int hanW = MeasureTextWidth(font, L"\x6C49"); // 汉
    if (hanW <= 0) {
        hanW = 12;
    }
    return (std::max)(1, avail / hanW);
}

static int ContentMaxWidth(int tipW) {
    return (std::max)(1, tipW - 2 * kMarkerTipMarginX);
}

static std::wstring GbkToWide(const char* gbk) {
    std::wstring out;
    if (!gbk || !gbk[0]) {
        return out;
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, gbk, -1, nullptr, 0);
    if (n <= 1) {
        return out;
    }
    out.resize(static_cast<size_t>(n - 1));
    MultiByteToWideChar(CP_ACP, 0, gbk, -1, &out[0], n);
    return out;
}

// Split so each piece width <= maxW; keep whole wchar units (no mid-glyph cuts).
static std::vector<std::wstring> WrapWideToWidth(
    IWzFontPtr font, const std::wstring& text, int maxW)
{
    std::vector<std::wstring> parts;
    if (text.empty()) {
        return parts;
    }
    if (!font || maxW <= 0) {
        parts.push_back(text);
        return parts;
    }

    size_t i = 0;
    while (i < text.size()) {
        size_t end = i + 1;
        while (end <= text.size()) {
            const std::wstring chunk = text.substr(i, end - i);
            if (MeasureTextWidth(font, chunk.c_str()) > maxW) {
                break;
            }
            ++end;
        }
        if (end == i + 1) {
            // Single glyph wider than maxW — still emit it (avoids infinite loop).
            parts.push_back(text.substr(i, 1));
            ++i;
        } else {
            parts.push_back(text.substr(i, end - i - 1));
            i = end - 1;
        }
    }
    return parts;
}

static bool TryDrawWzStarRow(IWzCanvasPtr canvas, int tipWidth, int y, int* outH) {
    if (outH) {
        *outH = kMarkerTipLineH;
    }
    IWzCanvasPtr star = LoadWzCanvas(L"Effect/CharacterEff.img/1114000/2/0");
    if (!star) {
        return false;
    }
    unsigned sw = 0;
    unsigned sh = 0;
    try {
        sw = star->Getwidth();
        sh = star->Getheight();
    } catch (...) {
        return false;
    }
    if (sw == 0 || sh == 0) {
        return false;
    }

    constexpr int kFilled = 3;
    constexpr int kGap = 2;
    const int rowW = static_cast<int>(kFilled * sw + (kFilled - 1) * kGap);
    int x = (tipWidth - rowW) / 2;
    try {
        for (int i = 0; i < kFilled; ++i) {
            canvas->CopyEx(
                x, y, star, CANVAS_ALPHATYPE::CA_REMOVEALPHA,
                0, 0, 0, 0, 0, 0, Ztl_variant_t());
            x += static_cast<int>(sw) + kGap;
        }
    } catch (...) {
        return false;
    }
    if (outH) {
        *outH = (std::max)(kMarkerTipLineH, static_cast<int>(sh));
    }
    return true;
}

static IWzCanvasPtr GetMarkerTipCanvas(CUIToolTip* tip) {
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

// MakeLayer @ 0x8F3141 with bDoubleOutline: bottom inner white at y=h-2,
// corner whites at y=h-1. Wipe both rows before redrawing a single join line.
static void ScrubCompanionBottomOutline(IWzCanvasPtr canvas, int tipW, int tipH) {
    if (!canvas || tipW <= 0 || tipH < 2) {
        return;
    }
    try {
        canvas->DrawRectangle(0, tipH - 2, tipW, 2, kMarkerTipFillColor);
    } catch (...) {
    }
}

// One white separator at companion bottom inner Y (h-2): matches MakeLayer
// bottom inner stroke. Inner width (2, w-4) keeps left/right frame intact.
// With overlap=2, world Y = mainY (equip tip top edge) — above owner text.
static void DrawCompanionJoinWhiteLine(IWzCanvasPtr canvas, int tipW, int tipH) {
    if (!canvas || tipW < 5 || tipH < 2) {
        return;
    }
    try {
        canvas->DrawRectangle(2, tipH - 2, tipW - 4, 1, kMarkerTipOutlineColor);
    } catch (...) {
    }
}

// Equip MakeLayer top: (0,0)/(w-1,0) corners + inner line (2,1,w-4,1).
// Fill only the top 2px strip — never touch owner "XX的" / item name below.
static void ScrubEquipTopOutline(IWzCanvasPtr canvas, int tipW) {
    if (!canvas || tipW <= 0) {
        return;
    }
    try {
        canvas->DrawRectangle(0, 0, tipW, kMarkerEquipTopScrubH, kMarkerTipFillColor);
    } catch (...) {
    }
}

static int ComputeCompanionTipY(int mainY, int tipH) {
    // tipY = mainY - tipH + overlap → companion bottom covers equip top chrome.
    int tipY = mainY - tipH - kMarkerGapAbove + kMarkerBorderOverlap;
    if (tipY < 0) {
        tipY = 0;
    }
    return tipY;
}

static CUIToolTip* EnsureMarkerTip() {
    if (!g_markerTipInited) {
        g_markerTipInited = true;
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
            g_markerTipBuf);
    }
    return reinterpret_cast<CUIToolTip*>(g_markerTipBuf);
}

static void HideMarkerTip() {
    g_markerMainTip = nullptr;
    g_markerLastItemId = 0;
    g_markerLastX = -1;
    g_markerLastY = -1;
    g_markerLastW = 0;
    g_markerLastH = 0;
    g_markerLastVisualCount = 0;
    if (g_markerTipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipClear)(
            g_markerTipBuf);
    }
}

static bool HasMarkerTipLayer() {
    if (!g_markerTipInited) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_markerTipBuf);
        return tip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadMainTipScreenPos(CUIToolTip* mainTip, int& outX, int& outY, int& outW) {
    outX = 0;
    outY = 0;
    outW = 0;
    if (!mainTip) {
        return false;
    }
    __try {
        outW = mainTip->m_nWidth;
        if (!mainTip->m_pLayer || outW <= 0) {
            return false;
        }
        outX = mainTip->m_pLayer->rx;
        outY = mainTip->m_pLayer->ry;
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outX = 0;
        outY = 0;
        outW = 0;
        return false;
    }
}

// Content height only (no String2 chrome guess):
//   tipH_content = padTop + N*lineH + (N-1)*gap + padBottom
static int ComputeMarkerTipContentH(int visualLineCount) {
    if (visualLineCount <= 0) {
        return kMarkerTipPadY * 2 + kMarkerTipLineH;
    }
    const int gaps = (visualLineCount > 1) ? (visualLineCount - 1) * kMarkerTipLineGap : 0;
    return kMarkerTipPadY * 2 + visualLineCount * kMarkerTipLineH + gaps;
}

static std::vector<MarkerLine> CollectMockMarkerLines(GW_ItemSlotEquip* pe) {
    std::vector<MarkerLine> lines;
    lines.push_back({MarkerLineKind::Stars, nullptr});
    lines.push_back({MarkerLineKind::GbkText, kMockForgeGbk});
    lines.push_back({MarkerLineKind::GbkText, kMockGemGbk});
    if (SafeGetAnvilItemId(pe) != 0) {
        lines.push_back({MarkerLineKind::GbkText, kMockAnvilGbk});
    }
    return lines;
}

static std::vector<MarkerVisualLine> ExpandMarkerVisualLines(
    const std::vector<MarkerLine>& lines, IWzFontPtr rowFont, int tipW)
{
    std::vector<MarkerVisualLine> visual;
    const int maxW = ContentMaxWidth(tipW);
    for (const MarkerLine& line : lines) {
        if (line.kind == MarkerLineKind::Stars) {
            visual.push_back({MarkerLineKind::Stars, L""});
            continue;
        }
        if (!line.gbkText) {
            continue;
        }
        const std::wstring wide = GbkToWide(line.gbkText);
        if (wide.empty()) {
            continue;
        }
        const int tw = MeasureTextWidth(rowFont, wide.c_str());
        if (!rowFont || tw <= maxW) {
            visual.push_back({MarkerLineKind::GbkText, wide});
            continue;
        }
        for (const std::wstring& part : WrapWideToWidth(rowFont, wide, maxW)) {
            visual.push_back({MarkerLineKind::GbkText, part});
        }
    }
    return visual;
}

static void RenderMarkerVisualLines(
    IWzCanvasPtr canvas, int tipWidth, const std::vector<MarkerVisualLine>& lines,
    IWzFontPtr starFont, IWzFontPtr rowFont)
{
    if (!canvas || tipWidth <= 0 || lines.empty()) {
        return;
    }

    int y = kMarkerTipPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const MarkerVisualLine& line = lines[i];
        int usedH = kMarkerTipLineH;
        if (line.kind == MarkerLineKind::Stars) {
            int starH = kMarkerTipLineH;
            if (!TryDrawWzStarRow(canvas, tipWidth, y, &starH) && starFont) {
                // Dotum: U+2605 / U+2606; emoji U+2B50 often missing.
                DrawCenteredWide(
                    canvas, tipWidth, y, L"\x2605\x2605\x2605\x2606\x2606", starFont);
            }
            usedH = starH;
        } else if (line.kind == MarkerLineKind::GbkText && !line.text.empty() && rowFont) {
            // Each wrapped visual line is independently centered: x = (tipW - lineW)/2.
            DrawCenteredWide(canvas, tipWidth, y, line.text.c_str(), rowFont);
        }
        y += usedH;
        if (i + 1 < lines.size()) {
            y += kMarkerTipLineGap;
        }
    }
}

// Calibrate SetToolTip_String2 blank-line probe to shrink-fit targetH (slack 0–4px).
// Prior bug: always used lineCount+2 newlines then trusted oversized m_nHeight.
static int CreateMarkerTipLayerAtHeight(
    CUIToolTip* tip, int tipX, int tipY, int tipW, int targetH)
{
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

    // Grow until canvas covers content.
    for (int i = 0; i < 6 && tip->m_nHeight < targetH; ++i) {
        setLines(++need);
    }
    // Shrink excess empty band (keep <= slack).
    for (int i = 0; i < 8
         && need > 1
         && tip->m_nHeight > targetH + kMarkerTipHeightSlack; ++i) {
        const int prevH = tip->m_nHeight;
        setLines(--need);
        if (tip->m_nHeight < targetH) {
            setLines(++need); // restore last height that still fits content
            break;
        }
        if (tip->m_nHeight >= prevH) {
            break; // probe stopped changing
        }
    }

    return tip->m_nHeight > 0 ? tip->m_nHeight : targetH;
}

// Sidecar tip like set-attribute panel: floats ABOVE main equip tip; native untouch.
static void UpdateMockMarkerCompanionTip(CUIToolTip* mainTip, GW_ItemSlotEquip* pe) {
    const int itemId = SafeGetItemId(pe);
    if (!mainTip || itemId != kMockMapleBladeItemId) {
        HideMarkerTip();
        return;
    }

    int mainX = 0;
    int mainY = 0;
    int mainW = 0;
    if (!ReadMainTipScreenPos(mainTip, mainX, mainY, mainW)) {
        HideMarkerTip();
        return;
    }

    // Width must match equip tip exactly (same left edge when widths equal).
    const int tipW = mainW;
    if (tipW <= 0) {
        HideMarkerTip();
        return;
    }
    int tipX = mainX;
    if (tipX < 0) {
        tipX = 0;
    }

    IWzFontPtr starFont = EnsureMockFont(kMockStarColor);
    IWzFontPtr rowFont = EnsureMockFont(kMockRowColor);
    // MaxHanCharsPerLine(font, tipW) ≈ floor((tipW-16)/hanW); tipW=245 → ~19 汉字.

    const std::vector<MarkerLine> logical = CollectMockMarkerLines(pe);
    const std::vector<MarkerVisualLine> visual =
        ExpandMarkerVisualLines(logical, rowFont, tipW);
    const int visualCount = static_cast<int>(visual.size());
    const int targetH = ComputeMarkerTipContentH(visualCount);

    try {
        CUIToolTip* tip = EnsureMarkerTip();

        // Main tip canvas is re-drawn every DrawToolTip_Equip; scrub top every frame.
        auto scrubMainTop = [&]() {
            IWzCanvasPtr mainCanvas = GetMarkerTipCanvas(mainTip);
            if (mainCanvas) {
                ScrubEquipTopOutline(mainCanvas, tipW);
            }
        };

        // Reuse existing tip: re-dock + erase equip top chrome (seam must stay gone).
        if (itemId == g_markerLastItemId && tipX == g_markerLastX
            && tipW == g_markerLastW && visualCount == g_markerLastVisualCount
            && HasMarkerTipLayer() && tip->m_pLayer) {
            const int h = tip->m_nHeight > 0 ? tip->m_nHeight
                : (g_markerLastH > 0 ? g_markerLastH : targetH);
            const int tipY = ComputeCompanionTipY(mainY, h);
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
            scrubMainTop();
            g_markerMainTip = mainTip;
            g_markerLastY = tipY;
            g_markerLastH = h;
            return;
        }

        // Provisional Y from content height; corrected with actual m_nHeight below.
        int tipY = ComputeCompanionTipY(mainY, targetH);

        const int h = CreateMarkerTipLayerAtHeight(tip, tipX, tipY, tipW, targetH);

        tipY = ComputeCompanionTipY(mainY, h);
        if (tip->m_pLayer) {
            tip->m_pLayer->rx = tipX;
            tip->m_pLayer->ry = tipY;
        }

        IWzCanvasPtr canvas = GetMarkerTipCanvas(tip);
        if (!canvas) {
            HideMarkerTip();
            return;
        }
        // Scrub double white borders, then one MakeLayer-style white join line.
        ScrubCompanionBottomOutline(canvas, tipW, h);
        DrawCompanionJoinWhiteLine(canvas, tipW, h);
        scrubMainTop();
        RenderMarkerVisualLines(canvas, tipW, visual, starFont, rowFont);

        g_markerMainTip = mainTip;
        g_markerLastItemId = itemId;
        g_markerLastX = tipX;
        g_markerLastY = tipY;
        g_markerLastW = tipW;
        g_markerLastH = h;
        g_markerLastVisualCount = visualCount;
    } catch (...) {
        HideMarkerTip();
    }
}

void __fastcall CUIToolTip__ClearToolTip_Marker_hook(CUIToolTip* pThis, void* /*edx*/) {
    if (pThis && pThis == g_markerMainTip) {
        HideMarkerTip();
    }
    Original_ClearToolTip(pThis);
}
#endif // MOCK_EQUIP_MARKERS

void __fastcall CUIToolTip__DrawToolTip_Equip_hook(
    CUIToolTip* pThis, void* /*edx*/, int a2, GW_ItemSlotEquip* pe)
{
    // Dot-removal patches attach once in SetItem UI init; no per-draw Begin/End.
    CUIToolTip__DrawToolTip_Equip(pThis, a2, pe);

#if MOCK_EQUIP_MARKERS
    // Separate window above owner — never paints into native DrawToolTip_Equip canvas.
    UpdateMockMarkerCompanionTip(pThis, pe);
#endif

    // 灵韵技能区块（图标 + 说明 + 当前等级）画在 tip 底部预留高度内
    DrawSpiritSkillBlock(pThis, pe);

    const int nAnvilItemID = SafeGetAnvilItemId(pe);
    if (!pe || !nAnvilItemID || !pThis || !pThis->m_pLayer) {
        return;
    }
    try {
        Ztl_variant_t vIdx;
        V_VT(&vIdx) = VT_I4;
        V_I4(&vIdx) = 0;
        IWzCanvasPtr pCanvas = pThis->m_pLayer->Getcanvas(vIdx);
        if (!pCanvas) {
            return;
        }

        // Top-right corner — icons anchor at bottom-left, so y is the baseline.
        int iconX = pThis->m_nWidth - 32 - 14;
        int iconBaselineY = 6 + 32;
        CItemInfo::GetInstance()->DrawItemIconForSlot(
            pCanvas, nAnvilItemID, iconX, iconBaselineY, 0, 0, 0, 1, 0, 1);

        IWzFontPtr pFont;
        get_basic_font(std::addressof(pFont), 0);
        if (pFont) {
            pCanvas->DrawTextA(
                iconX - 4, iconBaselineY + 2,
                GbkToBstr(kWordTransmog),
                pFont, Ztl_variant_t(), Ztl_variant_t());
        }
    } catch (...) {}
}


namespace {
bool g_tooltipHooksAttached = false;
} // namespace

void FusionAnvil_BindDrawToolTipEquipTarget(void** outPtr) {
    if (!outPtr) {
        return;
    }
    *outPtr = reinterpret_cast<void*>(CUIToolTip__DrawToolTip_Equip);
}

void AttachFusionAnvilTooltipHooks() {
    if (g_tooltipHooksAttached) {
        return;
    }
    g_tooltipHooksAttached = true;

    ATTACH_HOOK(Original_GetItemString, Hook_GetItemString);
    ATTACH_HOOK(CUIToolTip__SetToolTip_Equip_Basic, CUIToolTip__SetToolTip_Equip_Basic_hook);
    ATTACH_HOOK(CUIToolTip__DrawToolTip_Equip, CUIToolTip__DrawToolTip_Equip_hook);
#if MOCK_EQUIP_MARKERS
    ATTACH_HOOK(Original_ClearToolTip, CUIToolTip__ClearToolTip_Marker_hook);
#endif
}
