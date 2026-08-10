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
// Match SetItem::ComputeSetTooltipRightOfMain — only clamp X to screen, keep Y.
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
    const int screenW = get_screen_width();
    if (screenW > 0 && outX + tipW > screenW) {
        outX = (std::max)(0, screenW - tipW);
    }
    if (outX < 0) {
        outX = 0;
    }
    // If screen clamp pulled us over the set tip, sit just under set instead of left-dock.
    if (hasSet && RectsOverlap(outX, outY, tipW, tipH > 0 ? tipH : 80, setX, setY, setW,
                               setH > 0 ? setH : 120)) {
        outX = setX + setW + kGrowthTipGap;
        outY = setY + (setH > 0 ? setH : 120) + kGrowthTipGap;
        if (outY < 0) {
            outY = 0;
        }
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
    std::string title;
    std::string body;
    SplitTitleBody(text, title, body);
    if (title.empty()) {
        HideGrowthTooltipInternal("emptyTitle");
        DiagUi("show abort emptyTitle itemId=%d", itemId);
        return;
    }
    // Segmented "N级效果" bodies are longer than Hyper summary — allow up to tip buf.
    if (body.size() > 2400) {
        body.resize(2400);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
            body.pop_back();
        }
    }

    int mainW = 0, mainH = 0, mainX = 0, mainY = 0;
    SehReadMainOrigin(mainTip, mainX, mainY, mainW, mainH);
    const int preferredW = mainW > 40 ? mainW : 180;

    int setX = 0, setY = 0, setW = 0, setH = 0;
    const int hasSet =
            SetItem::TryGetActiveSetTooltipRect(setX, setY, setW, setH) && setW > 0 ? 1 : 0;

    int dockX = 0, dockY = 0;
    ComputeGrowthDock(mainTip, preferredW, 80, dockX, dockY);

    CUIToolTip* tip = EnsureGrowthTooltip();
    try {
        tip->ClearToolTip();
        ZXString<char> zTitle(title.c_str());
        ZXString<char> zBody(body.c_str());
        tip->SetToolTip_String2(dockX, dockY, zTitle, zBody, 0, 0, 0, preferredW, 1, 0);
    } catch (...) {
        HideGrowthTooltipInternal("showException");
        DiagUi("show exception itemId=%d", itemId);
        return;
    }

    const int renderedW = SafeGetTipWidth(tip);
    const int renderedH = SafeGetTipHeight(tip);
    if (renderedW > 0) {
        ComputeGrowthDock(mainTip, renderedW, renderedH > 0 ? renderedH : 80, dockX, dockY);
        SafeRelMoveTip(tip, dockX, dockY);
    }
    SehBoostLayerAboveSet(tip);
    g_lastShownItemId = itemId;

    int lrx = 0, lry = 0, lw = 0, lh = 0, lz = 0, lvis = 0;
    SehReadLayerState(tip, lrx, lry, lw, lh, lz, lvis);
    DiagUi("show OK VERSION GROWTH_TIP_REEN_20260803 itemId=%d dock=%d,%d wh=%d,%d "
           "titleLen=%zu set=%d,%d,%d,%d hasSet=%d layer=%d,%d,%d,%d z=%d vis=%d "
           "main=%d,%d,%d,%d dockChain=%s",
           itemId, dockX, dockY, renderedW, renderedH, title.size(), setX, setY, setW, setH,
           hasSet, lrx, lry, lw, lh, lz, lvis, mainX, mainY, mainW, mainH,
           hasSet ? "main->set->growth" : "main->growth");
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
