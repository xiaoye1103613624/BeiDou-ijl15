// EquipDye.cpp — 共享 Character.wz 画布 HSL 染色（无 SEH 与 C++ 对象混用）

#include "stdafx.h"
#include "EquipDye.h"
#include "compat/hook.h"
#include "compat/wvs/util.h"
#include "compat/WzLib/IWzCanvas.h"
#include "compat/WzLib/IWzProperty.h"
#include "compat/WzLib/IWzResMan.h"
#include "ztl/ztl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr uintptr_t kAddr_LoadItemAction = 0x00410848; // IDA: CActionMan item-action load

thread_local int g_loadingItemId = 0;
bool g_allowFieldLoadDye = false;
bool g_hooksAttached = false;

std::mutex g_mu;
std::map<int, DyeHsl> g_ownedHsl;
std::map<int, std::map<int, DyeHsl>> g_charItemHsl;

struct CanvasBackup {
    int width;
    int height;
    int pitch;
    int fmt; // CANVAS_PIXFORMAT
    std::vector<unsigned char> bytes;
};
std::map<uintptr_t, CanvasBackup> g_canvasBackups;
std::map<int, std::unordered_set<uintptr_t>> g_itemTrackedCanvases;

DWORD g_deferredApplyAt = 0;
bool g_deferredApplyPending = false;
std::unordered_set<int> g_foreignRefreshChars;

