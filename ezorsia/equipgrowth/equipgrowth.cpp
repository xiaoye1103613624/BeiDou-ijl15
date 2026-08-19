#include "stdafx.h"
#include "EquipGrowthApi.h"
#include "../Client.h"
#include "../setitem/SetItemApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "compat/ClientAddresses.h"
#include "compat/wvs/secure.h"
#include "compat/wvs/tooltip.h"
#include "compat/wvs/util.h"
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

alignas(8) char g_growthTooltipBuf[kGrowthTooltipBufSize];
bool g_growthTooltipInited = false;
bool g_inGrowthUpdate = false;
CUIToolTip* g_activeMainTip = nullptr;
int g_lastHoverItemId = 0;
int g_lastShownItemId = 0;

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
        if (tip && tip->m_pLayer) {
            tip->m_pLayer->RelMove(x, y);
        }
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
        outX = tip->m_pLayer->rx;
        outY = tip->m_pLayer->ry;
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
    __try {
        mainW = mainTip->m_nWidth;
        mainH = mainTip->m_nHeight;
        if (mainTip->m_pLayer) {
            mainX = mainTip->m_pLayer->rx;
            mainY = mainTip->m_pLayer->ry;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mainX = mainY = mainW = mainH = 0;
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

static void SplitTitleBody(const std::string& text, std::string& title, std::string& body) {
    title.clear();
    body.clear();
    if (text.empty()) {
        return;
    }
    size_t pos = 0;
    while (pos < text.size() && text[pos] != '\r' && text[pos] != '\n') {
        ++pos;
    }
    title.assign(text, 0, pos);
    while (pos < text.size() && (text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }
    body.assign(text, pos, std::string::npos);
    for (char& c : body) {
        if (c == '\r') {
            c = '\n';
        }
    }
}

static bool RectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Right-chain adsorb like set tip: [equip] → [set?] → [growth] → [compare]
// Match SetItem::ComputeSetTooltipRightOfMain — only clamp X to screen, keep Y = mainY.
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

    // Prefer live main tip Y always (set tip may lag one frame).
    outY = mainY;
    if (hasSet) {
        outX = setX + setW + kGrowthTipGap;
        // If set tip Y is valid, keep vertical align with the chain.
        if (setY != 0 || setH > 0) {
            outY = setY;
        }
    } else {
        outX = mainX + mainW + kGrowthTipGap;
    }
    if (outY < 0) {
        outY = 0;
    }
    const int screenW = get_screen_width();
    if (screenW > 0 && outX + tipW > screenW) {
        // Prefer sit under main rather than pin at screen-right forever.
        outX = mainX;
        outY = mainY + (mainH > 0 ? mainH : 120) + kGrowthTipGap;
        if (outX + tipW > screenW) {
            outX = (std::max)(0, screenW - tipW);
        }
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

static constexpr int kGrowthPadX = 8;
static constexpr int kGrowthPadY = 6;
static constexpr int kGrowthLineH = 16;
static constexpr int kGrowthTitleExtraH = 2;
static constexpr int kGrowthSepGapY = 3;
static constexpr int kGrowthTierGapY = 3;
static constexpr int kGrowthIndentChars = 1;
static constexpr unsigned int kGrowthSepColor = 0x80FFFFFF;
static constexpr char kGrowthColonSep[] = "\x20\x3A\x20"; // " : "
static constexpr unsigned long kGrowthColLime = 0xFFCCFF00;
static constexpr unsigned long kGrowthColWhite = 0xFFFFFFFF;
static constexpr unsigned long kGrowthColGrey = 0xFFBBBBBB;
static constexpr unsigned long kGrowthColOrange = 0xFFFFCC00;

typedef void(__cdecl* GrowthGetBasicFont_t)(IWzFontPtr*, int);
typedef HRESULT(__thiscall* GrowthWzFontCreate_t)(
        IWzFont*, Ztl_bstr_t, unsigned long, unsigned long, const Ztl_variant_t&);
static auto GrowthGetBasicFont = reinterpret_cast<GrowthGetBasicFont_t>(0x0098A707);
static auto GrowthWzFontCreate = reinterpret_cast<GrowthWzFontCreate_t>(0x0046341A);

enum class GrowthStyle { Title, ActiveHeader, White, Grey, Orange };

struct GrowthTipFonts {
    IWzFontPtr title;
    IWzFontPtr header;
    IWzFontPtr white;
    IWzFontPtr grey;
    IWzFontPtr orange;
    bool ready = false;
};
static GrowthTipFonts g_growthFonts;
static int g_hoverItemLevel = 0;

static Ztl_bstr_t GrowthGbkToBstr(const char* sGbk) {
    wchar_t wbuf[256] = {};
    if (!sGbk || MultiByteToWideChar(CP_ACP, 0, sGbk, -1, wbuf, _countof(wbuf)) <= 0) {
        return Ztl_bstr_t(L"");
    }
    return Ztl_bstr_t(wbuf);
}

static bool GrowthCreateFont(IWzFontPtr& out, unsigned long color, int size, bool bold) {
    if (out) {
        return true;
    }
    try {
        PcCreateObject<IWzFontPtr>(L"Canvas#Font", out, nullptr);
        if (!out) {
            return false;
        }
        const Ztl_variant_t style(bold ? L"B" : L"");
        return SUCCEEDED(GrowthWzFontCreate(out, L"Dotum", size, color, style));
    } catch (...) {
        out = nullptr;
        return false;
    }
}

static void EnsureGrowthFonts() {
    if (g_growthFonts.ready) {
        return;
    }
    GrowthCreateFont(g_growthFonts.title, kGrowthColLime, 12, true);
    GrowthCreateFont(g_growthFonts.header, kGrowthColLime, 12, true);
    GrowthCreateFont(g_growthFonts.white, kGrowthColWhite, 12, false);
    GrowthCreateFont(g_growthFonts.grey, kGrowthColGrey, 12, false);
    GrowthCreateFont(g_growthFonts.orange, kGrowthColOrange, 12, false);
    if (!g_growthFonts.white) {
        try {
            GrowthGetBasicFont(std::addressof(g_growthFonts.white), 0);
            g_growthFonts.header = g_growthFonts.white;
            g_growthFonts.title = g_growthFonts.white;
            g_growthFonts.grey = g_growthFonts.white;
            g_growthFonts.orange = g_growthFonts.white;
        } catch (...) {
        }
    }
    if (!g_growthFonts.orange) {
        g_growthFonts.orange = g_growthFonts.white;
    }
    g_growthFonts.ready = g_growthFonts.white != nullptr;
}

static IWzFontPtr GetGrowthFont(GrowthStyle style) {
    switch (style) {
    case GrowthStyle::Title:
        return g_growthFonts.title ? g_growthFonts.title : g_growthFonts.white;
    case GrowthStyle::ActiveHeader:
        return g_growthFonts.header ? g_growthFonts.header : g_growthFonts.white;
    case GrowthStyle::Grey:
        return g_growthFonts.grey ? g_growthFonts.grey : g_growthFonts.white;
    case GrowthStyle::Orange:
        return g_growthFonts.orange ? g_growthFonts.orange : g_growthFonts.white;
    case GrowthStyle::White:
    default:
        return g_growthFonts.white;
    }
}

enum class GrowthLineKind { Title, TierHeader, Stat, Other };

struct GrowthSeg {
    std::string text;
    GrowthStyle style = GrowthStyle::White;
};

struct GrowthLine {
    std::vector<GrowthSeg> segments;
    GrowthLineKind kind = GrowthLineKind::Other;
    bool separatorBefore = false;
    bool indent = false;
    bool center = false;
    bool tierGapAfter = false;
};

static bool IsGrowthTierHeader(const std::string& line, int& outNode) {
    outNode = 0;
    if (line.find("\xBC\xB6\xD0\xA7\xB9\xFB") == std::string::npos) {
        return false;
    }
    size_t p = 0;
    while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
        outNode = outNode * 10 + (line[p] - '0');
        ++p;
    }
    return outNode > 0;
}

// Attack / magic / combat% labels — orange when tier active (match set tip).
static bool IsImportantGrowthLabel(const std::string& label) {
    if (label.find("\xB9\xA5\xBB\xF7\xC1\xA6") != std::string::npos) { // 攻击力
        return true;
    }
    if (label.find("\xC4\xA7\xC1\xA6") != std::string::npos) { // 魔法力
        return true;
    }
    if (label.find("\xC9\xCB\xBA\xA6") != std::string::npos) { // 伤害
        return true;
    }
    if (label.find("\xCE\xDE\xCA\xD3") != std::string::npos) { // 无视
        return true;
    }
    return false;
}

static void SplitStatLabelValue(const std::string& line, std::string& label, std::string& value) {
    label.clear();
    value.clear();
    // Prefer " : " / ":" then fall back to " +"
    size_t colon = line.find(" : ");
    if (colon != std::string::npos) {
        label = line.substr(0, colon);
        value = line.substr(colon + 3);
        return;
    }
    colon = line.find(':');
    if (colon != std::string::npos) {
        label = line.substr(0, colon);
        while (!label.empty() && (label.back() == ' ' || label.back() == '\t')) {
            label.pop_back();
        }
        size_t v = colon + 1;
        while (v < line.size() && (line[v] == ' ' || line[v] == '\t')) {
            ++v;
        }
        value = line.substr(v);
        return;
    }
    size_t plus = line.find(" +");
    if (plus != std::string::npos) {
        label = line.substr(0, plus);
        value = line.substr(plus + 1); // keep leading '+'
        return;
    }
    plus = line.find('+');
    if (plus != std::string::npos && plus > 0) {
        label = line.substr(0, plus);
        while (!label.empty() && (label.back() == ' ' || label.back() == '\t')) {
            label.pop_back();
        }
        value = line.substr(plus);
        return;
    }
    label = line;
}

static void BuildGrowthLayout(const std::string& title, const std::string& body, int itemLevel,
        std::vector<GrowthLine>& out) {
    out.clear();
    if (!title.empty()) {
        GrowthLine t;
        t.kind = GrowthLineKind::Title;
        t.center = true;
        t.segments.push_back({title, GrowthStyle::Title});
        out.push_back(std::move(t));
    }

    bool tierActive = true;
    bool sawTier = false;
    size_t i = 0;
    while (i < body.size()) {
        size_t j = i;
        while (j < body.size() && body[j] != '\n' && body[j] != '\r') {
            ++j;
        }
        std::string raw = body.substr(i, j - i);
        while (j < body.size() && (body[j] == '\n' || body[j] == '\r')) {
            ++j;
        }
        i = j;
        if (raw.empty()) {
            continue;
        }

        int node = 0;
        if (IsGrowthTierHeader(raw, node)) {
            if (sawTier && !out.empty()) {
                out.back().tierGapAfter = true;
            }
            sawTier = true;
            // Achieved nodes are those with node < itemLevel (same as WZ gainLevel).
            tierActive = itemLevel > 0 && node < itemLevel;
            GrowthLine header;
            header.kind = GrowthLineKind::TierHeader;
            header.separatorBefore = true;
            header.segments.push_back(
                    {raw, tierActive ? GrowthStyle::ActiveHeader : GrowthStyle::Grey});
            out.push_back(std::move(header));
            continue;
        }

        std::string label;
        std::string value;
        SplitStatLabelValue(raw, label, value);

        GrowthLine stat;
        stat.kind = GrowthLineKind::Stat;
        stat.indent = true;
        if (!tierActive) {
            if (!label.empty()) {
                std::string left = label;
                left += kGrowthColonSep;
                stat.segments.push_back({left, GrowthStyle::Grey});
            }
            if (!value.empty()) {
                stat.segments.push_back({value, GrowthStyle::Grey});
            } else if (stat.segments.empty()) {
                stat.segments.push_back({raw, GrowthStyle::Grey});
            }
        } else {
            const GrowthStyle labelStyle =
                    IsImportantGrowthLabel(label) ? GrowthStyle::Orange : GrowthStyle::White;
            if (!label.empty()) {
                std::string left = label;
                left += kGrowthColonSep;
                stat.segments.push_back({left, labelStyle});
            }
            if (!value.empty()) {
                stat.segments.push_back({value, GrowthStyle::White});
            } else if (stat.segments.empty()) {
                stat.segments.push_back({raw, GrowthStyle::White});
            }
        }
        out.push_back(std::move(stat));
    }
    if (sawTier && !out.empty()) {
        out.back().tierGapAfter = true;
    }
}

static int GrowthContentHeight(const std::vector<GrowthLine>& lines) {
    int y = kGrowthPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const GrowthLine& line = lines[i];
        if (line.separatorBefore) {
            y += kGrowthSepGapY;
        }
        y += kGrowthLineH;
        if (i == 0 && line.kind == GrowthLineKind::Title) {
            y += kGrowthTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kGrowthTierGapY;
        }
    }
    return y + kGrowthPadY;
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

static IWzCanvasPtr GrowthGetCanvas(CUIToolTip* tip) {
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

static void GrowthDrawText(IWzCanvasPtr canvas, int x, int y, const char* text, IWzFontPtr font) {
    if (!canvas || !text || !text[0] || !font) {
        return;
    }
    try {
        canvas->DrawTextA(x, y, GrowthGbkToBstr(text), font, Ztl_variant_t(), Ztl_variant_t());
    } catch (...) {
    }
}

static int GrowthMeasure(IWzFontPtr font, const char* text) {
    if (!font || !text) {
        return 0;
    }
    try {
        return static_cast<int>(font->CalcTextWidth(GrowthGbkToBstr(text), Ztl_variant_t()));
    } catch (...) {
        return 0;
    }
}

static void DrawGrowthSeparator(IWzCanvasPtr canvas, int width, int y) {
    if (!canvas || width <= 2 * kGrowthPadX + 1) {
        return;
    }
    try {
        canvas->DrawRectangle(kGrowthPadX, y, width - 2 * kGrowthPadX, 1, kGrowthSepColor);
    } catch (...) {
    }
}

static int GrowthMeasureLineWidth(const GrowthLine& line) {
    int w = 0;
    if (line.indent) {
        w += GrowthMeasure(GetGrowthFont(GrowthStyle::White), "\xA1\xA1") * kGrowthIndentChars;
    }
    for (const GrowthSeg& seg : line.segments) {
        w += GrowthMeasure(GetGrowthFont(seg.style), seg.text.c_str());
    }
    return w;
}

static void DrawGrowthLine(IWzCanvasPtr canvas, int width, int y, const GrowthLine& line) {
    int x = kGrowthPadX;
    if (line.center && !line.segments.empty()) {
        const int tw = GrowthMeasureLineWidth(line);
        x = (std::max)(kGrowthPadX, (width - tw) / 2);
    } else if (line.indent) {
        x = kGrowthPadX
                + GrowthMeasure(GetGrowthFont(GrowthStyle::White), "\xA1\xA1") * kGrowthIndentChars;
    }
    for (const GrowthSeg& seg : line.segments) {
        if (seg.text.empty()) {
            continue;
        }
        IWzFontPtr font = GetGrowthFont(seg.style);
        GrowthDrawText(canvas, x, y, seg.text.c_str(), font);
        x += GrowthMeasure(font, seg.text.c_str());
    }
}

static void RenderGrowthCanvas(IWzCanvasPtr canvas, const std::vector<GrowthLine>& lines, int width) {
    if (!canvas || lines.empty() || !g_growthFonts.ready) {
        return;
    }
    int y = kGrowthPadY;
    for (size_t i = 0; i < lines.size(); ++i) {
        const GrowthLine& line = lines[i];
        if (line.separatorBefore) {
            DrawGrowthSeparator(canvas, width, y - 2);
            y += kGrowthSepGapY;
        }
        DrawGrowthLine(canvas, width, y, line);
        y += kGrowthLineH;
        if (i == 0 && line.kind == GrowthLineKind::Title) {
            y += kGrowthTitleExtraH;
        }
        if (line.tierGapAfter) {
            y += kGrowthTierGapY;
        }
    }
}

static void ShowGrowthTooltipAt(CUIToolTip* mainTip, int itemId, const char* text, int itemLevel) {
    if (!text || text[0] == '\0') {
        HideGrowthTooltipInternal("emptyText");
        return;
    }
    std::string title;
    std::string body;
    SplitTitleBody(text, title, body);
    if (title.empty()) {
        HideGrowthTooltipInternal("emptyTitle");
        DiagUi("show abort emptyTitle itemId=%d", itemId);
        return;
    }
    if (body.size() > 2400) {
        body.resize(2400);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
            body.pop_back();
        }
    }

    EnsureGrowthFonts();
    if (!g_growthFonts.orange) {
        GrowthCreateFont(g_growthFonts.orange, kGrowthColOrange, 12, false);
    }
    std::vector<GrowthLine> lines;
    BuildGrowthLayout(title, body, itemLevel, lines);
    if (lines.empty()) {
        HideGrowthTooltipInternal("emptyLines");
        return;
    }

    int mainW = 0, mainH = 0, mainX = 0, mainY = 0;
    SehReadMainOrigin(mainTip, mainX, mainY, mainW, mainH);
    const int preferredW = mainW > 40 ? mainW : 180;

    int setX = 0, setY = 0, setW = 0, setH = 0;
    const int hasSet =
            SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0 ? 1 : 0;

    const int targetH = GrowthContentHeight(lines);
    int dockX = 0, dockY = 0;
    ComputeGrowthDock(mainTip, preferredW, targetH, dockX, dockY);

    CUIToolTip* tip = EnsureGrowthTooltip();
    try {
        CreateGrowthTipLayerAtHeight(tip, dockX, dockY, preferredW, targetH);
        IWzCanvasPtr canvas = GrowthGetCanvas(tip);
        if (canvas && g_growthFonts.ready) {
            int drawW = preferredW;
            try {
                const int cw = static_cast<int>(canvas->width);
                if (cw > 0) {
                    drawW = cw;
                }
            } catch (...) {
            }
            RenderGrowthCanvas(canvas, lines, drawW);
        } else {
            // Fallback: vanilla String2 if fonts unavailable.
            tip->ClearToolTip();
            ZXString<char> zTitle(title.c_str());
            ZXString<char> zBody(body.c_str());
            tip->SetToolTip_String2(dockX, dockY, zTitle, zBody, 0, 0, 0, preferredW, 1, 0);
        }
    } catch (...) {
        HideGrowthTooltipInternal("showException");
        DiagUi("show exception itemId=%d", itemId);
        return;
    }

    const int renderedW = SafeGetTipWidth(tip);
    const int renderedH = SafeGetTipHeight(tip);
    if (renderedW > 0) {
        ComputeGrowthDock(mainTip, renderedW, renderedH > 0 ? renderedH : targetH, dockX, dockY);
        SafeRelMoveTip(tip, dockX, dockY);
    }
    SehBoostLayerAboveSet(tip);
    g_lastShownItemId = itemId;

    int lrx = 0, lry = 0, lw = 0, lh = 0, lz = 0, lvis = 0;
    SehReadLayerState(tip, lrx, lry, lw, lh, lz, lvis);
    DiagUi("show OK VERSION GROWTH_TIP_SETSTYLE_20260817b itemId=%d lv=%d dock=%d,%d wh=%d,%d "
           "titleLen=%zu set=%d,%d,%d,%d hasSet=%d layer=%d,%d,%d,%d z=%d vis=%d "
           "main=%d,%d,%d,%d dockChain=%s fonts=%d",
           itemId, itemLevel, dockX, dockY, renderedW, renderedH, title.size(), setX, setY, setW,
           setH, hasSet, lrx, lry, lw, lh, lz, lvis, mainX, mainY, mainW, mainH,
           hasSet ? "main->set->growth" : "main->growth", g_growthFonts.ready ? 1 : 0);
    EquipCompare::RelayoutActiveCompareTip();
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

    // pe=authoritative hover: allow rebinding when the main tip instance changes
    // (inventory → character equip after use/dblclick left g_activeMainTip stale).
    if (g_activeMainTip && mainTip != g_activeMainTip) {
        if (pe) {
            HideGrowthTooltipInternal("switchTip");
            g_activeMainTip = mainTip;
        } else {
            DiagUi("skip otherTip itemId=%d locked=%d", itemId, g_lastHoverItemId);
            return;
        }
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
                g_hoverItemLevel = SafeGetItemLevel(pe);
                EquipGrowth::OnHoverEquip(itemId, SafeGetEnhance(pe), g_hoverItemLevel,
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

        int itemLevel = EquipGrowth::GetCachedItemLevel(itemId);
        if (itemLevel <= 0) {
            itemLevel = g_hoverItemLevel;
        }
        if (itemLevel <= 0) {
            itemLevel = 1;
        }

        if (itemId == g_lastShownItemId && SehHasTipLayer()) {
            CUIToolTip* tip = reinterpret_cast<CUIToolTip*>(g_growthTooltipBuf);
            int tipW = SafeGetTipWidth(tip);
            int tipH = SafeGetTipHeight(tip);
            int x = 0, y = 0;
            ComputeGrowthDock(mainTip, tipW, tipH, x, y);
            SafeRelMoveTip(tip, x, y);
            SehBoostLayerAboveSet(tip);
            int lrx = 0, lry = 0, lw = 0, lh = 0, lz = 0, lvis = 0;
            SehReadLayerState(tip, lrx, lry, lw, lh, lz, lvis);
            int setX = 0, setY = 0, setW = 0, setH = 0;
            const int hasSet =
                    SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0 ? 1
                                                                                           : 0;
            DiagUi("follow RelMove VERSION GROWTH_TIP_SETSTYLE_20260817b itemId=%d lv=%d dock=%d,%d "
                   "layer=%d,%d,%d,%d z=%d vis=%d hasSet=%d set=%d,%d,%d,%d dockChain=%s",
                   itemId, itemLevel, x, y, lrx, lry, lw, lh, lz, lvis, hasSet, setX, setY, setW,
                   setH, hasSet ? "main->set->growth" : "main->growth");
            EquipCompare::RelayoutActiveCompareTip();
            g_inGrowthUpdate = false;
            return;
        }

        ShowGrowthTooltipAt(mainTip, itemId, text, itemLevel);
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
    g_hoverItemLevel = 0;
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
}

bool TryGetRectImpl(int& outX, int& outY, int& outW, int& outH) {
    return SehReadTipRect(outX, outY, outW, outH);
}
} // namespace

void EquipGrowth_Hide() {
    HideImpl();
}

void EquipGrowth_OnMainTipClearing(CUIToolTip* tip) {
    if (!tip) {
        return;
    }
    if (tip == g_activeMainTip) {
        HideImpl();
    }
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
