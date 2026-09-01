#include "stdafx.h"
#include "EquipGrowthApi.h"
#include "../Client.h"
#include "../setitem/SetItemApi.h"
#include "../setitem/equiptooltip_style.h"
#include "../equipcompare/EquipCompareApi.h"
#include "compat/ClientAddresses.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
#include <comdef.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr int kGrowthTooltipBufSize = 0xB00;
constexpr int kGrowthTipGap = 4;
constexpr int kGrowthLayerZBoost = 80;
constexpr uintptr_t kAddr_TSecTypeGetData = 0x0042873D;
// IDA: sub_8ED8E0 → ZtlSecurePacked nItemLevel @ 0xBA; nCUC (scroll level) @ 0x2E after nRUC.
constexpr size_t kOffset_nCUC = 0x2E;
constexpr size_t kOffset_nItemLevel = 0xBA;
constexpr size_t kOffset_nEnhance = 0x10D;

// Match SetItem companion tip metrics (pad / indent / line height).
constexpr int kGrowthTipPadX = 8;
constexpr int kGrowthTipPadY = 6;
constexpr int kGrowthTipLineH = 16;
constexpr int kGrowthTipTitleExtraH = 2;
constexpr int kGrowthTipSepGapY = 3;
constexpr int kGrowthTipTierGapY = 3;
constexpr int kGrowthTipIndentChars = 1;
constexpr int kGrowthTipHeightSlack = 3;
constexpr unsigned int kGrowthTipSepColor = 0x80FFFFFF;
constexpr unsigned long kColTitleLime = 0xFFCCFF00;
constexpr unsigned long kColWhite = 0xFFFFFFFF;
constexpr unsigned long kColGrey = 0xFFBBBBBB;
constexpr char kGrowthTipColonSep[] = "\x20\x3A\x20"; // " : "
// GBK「级效果」
constexpr char kLevelEffectSuffix[] = "\xBC\xB6\xD0\xA7\xB9\xFB";

enum class GrowthTipStyle { Title, Header, White, Grey };
enum class GrowthTipLayout { Left, Center, IndentLeft };

struct GrowthTipSegment {
    std::string text;
    GrowthTipStyle style = GrowthTipStyle::White;
};

struct GrowthTipLine {
    std::vector<GrowthTipSegment> segments;
    bool separatorBefore = false;
    bool tierGapAfter = false;
    GrowthTipLayout layout = GrowthTipLayout::Left;
};

struct GrowthTipFonts {
    IWzFontPtr title;
    IWzFontPtr header;
    IWzFontPtr white;
    IWzFontPtr grey;
    bool ready = false;
};

typedef void(__cdecl* GetBasicFont_t)(IWzFontPtr*, int);
typedef HRESULT(__thiscall* WzFontCreate_t)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto get_basic_font = reinterpret_cast<GetBasicFont_t>(0x0098A707);
static auto WzFontCreate = reinterpret_cast<WzFontCreate_t>(0x0046341A);

GrowthTipFonts g_growthTipFonts;

alignas(8) char g_growthTooltipBuf[kGrowthTooltipBufSize];
bool g_growthTooltipInited = false;
// Second buffer: growth tip for equipped item in the compare column.
alignas(8) char g_cmpGrowthTooltipBuf[kGrowthTooltipBufSize];
bool g_cmpGrowthTooltipInited = false;
bool g_inGrowthUpdate = false;
CUIToolTip* g_activeMainTip = nullptr;
int g_lastHoverItemId = 0;
int g_lastShownItemId = 0;
int g_lastDockX = 0;
int g_lastDockY = 0;
int g_cmpLastShownItemId = 0;
int g_cmpLastDockX = 0;
int g_cmpLastDockY = 0;

typedef int(__thiscall* TSecTypeGetData_t)(const void*);
static auto TSecTypeGetData = reinterpret_cast<TSecTypeGetData_t>(kAddr_TSecTypeGetData);

static bool IsEquipItemId(int itemId) {
    return itemId >= 1000000 && itemId < 2000000;
}

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

static int SafeGetItemId(void* pe) {
    return DecodeItemIdAt(pe, 0xC);
}