using t_LoadItemAction = int(__cdecl*)(int nItemID, int nAction, int bFlip, IUnknown** ppResult);
static auto Orig_LoadItemAction = reinterpret_cast<t_LoadItemAction>(kAddr_LoadItemAction);

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void RgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxc = (std::max)(r, (std::max)(g, b));
    float minc = (std::min)(r, (std::min)(g, b));
    l = (maxc + minc) * 0.5f;
    if (maxc == minc) { h = 0.f; s = 0.f; return; }
    float d = maxc - minc;
    s = l > 0.5f ? d / (2.f - maxc - minc) : d / (maxc + minc);
    if (maxc == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (maxc == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h *= 60.f;
}

static float Hue2Rgb(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

static void HslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    h = fmodf(h, 360.f);
    if (h < 0.f) h += 360.f;
    float hn = h / 360.f;
    if (s <= 0.f) { r = g = b = l; return; }
    float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    float p = 2.f * l - q;
    r = Hue2Rgb(p, q, hn + 1.f / 3.f);
    g = Hue2Rgb(p, q, hn);
    b = Hue2Rgb(p, q, hn - 1.f / 3.f);
}

static unsigned ShiftPixel8888(unsigned px, const DyeHsl& hsl) {
    unsigned a = (px >> 24) & 0xFF;
    if (a == 0) return px;
    float r = ((px >> 16) & 0xFF) / 255.f;
    float g = ((px >> 8) & 0xFF) / 255.f;
    float b = (px & 0xFF) / 255.f;
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float tint = lum < 0.08f ? lum / 0.08f : 1.f;
    float h, s, l;
    RgbToHsl(r, g, b, h, s, l);
    h += hsl.hue * tint;
    s = clampf(s + hsl.sat * tint, 0.f, 1.f);
    l = clampf(l + hsl.light * tint, 0.f, 1.f);
    HslToRgb(h, s, l, r, g, b);
    auto to8 = [](float v) -> unsigned {
        int i = (int)(clampf(v, 0.f, 1.f) * 255.f + 0.5f);
        return (unsigned)(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return (a << 24) | (to8(r) << 16) | (to8(g) << 8) | to8(b);
}

static unsigned short ShiftPixel4444(unsigned short px, const DyeHsl& hsl) {
    unsigned a = (px >> 12) & 0xF;
    if (a == 0) return px;
    float r = ((px >> 8) & 0xF) / 15.f;
    float g = ((px >> 4) & 0xF) / 15.f;
    float b = (px & 0xF) / 15.f;
    float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float tint = lum < 0.08f ? lum / 0.08f : 1.f;
    float h, s, l;
    RgbToHsl(r, g, b, h, s, l);
    h += hsl.hue * tint;
    s = clampf(s + hsl.sat * tint, 0.f, 1.f);
    l = clampf(l + hsl.light * tint, 0.f, 1.f);
    HslToRgb(h, s, l, r, g, b);
    auto to4 = [](float v) -> unsigned short {
        int i = (int)(clampf(v, 0.f, 1.f) * 15.f + 0.5f);
        return (unsigned short)(i < 0 ? 0 : (i > 15 ? 15 : i));
    };
    return (unsigned short)((a << 12) | (to4(r) << 8) | (to4(g) << 4) | to4(b));
}

// 纯指针操作，避免在 SEH 帧里放 C++ 对象
static bool DyeRawLocked(unsigned char* ptr, int pitch, unsigned w, unsigned h, int fmt, const DyeHsl& hsl) {
    if (!ptr || pitch <= 0 || w == 0 || h == 0) return false;
    if (fmt == (int)CP_A4R4G4B4) {
        for (unsigned y = 0; y < h; ++y) {
            unsigned short* row = reinterpret_cast<unsigned short*>(ptr + y * pitch);
            for (unsigned x = 0; x < w; ++x) row[x] = ShiftPixel4444(row[x], hsl);
        }
    } else {
        for (unsigned y = 0; y < h; ++y) {
            unsigned* row = reinterpret_cast<unsigned*>(ptr + y * pitch);
            for (unsigned x = 0; x < w; ++x) row[x] = ShiftPixel8888(row[x], hsl);
        }
    }
    return true;
}

static bool BackupAndDyeCanvas(IWzCanvas* canvas, const DyeHsl& hsl, int itemId) {
    if (!canvas || hsl.nearZero()) return false;
    try {
        IWzRawCanvasPtr raw = canvas->GetrawCanvas(0, 0);
        if (!raw) return false;
        int pitch = 0;
        Ztl_variant_t addr = raw->_LockAddress(&pitch);
        unsigned char* ptr = nullptr;
        if (V_VT(&addr) == VT_BYREF) ptr = reinterpret_cast<unsigned char*>(V_BYREF(&addr));
        else ptr = reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(V_UI4(&addr)));
        unsigned w = raw->Getwidth();
        unsigned h = raw->Getheight();
        int fmt = (int)raw->GetpixelFormat();
        if (!ptr || w == 0 || h == 0 || pitch <= 0) {
            tagRECT rc = {0, 0, (LONG)w, (LONG)h};
            raw->_UnlockAddress(&rc);
            return false;
        }

        uintptr_t key = reinterpret_cast<uintptr_t>(canvas);
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_canvasBackups.find(key) == g_canvasBackups.end()) {
                CanvasBackup bak;
                bak.width = (int)w;
                bak.height = (int)h;
                bak.pitch = pitch;
                bak.fmt = fmt;
                bak.bytes.resize((size_t)pitch * h);
                memcpy(bak.bytes.data(), ptr, bak.bytes.size());
                g_canvasBackups[key] = bak;
            } else {
                const CanvasBackup& bak = g_canvasBackups[key];
                if ((int)bak.bytes.size() >= pitch * (int)h) {
                    memcpy(ptr, bak.bytes.data(), (size_t)pitch * h);
                }
            }
            g_itemTrackedCanvases[itemId].insert(key);
        }

        DyeRawLocked(ptr, pitch, w, h, fmt, hsl);
        tagRECT dirty = {0, 0, (LONG)w, (LONG)h};
        raw->_UnlockAddress(&dirty);
        return true;
    } catch (...) {
        return false;
    }
}

