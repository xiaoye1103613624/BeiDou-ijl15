#include "stdafx.h"
#include "hairsway.h"

#include "hook.h"
#include "debug.h"
// THE WEATHER COUPLING IS OPTIONAL, and this is the whole of it. Hair sway reads the
// sky in exactly one place, IsRoughWeather() below, to stiffen its motion under a storm
// or a blizzard. Without a weather system it runs at its baseline and nothing else
// changes, so the include is guarded rather than required: the standalone bundle ships a
// no-op hairsway_weather.h, and if the weather system is installed later this picks up
// the real header on its own with no edit here.
//
// The shim is deliberately NOT called weather.h. A weather system installs its own header
// under that name into this same directory, and other modules include it directly, so a
// shim wearing that name would shadow the real one -- silently, and the symptom would read
// as the weather system breaking rather than as a stray stub header.
#if __has_include("weather.h")
#  include "weather.h"
#  define HAIRSWAY_HAS_WEATHER 1
#else
#  include "hairsway_weather.h"
#endif
#include "wvs/avatar.h"
#include "wvs/util.h"
#include "ztl/ztl.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr DWORD kPeriodMs        = 3400;
constexpr int   kTipTravelPx     = 2;
constexpr float kBaseStrength    = 0.90f;
constexpr float kRoughWeatherStrength = 1.40f;
constexpr int   kCanvasPad       = 3;
// Three sub-pixel bend phases per rendered pixel keep the curved silhouette
// moving gradually even though Canvas ultimately lands on integer pixels.
constexpr int   kPoseSubsteps    = 3;
constexpr int   kMaxPose         = kTipTravelPx * kPoseSubsteps;
constexpr int   kPoseCount       = kMaxPose * 2 + 1;
constexpr int   kRigidAboveRootPx = 9;
constexpr DWORD kIdleRefreshMs   = 200;
constexpr uintptr_t kAddr_CAvatar_ClearActionLayer = 0x00453A29;
constexpr uintptr_t kAddr_CAvatar_SetMoveAction    = 0x004520F1;
constexpr uintptr_t kAddr_CreateAnimLayer           = 0x0043EA3E;
constexpr size_t kOff_AvatarMoveAction = 0x4E8;
constexpr uintptr_t kAddr_CUserLocal_Instance = 0x00BEBF98;
constexpr size_t kOff_AvatarInUser = 0x88;

// The client factory is important here: it inserts the result into the same
// world/camera compositor used by avatar-attached effects.  CreateLayer alone
// only allocates a sibling object and is not enough to make an avatar child draw.
using CreateAnimLayerFn = void** (__cdecl*)(void**, void*, int, void*, int, int,
                                             void*, int, int, int);
auto CreateAnimLayer = reinterpret_cast<CreateAnimLayerFn>(kAddr_CreateAnimLayer);

std::vector<std::wstring> ChildNames(IWzPropertyPtr pNode) {
    std::vector<std::wstring> names;
    if (!pNode) return names;
    IEnumVARIANTPtr pEnum = pNode->_NewEnum;
    if (!pEnum) return names;
    for (;;) {
        Ztl_variant_t v;
        ULONG got = 0;
        if (FAILED(pEnum->Next(1, &v, &got)) || !got) break;
        if (V_VT(&v) == VT_BSTR && V_BSTR(&v)) names.emplace_back(V_BSTR(&v));
    }
    return names;
}

// Avatar-frame finalization reads canvas properties such as origin, map and z.
// Every frame installed in the WZ tree must retain those source properties.
bool CopyCanvasProperty(IWzCanvasPtr src, IWzCanvasPtr dst) {
    try {
        IWzPropertyPtr sp = src->property;
        IWzPropertyPtr dp = dst->property;
        if (!sp || !dp) return false;
        for (const std::wstring& name : ChildNames(sp))
            dp->item[name.c_str()] = sp->item[name.c_str()];
        return true;
    } catch (...) {
        return false;
    }
}

struct OverlaySet {
    IWzGr2DLayerPtr original;
    IWzGr2DLayerPtr frame[kPoseCount];
    int shown = -1;
};

// One source/profile pair has a small, fixed bank of bent canvases.  The canvas
// pixels are never changed once a Gr2D layer has taken them: Canvas.dll caches an
// uploaded texture for avatar layers.  Instead (just like foliage sway), separate
// renderer layers hold the pre-rendered poses and the tick changes only `visible`.
struct LiveCanvas {
    IWzCanvasPtr source;
    IWzCanvasPtr blank;
    IWzCanvasPtr frame[kPoseCount];
    int width = 0;
    int height = 0;
    float tipScale = 1.0f;
    int rootInset = 0;
    std::vector<OverlaySet> overlays;
};

