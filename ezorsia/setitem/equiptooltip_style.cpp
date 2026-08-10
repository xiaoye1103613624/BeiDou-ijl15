#include "stdafx.h"
#include "equiptooltip_style.h"
#include "Memory.h"
#include "compat/hook.h"
#include "compat/ClientAddresses.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include "compat/ztl/ztl.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Custom equip tip = set-tip approach:
//   SetToolTip_String2 sizing probe + Dotum canvas draw (ColonCenter / centered name).
// Hides vanilla attribute body by skipping DrawToolTip_Equip when custom succeeds.
// No AddInfoEx Detour / no name scrub. Fonts only after graphics ready (first draw).

namespace {
bool g_styleAttached = false;

static constexpr uintptr_t kAddInfoExStoreDot = 0x008F3AF2;
static constexpr uintptr_t kPrintLineSkipDot = 0x008F516C;
static constexpr uintptr_t kAddr_GetItemName = 0x005CF63E;
static constexpr uintptr_t kAddr_WzFontCreate = 0x0046341A;
static constexpr uintptr_t kAddr_get_basic_font = 0x0098A707;
static constexpr uintptr_t kAddr_TSecTypeGetData = 0x0042873D;
static constexpr uintptr_t kAddr_StringPoolGet = 0x0079E993;
static constexpr uintptr_t kAddr_StringPoolInstance = 0x0079E805;
// CItemInfo::GetWeaponCategoryName — maps GetWeaponType→StringPool.
// 134/135 fall through GetWeaponType=0, so Equip_Basic uses bodypart10→盾牌.
// 116→BP33: Equip_Basic falls to GetEquipCategoryByBodyPart (0x5C9E61), where
// case 33 shares the pet-equip StringPool 3811（宠物装备）. Override by itemId.
static constexpr uintptr_t kAddr_GetWeaponCategoryName = 0x005C99FC;
// CItemInfo::GetEquipCategoryByBodyPart — BP33→StringPool 3811「宠物的装备」.
// Equip_Basic uses this when GetWeaponCategoryName returns empty (116).
static constexpr uintptr_t kAddr_GetEquipCategoryByBodyPart = 0x005C9E61;
// Game-heap ZXString::Assign (same as mesouncap). DLL ZXString::Assign uses a
// separate ZAllocEx pool; tooltip then draws/frees via game 0x4062DF → AV.
// Do NOT call Empty@0x414663 here — caller out ZXString is uninitialized.
static constexpr uintptr_t kAddr_ZXString_Assign = 0x00414617;

static constexpr int kPadX = 8;
static constexpr int kPadY = 6;
static constexpr int kLineH = 14;
static constexpr int kTitleExtraH = 2;
static constexpr int kSepGapY = 3;
static constexpr unsigned int kSepColor = 0x80FFFFFF;
static constexpr char kColonSep[] = "\x20\x3A\x20";
static constexpr unsigned long kColWhite = 0xFFFFFFFF;
static constexpr unsigned long kColOrange = 0xFFFF9900;
static constexpr unsigned long kColTitle = 0xFFFFFF66; // soft gold-ish for name

static constexpr unsigned kPoolStatIds[] = {
        2042, 2043, 2044, 2045, 2046, 2047, // STR..MP
        657, 658, 659, 660, 661, 662, 663, 664, 665 // PAD..Jump
};

// GBK: ???? / ????? (byte form avoids greedy \x parsing)
static const char kLabelCategory[] = {
        '\xD7', '\xB0', '\xB1', '\xB8', '\xB7', '\xD6', '\xC0', '\xE0', 0};
static const char kLabelUpgrade[] = {
        '\xBF', '\xC9', '\xC9', '\xFD', '\xBC', '\xB6', '\xB4', '\xCE', '\xCA', '\xFD', 0};

enum class LineStyle { Title, White, Orange };
enum class LineLayout { Left, Center, ColonCenter };

struct TipLine {
    std::string a;
    std::string b;
    LineStyle styleA = LineStyle::White;
    LineStyle styleB = LineStyle::White;
    LineLayout layout = LineLayout::Left;
    bool sepBefore = false;
};

class GW_ItemSlotEquipLocal {
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
    MEMBER_AT(int, 0xF9, nAnvilItemID)
};

typedef void(__cdecl* GetBasicFont_t)(IWzFontPtr*, int);
typedef HRESULT(__thiscall* WzFontCreate_t)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
typedef ZXString<char>*(__fastcall* StringPoolGet_t)(
        void*, void*, ZXString<char>*, unsigned int, char);

static auto get_basic_font = reinterpret_cast<GetBasicFont_t>(kAddr_get_basic_font);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(kAddr_WzFontCreate);
static auto StringPoolGet = reinterpret_cast<StringPoolGet_t>(kAddr_StringPoolGet);

static IWzFontPtr g_fontWhite;
static IWzFontPtr g_fontOrange;
static IWzFontPtr g_fontTitle;
static bool g_fontsReady = false;

static CUIToolTip* g_notedTip = nullptr;
static int g_notedX = 0;
static int g_notedY = 0;

static char g_statLabel[15][64] = {};
static bool g_statLabelsReady = false;

static void PatchSkipBulletJe(uintptr_t jeAddr, int origDisp) {
    Memory::WriteByte(jeAddr + 0, 0xE9);
    Memory::WriteByte(jeAddr + 1, static_cast<unsigned char>(origDisp & 0xFF));
    Memory::WriteByte(jeAddr + 2, static_cast<unsigned char>((origDisp >> 8) & 0xFF));
    Memory::WriteByte(jeAddr + 3, static_cast<unsigned char>((origDisp >> 16) & 0xFF));
    Memory::WriteByte(jeAddr + 4, static_cast<unsigned char>((origDisp >> 24) & 0xFF));
    Memory::WriteByte(jeAddr + 5, 0x90);
}

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static bool CreateDotum(IWzFontPtr& out, unsigned long color) {
    if (out) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
        if (!out) {
            return false;
        }
        const Ztl_variant_t style(L"");
        if (FAILED(WzFontCreate(out, L"Dotum", 12, color, style))) {
            out = nullptr;
            return false;
        }
        return true;
    } catch (...) {
        out = nullptr;
        return false;
    }
}