static void RestoreCanvasKey(uintptr_t key) {
    auto it = g_canvasBackups.find(key);
    if (it == g_canvasBackups.end()) return;
    IWzCanvas* canvas = reinterpret_cast<IWzCanvas*>(key);
    try {
        IWzRawCanvasPtr raw = canvas->GetrawCanvas(0, 0);
        if (!raw) return;
        int pitch = 0;
        Ztl_variant_t addr = raw->_LockAddress(&pitch);
        unsigned char* ptr = nullptr;
        if (V_VT(&addr) == VT_BYREF) ptr = reinterpret_cast<unsigned char*>(V_BYREF(&addr));
        else ptr = reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(V_UI4(&addr)));
        const CanvasBackup& bak = it->second;
        if (ptr && pitch > 0 && bak.height > 0) {
            memcpy(ptr, bak.bytes.data(), (size_t)bak.pitch * bak.height);
        }
        tagRECT dirty = {0, 0, bak.width, bak.height};
        raw->_UnlockAddress(&dirty);
    } catch (...) {
    }
}

static const wchar_t* FolderForItem(int itemId) {
    switch (itemId / 10000) {
    case 104: return L"Coat";
    case 105: return L"Longcoat";
    case 106: return L"Pants";
    case 110: return L"Cape";
    case 170: return L"Weapon";
    default: return nullptr;
    }
}

static void DyeItemWzCommonActions(int itemId, const DyeHsl& hsl) {
    const wchar_t* folder = FolderForItem(itemId);
    if (!folder) return;
    static const wchar_t* kActions[] = {
        L"stand1", L"stand2", L"walk1", L"walk2", L"alert",
        L"swingO1", L"swingO2", L"swingO3", L"swingT1", L"swingT2", L"swingT3",
        L"stabO1", L"stabO2", L"shoot1", L"prone", L"jump", L"sit", L"fly"
    };
    wchar_t base[128];
    swprintf_s(base, L"Character/%s/%08d.img", folder, itemId);
    IWzResManPtr rm = get_rm();
    if (!rm) return;

    for (const wchar_t* act : kActions) {
        wchar_t path[192];
        swprintf_s(path, L"%s/%s", base, act);
        try {
            Ztl_variant_t obj = rm->GetObjectA(path, vtEmpty, vtEmpty);
            IUnknownPtr unk = get_unknown(obj);
            if (!unk) continue;
            IWzPropertyPtr prop = unk;
            if (!prop) continue;
            for (int i = 0; i < 24; ++i) {
                wchar_t frame[16];
                swprintf_s(frame, L"%d", i);
                Ztl_variant_t fv = prop->Getitem(frame);
                IUnknownPtr funk = get_unknown(fv);
                if (!funk) break;
                IWzPropertyPtr fp = funk;
                IWzCanvasPtr canvas;
                if (fp) {
                    Ztl_variant_t cv = fp->Getitem(L"0");
                    IUnknownPtr cunk = get_unknown(cv);
                    if (cunk) canvas = cunk;
                }
                if (!canvas) canvas = funk;
                if (canvas) BackupAndDyeCanvas(canvas, hsl, itemId);
            }
        } catch (...) {
        }
    }
}

int __cdecl Hook_LoadItemAction(int nItemID, int nAction, int bFlip, IUnknown** ppResult) {
    int prev = g_loadingItemId;
    g_loadingItemId = nItemID;
    int r = Orig_LoadItemAction(nItemID, nAction, bFlip, ppResult);
    if (g_allowFieldLoadDye && ppResult && *ppResult) {
        DyeHsl hsl;
        bool has = false;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            auto it = g_ownedHsl.find(nItemID);
            if (it != g_ownedHsl.end() && !it->second.nearZero()) {
                hsl = it->second;
                has = true;
            }
        }
        if (has) {
            IWzCanvasPtr canvas = *ppResult;
            if (canvas) BackupAndDyeCanvas(canvas, hsl, nItemID);
        }
    }
    g_loadingItemId = prev;
    return r;
}

} // namespace

namespace EquipDye {

void Hook() {
    if (g_hooksAttached) return;
    g_hooksAttached = true;
    ATTACH_HOOK(Orig_LoadItemAction, Hook_LoadItemAction);
}

void SyncItemHslFromList(const std::vector<DyeEntry>& entries) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_ownedHsl.clear();
    for (const auto& e : entries) {
        if (!e.hsl.nearZero()) g_ownedHsl[e.itemId] = e.hsl;
    }
}