int OffsetNow(DWORD now);
bool IsRoughWeather();
int PoseFor(const LiveCanvas& live, int baseOffset, bool roughWeather);
int CurrentAvatarAction(CAvatar* avatar);
std::unordered_map<uint64_t, LiveCanvas> g_live;
struct PendingLayer {
    LiveCanvas* live;
    int zOffset;
};
std::vector<PendingLayer> g_pendingLayers;
bool g_creatingOverlay = false;
bool g_independentLayersEnabled = false;
int g_captureDepth = 0;
CAvatar* g_lastAvatar = nullptr;

// The avatar compositor's CopyEx call has already resolved the WZ `map`
// anchors. Replacing its SOURCE canvas here therefore preserves the exact native
// position for every action/frame; no external origin or fixed offset is needed.
constexpr size_t kVtbl_RawCopy = 32; // byte-verified: compositor calls [vtable+0x80]
using RawCopy = HRESULT (__stdcall*)(IWzCanvas*, int, int, IWzCanvas*, VARIANT);
RawCopy g_rawCopy = nullptr;
bool g_rawCopyHooked = false;
// Some stock action frames (including prone) compose through CopyEx instead of
// Copy.  It is the next IWzCanvas vtable method, not an alternate signature for
// slot 32.
constexpr size_t kVtbl_RawCopyEx = 33;
using RawCopyEx = HRESULT (__stdcall*)(IWzCanvas*, int, int, IWzCanvas*,
                                       CANVAS_ALPHATYPE, int, int, int, int,
                                       int, int, VARIANT);
RawCopyEx g_rawCopyEx = nullptr;
bool g_rawCopyExHooked = false;
int g_copyBuildDepth = 0;
int g_copyPoseCursor = 0;
int g_copyMatchesThisBuild = 0;
std::unordered_map<void*, int> g_copyTargetPose;

// The client may keep a resolved UOL canvas alive across avatar builds (prone is
// one such case).  Keep the hair sources used by *this* build separately from
// the permanent pose cache, so a stale source can be mapped back to the current
// runtime-recoloured pose bank.
std::vector<uint64_t> g_activeHairKeys;

// `PrepareActionLayer` does not create new Gr2D layers.  It inserts each action
// frame into the avatar's existing part layers, so CreateLayer cannot see these
// canvases.  This is the COM vtable slot for IWzGr2DLayer::raw_InsertCanvas:
// 3 IUnknown + 2 IWzSerialize + 17 IWzShape2D + 22 IWzVector2D + 21 here.
constexpr size_t kVtbl_RawInsertCanvas = 65;
using RawInsertCanvas = HRESULT (__stdcall*)(IWzGr2DLayer*, IWzCanvas*, VARIANT,
                                              VARIANT, VARIANT, VARIANT, VARIANT,
                                              VARIANT*);
RawInsertCanvas g_rawInsertCanvas = nullptr;
bool g_rawInsertHooked = false;
int g_rawInsertCallsThisBuild = 0;
int g_rawInsertMatchesThisBuild = 0;

inline uint64_t LiveKey(IWzCanvas* raw, int rootInset, float tipScale) {
    return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw)) << 8)
         | (static_cast<unsigned int>(rootInset) << 3)
         | (tipScale < 0.75f ? 1u : 0u);
}

inline int PoseSlot(int offset) {
    if (offset < -kMaxPose) offset = -kMaxPose;
    if (offset >  kMaxPose) offset =  kMaxPose;
    return offset + kMaxPose;
}

// Draw a single immutable bent pose.  Raw DrawRectangle is intentional: it is
// Canvas.dll's publishing path and is already proven by another module in this DLL.
bool RenderFrame(LiveCanvas& live, int tipOffset, IWzCanvasPtr& out) {
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", out, nullptr);
        if (!out) return false;
        out->Create(live.width + kCanvasPad * 2, live.height, 0, CP_A8R8G8B8);
        if (!CopyCanvasProperty(live.source, out)) return false;
        out->cx = live.source->cx + kCanvasPad;
        out->cy = live.source->cy;
        out->raw_DrawRectangle(0, 0, live.width + kCanvasPad * 2, live.height, 0x00FFFFFF);

        int root = static_cast<int>(live.source->cy) + live.rootInset;
        if (root < 0) root = 0;
        if (root >= live.height - 1) root = live.height - 2;
        const int freeBelow = (live.height - 1 - root > 0) ? (live.height - 1 - root) : 1;
        const int freeAbove = (root > kRigidAboveRootPx)
                            ? (root - kRigidAboveRootPx) : 1;
        for (int y = 0; y < live.height; ++y) {
            // Long styles do not all hang down.  Tall spikes and swept-up hair
            // live ABOVE the head attachment, so measure free length outward
            // from the anchor on either side.  The root row itself remains fixed.
            const int aboveRoot = root - y;
            const float t = (y < root)
                          ? (aboveRoot <= kRigidAboveRootPx ? 0.0f
                             : static_cast<float>(aboveRoot - kRigidAboveRootPx)
                               / static_cast<float>(freeAbove))
                          : (y > root)
                          ? static_cast<float>(y - root) / static_cast<float>(freeBelow)
                          : 0.0f;
            const float bend = static_cast<float>(tipOffset) /
                               static_cast<float>(kPoseSubsteps);
            const int dx = static_cast<int>(lroundf(bend * t * t));
            int x = 0;
            while (x < live.width) {
                uint32_t colour = 0;
                if (FAILED(live.source->get_pixel(x, y, &colour)) || (colour >> 24) == 0) {
                    ++x;
                    continue;
                }
                int end = x + 1;
                while (end < live.width) {
                    uint32_t next = 0;
                    if (FAILED(live.source->get_pixel(end, y, &next)) || next != colour) break;
                    ++end;
                }
                out->raw_DrawRectangle(kCanvasPad + dx + x, y, end - x, 1, colour);
                x = end;
            }
        }
        return true;
    } catch (...) {
        out = nullptr;
        return false;
    }
}