static void EnsureFonts() {
    if (g_fontsReady) {
        return;
    }
    CreateDotum(g_fontWhite, kColWhite);
    CreateDotum(g_fontOrange, kColOrange);
    CreateDotum(g_fontTitle, kColTitle);
    if (!g_fontWhite) {
        try {
            get_basic_font(std::addressof(g_fontWhite), 0);
            g_fontOrange = g_fontWhite;
            g_fontTitle = g_fontWhite;
        } catch (...) {
        }
    }
    if (!g_fontOrange) {
        g_fontOrange = g_fontWhite;
    }
    if (!g_fontTitle) {
        g_fontTitle = g_fontWhite;
    }
    g_fontsReady = g_fontWhite != nullptr;
}

static IWzFontPtr FontOf(LineStyle s) {
    switch (s) {
    case LineStyle::Title:
        return g_fontTitle ? g_fontTitle : g_fontWhite;
    case LineStyle::Orange:
        return g_fontOrange ? g_fontOrange : g_fontWhite;
    default:
        return g_fontWhite;
    }
}

static int Measure(IWzFontPtr font, const char* gbk) {
    if (!gbk || !gbk[0]) {
        return 0;
    }
    if (font) {
        try {
            return static_cast<int>(font->CalcTextWidth(GbkToBstr(gbk), Ztl_variant_t()));
        } catch (...) {
        }
    }
    int w = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(gbk); *p; ++p) {
        w += (*p & 0x80) ? 12 : 7;
    }
    return w;
}