void MergeItemHslFromList(int charId, const std::vector<DyeEntry>& entries) {
    std::lock_guard<std::mutex> lock(g_mu);
    std::map<int, DyeHsl>& m = g_charItemHsl[charId];
    for (const auto& e : entries) {
        if (e.hsl.nearZero()) m.erase(e.itemId);
        else m[e.itemId] = e.hsl;
    }
}

DyeHsl GetOwnedHsl(int itemId) {
    std::lock_guard<std::mutex> lock(g_mu);
    auto it = g_ownedHsl.find(itemId);
    return it == g_ownedHsl.end() ? DyeHsl() : it->second;
}

void SetOwnedHsl(int itemId, const DyeHsl& hsl) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (hsl.nearZero()) g_ownedHsl.erase(itemId);
    else g_ownedHsl[itemId] = hsl;
}

void ClearOwnedHsl(int itemId) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_ownedHsl.erase(itemId);
}

void SoftRefreshPreview(int itemId, const DyeHsl& hsl) {
    try { DyeItemWzCommonActions(itemId, hsl); } catch (...) {}
}

void RestoreSharedWzForItem(int itemId) {
    std::unordered_set<uintptr_t> keys;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_itemTrackedCanvases.find(itemId);
        if (it == g_itemTrackedCanvases.end()) return;
        keys = it->second;
    }
    for (uintptr_t key : keys) RestoreCanvasKey(key);
}

void ApplyOwnedItemDyeOnly() {
    g_allowFieldLoadDye = true;
    std::vector<std::pair<int, DyeHsl>> copy;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (const auto& kv : g_ownedHsl) {
            if (!kv.second.nearZero()) copy.push_back(kv);
        }
    }
    for (size_t i = 0; i < copy.size(); ++i) {
        try { DyeItemWzCommonActions(copy[i].first, copy[i].second); } catch (...) {}
    }
}

void ScheduleDeferredOwnedApply(int delayMs) {
    g_deferredApplyAt = GetTickCount() + (DWORD)(delayMs < 0 ? 0 : delayMs);
    g_deferredApplyPending = true;
}

void PollDeferredOwnedApply() {
    if (!g_deferredApplyPending) return;
    if ((int)(GetTickCount() - g_deferredApplyAt) < 0) return;
    g_deferredApplyPending = false;
    ApplyOwnedItemDyeOnly();
}

void OnCanvasResolved(void* punk) { (void)punk; }

void RequestForeignAvatarRefresh(int charId) {
    std::lock_guard<std::mutex> lock(g_mu);
    g_foreignRefreshChars.insert(charId);
}

bool TryConsumeForeignAvatarRefresh(int* outCharId) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_foreignRefreshChars.empty()) return false;
    auto it = g_foreignRefreshChars.begin();
    if (outCharId) *outCharId = *it;
    g_foreignRefreshChars.erase(it);
    return true;
}

void ApplyForeignVisibleDye(int charId) {
    std::map<int, DyeHsl> items;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_charItemHsl.find(charId);
        if (it == g_charItemHsl.end()) return;
        for (const auto& kv : it->second) {
            if (g_ownedHsl.count(kv.first)) continue;
            items[kv.first] = kv.second;
        }
    }
    for (const auto& kv : items) {
        try { DyeItemWzCommonActions(kv.first, kv.second); } catch (...) {}
    }
}

bool IsDyeableCategory(int itemId) {
    int cat = itemId / 10000;
    return cat == 104 || cat == 105 || cat == 106 || cat == 110 || cat == 170;
}

bool IsCashItemId(int itemId) {
    return IsDyeableCategory(itemId);
}

void SetLoadingItemId(int itemId) { g_loadingItemId = itemId; }
int GetLoadingItemId() { return g_loadingItemId; }
void SetAllowFieldLoadDye(bool allow) { g_allowFieldLoadDye = allow; }

} // namespace EquipDye

bool g_coloringPrismWndOpen = false;