// The stock avatar renderer flattens every part into a single action canvas.  To
// animate hair without restarting that canvas, its original WZ node is replaced
// with this property-identical transparent canvas during the stock build.  The
// real pixels are then supplied by independent Gr2D layers below.
bool MakeBlankFrame(LiveCanvas& live) {
    try {
        PcCreateObject<IWzCanvasPtr>(L"Canvas", live.blank, nullptr);
        if (!live.blank) return false;
        live.blank->Create(live.width, live.height, 0, CP_A8R8G8B8);
        if (!CopyCanvasProperty(live.source, live.blank)) return false;
        live.blank->cx = live.source->cx;
        live.blank->cy = live.source->cy;
        return true;
    } catch (...) {
        live.blank = nullptr;
        return false;
    }
}

void DestroyOverlays(LiveCanvas& live) {
    for (OverlaySet& set : live.overlays) {
        try { if (set.original) set.original->visible = 1; } catch (...) {}
        for (IWzGr2DLayerPtr& frame : set.frame) {
            try {
                if (frame) {
                    frame->visible = 0;
                    // Detach from the avatar overlay before dropping our final ref.
                    frame->overlay = Ztl_variant_t();
                }
            } catch (...) {}
            frame = nullptr;
        }
        set.original = nullptr;
    }
    live.overlays.clear();
}

void DestroyAllOverlays() {
    for (auto& entry : g_live) DestroyOverlays(entry.second);
}

IWzCanvas* ReplacementHairSource(IWzCanvas* target, IWzCanvas* source) {
    IWzCanvas* replacement = source;
    if (g_copyBuildDepth > 0 && source && target) {
        try {
            LiveCanvas* sourceProfile = nullptr;
            for (auto& entry : g_live) {
                LiveCanvas& live = entry.second;
                bool isHair = live.source.GetInterfacePtr() == source;
                for (int i = 0; i < kPoseCount; ++i) {
                    if (live.frame[i].GetInterfacePtr() == source) {
                        isHair = true;
                        break;
                    }
                }
                if (!isHair) continue;

                sourceProfile = &live;
                break;
            }
            if (sourceProfile) {
                // Prefer the current source itself.  If this is a stale UOL
                // resolution, find its corresponding current layer by its
                // render profile; those current canvases were created after
                // a runtime recolour installed its canvas swap.
                LiveCanvas* live = nullptr;
                for (uint64_t key : g_activeHairKeys) {
                    auto active = g_live.find(key);
                    if (active == g_live.end()) continue;
                    LiveCanvas& candidate = active->second;
                    if (candidate.source.GetInterfacePtr() == source) {
                        live = &candidate;
                        break;
                    }
                    if (!live && candidate.width == sourceProfile->width &&
                        candidate.height == sourceProfile->height &&
                        candidate.rootInset == sourceProfile->rootInset &&
                        candidate.tipScale == sourceProfile->tipScale) {
                        live = &candidate;
                    }
                }
                if (!live) live = sourceProfile;

                auto pos = g_copyTargetPose.find(target);
                if (pos == g_copyTargetPose.end()) {
                    // Each compositor destination is one native avatar frame.
                    // Advance once per destination, not once per hair part, so
                    // front/back/face hair remain in the same wind pose.
                    const int pose = (g_copyPoseCursor++ % kPoseCount) - kMaxPose;
                    pos = g_copyTargetPose.emplace(target, pose).first;
                }
                replacement = live->frame[PoseSlot(pos->second)].GetInterfacePtr();
                ++g_copyMatchesThisBuild;
            }
        } catch (...) {
            replacement = source;
        }
    }
    return replacement;
}

HRESULT __stdcall RawCopy_Hook(IWzCanvas* target, int dstLeft, int dstTop,
                               IWzCanvas* source, VARIANT alpha) {
    IWzCanvas* replacement = ReplacementHairSource(target, source);
    return g_rawCopy(target, dstLeft, dstTop, replacement, alpha);
}