static void DrawTextAt(IWzCanvasPtr canvas, int x, int y, const char* text, IWzFontPtr font) {
    if (!canvas || !text || !text[0] || !font) {
        return;
    }
    try {
        canvas->DrawTextA(x, y, GbkToBstr(text), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static int MeasureLine(const TipLine& line) {
    if (line.layout == LineLayout::ColonCenter) {
        const int spaceW = Measure(FontOf(line.styleB), "\x20");
        const int colonW = Measure(FontOf(line.styleB), "\x3A");
        const int leftW = Measure(FontOf(line.styleA), line.a.c_str()) + spaceW;
        const int rightW = spaceW + Measure(FontOf(line.styleB), line.b.c_str());
        return 2 * (std::max)(leftW, rightW) + colonW;
    }
    if (line.layout == LineLayout::Center) {
        return Measure(FontOf(line.styleA), line.a.c_str());
    }
    return Measure(FontOf(line.styleA), line.a.c_str()) + Measure(FontOf(line.styleB), line.b.c_str());
}

static int EstimateWidth(const std::vector<TipLine>& lines) {
    int maxLine = 0;
    for (const TipLine& line : lines) {
        maxLine = (std::max)(maxLine, MeasureLine(line));
    }
    return (std::max)(180, (std::min)(320, maxLine + 2 * kPadX)) + 40;
}

static int ContentHeight(const std::vector<TipLine>& lines) {
    int y = kPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].sepBefore) {
            y += kSepGapY;
        }
        y += kLineH;
        if (i == 0 && lines[i].layout == LineLayout::Center) {
            y += kTitleExtraH;
        }
    }
    return y + kPadY;
}

static void DrawLine(IWzCanvasPtr canvas, int width, int y, const TipLine& line) {
    if (line.layout == LineLayout::ColonCenter) {
        IWzFontPtr lf = FontOf(line.styleA);
        IWzFontPtr vf = FontOf(line.styleB);
        const int labelW = Measure(lf, line.a.c_str());
        const int spaceW = Measure(vf, "\x20");
        const int colonW = Measure(vf, "\x3A");
        const int colonX = width / 2 - colonW / 2;
        const int sepX = colonX - spaceW;
        const int labelX = sepX - labelW;
        DrawTextAt(canvas, labelX, y, line.a.c_str(), lf);
        DrawTextAt(canvas, sepX, y, kColonSep, vf);
        DrawTextAt(canvas, colonX + colonW + spaceW, y, line.b.c_str(), vf);
        return;
    }
    IWzFontPtr f = FontOf(line.styleA);
    if (line.layout == LineLayout::Center) {
        const int w = Measure(f, line.a.c_str());
        DrawTextAt(canvas, (std::max)(kPadX, (width - w) / 2), y, line.a.c_str(), f);
        return;
    }
    DrawTextAt(canvas, kPadX, y, line.a.c_str(), f);
}

static void RenderLines(IWzCanvasPtr canvas, const std::vector<TipLine>& lines, int width) {
    int y = kPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const TipLine& line = lines[i];
        if (line.sepBefore) {
            try {
                canvas->DrawRectangle(kPadX, y - 2, width - 2 * kPadX, 1, kSepColor);
            } catch (...) {
            }
            y += kSepGapY;
        }
        DrawLine(canvas, width, y, line);
        y += kLineH;
        if (i == 0 && line.layout == LineLayout::Center) {
            y += kTitleExtraH;
        }
    }
}