static int SafeReadPackedByte(void* pe, size_t offset) {
    if (!pe) {
        return 0;
    }
    __try {
        auto* packed = reinterpret_cast<ZtlSecurePacked<unsigned char>*>(
                reinterpret_cast<char*>(pe) + offset);
        const int v = static_cast<int>(static_cast<unsigned char>(*packed));
        if (v < 0 || v > 99) {
            return 0;
        }
        return v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int SafeGetEnhance(void* pe) {
    if (!pe) {
        return 0;
    }
    __try {
        const unsigned char e = *reinterpret_cast<unsigned char*>(
                reinterpret_cast<char*>(pe) + kOffset_nEnhance);
        if (e > 10) {
            return 0;
        }
        return static_cast<int>(e);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static int SafeGetItemLevel(void* pe) {
    return SafeReadPackedByte(pe, kOffset_nItemLevel);
}

static int SafeGetScrollLevel(void* pe) {
    return SafeReadPackedByte(pe, kOffset_nCUC);
}

// --- SEH helpers: no C++ objects with destructors in these functions ---

static bool SehHasTipLayer() {
    if (!g_growthTooltipInited) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf);
        return tip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SafeRelMoveTip(CUIToolTip* tip, int x, int y) {
    __try {
        if (!tip || !tip->m_pLayer) {
            return;
        }
        // RelMove + force rx/ry/stored (same as set companion). MakeLayer clamp can leave (0,0).
        tip->m_pLayer->RelMove(x, y);
        tip->m_pLayer->rx = x;
        tip->m_pLayer->ry = y;
        tip->m_nLayerLeft = x;
        tip->m_nLayerTop = y;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static int SafeGetTipWidth(CUIToolTip* tip) {
    int w = 0;
    __try {
        if (tip && tip->m_nWidth > 0) {
            w = tip->m_nWidth;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        w = 0;
    }
    return w;
}

static int SafeGetTipHeight(CUIToolTip* tip) {
    int h = 0;
    __try {
        if (tip && tip->m_nHeight > 0) {
            h = tip->m_nHeight;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        h = 0;
    }
    return h;
}

static bool SehReadTipRect(int& outX, int& outY, int& outW, int& outH) {
    outX = outY = outW = outH = 0;
    if (!g_growthTooltipInited || g_lastShownItemId <= 0) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf);
        if (!tip || !tip->m_pLayer) {
            return false;
        }
        outW = tip->m_nWidth;
        outH = tip->m_nHeight;
        const int storedX = tip->m_nLayerLeft;
        const int storedY = tip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else if (g_lastDockX != 0 || g_lastDockY != 0) {
            outX = g_lastDockX;
            outY = g_lastDockY;
        } else {
            outX = tip->m_pLayer->rx;
            outY = tip->m_pLayer->ry;
        }
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehReadMainOrigin(CUIToolTip* mainTip, int& mainX, int& mainY, int& mainW, int& mainH) {
    mainX = mainY = mainW = mainH = 0;
    if (!mainTip) {
        return;
    }
    // ShowItemToolTip / MakeLayer screen anchor — shop/storage often leave rx/ry=0.
    EquipTooltipStyle_GetHoverPos(mainTip, mainX, mainY);
    __try {
        mainW = mainTip->m_nWidth;
        mainH = mainTip->m_nHeight;
        const int storedX = mainTip->m_nLayerLeft;
        const int storedY = mainTip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            mainX = storedX;
            mainY = storedY;
        } else if (mainTip->m_pLayer) {
            const int rx = mainTip->m_pLayer->rx;
            const int ry = mainTip->m_pLayer->ry;
            if (rx != 0 || ry != 0) {
                mainX = rx;
                mainY = ry;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mainW = mainH = 0;
    }
}

static void SehReadLayerState(CUIToolTip* tip, int& rx, int& ry, int& w, int& h, int& z, int& vis) {
    rx = ry = w = h = z = vis = 0;
    if (!tip) {
        return;
    }
    __try {
        w = tip->m_nWidth;
        h = tip->m_nHeight;
        if (tip->m_pLayer) {
            rx = tip->m_pLayer->rx;
            ry = tip->m_pLayer->ry;
            z = tip->m_pLayer->z;
            vis = tip->m_pLayer->visible;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rx = ry = w = h = z = vis = 0;
    }
}

static void SehBoostLayerAboveSet(CUIToolTip* tip) {
    if (!tip) {
        return;
    }
    // Setlike: pin a stable z once — do NOT keep stacking +80 every RelMove.
    __try {
        if (!tip->m_pLayer) {
            return;
        }
        const int cur = tip->m_pLayer->z;
        if (cur < kGrowthLayerZBoost) {
            tip->m_pLayer->z = kGrowthLayerZBoost;
        }
        tip->m_pLayer->visible = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static CUIToolTip* EnsureGrowthTooltip() {
    if (!g_growthTooltipInited) {
        // Match SetItem: call game ctor directly (avoid plugin vtable quirks).
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
                g_growthTooltipBuf);
        g_growthTooltipInited = true;
    }
    return reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf);
}

static CUIToolTip* EnsureCmpGrowthTooltip() {
    if (!g_cmpGrowthTooltipInited) {
        reinterpret_cast<void(__thiscall*)(void*)>(ClientAddresses::kToolTipCtor)(
                g_cmpGrowthTooltipBuf);
        g_cmpGrowthTooltipInited = true;
    }
    return reinterpret_cast<CUIToolTip*>(g_cmpGrowthTooltipBuf);
}

static void HideCmpGrowthTooltipInternal() {
    if (g_cmpGrowthTooltipInited) {
        try {
            reinterpret_cast<CUIToolTip*>(g_cmpGrowthTooltipBuf)->ClearToolTip();
        } catch (...) {
        }
    }
    g_cmpLastShownItemId = 0;
    g_cmpLastDockX = 0;
    g_cmpLastDockY = 0;
}

static bool SehHasCmpTipLayer() {
    if (!g_cmpGrowthTooltipInited) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_cmpGrowthTooltipBuf);
        return tip->m_pLayer != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void ComputeCmpGrowthDock(CUIToolTip* compareTip, int tipW, int& outX, int& outY) {
    if (tipW <= 0) {
        tipW = 180;
    }
    int setX = 0, setY = 0, setW = 0, setH = 0;
    if (SetItem::TryGetActiveCompareSetTooltipRect(setX, setY, setW, setH) && setW > 0) {
        outX = setX + setW + kGrowthTipGap;
        outY = setY;
    } else {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        SehReadMainOrigin(compareTip, cx, cy, cw, ch);
        if (cw <= 0) {
            cw = 180;
        }
        outX = cx + cw + kGrowthTipGap;
        outY = cy;
    }
    if (outY < 0) {
        outY = 0;
    }
    if (outX < 0) {
        outX = 0;
    }
}

static bool SehReadCmpTipRect(int& outX, int& outY, int& outW, int& outH) {
    outX = outY = outW = outH = 0;
    if (!g_cmpGrowthTooltipInited || g_cmpLastShownItemId <= 0) {
        return false;
    }
    __try {
        CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_cmpGrowthTooltipBuf);
        if (!tip || !tip->m_pLayer) {
            return false;
        }
        outW = tip->m_nWidth;
        outH = tip->m_nHeight;
        const int storedX = tip->m_nLayerLeft;
        const int storedY = tip->m_nLayerTop;
        if (storedX != 0 || storedY != 0) {
            outX = storedX;
            outY = storedY;
        } else if (g_cmpLastDockX != 0 || g_cmpLastDockY != 0) {
            outX = g_cmpLastDockX;
            outY = g_cmpLastDockY;
        } else {
            outX = tip->m_pLayer->rx;
            outY = tip->m_pLayer->ry;
        }
        return outW > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static Ztl_bstr_t GbkToBstr(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static bool CreateGrowthFont(IWzFontPtr& out, unsigned long color, int size, bool bold) {
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

static void EnsureGrowthTipFonts() {
    if (g_growthTipFonts.ready) {
        return;
    }
    CreateGrowthFont(g_growthTipFonts.title, kColTitleLime, 12, true);
    CreateGrowthFont(g_growthTipFonts.header, kColTitleLime, 12, true);
    CreateGrowthFont(g_growthTipFonts.white, kColWhite, 12, false);
    CreateGrowthFont(g_growthTipFonts.grey, kColGrey, 12, false);
    if (!g_growthTipFonts.white) {
        try {
            get_basic_font(std::addressof(g_growthTipFonts.white), 0);
            g_growthTipFonts.title = g_growthTipFonts.white;
            g_growthTipFonts.header = g_growthTipFonts.white;
            g_growthTipFonts.grey = g_growthTipFonts.white;
        } catch (...) {
        }
    }
    if (!g_growthTipFonts.grey) {
        g_growthTipFonts.grey = g_growthTipFonts.white;
    }
    g_growthTipFonts.ready = g_growthTipFonts.white != nullptr;
}

static IWzFontPtr GetGrowthTipFont(GrowthTipStyle style) {
    switch (style) {
    case GrowthTipStyle::Title:
        return g_growthTipFonts.title ? g_growthTipFonts.title : g_growthTipFonts.white;
    case GrowthTipStyle::Header:
        return g_growthTipFonts.header ? g_growthTipFonts.header : g_growthTipFonts.white;
    case GrowthTipStyle::Grey:
        return g_growthTipFonts.grey ? g_growthTipFonts.grey : g_growthTipFonts.white;
    case GrowthTipStyle::White:
    default:
        return g_growthTipFonts.white;
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

static int MeasureLineWidth(const GrowthTipLine& line) {
    int w = 0;
    if (line.layout == GrowthTipLayout::IndentLeft) {
        w += MeasureFontTextWidth(GetGrowthTipFont(GrowthTipStyle::White), "\xA1\xA1")
                * kGrowthTipIndentChars;
    }
    for (const GrowthTipSegment& seg : line.segments) {
        w += MeasureFontTextWidth(GetGrowthTipFont(seg.style), seg.text.c_str());
    }
    return w;
}

static void DrawGrowthTipText(IWzCanvasPtr canvas, int x, int y, const char* text, IWzFontPtr font) {
    if (!canvas || !text || !text[0] || !font) {
        return;
    }
    try {
        canvas->DrawTextA(x, y, GbkToBstr(text), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static void DrawGrowthTipLine(IWzCanvasPtr canvas, int width, int y, const GrowthTipLine& line) {
    const int lineW = MeasureLineWidth(line);
    int x = kGrowthTipPadX;
    if (line.layout == GrowthTipLayout::Center) {
        x = (std::max)(kGrowthTipPadX, (width - lineW) / 2);
    } else if (line.layout == GrowthTipLayout::IndentLeft) {
        const int indent = MeasureFontTextWidth(GetGrowthTipFont(GrowthTipStyle::White), "\xA1\xA1")
                * kGrowthTipIndentChars;
        x = kGrowthTipPadX + indent;
    }
    for (const GrowthTipSegment& seg : line.segments) {
        if (seg.text.empty()) {
            continue;
        }
        IWzFontPtr font = GetGrowthTipFont(seg.style);
        DrawGrowthTipText(canvas, x, y, seg.text.c_str(), font);
        x += MeasureFontTextWidth(font, seg.text.c_str());
    }
}

static void DrawGrowthTipSeparator(IWzCanvasPtr canvas, int width, int y) {
    if (!canvas || width <= 2 * kGrowthTipPadX + 1) {
        return;
    }
    try {
        canvas->DrawRectangle(kGrowthTipPadX, y, width - 2 * kGrowthTipPadX, 1, kGrowthTipSepColor);
    } catch (...) {
    }
}

static int ComputeGrowthTipContentHeight(const std::vector<GrowthTipLine>& lines) {
    int y = kGrowthTipPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const GrowthTipLine& line = lines[i];
        if (line.separatorBefore) {
            y += kGrowthTipSepGapY;
        }
        y += kGrowthTipLineH;
        if (i == 0 && line.layout == GrowthTipLayout::Center) {
            y += kGrowthTipTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kGrowthTipTierGapY;
        }
    }
    return y + kGrowthTipPadY;
}

static int ResolveGrowthTipWidth(const std::vector<GrowthTipLine>& lines, int preferred) {
    int maxLine = 0;
    for (const GrowthTipLine& line : lines) {
        maxLine = (std::max)(maxLine, MeasureLineWidth(line));
    }
    const int contentW = (std::max)(180, (std::min)(320, maxLine + 2 * kGrowthTipPadX));
    if (preferred > 40) {
        // Match set tip width exactly so indent/columns line up.
        return preferred;
    }
    return contentW;
}

static int CreateGrowthTipLayerAtHeight(CUIToolTip* tip, int tipX, int tipY, int tipW, int targetH) {
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
    for (int i = 0; i < 8 && need > 1 && tip->m_nHeight > targetH + kGrowthTipHeightSlack; ++i) {
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

static void RenderGrowthTooltipCanvas(IWzCanvasPtr canvas, const std::vector<GrowthTipLine>& lines,
        int width) {
    if (!canvas || lines.empty()) {
        return;
    }
    int y = kGrowthTipPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const GrowthTipLine& line = lines[i];
        if (line.separatorBefore) {
            DrawGrowthTipSeparator(canvas, width, y - 2);
            y += kGrowthTipSepGapY;
        }
        DrawGrowthTipLine(canvas, width, y, line);
        y += kGrowthTipLineH;
        if (i == 0 && line.layout == GrowthTipLayout::Center) {
            y += kGrowthTipTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kGrowthTipTierGapY;
        }
    }
}

static bool EndsWithLevelEffect(const std::string& line) {
    const size_t n = sizeof(kLevelEffectSuffix) - 1;
    return line.size() >= n && line.compare(line.size() - n, n, kLevelEffectSuffix) == 0;
}

static bool StartsWithAsciiDigit(const std::string& line) {
    return !line.empty() && line[0] >= '0' && line[0] <= '9';
}

static std::vector<GrowthTipLine> BuildGrowthTipLayout(const char* text, int itemLevel) {
    std::vector<GrowthTipLine> lines;
    if (!text || !text[0]) {
        return lines;
    }
    std::string raw(text);
    for (char& c : raw) {
        if (c == '\r') {
            c = '\n';
        }
    }
    size_t pos = 0;
    bool first = true;
    bool sawLevelHeader = false;
    // Match FillBonusFromWz: node N is applied when itemLevel > N.
    bool tierActive = true;
    while (pos < raw.size()) {
        size_t end = raw.find('\n', pos);
        if (end == std::string::npos) {
            end = raw.size();
        }
        std::string row = raw.substr(pos, end - pos);
        pos = end + 1;
        while (!row.empty() && (row.back() == ' ' || row.back() == '\t')) {
            row.pop_back();
        }
        if (row.empty()) {
            continue;
        }
        if (first) {
            GrowthTipLine title;
            title.layout = GrowthTipLayout::Center;
            title.segments.push_back({row, GrowthTipStyle::Title});
            lines.push_back(std::move(title));
            first = false;
            continue;
        }
        if (StartsWithAsciiDigit(row) && EndsWithLevelEffect(row)) {
            if (sawLevelHeader && !lines.empty()) {
                lines.back().tierGapAfter = true;
            }
            int node = 0;
            for (size_t i = 0; i < row.size() && row[i] >= '0' && row[i] <= '9'; ++i) {
                node = node * 10 + (row[i] - '0');
            }
            // Unmet growth steps are grey (same as inactive set tiers).
            tierActive = itemLevel > node;
            GrowthTipLine header;
            header.separatorBefore = true;
            header.layout = GrowthTipLayout::Left;
            header.segments.push_back(
                    {row, tierActive ? GrowthTipStyle::Header : GrowthTipStyle::Grey});
            lines.push_back(std::move(header));
            sawLevelHeader = true;
            continue;
        }
        GrowthTipLine attr;
        attr.layout = GrowthTipLayout::IndentLeft;
        const GrowthTipStyle bodyStyle =
                tierActive ? GrowthTipStyle::White : GrowthTipStyle::Grey;
        const size_t colon = row.find(kGrowthTipColonSep);
        if (colon != std::string::npos) {
            attr.segments.push_back({row.substr(0, colon) + kGrowthTipColonSep, bodyStyle});
            attr.segments.push_back({row.substr(colon + 3), bodyStyle});
        } else {
            attr.segments.push_back({row, bodyStyle});
        }
        lines.push_back(std::move(attr));
    }
    return lines;
}

static void DiagUi(const char* fmt, ...);

static void HideGrowthTooltipInternal(const char* reason) {
    if (g_growthTooltipInited) {
        try {
            reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf)->ClearToolTip();
        } catch (...) {
        }
    }
    if (g_lastShownItemId > 0 || (reason && reason[0])) {
        DiagUi("hide itemId=%d reason=%s", g_lastShownItemId, reason ? reason : "?");
    }
    g_lastShownItemId = 0;
}

// Right-chain: [equip] → [set?] → [growth] → [compare]. Always stay to the right.
static void ComputeGrowthDock(CUIToolTip* mainTip, int tipW, int tipH, int& outX, int& outY) {
    if (tipW <= 0) {
        tipW = 180;
    }
    (void)tipH;

    int mainX = 0, mainY = 0, mainW = 0, mainH = 0;
    SehReadMainOrigin(mainTip, mainX, mainY, mainW, mainH);
    if (mainW <= 0) {
        mainW = 180;
    }

    int setX = 0, setY = 0, setW = 0, setH = 0;
    const bool hasSet =
            SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0;

    if (hasSet) {
        outX = setX + setW + kGrowthTipGap;
        outY = setY;
    } else {
        outX = mainX + mainW + kGrowthTipGap;
        outY = mainY;
    }
    if (outY < 0) {
        outY = 0;
    }
    if (outX < 0) {
        outX = 0;
    }
}

static void DiagUi(const char* fmt, ...) {
    char line[448];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(line, _countof(line), _TRUNCATE, fmt, ap);
    va_end(ap);
    FILE* f = nullptr;
    if (fopen_s(&f, "equip_growth_tip.log", "a") == 0 && f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d UI %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line);
        fclose(f);
    }
}

static void ShowGrowthTooltipAt(CUIToolTip* mainTip, int itemId, const char* text) {
    if (!text || text[0] == '\0') {
        HideGrowthTooltipInternal("emptyText");
        return;
    }
    std::vector<GrowthTipLine> lines =
            BuildGrowthTipLayout(text, EquipGrowth::GetGrowthTipItemLevel(itemId));
    if (lines.empty()) {
        HideGrowthTooltipInternal("emptyLayout");
        DiagUi("show abort emptyLayout itemId=%d", itemId);
        return;
    }

    EnsureGrowthTipFonts();
    if (!g_growthTipFonts.ready || !g_growthTipFonts.white) {
        HideGrowthTooltipInternal("noFont");
        DiagUi("show abort noFont itemId=%d", itemId);
        return;
    }

    int mainW = 0, mainH = 0, mainX = 0, mainY = 0;
    SehReadMainOrigin(mainTip, mainX, mainY, mainW, mainH);

    int setX = 0, setY = 0, setW = 0, setH = 0;
    const int hasSet =
            SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0 ? 1 : 0;
    // Prefer set tip width so attribute indent / columns line up visually.
    const int preferredW = hasSet && setW > 40 ? setW : (mainW > 40 ? mainW : 0);
    const int width = ResolveGrowthTipWidth(lines, preferredW);
    const int targetH = ComputeGrowthTipContentHeight(lines);

    int dockX = 0, dockY = 0;
    ComputeGrowthDock(mainTip, width, targetH, dockX, dockY);

    CUIToolTip* tip = EnsureGrowthTooltip();
    try {
        tip->ClearToolTip();
        CreateGrowthTipLayerAtHeight(tip, dockX, dockY, width, targetH);
    } catch (...) {
        HideGrowthTooltipInternal("showException");
        DiagUi("show exception itemId=%d", itemId);
        return;
    }

    IWzCanvasPtr canvas = GetTooltipCanvas(tip);
    if (!canvas) {
        HideGrowthTooltipInternal("noCanvas");
        DiagUi("show abort noCanvas itemId=%d", itemId);
        return;
    }
    int drawW = SafeGetTipWidth(tip);
    if (drawW <= 0) {
        drawW = width;
    }
    try {
        const int cw = static_cast<int>(canvas->width);
        if (cw > 0) {
            drawW = cw;
        }
    } catch (...) {
    }
    RenderGrowthTooltipCanvas(canvas, lines, drawW);

    const int renderedW = SafeGetTipWidth(tip);
    const int renderedH = SafeGetTipHeight(tip);
    if (renderedW > 0) {
        ComputeGrowthDock(mainTip, renderedW, renderedH > 0 ? renderedH : targetH, dockX, dockY);
        SafeRelMoveTip(tip, dockX, dockY);
    }
    g_lastDockX = dockX;
    g_lastDockY = dockY;
    SehBoostLayerAboveSet(tip);
    g_lastShownItemId = itemId;

    int lrx = 0, lry = 0, lw = 0, lh = 0, lz = 0, lvis = 0;
    SehReadLayerState(tip, lrx, lry, lw, lh, lz, lvis);
    DiagUi("show OK VERSION GROWTH_TIP_SETSTYLE_20260831 itemId=%d dock=%d,%d wh=%d,%d "
           "lines=%zu set=%d,%d,%d,%d hasSet=%d layer=%d,%d,%d,%d z=%d vis=%d "
           "main=%d,%d,%d,%d dockChain=%s",
           itemId, dockX, dockY, renderedW, renderedH, lines.size(), setX, setY, setW, setH,
           hasSet, lrx, lry, lw, lh, lz, lvis, mainX, mainY, mainW, mainH,
           hasSet ? "main->set->growth" : "main->growth");
    EquipCompare::RelayoutActiveCompareTip();
}

static void ShowCmpGrowthTooltipAt(CUIToolTip* compareTip, int itemId, const char* text) {
    if (!compareTip || !text || text[0] == '\0') {
        HideCmpGrowthTooltipInternal();
        return;
    }
    std::vector<GrowthTipLine> lines =
            BuildGrowthTipLayout(text, EquipGrowth::GetGrowthTipItemLevel(itemId));
    if (lines.empty()) {
        HideCmpGrowthTooltipInternal();
        return;
    }
    EnsureGrowthTipFonts();
    if (!g_growthTipFonts.ready || !g_growthTipFonts.white) {
        HideCmpGrowthTooltipInternal();
        return;
    }

    int cx = 0, cy = 0, cw = 0, ch = 0;
    SehReadMainOrigin(compareTip, cx, cy, cw, ch);
    int setX = 0, setY = 0, setW = 0, setH = 0;
    const bool hasCmpSet =
            SetItem::TryGetActiveCompareSetTooltipRect(setX, setY, setW, setH) && setW > 0;
    const int preferredW = hasCmpSet && setW > 40 ? setW : (cw > 40 ? cw : 0);
    const int width = ResolveGrowthTipWidth(lines, preferredW);
    const int targetH = ComputeGrowthTipContentHeight(lines);

    int dockX = 0, dockY = 0;
    ComputeCmpGrowthDock(compareTip, width, dockX, dockY);

    CUIToolTip* tip = EnsureCmpGrowthTooltip();
    try {
        tip->ClearToolTip();
        CreateGrowthTipLayerAtHeight(tip, dockX, dockY, width, targetH);
    } catch (...) {
        HideCmpGrowthTooltipInternal();
        return;
    }
    IWzCanvasPtr canvas = GetTooltipCanvas(tip);
    if (!canvas) {
        HideCmpGrowthTooltipInternal();
        return;
    }
    int drawW = SafeGetTipWidth(tip);
    if (drawW <= 0) {
        drawW = width;
    }
    try {
        const int cwCanvas = static_cast<int>(canvas->width);
        if (cwCanvas > 0) {
            drawW = cwCanvas;
        }
    } catch (...) {
    }
    RenderGrowthTooltipCanvas(canvas, lines, drawW);

    const int renderedW = SafeGetTipWidth(tip);
    const int renderedH = SafeGetTipHeight(tip);
    if (renderedW > 0) {
        ComputeCmpGrowthDock(compareTip, renderedW, dockX, dockY);
        SafeRelMoveTip(tip, dockX, dockY);
    }
    g_cmpLastDockX = dockX;
    g_cmpLastDockY = dockY;
    SehBoostLayerAboveSet(tip);
    g_cmpLastShownItemId = itemId;
    (void)renderedH;
}

static void UpdateGrowthTip(CUIToolTip* mainTip, int itemId, void* pe, bool prepareLocal) {
    // Companion tip re-enabled 2026-08-03 (was hard-off for enter-crash A/B).
    // Still only SetItem Show/AfterEquipTipDrawn — no self Detour.
    if (!Client::enableGrowthCompanionTip || g_inGrowthUpdate || !mainTip) {
        return;
    }
    if (!IsEquipItemId(itemId)) {
        HideGrowthTooltipInternal("nonEquip");
        return;
    }

    // Setlike: only ignore other tip instances (compare). pe=authoritative hover switches item.
    if (g_activeMainTip && mainTip != g_activeMainTip) {
        DiagUi("skip otherTip itemId=%d locked=%d", itemId, g_lastHoverItemId);
        return;
    }
    if (g_lastHoverItemId > 0 && itemId != g_lastHoverItemId) {
        if (pe) {
            HideGrowthTooltipInternal("switchItem");
            g_lastHoverItemId = itemId;
        } else {
            DiagUi("skip otherItem itemId=%d locked=%d pe=0", itemId, g_lastHoverItemId);
            return;
        }
    }

    g_inGrowthUpdate = true;
    g_activeMainTip = mainTip;
    g_lastHoverItemId = itemId;

    try {
        if (prepareLocal) {
            if (pe) {
                EquipGrowth::OnHoverEquip(itemId, SafeGetEnhance(pe), SafeGetItemLevel(pe),
                                          SafeGetScrollLevel(pe));
            } else {
                // Id-only path: do not spam 0x17D; paint cache if any.
                EquipGrowth::RequestGrowthTip(itemId);
            }
        }

        if (itemId != g_lastHoverItemId) {
            g_inGrowthUpdate = false;
            return;
        }

        if (EquipGrowth::IsGrowthTipResolved(itemId) && !EquipGrowth::HasGrowthTip(itemId)) {
            HideGrowthTooltipInternal("resolvedEmpty");
            g_inGrowthUpdate = false;
            return;
        }

        const char* text = EquipGrowth::GetGrowthTipText(itemId);
        if (!text || text[0] == '\0') {
            DiagUi("no text itemId=%d resolved=%d has=%d pe=%d lastShown=%d",
                   itemId,
                   EquipGrowth::IsGrowthTipResolved(itemId) ? 1 : 0,
                   EquipGrowth::HasGrowthTip(itemId) ? 1 : 0,
                   pe ? 1 : 0,
                   g_lastShownItemId);
            if (g_lastShownItemId != itemId) {
                HideGrowthTooltipInternal("noText");
            }
            g_inGrowthUpdate = false;
            return;
        }

        if (itemId == g_lastShownItemId && SehHasTipLayer()) {
            CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf);
            int tipW = SafeGetTipWidth(tip);
            int tipH = SafeGetTipHeight(tip);
            int x = 0, y = 0;
            ComputeGrowthDock(mainTip, tipW, tipH, x, y);
            SafeRelMoveTip(tip, x, y);
            g_lastDockX = x;
            g_lastDockY = y;
            SehBoostLayerAboveSet(tip);
            int lrx = 0, lry = 0, lw = 0, lh = 0, lz = 0, lvis = 0;
            SehReadLayerState(tip, lrx, lry, lw, lh, lz, lvis);
            int setX = 0, setY = 0, setW = 0, setH = 0;
            const int hasSet =
                    SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0 ? 1
                                                                                           : 0;
            DiagUi("follow RelMove VERSION GROWTH_TIP_REEN_20260803 itemId=%d dock=%d,%d "
                   "layer=%d,%d,%d,%d z=%d vis=%d hasSet=%d set=%d,%d,%d,%d dockChain=%s",
                   itemId, x, y, lrx, lry, lw, lh, lz, lvis, hasSet, setX, setY, setW, setH,
                   hasSet ? "main->set->growth" : "main->growth");
            EquipCompare::RelayoutActiveCompareTip();
            g_inGrowthUpdate = false;
            return;
        }

        ShowGrowthTooltipAt(mainTip, itemId, text);
    } catch (...) {
        HideGrowthTooltipInternal("updateException");
    }
    g_inGrowthUpdate = false;
}

void HideImpl() {
    const int prevItemId = g_lastHoverItemId;
    HideGrowthTooltipInternal("mainClear");
    g_activeMainTip = nullptr;
    g_lastHoverItemId = 0;
    // 允许下一轮悬停重新拉取「此前无成长」的装备（服务端条件放宽 / 升级后）
    EquipGrowth::InvalidateEmptyCache(prevItemId);
}

void OnEquipTipDrawnImpl(CUIToolTip* tip, int itemId, void* pe) {
    if (!Client::enableGrowthCompanionTip || !tip) {
        return;
    }
    if (!IsEquipItemId(itemId)) {
        return;
    }
    DiagUi("drawn itemId=%d VERSION GROWTH_TIP_REEN_20260803 pe=%d", itemId, pe ? 1 : 0);
    UpdateGrowthTip(tip, itemId, pe, true);
}

void RedrawActiveImpl() {
    if (!Client::enableGrowthCompanionTip || !g_activeMainTip || g_lastHoverItemId <= 0) {
        return;
    }
    // Follow set tip RelMove each tick — do not force Clear/recreate.
    UpdateGrowthTip(g_activeMainTip, g_lastHoverItemId, nullptr, false);
}

void OnCacheUpdatedImpl(int itemId) {
    if (!Client::enableGrowthCompanionTip) {
        return;
    }
    if (itemId == g_lastHoverItemId && g_activeMainTip) {
        g_lastShownItemId = 0; // force text refresh
        UpdateGrowthTip(g_activeMainTip, g_lastHoverItemId, nullptr, false);
    }
    // Compare-side growth may share the same itemId cache.
    if (itemId == g_cmpLastShownItemId && g_cmpLastShownItemId > 0) {
        // Force recreate on next Relayout/Update from compare tip.
        g_cmpLastShownItemId = 0;
    }
}

bool TryGetRectImpl(int& outX, int& outY, int& outW, int& outH) {
    return SehReadTipRect(outX, outY, outW, outH);
}
} // namespace

void EquipGrowth_Hide() {
    HideImpl();
}

void EquipGrowth_OnEquipTipDrawn(CUIToolTip* tip, GW_ItemSlotEquip* pe) {
    OnEquipTipDrawnImpl(tip, SafeGetItemId(pe), pe);
}

void EquipGrowth_OnEquipTipDrawnId(CUIToolTip* tip, int itemId) {
    OnEquipTipDrawnImpl(tip, itemId, nullptr);
}

void EquipGrowth_RedrawActive() {
    RedrawActiveImpl();
}

void EquipGrowth_OnCacheUpdated(int itemId) {
    OnCacheUpdatedImpl(itemId);
}

bool EquipGrowth::TryGetActiveGrowthTooltipRect(int& outX, int& outY, int& outW, int& outH) {
    return TryGetRectImpl(outX, outY, outW, outH);
}

bool EquipGrowth::TryGetActiveCompareGrowthTooltipRect(int& outX, int& outY, int& outW,
        int& outH) {
    return SehReadCmpTipRect(outX, outY, outW, outH);
}

void EquipGrowth::HideCompareCompanion() {
    HideCmpGrowthTooltipInternal();
}

void EquipGrowth::RelayoutCompareCompanion(CUIToolTip* compareTip) {
    if (!Client::enableGrowthCompanionTip || !compareTip || g_cmpLastShownItemId <= 0
            || !SehHasCmpTipLayer()) {
        return;
    }
    CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_cmpGrowthTooltipBuf);
    int tipW = SafeGetTipWidth(tip);
    int x = 0, y = 0;
    ComputeCmpGrowthDock(compareTip, tipW > 0 ? tipW : 180, x, y);
    SafeRelMoveTip(tip, x, y);
    g_cmpLastDockX = x;
    g_cmpLastDockY = y;
    SehBoostLayerAboveSet(tip);
}

void EquipGrowth::UpdateCompareCompanion(CUIToolTip* compareTip, int itemId, void* pe) {
    if (!Client::enableGrowthCompanionTip || !compareTip || !IsEquipItemId(itemId)) {
        HideCmpGrowthTooltipInternal();
        return;
    }
    if (pe) {
        EquipGrowth::OnHoverEquip(itemId, SafeGetEnhance(pe), SafeGetItemLevel(pe),
                                  SafeGetScrollLevel(pe));
    } else {
        EquipGrowth::RequestGrowthTip(itemId);
    }
    if (EquipGrowth::IsGrowthTipResolved(itemId) && !EquipGrowth::HasGrowthTip(itemId)) {
        HideCmpGrowthTooltipInternal();
        return;
    }
    const char* text = EquipGrowth::GetGrowthTipText(itemId);
    if (!text || text[0] == '\0') {
        if (g_cmpLastShownItemId != itemId) {
            HideCmpGrowthTooltipInternal();
        }
        return;
    }
    if (itemId == g_cmpLastShownItemId && SehHasCmpTipLayer()) {
        RelayoutCompareCompanion(compareTip);
        return;
    }
    ShowCmpGrowthTooltipAt(compareTip, itemId, text);
}