HRESULT __stdcall RawCopyEx_Hook(IWzCanvas* target, int dstLeft, int dstTop,
                                 IWzCanvas* source, CANVAS_ALPHATYPE alpha,
                                 int width, int height, int srcLeft, int srcTop,
                                 int srcWidth, int srcHeight, VARIANT adjust) {
    IWzCanvas* replacement = ReplacementHairSource(target, source);
    return g_rawCopyEx(target, dstLeft, dstTop, replacement, alpha, width, height,
                       srcLeft, srcTop, srcWidth, srcHeight, adjust);
}

void EnsureCopyHook(IWzCanvasPtr canvas) {
    if ((g_rawCopyHooked && g_rawCopyExHooked) || !canvas) return;
    try {
        void** vtable = *reinterpret_cast<void***>(canvas.GetInterfacePtr());
        if (!vtable) return;
        if (!g_rawCopyHooked && vtable[kVtbl_RawCopy]) {
            g_rawCopy = reinterpret_cast<RawCopy>(vtable[kVtbl_RawCopy]);
            if (ATTACH_HOOK(g_rawCopy, RawCopy_Hook)) g_rawCopyHooked = true;
        }
        if (!g_rawCopyExHooked && vtable[kVtbl_RawCopyEx]) {
            g_rawCopyEx = reinterpret_cast<RawCopyEx>(vtable[kVtbl_RawCopyEx]);
            if (ATTACH_HOOK(g_rawCopyEx, RawCopyEx_Hook)) g_rawCopyExHooked = true;
        }
    } catch (...) {
        if (!g_rawCopyHooked) g_rawCopy = nullptr;
        if (!g_rawCopyExHooked) g_rawCopyEx = nullptr;
    }
}

LiveCanvas* LiveFor(IWzCanvasPtr src, float tipScale, int rootInset) {
    if (!src) return nullptr;
    const uint64_t key = LiveKey(src, rootInset, tipScale);
    auto it = g_live.find(key);
    if (it != g_live.end()) return &it->second;

    LiveCanvas live;
    try {
        live.width = static_cast<int>(src->width);
        live.height = static_cast<int>(src->height);
        if (live.width <= 1 || live.height <= 1 || live.width > 256 || live.height > 256)
            return nullptr;
        live.source = src;
        live.tipScale = tipScale;
        live.rootInset = rootInset;
        for (int pose = -kMaxPose; pose <= kMaxPose; ++pose) {
            if (!RenderFrame(live, pose, live.frame[PoseSlot(pose)])) {
                return nullptr;
            }
        }
        if (!MakeBlankFrame(live)) return nullptr;
    } catch (...) {
        return nullptr;
    }
    return &g_live.emplace(key, std::move(live)).first->second;
}

struct Swap {
    IWzPropertyPtr parent;
    std::wstring name;
    IUnknownPtr original;
};
std::vector<Swap> g_swaps;

// Some static actions cache a UOL object (for example prone/0/hair) before
// resolving it to default/hair.  Swapping default/hair alone is then too late:
// the cached UOL still yields the old source while the action is flattened.
// Replace the action leaf itself for this one build, then restore the UOL.
void SwapActionHairLeaves(IWzPropertyPtr img, const std::wstring& leaf,
                          IWzCanvasPtr replacement) {
    if (!img || !replacement) return;
    try {
        for (const std::wstring& actionName : ChildNames(img)) {
            if (actionName == L"default" || actionName == L"backDefault") continue;
            IUnknownPtr actionUnk = img->item[actionName.c_str()].GetUnknown();
            IWzPropertyPtr action;
            if (!actionUnk || FAILED(actionUnk.QueryInterface(__uuidof(IWzProperty), &action)) || !action)
                continue;
            for (const std::wstring& frameName : ChildNames(action)) {
                IUnknownPtr frameUnk = action->item[frameName.c_str()].GetUnknown();
                IWzPropertyPtr frame;
                if (!frameUnk || FAILED(frameUnk.QueryInterface(__uuidof(IWzProperty), &frame)) || !frame)
                    continue;
                IUnknownPtr original = frame->item[leaf.c_str()].GetUnknown();
                if (!original) continue;
                g_swaps.push_back({frame, leaf, original});
                frame->item[leaf.c_str()] = static_cast<IUnknown*>(replacement);
            }
        }
    } catch (...) {
    }
}