static int CreateLayerAtHeight(CUIToolTip* tip, int tipX, int tipY, int tipW, int targetH) {
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
    for (int i = 0; i < 8 && need > 1 && tip->m_nHeight > targetH + 3; ++i) {
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

static IWzCanvasPtr GetCanvas(CUIToolTip* tip) {
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

static void EnsureStatLabels() {
    if (g_statLabelsReady) {
        return;
    }
    void* pool = nullptr;
    try {
        pool = reinterpret_cast<void*(__cdecl*)()>(kAddr_StringPoolInstance)();
    } catch (...) {
        return;
    }
    if (!pool) {
        return;
    }
    for (int i = 0; i < 15; ++i) {
        try {
            ZXString<char> s;
            StringPoolGet(pool, nullptr, &s, kPoolStatIds[i], 0);
            const char* p = static_cast<const char*>(s);
            if (!p) {
                continue;
            }
            // Strip trailing spaces / colons for ColonCenter label.
            std::string clean = p;
            while (!clean.empty()) {
                const unsigned char c = static_cast<unsigned char>(clean.back());
                if (c == ' ' || c == ':' || c == '\t') {
                    clean.pop_back();
                    continue;
                }
                if (clean.size() >= 2) {
                    const unsigned char c1 = static_cast<unsigned char>(clean[clean.size() - 2]);
                    const unsigned char c0 = static_cast<unsigned char>(clean[clean.size() - 1]);
                    if (c1 == 0xA3 && c0 == 0xBA) {
                        clean.resize(clean.size() - 2);
                        continue;
                    }
                }
                break;
            }
            _snprintf_s(g_statLabel[i], _TRUNCATE, "%s", clean.c_str());
        } catch (...) {
        }
    }
    g_statLabelsReady = true;
}

static int SafeItemId(GW_ItemSlotEquipLocal* pe) {
    if (!pe) {
        return 0;
    }
    try {
        return reinterpret_cast<int(__thiscall*)(const void*)>(kAddr_TSecTypeGetData)(
                reinterpret_cast<const char*>(pe) + 0xC);
    } catch (...) {
        return 0;
    }
}

static short SafeStat(GW_ItemSlotEquipLocal* pe, int which) {
    if (!pe) {
        return 0;
    }
    try {
        switch (which) {
        case 0: return pe->niSTR;
        case 1: return pe->niDEX;
        case 2: return pe->niINT;
        case 3: return pe->niLUK;
        case 4: return pe->niMaxHP;
        case 5: return pe->niMaxMP;
        case 6: return pe->niPAD;
        case 7: return pe->niMAD;
        case 8: return pe->niPDD;
        case 9: return pe->niMDD;
        case 10: return pe->niACC;
        case 11: return pe->niEVA;
        case 12: return pe->niCraft;
        case 13: return pe->niSpeed;
        case 14: return pe->niJump;
        default: return 0;
        }
    } catch (...) {
        return 0;
    }
}

static int SafeRuc(GW_ItemSlotEquipLocal* pe) {
    if (!pe) {
        return -1;
    }
    try {
        return static_cast<int>(pe->nRUC);
    } catch (...) {
        return -1;
    }
}

static std::string GetItemName(int itemId) {
    if (itemId <= 0) {
        return "";
    }
    try {
        ZXString<char> s;
        reinterpret_cast<ZXString<char>*(__thiscall*)(CItemInfo*, ZXString<char>*, int)>(
                kAddr_GetItemName)(CItemInfo::GetInstance(), &s, itemId);
        const char* p = static_cast<const char*>(s);
        return p ? p : "";
    } catch (...) {
        return "";
    }
}

// Official category names (mxd.dvg.cn 装备分类 / CMS). Strict GBK — verify bytes:
//   袋=B4FC (NOT 代=B4FA), 腾=CCDA, 勋=D1AB, 脏=D4E0, 链=C1B4.
// Shared Si slot (islot=Si, bp −10): 109 / 134 / 135 — subtype is ID-range text, not islot.
struct SiCatRange {
    int lo;
    int hi;
    const char* gbk;
};

static const char kCatShield[] = "\xB6\xDC\xC5\xC6";                     // 盾牌
static const char kCatKatara[] = "\xCB\xAB\xB5\xB6";                     // 双刀
static const char kCatSubWeapon[] = "\xB8\xA8\xD6\xFA\xCE\xE4\xC6\xF7"; // 辅助武器
static const char kCatPocket[] = "\xBF\xDA\xB4\xFC\xB5\xC0\xBE\xDF";     // 口袋道具 (mxd aPocket)
static const char kCatShoulder[] = "\xBC\xE7\xCA\xCE";                   // 肩饰
static const char kCatBadge[] = "\xBB\xD5\xD5\xC2";                      // 徽章 Badge 118
static const char kCatEmblem[] = "\xCE\xC6\xD5\xC2";                     // 纹章 Emblem 119
static const char kCatTotem[] = "\xCD\xBC\xCC\xDA";                      // 图腾
static const char kCatAndroid[] = "\xD6\xC7\xC4\xDC\xBB\xFA\xC6\xF7\xC8\xCB"; // 智能机器人
static const char kCatMechHeart[] = "\xBB\xFA\xD0\xB5\xD0\xC4\xD4\xE0"; // ji xie xin zang
static const char kCatBelt[] = "\xD1\xFC\xB4\xF8";                       // 腰带
static const char kCatMedal[] = "\xD1\xAB\xD5\xC2";                      // 勋章
static const char kCatPendant[] = "\xCF\xEE\xC1\xB4";                    // 项链

static const SiCatRange kSi135Ranges[] = {
        {1352000, 1352014, "\xC4\xA7\xB7\xA8\xBC\xFD\xCA\xB8"},     // 魔法箭矢
        {1352100, 1352114, "\xBF\xA8\xC6\xAC"},                     // 卡片
        {1352200, 1352209, "\xB5\xF5\xD7\xB9"},                     // 吊坠
        {1352210, 1352219, "\xC4\xEE\xD6\xE9"},                     // 念珠
        {1352220, 1352229, "\xCB\xF8\xC1\xB4"},                     // 锁链
        {1352230, 1352259, "\xC4\xA7\xB5\xC0\xCA\xE9"},             // 魔道书
        {1352260, 1352269, "\xBC\xFD\xD3\xF0"},                     // 箭羽
        {1352270, 1352279, "\xB9\xAD\xBC\xFD\xBB\xA4\xD6\xB8"},     // 弓箭护指
        {1352280, 1352289, "\xB6\xCC\xBD\xA3\xBD\xA3\xC7\xCA"},     // 短剑剑鞘
        {1352290, 1352299, "\xBB\xA4\xB7\xFB"},                     // 护符
        {1352300, 1352304, "\xB0\xCB\xD8\xD4\xB1\xA6\xBA\xD0"},     // 八卦宝盒
        {1352400, 1352409, "\xB1\xA6\xD6\xE9"},                     // 宝珠
        {1352500, 1352509, "\xBE\xAB\xCB\xE8"},                     // 精髓
        {1352600, 1352609, "\xCA\xD6\xEF\xED"},                     // 手镯
        {1352700, 1352710, "\xD7\xB0\xB5\xAF"},                     // 装弹
        {1352800, 1352809, "\xD0\xA1\xB5\xB6"},                     // 小刀
        {1352810, 1352819, "\xBF\xDA\xC9\xDA"},                     // 口哨
        {1352820, 1352829, "\xC8\xAD\xCC\xD7"},                     // 拳套
        {1352830, 1352840, "\xD0\xA1\xCC\xAB\xB5\xB6"},             // 小太刀
        {1352860, 1352888, "\xC8\xAD\xCC\xEC"},                     // 拳天
        {1352900, 1352909, "\xCA\xD6\xCD\xF3\xBB\xA4\xBC\xD7"},     // 手腕护甲
        {1352910, 1352919, "\xD3\xA5\xD1\xDB"},                     // 鹰眼
        {1352920, 1352929, "\xBB\xF0\xD2\xA9\xCD\xB0"},             // 火药桶
        {1352930, 1352938, "\xCC\xEC\xC1\xFA\xB4\xB8"},             // 天龙锤
        {1352940, 1352948, "\xC1\xFA\xC9\xF1\xD2\xC5\xB2\xFA"},     // 龙神遗产
        {1352950, 1352959, "\xBC\xAB\xCF\xDE\xC7\xF2"},             // 极限球
        {1352960, 1352969, "\xBF\xF1\xD2\xB0\xD6\xAE\xD1\xC0"},     // 狂野之牙
        {1352970, 1352978, "\xCA\xA5\xB5\xD8\xD6\xAE\xB9\xE2"},     // 圣地之光
        {1353000, 1353009, "\xBF\xD8\xD6\xC6\xC6\xF7"},             // 控制器
        {1353010, 1353110, "\xBA\xFC\xC0\xEA\xD6\xE9"},             // 狐狸珠
        {1353200, 1353209, "\xC6\xE5\xD7\xD3"},                     // 棋子
        {1353300, 1353310, "\xB7\xA2\xC9\xE4\xC6\xF7"},             // 发射器
        {1353400, 1353409, "\xB1\xAC\xC6\xC6\xB5\xAF"},             // 爆破弹
        {1353500, 1353508, "\xC4\xA7\xB7\xA8\xD6\xAE\xD2\xED"},     // 魔法之翼
        {1353600, 1353608, "\xBE\xAB\xC6\xF8\xD6\xE9"},             // 精气珠
        {1353700, 1353709, "\xD2\xC5\xCE\xEF"},                     // 遗物
        {1353800, 1353900, "\xC9\xC8\xD7\xB9"},                     // 扇坠
        {1354000, 1354009, "\xCA\xD6\xC1\xB4"},                     // 手链
};

// Official Si subtype label. 109→盾牌; 134→双刀; 135→ID-range; else nullptr.
static const char* OfficialSiCategoryGbk(int itemId) {
    const int cat = itemId / 10000;
    if (cat == 109) {
        return kCatShield;
    }
    if (cat == 134) {
        return kCatKatara;
    }
    if (cat != 135) {
        return nullptr;
    }
    for (const auto& r : kSi135Ranges) {
        if (itemId >= r.lo && itemId <= r.hi) {
            return r.gbk;
        }
    }
    return kCatSubWeapon;
}

static std::string CategoryLabel(int itemId) {
    if (const char* si = OfficialSiCategoryGbk(itemId)) {
        return si;
    }
    const int cat = itemId / 10000;
    switch (cat) {
    case 100: return "\xC3\xB1\xD7\xD3"; // 帽子
    case 104: return "\xC9\xCF\xD2\xC2"; // 上衣
    case 105: return "\xCC\xD7\xB7\xFE"; // 套服
    case 106: return "\xC0\xA8\xD7\xD3"; // 裤裙
    case 107: return "\xD0\xAC\xD7\xD3"; // 鞋子
    case 108: return "\xCA\xD6\xCC\xD7"; // 手套
    case 110: return "\xC5\xFB\xB7\xE7"; // 披风
    case 111: return "\xBD\xE4\xD6\xB8"; // 戒指
    case 112: return kCatPendant;        // 项链
    case 113: return kCatBelt;           // 腰带
    case 114: return kCatMedal;          // 勋章
    case 115: return kCatShoulder;       // 肩饰
    case 116: return kCatPocket;         // 口袋道具（勿走 BP33→宠物装备）
    case 118: return kCatBadge;          // 徽章
    case 119: return kCatEmblem;         // 纹章
    case 120: return kCatTotem;          // 图腾
    case 166: return kCatAndroid;
    case 167: return "\xBB\xFA\xD0\xB5\xD0\xC4\xD4\xE0";
    default:
        if (cat >= 130 && cat <= 149) {
            return "\xCE\xE4\xC6\xF7"; // 武器
        }
        if (cat == 170) {
            return "\xCE\xE4\xC6\xF7"; // 武器
        }
        char buf[16];
        _snprintf_s(buf, _TRUNCATE, "%d", cat);
        return buf;
    }
}

using GetWeaponCategoryNameFn = ZXString<char>*(__cdecl*)(ZXString<char>* out, int itemId);
static auto Original_GetWeaponCategoryName =
        reinterpret_cast<GetWeaponCategoryNameFn>(kAddr_GetWeaponCategoryName);
static auto Original_GetEquipCategoryByBodyPart =
        reinterpret_cast<GetWeaponCategoryNameFn>(kAddr_GetEquipCategoryByBodyPart);

using ZXAssignFn = int(__thiscall*)(void* self, const char* src, size_t size);

// Safe ZXString write for uninitialized tooltip outs (never Empty / DLL Assign).
static ZXString<char>* AssignCategoryLabel(ZXString<char>* out, const char* label) {
    *reinterpret_cast<char**>(out) = nullptr;
    auto Assign = reinterpret_cast<ZXAssignFn>(kAddr_ZXString_Assign);
    Assign(out, label, std::strlen(label));
    return out;
}

// Prefer official labels over bodypart StringPool fallbacks.
// CRITICAL: SetToolTip_Equip_Basic @0x8ECA0C may pass an uninitialized ZXString;
// never Empty/DLL Assign — use *out=nullptr + game Assign@0x414617 only.
static const char* OverrideCategoryGbk(int itemId) {
    const int cat = itemId / 10000;
    switch (cat) {
    case 113: return kCatBelt;
    case 114: return kCatMedal;
    case 115: return kCatShoulder;
    case 116: return kCatPocket;
    case 118: return kCatBadge;
    case 119: return kCatEmblem;
    case 120: return kCatTotem;
    case 134:
    case 135: return OfficialSiCategoryGbk(itemId);
    case 166: return kCatAndroid;
    case 167: return "\xBB\xFA\xD0\xB5\xD0\xC4\xD4\xE0";
    default: return nullptr;
    }
}

static ZXString<char>* __cdecl Hook_GetWeaponCategoryName(ZXString<char>* out, int itemId) {
    if (!out) {
        return Original_GetWeaponCategoryName(out, itemId);
    }
    if (const char* label = OverrideCategoryGbk(itemId)) {
        return AssignCategoryLabel(out, label);
    }
    return Original_GetWeaponCategoryName(out, itemId);
}

// Belt-and-suspenders: inventory/other UIs may call bodypart category directly.
static ZXString<char>* __cdecl Hook_GetEquipCategoryByBodyPart(ZXString<char>* out, int itemId) {
    if (!out) {
        return Original_GetEquipCategoryByBodyPart(out, itemId);
    }
    if (const char* label = OverrideCategoryGbk(itemId)) {
        return AssignCategoryLabel(out, label);
    }
    return Original_GetEquipCategoryByBodyPart(out, itemId);
}

static void AppendColon(
        std::vector<TipLine>& lines,
        const char* label,
        const char* value,
        LineStyle ls = LineStyle::White,
        LineStyle vs = LineStyle::White) {
    TipLine line;
    line.layout = LineLayout::ColonCenter;
    line.a = label ? label : "";
    line.b = value ? value : "";
    line.styleA = ls;
    line.styleB = vs;
    lines.push_back(std::move(line));
}

static std::vector<TipLine> BuildLayout(GW_ItemSlotEquipLocal* pe) {
    std::vector<TipLine> lines;
    const int itemId = SafeItemId(pe);
    if (itemId / 1000000 != 1) {
        return lines;
    }
    EnsureStatLabels();

    TipLine title;
    title.layout = LineLayout::Center;
    title.styleA = LineStyle::Title;
    title.a = GetItemName(itemId);
    if (title.a.empty()) {
        char buf[32];
        _snprintf_s(buf, _TRUNCATE, "%d", itemId);
        title.a = buf;
    }
    lines.push_back(std::move(title));

    AppendColon(lines, kLabelCategory, CategoryLabel(itemId).c_str());

    bool anyStat = false;
    for (int i = 0; i < 15; ++i) {
        const short v = SafeStat(pe, i);
        if (v == 0) {
            continue;
        }
        char val[32];
        if (i >= 6 && i <= 9) {
            _snprintf_s(val, _TRUNCATE, "%d", static_cast<int>(v));
        } else {
            _snprintf_s(val, _TRUNCATE, "+%d", static_cast<int>(v));
        }
        const char* lab = g_statLabel[i][0] ? g_statLabel[i] : "?";
        TipLine line;
        line.layout = LineLayout::ColonCenter;
        line.a = lab;
        line.b = val;
        line.styleA = (i == 6 || i == 7) ? LineStyle::Orange : LineStyle::White;
        line.styleB = LineStyle::White;
        line.sepBefore = !anyStat;
        anyStat = true;
        lines.push_back(std::move(line));
    }

    const int ruc = SafeRuc(pe);
    if (ruc >= 0) {
        char val[16];
        _snprintf_s(val, _TRUNCATE, "%d", ruc);
        TipLine line;
        line.layout = LineLayout::ColonCenter;
        line.a = kLabelUpgrade;
        line.b = val;
        line.sepBefore = true;
        lines.push_back(std::move(line));
    }

    return lines;
}

static bool DrawCustomInner(CUIToolTip* tip, GW_ItemSlotEquip* peRaw, int extraBottomPad) {
    if (!tip || !peRaw) {
        return false;
    }
    auto* pe = reinterpret_cast<GW_ItemSlotEquipLocal*>(peRaw);
    EnsureFonts();
    if (!g_fontsReady || !g_fontWhite) {
        return false;
    }

    std::vector<TipLine> lines = BuildLayout(pe);
    if (lines.empty()) {
        return false;
    }

    int tipX = g_notedX;
    int tipY = g_notedY;
    if (g_notedTip == tip) {
        // keep noted
    } else if (tip->m_pLayer) {
        try {
            tipX = tip->m_pLayer->rx;
            tipY = tip->m_pLayer->ry;
        } catch (...) {
        }
    }
    if (tipX < 0) {
        tipX = 0;
    }
    if (tipY < 0) {
        tipY = 0;
    }

    const int width = EstimateWidth(lines);
    int targetH = ContentHeight(lines) + (std::max)(0, extraBottomPad);
    CreateLayerAtHeight(tip, tipX, tipY, width, targetH);

    IWzCanvasPtr canvas = GetCanvas(tip);
    if (!canvas) {
        return false;
    }
    int drawW = tip->m_nWidth > 0 ? tip->m_nWidth : width;
    try {
        const int cw = static_cast<int>(canvas->width);
        if (cw > 0) {
            drawW = cw;
        }
    } catch (...) {
    }
    RenderLines(canvas, lines, drawW);
    return true;
}

static bool SehDrawCustom(CUIToolTip* tip, GW_ItemSlotEquip* pe, int extraBottomPad) {
    __try {
        return DrawCustomInner(tip, pe, extraBottomPad);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace

void EquipTooltipStyle_NoteHoverPos(CUIToolTip* tip, int x, int y) {
    g_notedTip = tip;
    g_notedX = x;
    g_notedY = y;
}

bool EquipTooltipStyle_TryDrawCustom(
        CUIToolTip* tip,
        GW_ItemSlotEquip* pe,
        int extraBottomPad) {
#if !EQUIP_TIP_STYLE_CUSTOM_CANVAS
    (void)tip;
    (void)pe;
    (void)extraBottomPad;
    return false;
#else
    return SehDrawCustom(tip, pe, extraBottomPad);
#endif
}

void AttachEquipTooltipStyleHooks() {
    if (g_styleAttached) {
        return;
    }
    g_styleAttached = true;

    // Official Si/Pocket labels for tooltip「装备分类」(does not touch WZ islot).
    ATTACH_HOOK(Original_GetWeaponCategoryName, Hook_GetWeaponCategoryName);
    ATTACH_HOOK(Original_GetEquipCategoryByBodyPart, Hook_GetEquipCategoryByBodyPart);

    // Bullet-strip patches are only for custom Dotum canvas. Vanilla tip
    // (CUSTOM_CANVAS=0) must keep original AddInfoEx/PrintLine — otherwise
    // 物品等级/道具经验/描述 stack on 要求/金锤子 (line-advance collapse).
#if EQUIP_TIP_STYLE_CUSTOM_CANVAS
    Memory::WriteByte(kAddInfoExStoreDot + 0, 0x33);
    Memory::WriteByte(kAddInfoExStoreDot + 1, 0xC0);
    Memory::WriteByte(kAddInfoExStoreDot + 2, 0x89);
    Memory::WriteByte(kAddInfoExStoreDot + 3, 0x46);
    Memory::WriteByte(kAddInfoExStoreDot + 4, 0x1C);
    Memory::WriteByte(kAddInfoExStoreDot + 5, 0x90);
    PatchSkipBulletJe(kPrintLineSkipDot, 0x139);
#endif
}