IWzPropertyPtr PropertyAtPath(IWzPropertyPtr root, const std::wstring& path) {
    if (!root) return nullptr;
    try {
        IWzPropertyPtr cur = root;
        size_t at = 0;
        while (at <= path.size()) {
            const size_t slash = path.find(L'/', at);
            const std::wstring part = path.substr(at, slash == std::wstring::npos
                ? std::wstring::npos : slash - at);
            if (!part.empty()) {
                IUnknownPtr u = cur->item[part.c_str()].GetUnknown();
                if (!u || FAILED(u.QueryInterface(__uuidof(IWzProperty), &cur)) || !cur)
                    return nullptr;
            }
            if (slash == std::wstring::npos) break;
            at = slash + 1;
        }
        return cur;
    } catch (...) {
        return nullptr;
    }
}

int OffsetNow(DWORD now) {
    constexpr float twoPi = 6.2831853f;
    const float t = static_cast<float>(now % 600000u) * 0.001f;
    const float mainSweep = sinf(twoPi * t / (kPeriodMs * 0.001f));
    const float returnEase = sinf(twoPi * t / 1.73f + 0.91f);
    const float slowDrift = 0.62f * sinf(twoPi * t / 7.10f + 0.37f)
                          + 0.38f * sinf(twoPi * t / 11.63f + 2.14f);
    const float wave = 0.82f * mainSweep + 0.18f * returnEase;
    const float strength = 0.82f + 0.18f * slowDrift;
    int offset = static_cast<int>(lroundf(static_cast<float>(kMaxPose) * strength * wave));
    if (offset < -kMaxPose) offset = -kMaxPose;
    if (offset >  kMaxPose) offset =  kMaxPose;
    return offset;
}

bool IsRoughWeather() {
#ifdef HAIRSWAY_HAS_WEATHER
    const unsigned char sky = Weather::CurrentSky();
    return sky == Weather::SKY_STORM || sky == Weather::SKY_BLIZZARD;
#else
    return false;   // no weather system: baseline motion, always
#endif
}

int PoseFor(const LiveCanvas& live, int baseOffset, bool roughWeather) {
    const float weather = kBaseStrength * (roughWeather ? kRoughWeatherStrength : 1.0f);
    return static_cast<int>(lroundf(static_cast<float>(baseOffset) * live.tipScale * weather));
}

// The v83 renderer composites every body part (including hair) into one final
// action-frame canvas. A replacement rebuild therefore must never run while an
// animated action is in progress, or its cache cursor will restart that action.
int CurrentAvatarAction(CAvatar* avatar) {
    __try {
        return avatar ? avatar->GetCurrentAction(nullptr) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void ReloadIdleAvatar(CAvatar* avatar) {
    __try {
        if (!avatar) return;
        const int moveAction = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(avatar) + kOff_AvatarMoveAction);
        auto clear = reinterpret_cast<void(__thiscall*)(void*, int)>(
            kAddr_CAvatar_ClearActionLayer);
        auto setMove = reinterpret_cast<void(__thiscall*)(void*, int, int)>(
            kAddr_CAvatar_SetMoveAction);
        clear(avatar, 0);
        clear(avatar, 1);
        setMove(avatar, moveAction, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void ShowPose(LiveCanvas& live, int pose) {
    if (!g_independentLayersEnabled) return;
    const int wanted = PoseSlot(pose);
    for (OverlaySet& set : live.overlays) {
        if (set.shown == wanted) continue;
        for (int i = 0; i < kPoseCount; ++i) {
            try { if (set.frame[i]) set.frame[i]->visible = (i == wanted) ? 1 : 0; } catch (...) {}
        }
        set.shown = wanted;
    }
}

bool MakeOverlaySet(LiveCanvas& live, const IWzGr2DLayerPtr& original) {
    if (!original || !get_gr()) return false;
    for (const OverlaySet& old : live.overlays) {
        if (old.original.GetInterfacePtr() == original.GetInterfacePtr()) return true;
    }

    OverlaySet set;
    set.original = original;
    try {
        const int z = original->z;
        const int x = original->rx - kCanvasPad;
        const int y = original->ry;
        const int flip = original->flip;
        const Ztl_variant_t origin = original->origin;
        const Ztl_variant_t overlay = original->overlay;

        g_creatingOverlay = true;
        for (int i = 0; i < kPoseCount; ++i) {
            IWzGr2DLayerPtr made = get_gr()->CreateLayer(
                0, 0, live.width + kCanvasPad * 2, live.height, z,
                static_cast<IUnknown*>(live.frame[i]), Ztl_variant_t());
            if (!made) {
                g_creatingOverlay = false;
                return false;
            }
            made->origin = origin;
            made->overlay = overlay;
            made->flip = flip;
            made->RelMove(x, y, Ztl_variant_t(), Ztl_variant_t());
            made->visible = 0;
            set.frame[i] = made;
        }
        g_creatingOverlay = false;
        original->visible = 0;
        live.overlays.push_back(std::move(set));
        return true;
    } catch (...) {
        g_creatingOverlay = false;
        return false;
    }
}

// Make an independent hair layer in the same coordinate system as the avatar's
// flattened body canvas.  Unlike MakeOverlaySet this deliberately leaves `host`
// visible: it is the hairless body/face canvas we need the hair to sit over.
bool MakeIndependentOverlaySet(LiveCanvas& live, const IWzGr2DLayerPtr& host,
                               int zOffset, int initialPose) {
    if (!host || !g_lastAvatar) return false;
    for (const OverlaySet& old : live.overlays) {
        if (old.original.GetInterfacePtr() == host.GetInterfacePtr()) return true;
    }

    OverlaySet set;
    set.original = host; // ownership/identity only; never hidden by this path.
    try {
        const int flip = host->flip;
        // `rx`/`ry` are the avatar renderer's already-resolved local position
        // for this action frame.  They include its body/head map resolution, so
        // using them anchors hair where vanilla drew it instead of to a guessed
        // fixed body-origin offset.
        const int anchorX = host->rx;
        const int anchorY = host->ry;
        IWzVector2DPtr bodyOrigin = g_lastAvatar->m_pBodyOrigin;
        if (!bodyOrigin) return false;

        g_creatingOverlay = true;
        for (int i = 0; i < kPoseCount; ++i) {
            IWzPropertyPtr node;
            PcCreateObject<IWzPropertyPtr>(L"Property", node, nullptr);
            if (!node) {
                g_creatingOverlay = false;
                return false;
            }
            // CreateAnimLayer treats numbered children as animation frames.  A
            // one-child property makes each pre-bent canvas a stable layer.
            node->item[L"0"] = static_cast<IUnknown*>(live.frame[i]);
            IWzProperty* rawNode = node.GetInterfacePtr();
            rawNode->AddRef();
            bodyOrigin->AddRef();
            host->AddRef();
            void* rawLayer = nullptr;
            CreateAnimLayer(&rawLayer, rawNode, flip,
                            bodyOrigin.GetInterfacePtr(), anchorX, anchorY,
                            host.GetInterfacePtr(), zOffset, 255, 0);
            IWzGr2DLayerPtr made(reinterpret_cast<IWzGr2DLayer*>(rawLayer), false);
            if (!made) {
                g_creatingOverlay = false;
                return false;
            }
            made->visible = (g_independentLayersEnabled && i == PoseSlot(initialPose)) ? 1 : 0;
            set.frame[i] = made;
        }
        g_creatingOverlay = false;
        set.shown = PoseSlot(initialPose);
        live.overlays.push_back(std::move(set));
        return true;
    } catch (...) {
        g_creatingOverlay = false;
        return false;
    }
}

// Measure where the client actually composited a hair sprite in its completed
// avatar canvas. This is intentionally read-only: it is the ground truth needed
// to replace the previous guessed body-origin offsets.
bool FindCompositedAnchor(IWzCanvasPtr full, IWzCanvasPtr sprite, int& outX,
                          int& outY, int& outScore) {
    outX = outY = outScore = 0;
    try {
        const int fw = full ? static_cast<int>(full->width) : 0;
        const int fh = full ? static_cast<int>(full->height) : 0;
        const int sw = sprite ? static_cast<int>(sprite->width) : 0;
        const int sh = sprite ? static_cast<int>(sprite->height) : 0;
        if (fw < 2 || fh < 2 || sw < 2 || sh < 2 || sw > fw || sh > fh) return false;
        struct Sample { int x, y; uint32_t colour; } samples[48];
        int count = 0;
        for (int y = 0; y < sh && count < _countof(samples); y += 2) {
            for (int x = (y & 3); x < sw && count < _countof(samples); x += 3) {
                uint32_t c = 0;
                if (FAILED(sprite->get_pixel(x, y, &c)) || (c >> 24) == 0) continue;
                samples[count++] = {x, y, c};
            }
        }
        if (count < 8) return false;
        int best = -1, bestX = 0, bestY = 0;
        for (int y = 0; y <= fh - sh; ++y) for (int x = 0; x <= fw - sw; ++x) {
            int score = 0;
            for (int i = 0; i < count; ++i) {
                uint32_t c = 0;
                if (SUCCEEDED(full->get_pixel(x + samples[i].x, y + samples[i].y, &c)) &&
                    c == samples[i].colour) ++score;
            }
            if (score > best) { best = score; bestX = x; bestY = y; }
        }
        outX = bestX; outY = bestY; outScore = best;
        return best >= count / 2;
    } catch (...) {
        return false;
    }
}

HRESULT __stdcall RawInsertCanvas_Hook(IWzGr2DLayer* layer, IWzCanvas* canvas,
                                       VARIANT delay, VARIANT alpha0, VARIANT alpha1,
                                       VARIANT zoom0, VARIANT zoom1, VARIANT* outIndex) {
    // Prone installs its already-resolved canvas directly into an avatar layer,
    // bypassing both canvas Copy methods.  Substitute before the stock insert so
    // the native layer retains its exact map, origin, z and action timing.
    IWzCanvas* replacement = canvas;
    if (g_captureDepth > 0 && canvas && !g_creatingOverlay) {
        replacement = ReplacementHairSource(reinterpret_cast<IWzCanvas*>(layer), canvas);
        if (replacement != canvas) ++g_rawInsertMatchesThisBuild;
    }
    const HRESULT hr = g_rawInsertCanvas(layer, replacement, delay, alpha0, alpha1,
                                         zoom0, zoom1, outIndex);
    if (SUCCEEDED(hr) && g_captureDepth > 0 && canvas && !g_creatingOverlay) {
        ++g_rawInsertCallsThisBuild;
    }
    return hr;
}

void EnsureInsertCapture(CAvatar* avatar) {
    if (g_rawInsertHooked || !avatar) return;
    try {
        IWzGr2DLayer* host = avatar->m_pLayerUnderFace.GetInterfacePtr();
        if (!host) return;
        void** vtable = *reinterpret_cast<void***>(host);
        if (!vtable || !vtable[kVtbl_RawInsertCanvas]) return;
        g_rawInsertCanvas = reinterpret_cast<RawInsertCanvas>(vtable[kVtbl_RawInsertCanvas]);
        if (ATTACH_HOOK(g_rawInsertCanvas, RawInsertCanvas_Hook)) {
            g_rawInsertHooked = true;
        }
    } catch (...) {
        g_rawInsertCanvas = nullptr;
    }
}

} // namespace

bool HairSway_Applies(void* pAvatar, int hairId) {
    // anHairEquip[0] is structurally the active hairstyle slot, so it needs no
    // guessed numeric range.  This covers every Hair.wz id this client supports,
    // including imported or future styles outside the classic 3xxxx families.
    if (hairId <= 0 || !pAvatar) return false;
    __try {
        void* user = *reinterpret_cast<void**>(kAddr_CUserLocal_Instance);
        return user && pAvatar == reinterpret_cast<unsigned char*>(user) + kOff_AvatarInUser;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void HairSway_BeginLayer(void* pAvatar, int hairId) {
    if (!HairSway_Applies(pAvatar, hairId)) return;
    g_lastAvatar = reinterpret_cast<CAvatar*>(pAvatar);
    // The upcoming PrepareActionLayer creates a fresh set of source layers.  The
    // previous hidden originals and their siblings can now be detached safely.
    DestroyAllOverlays();
    g_pendingLayers.clear();
    g_copyTargetPose.clear();
    g_activeHairKeys.clear();
    g_copyMatchesThisBuild = 0;
    g_rawInsertCallsThisBuild = 0;
    g_rawInsertMatchesThisBuild = 0;
    g_copyPoseCursor = PoseSlot(OffsetNow(GetTickCount()));
    try {
        wchar_t wzPath[80];
        _snwprintf_s(wzPath, _countof(wzPath), _TRUNCATE, L"Character/Hair/%08d.img", hairId);
        IWzPropertyPtr img = get_rm()->GetObjectA(wzPath).GetUnknown();
        if (!img) return;

        struct LayerSpec { const wchar_t* path; float tipScale; int rootInset; int zOffset; };
        static const LayerSpec kLayers[] = {
            { L"default/hairOverHead",          0.50f, 4,  2 },
            { L"default/hair",                  0.50f, 8,  1 },
            { L"default/hairBelowBody",         1.00f, 0, -1 },
            { L"backDefault/backHair",          1.00f, 0, -1 },
            { L"backDefault/backHairBelowCap",  1.00f, 0, -1 },
        };
        const int baseOffset = OffsetNow(GetTickCount());
        const bool rough = IsRoughWeather();
        EnsureInsertCapture(g_lastAvatar);
        for (const LayerSpec& layer : kLayers) {
            const std::wstring full(layer.path);
            const size_t slash = full.find_last_of(L'/');
            IWzPropertyPtr parent = PropertyAtPath(img, full.substr(0, slash));
            if (!parent) continue;
            const std::wstring leaf = full.substr(slash + 1);
            IUnknownPtr u = parent->item[leaf.c_str()].GetUnknown();
            IWzCanvasPtr original;
            if (!u || FAILED(u.QueryInterface(__uuidof(IWzCanvas), &original)) || !original) continue;
            LiveCanvas* live = LiveFor(original, layer.tipScale, layer.rootInset);
            if (!live) continue;
            g_activeHairKeys.push_back(LiveKey(original.GetInterfacePtr(), layer.rootInset,
                                               layer.tipScale));
            EnsureCopyHook(live->frame[kMaxPose]);
            g_swaps.push_back({parent, leaf, u});
            // Keep the client-composited hair path until the per-part resolved
            // map anchor is captured.  It is the only path that is guaranteed to
            // align with every native action frame.
            IWzCanvasPtr pose = live->frame[PoseSlot(PoseFor(*live, baseOffset, rough))];
            parent->item[leaf.c_str()] = static_cast<IUnknown*>(pose);
            SwapActionHairLeaves(img, leaf, pose);
        }
        if (g_rawCopyHooked || g_rawCopyExHooked) ++g_copyBuildDepth;
        // Limit layer interception to prone. Other actions already use the
        // compositor Copy route that is proven to preserve their animation.
        if (g_rawInsertHooked && CurrentAvatarAction(g_lastAvatar) == 33)
            ++g_captureDepth;
    } catch (...) {
    }
}

void HairSway_EndLayer() {
    for (auto it = g_swaps.rbegin(); it != g_swaps.rend(); ++it) {
        try {
            if (it->parent && it->original)
                it->parent->item[it->name.c_str()] = static_cast<IUnknown*>(it->original);
        } catch (...) {}
    }
    g_swaps.clear();
    if (g_copyBuildDepth > 0) --g_copyBuildDepth;
    if (g_captureDepth > 0) --g_captureDepth;
    if (g_lastAvatar && CurrentAvatarAction(g_lastAvatar) == 33)
        LogMessage("hairsway: prone copy=%d insert=%d/%d live-canvases=%d",
                   g_copyMatchesThisBuild, g_rawInsertMatchesThisBuild,
                   g_rawInsertCallsThisBuild, static_cast<int>(g_live.size()));

    g_pendingLayers.clear();
    g_activeHairKeys.clear();
}

// UNUSED, AND THE COMMENT THAT USED TO SIT HERE WAS WRONG. It said "called by
// weather.cpp's one global CreateLayer hook", which was never true -- that hook calls
// Lamps_CaptureLayer, and a tree-wide search for HairSway_CaptureLayer finds only this
// definition and its declaration.
//
// It is the other half of the independent-layer prototype, which is switched off at
// g_independentLayersEnabled and never switched on, so this and MakeIndependentOverlaySet
// are both unreachable. Kept rather than cut because they go together and because the
// prototype is the intended route away from the composited path; if you are reading this
// to decide whether to delete it, delete both or neither.
//
// What it WOULD do: during avatar assembly it is handed the real, already-mapped hair
// layer, and the replacement layers inherit its origin, overlay, z, flip and relative
// position, so movement and action timing stay owned by the stock avatar layer.
bool HairSway_CaptureLayer(IWzGr2DLayer* layer, IWzCanvas* insertedCanvas) {
    if (g_captureDepth <= 0 || g_creatingOverlay || !layer || !insertedCanvas) return false;
    try {
        for (auto& entry : g_live) {
            LiveCanvas& live = entry.second;
            // Avatar action-frame caches retain the original WZ canvas even while
            // the narrow WZ swap is open.  That is normal (and why a layer rebuild
            // used to be necessary).  Match the cached original here; the overlay
            // then replaces it with our independently-rendered pose bank.
            if (live.source.GetInterfacePtr() == insertedCanvas) {
                const bool made = MakeOverlaySet(live, IWzGr2DLayerPtr(layer));
                LogMessage("hairsway: cached source %s on layer %p",
                           made ? "captured" : "could not be captured", layer);
                return made;
            }
            for (int i = 0; i < kPoseCount; ++i) {
                if (live.frame[i].GetInterfacePtr() == insertedCanvas)
                    {
                        const bool made = MakeOverlaySet(live, IWzGr2DLayerPtr(layer));
                        LogMessage("hairsway: frame %d %s on layer %p", i,
                                   made ? "captured" : "could not be captured", layer);
                        return made;
                    }
            }
        }
    } catch (...) {}
    return false;
}

void HairSway_Tick() {
    static DWORD next = 0;
    static int lastStaticReloadPose = 99;
    static int loggedAction = -999;
    const DWORD now = GetTickCount();
    if (static_cast<LONG>(now - next) < 0 || g_live.empty()) return;
    next = now + kIdleRefreshMs;
    const int baseOffset = OffsetNow(now);
    const bool rough = IsRoughWeather();
    for (auto& entry : g_live) ShowPose(entry.second, PoseFor(entry.second, baseOffset, rough));

    // Prone and sit are single/static-frame actions. They have no native frame
    // cadence through which the compositor can advance a pose bank, and their
    // cached canvas may predate a runtime recolour. Refresh only these stable poses;
    // movement, jumps, attacks and flips still never take this path.
    if (g_lastAvatar) {
        const int action = CurrentAvatarAction(g_lastAvatar);
        if (action != loggedAction) {
            loggedAction = action;
            LogMessage("hairsway: live action=%d", action);
        }
        if (baseOffset == lastStaticReloadPose) return;
        if (action == 33 || action == 39) {
            lastStaticReloadPose = baseOffset;
            ReloadIdleAvatar(g_lastAvatar);
        }
    }
}
