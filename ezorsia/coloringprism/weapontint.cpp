// ============================================================
// weapontint.cpp: runtime HSB re-tint of the worn Cash weapon (Coloring Prism)
//
// v83 has NO item-dye system, so the recolor is done here: the weapon's WZ
// canvases are cloned, transformed in HSV, and swapped into the WZ tree for
// exactly as long as it takes the client to build the avatar's action layers.
// The layers keep their own refs to the clones, the tree is put back, and the
// avatar is left holding recolored sprites while every other consumer of that
// weapon img (inventory icon, tooltip, another player wearing the same sticker)
// still sees the original pixels.
//
//   PrepareActionLayer(pAvatar)          <- Detoured here
//     |  Swap()      Character/Weapon/<sticker>.img/<action>/<frame>/<part>
//     |                   := tinted clone            (originals remembered)
//     |  <original PrepareActionLayer builds the layers from the tree>
//     |  Restore()   put every original back
//     v
//
// WHY THIS SHAPE, and not the two obvious alternatives:
//   * Recoloring the WZ canvas IN PLACE would tint the item everywhere and for
//     everyone -- the canvases are shared, cached resources.
//   * Substituting inside get_unknown / Ztl_variant_t::GetUnknown would put a
//     string compare on the hottest resource path in the client, for every
//     canvas it ever resolves.
// The swap window is the narrowest place where "this avatar, this weapon" is
// known and the canvases have not been read yet.
//
// PROVEN PRIMITIVES:
//   IWzProperty::item[name] = <value>                     (writes into the WZ tree)
//   PcCreateObject<IWzCanvasPtr>(L"Canvas") + Create()
//   IWzCanvas::CopyEx(..., CA_OVERWRITE, ...)
//   IWzCanvas::Create(w,h,mag,fmt) / cx / cy
// The one primitive this file introduces is IWzRawCanvas::_LockAddress, used to
// walk the cloned pixels. It is guarded and has a Getpixel/DrawRectangle
// fallback, so if it ever fails on this build the feature degrades to slow
// rather than to broken.
//
// -------------------------------------------------------------------------
// WARNING: CLIENT-BUILD-SPECIFIC ADDRESSES
// Every 0x00XXXXXX below is tied to THIS v83 MapleStory.exe (image base
// 0x400000). Proof for each is written beside it.
//
// This file owns exactly ONE address: CAvatar::PrepareActionLayer (0x00453AD1).
// Keep it that way. A second Detour on it, from anywhere, is the documented hazard.
// -------------------------------------------------------------------------
//
// SCOPE: every player in the map, not just the local one. Remote avatars are
// identified by the character name at CUser+0x11AC (see that constant) and matched
// against a table the server pushes for the current map; the local player's own
// avatar takes the live slider values while the prism window is open. TintForAvatar()
// is the single place that decides which of the three cases applies.
// ============================================================

#include "stdafx.h"
// BeiDou port shim
#ifndef LOG_ONCE
#define LOG_ONCE(...) ((void)0)
#endif
#ifndef LOG_ONCE_PER_ID
#define LOG_ONCE_PER_ID(id, ...) ((void)0)
#endif
#ifndef AVATAR_EQUIP_SLOTS
#define AVATAR_EQUIP_SLOTS 60
#endif
#include "compat/hook.h"
#include "weapontint.h"
#include "../hairsway/hairsway.h"

#include "compat/wvs/avatar.h"
#include "compat/wvs/packet_legacy.h"
#include "compat/wvs/Packet.h"
#include "compat/wvs/util.h"
#include "ztl/ztl.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// =====================================================
// ADDRESSES
// =====================================================
// CAvatar::PrepareActionLayer: CAvatar's vtable is stored by its constructor
// (CAvatar::Constructor @0x0044FE6C) at 0x0044FFB1:
//     mov dword ptr [esi], 0xAF14D8
// and 0x00AF14D8[5] = 0x00453AD1. Slot 5 is PrepareActionLayer: CAvatar::
// SetRidingVehicle (0x00456E35, already wrapped in wvs/avatar.h) ends with
//     push 0 / push 0x64 / push 6 / mov ecx,esi / call dword ptr [eax+0x14]
// i.e. a 3-arg call through vtable offset 0x14 = slot 5, matching the
// PrepareActionLayer(nActionSpeed, nWalkSpeed, bKeyDown) declaration.
constexpr uintptr_t kAddr_CAvatar_PrepareActionLayer = 0x00453AD1;
// CAvatar's FACE layer builder, called from SetEmotion (0x00451D82). Owned by this file;
// nothing else in the repo hooks it. See BuildFaceLayer_Hook for why it is needed.
constexpr uintptr_t kAddr_CAvatar_BuildFaceLayer     = 0x00453696;

// CUserLocal* singleton: non-null only inside a field.
constexpr uintptr_t kAddr_CUserLocal_Instance = 0x00BEBF98;

// CAvatar sits at CUser+0x88, for EVERY CUser: CUser::CUser (0x0092DCFC) runs
// `lea edi,[esi+0x88]; call 0x0044FE6C` (the CAvatar ctor) at 0x0092DD14.
constexpr size_t kOff_AvatarInUser = 0x88;

// CUser+0x11AC is the character name (a ZXString<char>, so the field is the char*).
// This is how a remote avatar is identified, and it is the piece that kept the tint
// local-only.
//
// Found by observation, not by tracing: CUser::CUser stores its only argument at
// +0x11A8, but CUserRemote passes 0 there (0x004B22A5 `xor edi,edi; push edi`), so
// that neighbour is a flag rather than an id; and 0x00BEBF94 -- the global that
// looked like CUserPool -- is a path-keyed cache (its method 0x0043FB25 parses a
// leading '/' or '\\'). A one-shot scan of CUserLocal for pointers to short
// printable strings reported exactly one plausible member, +0x11AC, holding the
// live character name on two different clients simultaneously ("pink" and "Admin").
//
// Names are unique per world, so they key the tint table directly and no character
// id is needed anywhere in this feature.
constexpr size_t kOff_CharacterNameInUser = 0x11AC;

// CClientSocket singleton + SendPacket, as used by every other custom opcode here.
constexpr uintptr_t kAddr_ClientSocket_Instance = 0x00BE7914;
constexpr uintptr_t kAddr_ClientSocket_Send     = 0x0049637B;

// CAvatar::SetMoveAction, vtable slot 4 (0x00AF14D8[4]): the CAvatar-level
// implementation. Called directly, not through the vtable; see SehReloadActionLayer.
constexpr uintptr_t kAddr_CAvatar_SetMoveAction = 0x004520F1;

// CAvatar::ClearActionLayer(int nLayerSet), `ret 4`. Wipes the selected 0x5DC
// layer set: 0xA2 entries at +0x10, 0xA2 at +0x298, 0x2E at +0x520. The client
// calls it as ClearActionLayer(0) from PrepareActionLayer @0x00453AF7 and
// ClearActionLayer(1) from SetMoveAction @0x00452120.
constexpr uintptr_t kAddr_CAvatar_ClearActionLayer = 0x00453A29;

// m_nMoveAction, written by CAvatar::Init from its nMA argument and compared by
// SetMoveAction at 0x004520FF.
constexpr size_t kOff_MoveAction = 0x4E8;

// =====================================================
// PROTOCOL
// =====================================================
constexpr uint8_t kAction_RequestSnapshot = 0;
constexpr uint8_t kAction_Apply           = 1;
constexpr uint8_t kAction_Restore         = 2;
// Look (hair / eye) variants. SEPARATE actions rather than apply/restore with a dummy
// address, because those two run the server's resolve(), which demands a Cash equip at
// a given inventory position -- hair and eyes satisfy none of that. The handler's
// switch ends in a default no-op, so an older server ignores 3/4 instead of desyncing.
constexpr uint8_t kAction_ApplyLook       = 3;
constexpr uint8_t kAction_RestoreLook     = 4;

constexpr uint8_t kResp_Snapshot    = 1;
constexpr uint8_t kResp_Result      = 2;
constexpr uint8_t kResp_RemoteTable = 3;   // everyone in the map, keyed by name

// =====================================================
// STATE
// =====================================================
// g_mtx guards the two fields WeaponTint_HandleSync writes; that runs on the
// RECEIVE thread while every reader is on the main thread.
constexpr int kAvatarEquipSlots = AVATAR_EQUIP_SLOTS;

// itemId -> the tint stored on that item. One entry per DYED item; anything absent
// is vanilla.
using TintMap = std::unordered_map<int, WeaponTint>;

std::mutex g_mtx;
TintMap    g_saved;                       // the local player's dyed items
WeaponTintResult g_lastResult = kTintResult_None;
// Set by the receive thread when a snapshot actually CHANGES the tint, consumed by
// WeaponTint_Tick on the main thread. The repaint cannot be done where the packet
// lands: clearing the clone cache and rebuilding the avatar's layers are both
// main-thread-only, and the receive thread reaching into them is how you get a
// torn layer mid-frame.
bool g_syncDirty = false;

// OTHER PLAYERS. The server pushes one entry per player in the map who is wearing a
// tinted Cash weapon, keyed by character name (unique per world, and the only
// identity the client can read off an arbitrary CAvatar -- see kOff_CharacterNameInUser).
std::unordered_map<std::string, TintMap> g_remoteTints;
// Last CAvatar seen wearing each name, recorded by TintForAvatar as the renderer
// hands them over. Purely a repaint shortcut: there is no way to enumerate CUsers
// in this repo, so this is how a tint update finds the avatar to rebuild. Entries
// go stale when a player leaves, which is why every use re-reads the name off the
// pointer and discards it on a mismatch.
std::unordered_map<std::string, CAvatar*> g_avatarByName;
// Names whose tint changed and whose avatar still needs repainting, drained by the tick.
std::vector<std::string> g_remoteDirty;

// Main thread only. While the prism window is open its sliders override the stored
// tint for the ONE item being dyed; everything else the player wears keeps its own.
WeaponTint g_preview;
int        g_previewItemId = 0;
bool       g_previewActive = false;
int        g_forcedScope   = 0;           // >0 while a DLL-owned preview avatar builds
CAvatar*   g_forcedAvatar  = nullptr;
TintMap    g_forcedTints;
// The Coloring Prism's CAvatar continues to build its face layer after Init returns.
// It must retain its own live tint until that avatar is released, but no other avatar
// may ever match this pointer (in particular, not the player in the field).
CAvatar*   g_prismPreviewAvatar = nullptr;

// =====================================================
// HSV
// =====================================================
// Straight HSV round trip on 8-bit channels. `hue` rotates, `chroma` and
// `bright` scale saturation and value by (100 + delta)%. Fully transparent
// pixels are skipped so the sprite's cutout is untouched, and a fully
// desaturated pixel keeps hue 0 -- which is why a pure-grey weapon only
// responds to Tone once Chroma has been raised, exactly like the live game.
inline void RgbToHsv(int r, int g, int b, float& h, float& s, float& v) {
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const int d  = mx - mn;
    v = mx / 255.0f;
    s = mx == 0 ? 0.0f : static_cast<float>(d) / mx;
    if (d == 0) {
        h = 0.0f;
    } else if (mx == r) {
        h = 60.0f * fmodf((static_cast<float>(g - b) / d), 6.0f);
    } else if (mx == g) {
        h = 60.0f * ((static_cast<float>(b - r) / d) + 2.0f);
    } else {
        h = 60.0f * ((static_cast<float>(r - g) / d) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
}

inline void HsvToRgb(float h, float s, float v, int& r, int& g, int& b) {
    const float c = v * s;
    const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float rf, gf, bf;
    if      (h <  60.0f) { rf = c; gf = x; bf = 0; }
    else if (h < 120.0f) { rf = x; gf = c; bf = 0; }
    else if (h < 180.0f) { rf = 0; gf = c; bf = x; }
    else if (h < 240.0f) { rf = 0; gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; gf = 0; bf = c; }
    else                 { rf = c; gf = 0; bf = x; }
    auto to8 = [](float f) {
        int n = static_cast<int>(f * 255.0f + 0.5f);
        return n < 0 ? 0 : (n > 255 ? 255 : n);
    };
    r = to8(rf + m); g = to8(gf + m); b = to8(bf + m);
}

// One BGRA8888 pixel, transformed in place. Returns false if nothing changed
// (fully transparent), so the caller can skip the write.
inline bool TintPixel(uint32_t& argb, const WeaponTint& t) {
    const uint32_t a = (argb >> 24) & 0xFF;
    if (a == 0) return false;
    int r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    float h, s, v;
    RgbToHsv(r, g, b, h, s, v);
    h += t.hue;
    while (h >= 360.0f) h -= 360.0f;
    while (h <    0.0f) h += 360.0f;
    s *= (100.0f + t.chroma) / 100.0f;
    v *= (100.0f + t.bright) / 100.0f;
    if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    HsvToRgb(h, s, v, r, g, b);
    argb = (a << 24) | (r << 16) | (g << 8) | b;
    return true;
}

// =====================================================
// CANVAS CLONE + RECOLOR
// =====================================================
// Everything from here to CloneTinted deals in RAW COM pointers and PODs on
// purpose: MSVC refuses `__try` in any function that also has objects requiring
// unwinding (C2712), and _com_ptr_t / Ztl_variant_t both do. The smart-pointer
// half lives in CloneTinted, which uses C++ EH only.

// HOW THE PIXELS ARE READ AND WRITTEN -- and why it is this exact pair.
//
// READ: lock the clone's raw buffer once and copy it out wholesale. One COM call
// for the whole sprite instead of one per pixel.
//
// WRITE: `DrawRectangle`, batched into horizontal runs of equal colour.
//
// The asymmetry is not an oversight. Writing straight into the locked buffer was
// tried and it DOES change what the canvas reports -- a changed pixel read back
// through get_pixel after unlocking returns the new value -- but the weapon still
// renders in its original colours. So the locked buffer is not what the renderer
// consumes; there is a converted/uploaded copy behind it that a raw poke does not
// invalidate, and neither _UnlockAddress(rect) nor _UnlockAddress(NULL) publishes
// it. DrawRectangle goes through the canvas's own drawing path and does.
//
// Do not "optimise" this back into a locked write. It looks like it works, it
// passes a read-back check, and the weapon stays the colour it started.
//
// The lock is still used for READING, where it is only ever asked to hand back
// bytes it already holds -- which is the part that was verified to be coherent.

// Read the whole canvas into `out` as ARGB8888. Returns false if the lock is
// unusable, which sends the caller to the per-pixel reader.
bool ReadPixelsLocked(IWzCanvas* pCanvas, int w, int h, uint32_t* out) {
    CANVAS_PIXFORMAT fmt = CP_UNKNOWN;
    __try {
        if (FAILED(pCanvas->get_pixelFormat(&fmt))) fmt = CP_UNKNOWN;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fmt = CP_UNKNOWN;
    }
    const int bpp = (fmt == CP_A8R8G8B8) ? 4 : ((fmt == CP_A4R4G4B4) ? 2 : 0);
    if (bpp == 0) return false;

    IWzRawCanvas* raw = nullptr;
    __try {
        if (FAILED(pCanvas->get_rawCanvas(0, 0, &raw))) raw = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw = nullptr;
    }
    if (!raw) return false;

    int pitch = 0;
    uint8_t* base = nullptr;
    __try {
        VARIANT vAddr;
        VariantInit(&vAddr);
        if (SUCCEEDED(raw->raw__LockAddress(&pitch, &vAddr))) {
            // Canvas.dll returns VT_BYREF|VT_UI4 (0x4013 observed): the pixel array
            // itself. `lVal` and `byref` are the same union slot on win32, and the
            // client's own consumer at 0x005DAA43 reads that slot straight as the
            // buffer, then advances rows by `pitch` (0x005DAA7B).
            const int base_vt = V_VT(&vAddr) & VT_TYPEMASK;
            if (base_vt == VT_I4 || base_vt == VT_UI4
                || base_vt == VT_INT || base_vt == VT_UINT) {
                base = (V_VT(&vAddr) & VT_BYREF)
                     ? reinterpret_cast<uint8_t*>(V_BYREF(&vAddr))
                     : reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(V_I4(&vAddr)));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        base = nullptr;
    }

    bool ok = false;
    if (base && pitch >= w * bpp) {
        __try {
            for (int y = 0; y < h; ++y) {
                const uint8_t* row = base + static_cast<size_t>(y) * pitch;
                uint32_t* dstRow = out + static_cast<size_t>(y) * w;
                if (bpp == 4) {
                    memcpy(dstRow, row, static_cast<size_t>(w) * 4);
                } else {
                    const uint16_t* px = reinterpret_cast<const uint16_t*>(row);
                    for (int x = 0; x < w; ++x) {
                        const uint16_t s = px[x];
                        dstRow[x] = ((((s >> 12) & 0xFu) * 17u) << 24)
                                  | ((((s >>  8) & 0xFu) * 17u) << 16)
                                  | ((((s >>  4) & 0xFu) * 17u) <<  8)
                                  |  (((s        & 0xFu) * 17u));
                    }
                }
            }
            ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
    }
    if (base) {
        // NULL, exactly as the client's own consumer does at 0x005DAA9C.
        __try { raw->raw__UnlockAddress(nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    __try { raw->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return ok;
}

// Per-pixel reader, for a build where the lock is unusable.
void ReadPixelsSlow(IWzCanvas* pCanvas, int w, int h, uint32_t* out) {
    __try {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t px = 0;
                if (FAILED(pCanvas->get_pixel(x, y, &px))) px = 0;
                out[static_cast<size_t>(y) * w + x] = px;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Write the tinted pixels back as horizontal runs of equal colour. Fully
// transparent pixels are skipped entirely -- they are already correct from the
// CopyEx, and they are the majority of a weapon sprite -- so a typical row costs a
// handful of calls rather than one per pixel. Returns the number of pixels changed.
int WriteRuns(IWzCanvas* pCanvas, int w, int h, const uint32_t* px) {
    int changed = 0;
    __try {
        for (int y = 0; y < h; ++y) {
            const uint32_t* row = px + static_cast<size_t>(y) * w;
            int x = 0;
            while (x < w) {
                const uint32_t c = row[x];
                if ((c >> 24) == 0) { ++x; continue; }      // transparent: leave it
                int end = x + 1;
                while (end < w && row[end] == c) ++end;
                pCanvas->raw_DrawRectangle(x, y, end - x, 1, c);
                changed += end - x;
                x = end;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return changed;
}

// Every child name of a property node, read out in full before the caller touches
// the node. (Declared here because CloneTinted needs it; the swap walk below uses
// the same helper for the same collect-then-mutate reason, documented there.)
std::vector<std::wstring> ChildNames(IWzPropertyPtr pNode);

// Give the clone the SOURCE NODE'S WZ children -- origin, z, and above all `map`,
// the attachment points (hand / navel) the avatar assembler positions the weapon by.
//
// THIS IS NOT OPTIONAL. A canvas from PcCreateObject(L"Canvas") has an empty
// property collection, and the client's frame finalizer (0x004123D2, reached from
// CActionMan::BuildAvatarActionFrames @0x00407A14) dereferences canvas->property
// WITHOUT a null check. Installing a property-less clone in the WZ tree is an
// access violation reading 0x00000000 the instant the avatar rebuilds -- which is
// exactly what a tinted preview did before this existed.
//
// Sub-nodes (`map` is itself a property) are shared by reference rather than deep
// copied: the client only ever reads them.
bool CopyCanvasProperty(IWzCanvasPtr src, IWzCanvasPtr dst) {
    IWzPropertyPtr sp, dp;
    try {
        sp = src->property;
        dp = dst->property;
    } catch (...) {
        return false;
    }
    if (!sp || !dp) return false;
    try {
        for (const std::wstring& name : ChildNames(sp)) {
            dp->item[name.c_str()] = sp->item[name.c_str()];
        }
    } catch (...) {
        return false;
    }
    return true;
}

IWzCanvasPtr CloneTinted(IWzCanvasPtr pSrc, const WeaponTint& t) {
    if (!pSrc) return nullptr;
    IWzCanvasPtr dst;
    try {
        const int w = static_cast<int>(pSrc->width);
        const int h = static_cast<int>(pSrc->height);
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return nullptr;

        PcCreateObject<IWzCanvasPtr>(L"Canvas", dst, nullptr);
        if (!dst) return nullptr;
        // Explicitly 32-bit so the pixel walk is format-independent.
        dst->Create(w, h, 0, CP_A8R8G8B8);
        // CA_OVERWRITE (255) copies the source verbatim, alpha included; the other
        // type, CA_REMOVEALPHA, would flatten the sprite's cutout to opaque.
        dst->CopyEx(0, 0, pSrc, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0);

        // Bail BEFORE tinting if the node cannot be made whole: a clone the client
        // would crash on must never reach the tree, and returning null here just
        // leaves that one sprite its original colour.
        if (!CopyCanvasProperty(pSrc, dst)) {
            LOG_ONCE("weapontint: canvas property could not be copied; that sprite "
                     "stays untinted rather than risking the frame finalizer");
            return nullptr;
        }

        // Read once, tint in memory, write back as runs.
        std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0);
        const bool fastRead = ReadPixelsLocked(dst, w, h, px.data());
        if (!fastRead) {
            LOG_ONCE("weapontint: raw lock unusable for reading; falling back to "
                     "get_pixel (%dx%d)", w, h);
            ReadPixelsSlow(dst, w, h, px.data());
        }
        int tinted = 0;
        for (uint32_t& p : px) {
            if (TintPixel(p, t)) ++tinted;
        }
        const int written = WriteRuns(dst, w, h, px.data());
        LOG_ONCE("weapontint: first clone %dx%d, fastRead=%d, %d px tinted, %d written",
                 w, h, fastRead ? 1 : 0, tinted, written);
        // The origin is what positions the sprite on the avatar; losing it would
        // fling the weapon off the character.
        dst->cx = pSrc->cx;
        dst->cy = pSrc->cy;
    } catch (...) {
        return nullptr;
    }
    return dst;
}

// =====================================================
// CACHE
// =====================================================
// Keyed by ORIGINAL CANVAS **and** TINT.
//
// Keying on the canvas alone and dropping the map whenever the tint changed was correct
// while one tint was live at a time, but a single frame now applies SEVERAL: an outfit
// can carry a different colour per item, and each item's effect layers carry a second
// one of their own. Each lookup would invalidate the map the previous one had just
// filled, so every canvas would be re-cloned every frame. Pointers are 32-bit in this
// client, so the pair packs into a uint64 exactly.
inline uint64_t CloneKey(IWzCanvas* raw, const WeaponTint& t) {
    return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw)) << 32) | t.Key();
}
std::unordered_map<uint64_t, IWzCanvasPtr> g_clones;

// The iris path keeps its OWN clone map. Sharing g_clones would be a latent aliasing
// bug -- one key mapping to two different transforms if a canvas were ever reachable
// from both paths -- and the invalidation rule is identical, so separating costs nothing.
std::unordered_map<IWzCanvas*, IWzCanvasPtr> g_irisClones;
uint32_t g_irisClonesKey = 0;
bool     g_irisClonesValid = false;

void ClearClones() {
    g_clones.clear();
    g_irisClones.clear();
    g_irisClonesValid = false;
}

IWzCanvasPtr GetTinted(IWzCanvasPtr pSrc, const WeaponTint& t) {
    if (!pSrc) return nullptr;
    const uint64_t key = CloneKey(pSrc, t);
    auto it = g_clones.find(key);
    if (it != g_clones.end()) return it->second;
    IWzCanvasPtr clone = CloneTinted(pSrc, t);
    g_clones.emplace(key, clone);   // cache the failure too, so it is not retried per frame
    return clone;
}

// =====================================================
// WZ-TREE SWAP
// =====================================================
struct Swap {
    IWzPropertyPtr parent;
    std::wstring   name;
    IWzCanvasPtr   original;
};
std::vector<Swap> g_swaps;
// Where the two walkers record the originals they displace. Normally g_swaps, which
// SwapGuard empties when a layer build ends; the item-effect scope below points it at
// its own list, because that build finishes on a completely different schedule.
std::vector<Swap>* g_swapSink = &g_swaps;
struct SwapSinkTo {
    std::vector<Swap>* prev;
    explicit SwapSinkTo(std::vector<Swap>& v) : prev(g_swapSink) { g_swapSink = &v; }
    ~SwapSinkTo() { g_swapSink = prev; }
};
// Face construction is deferred beyond CAvatar::Init. These swaps belong only to
// the Coloring Prism's one preview avatar and are restored before that avatar is
// released or replaced; ordinary per-layer swaps continue to use g_swaps above.
std::vector<Swap> g_previewFaceSwaps;

// Action code -> the WZ node name under Character/Weapon/<id>.img. The client's
// own converter is get_action_name_from_code @0x004A8CE6; reusing it keeps this
// file from carrying a duplicate action table that could drift from the client's.
auto get_action_name_from_code =
    reinterpret_cast<Ztl_bstr_t*(__cdecl*)(Ztl_bstr_t*, int)>(0x004A8CE6);

// The prism dyes the weapon, not its glow: the modern dialog says so outright
// ("Special effects will not be dyed"), and effect canvases are additively
// blended, so hue-rotating them reads as a bug rather than a recolor.
bool IsEffectPart(const wchar_t* name) {
    return name && _wcsnicmp(name, L"effect", 6) == 0;
}

// Every child name of a property node, read out in full before the caller touches
// the node.
//
// COLLECT FIRST, THEN MUTATE. Writing `pNode->item[name] = ...` while an
// IEnumVARIANT over that same node is still open invalidates the enumerator: the
// walk stops early or returns garbage, so most of the weapon's frames never get
// tinted and -- worse -- the half-mutated node is what CAvatar::Init then tries to
// build its layers from, which leaves the avatar with no sprites at all. Reading
// the names into a vector first costs one small allocation per frame and makes the
// mutation loop touch no live enumerator.
std::vector<std::wstring> ChildNames(IWzPropertyPtr pNode) {
    std::vector<std::wstring> names;
    if (!pNode) return names;
    IEnumVARIANTPtr pEnum = pNode->_NewEnum;
    if (!pEnum) return names;
    for (;;) {
        Ztl_variant_t v;
        ULONG got = 0;
        if (FAILED(pEnum->Next(1, &v, &got)) || got == 0) break;
        if (V_VT(&v) == VT_BSTR && V_BSTR(&v)) names.emplace_back(V_BSTR(&v));
    }
    return names;
}

// Recursively replace every canvas under `pNode` with its tinted clone, to any
// depth, remembering the originals in g_swaps.
//
// RECURSIVE ON PURPOSE. The categories do not agree on depth:
//     Accessory (face/eye/ear)   <emotion>/<canvas>              2 levels
//     Coat/Pants/Shoes/Glove/... <action>/<frame>/<part>         3
//     Cap/Cape                   same, plus a default/default    3
//     Cash weapon                <weaponType>/<action>/<frame>/<part>  4
// Walking for canvases instead of assuming a shape means one routine covers all of
// them, and anything not looked at yet.
// Which half of an item's art a walk is allowed to touch. The two are dyed separately --
// the reference dialog has a whole tab for effects -- and they must never be swapped in
// the same pass, because each carries its own tint.
enum class Layer { Body, Effects };

void SwapSubtree(IWzPropertyPtr pNode, const WeaponTint& t, int depth, Layer layer);

// Effects live under a node whose NAME starts with "effect", so an effects walk descends
// only into those and a body walk skips them. `effectItemID` is a property, not art, and
// is excluded from both by the canvas check further down.
void SwapEffectSubtrees(IWzPropertyPtr pNode, const WeaponTint& t, int depth) {
    if (!pNode || depth > 4) return;
    for (const std::wstring& name : ChildNames(pNode)) {
        IUnknownPtr pUnk = get_unknown(pNode->item[name.c_str()]);
        if (!pUnk) continue;
        const bool isEffect = IsEffectPart(name.c_str());

        // AN EFFECT IS USUALLY THE CANVAS, NOT A FOLDER OF THEM. The common shape is
        // <action>/<frame>/effect, a leaf canvas sitting beside the `weapon` canvas it
        // plays over; a folder of frames under `effect` is the rare one. Measured over
        // every equip that has effect art: 922 leaf canvases against 1 folder.
        //
        // So the canvas test has to come FIRST, and it has to happen here rather than in
        // SwapSubtree. A canvas answers QueryInterface(IWzProperty) too, because it
        // carries origin / z / map as children, so descending on the property interface
        // walks INTO the effect and finds only vectors and ints. That silently tinted
        // nothing at all: no crash, no log line, just an untinted glow.
        if (isEffect) {
            IWzCanvasPtr pOrig;
            if (SUCCEEDED(pUnk.QueryInterface(__uuidof(IWzCanvas), &pOrig)) && pOrig) {
                IWzCanvasPtr pTinted = GetTinted(pOrig, t);
                if (!pTinted) continue;
                g_swapSink->push_back(Swap{ pNode, name, pOrig });
                pNode->item[name.c_str()] = static_cast<IUnknown*>(pTinted);
                continue;
            }
        }

        IWzPropertyPtr pSub;
        if (FAILED(pUnk.QueryInterface(__uuidof(IWzProperty), &pSub)) || !pSub) continue;
        if (isEffect) SwapSubtree(pSub, t, depth + 1, Layer::Effects);
        else          SwapEffectSubtrees(pSub, t, depth + 1);
    }
}

void SwapSubtree(IWzPropertyPtr pNode, const WeaponTint& t, int depth, Layer layer) {
    if (!pNode || depth > 4) return;
    for (const std::wstring& name : ChildNames(pNode)) {
        // On a BODY walk these are skipped -- the real game says special effects are not
        // dyed with the item, and an additive glow reads as a bug when hue-rotated. On an
        // EFFECTS walk the caller has already descended into one, so everything below is
        // fair game.
        if (layer == Layer::Body && IsEffectPart(name.c_str())) continue;

        Ztl_variant_t v = pNode->item[name.c_str()];
        IUnknownPtr pUnk = get_unknown(v);
        if (!pUnk) continue;                 // delay / z ints, origin vectors

        // Canvas first: a canvas node also carries child properties (origin, z,
        // map), so testing for IWzProperty first would recurse into it instead.
        IWzCanvasPtr pOrig;
        if (SUCCEEDED(pUnk.QueryInterface(__uuidof(IWzCanvas), &pOrig)) && pOrig) {
            IWzCanvasPtr pTinted = GetTinted(pOrig, t);
            if (!pTinted) continue;
            g_swapSink->push_back(Swap{ pNode, name, pOrig });
            pNode->item[name.c_str()] = static_cast<IUnknown*>(pTinted);
            continue;
        }
        IWzPropertyPtr pSub;
        if (SUCCEEDED(pUnk.QueryInterface(__uuidof(IWzProperty), &pSub)) && pSub) {
            SwapSubtree(pSub, t, depth + 1, layer);
        }
    }
}

// Item id -> the Character subdirectory its sprites live in.
//
// The client's own mapper is ItemIdToCharacterSubdir @0x005C94A1, but it returns a
// ZXString<wchar_t> by out-param and pulls the name out of the obfuscated string
// pool, so reproducing the (tiny, stable) table is simpler than marshalling that.
// Both agree on `id / 10000` as the key; the weapon arm is the one that widens, to
// `id / 100000 in {13,14,16,17}`.
const wchar_t* CharacterSubdirOf(int itemId) {
    if (itemId <= 0) return nullptr;
    const int hi = itemId / 100000;
    if (hi == 13 || hi == 14 || hi == 16 || hi == 17) return L"Weapon";
    switch (itemId / 10000) {
        case 100: return L"Cap";
        case 101: case 102: case 103: return L"Accessory";   // face / eye / earring
        case 104: return L"Coat";
        case 105: return L"Longcoat";
        case 106: return L"Pants";
        case 107: return L"Shoes";
        case 108: return L"Glove";
        case 109: return L"Shield";
        case 110: return L"Cape";
        case 111: return L"Ring";
        case 112: return L"Accessory";                        // pendant
        default:  return nullptr;
    }
}

// Dispatch a subtree to whichever walk the layer asks for.
void SwapLayerOf(IWzPropertyPtr p, const WeaponTint& t, Layer layer) {
    if (layer == Layer::Effects) SwapEffectSubtrees(p, t, 0);
    else                         SwapSubtree(p, t, 0, Layer::Body);
}

// Tint one worn item's sprites for the action currently being built.
//
// Scoped to the action on purpose. Walking a whole img would be a few hundred
// canvases per item, and a full outfit re-tinted at drag speed would stall the
// client; one action is a handful. Items with no body actions (accessories, which
// are keyed by face emotion) have so few canvases that all of them are walked.
void SwapInTintFor(int itemId, const wchar_t* actionName, const WeaponTint& t, Layer layer) {
    const wchar_t* subdir = CharacterSubdirOf(itemId);
    if (!subdir) return;
    try {
        wchar_t path[80];
        _snwprintf_s(path, _countof(path), _TRUNCATE,
                     L"Character/%s/%08d.img", subdir, itemId);
        IWzPropertyPtr pImg = get_rm()->GetObjectA(path, vtEmpty, vtEmpty).GetUnknown();
        if (!pImg) return;

        bool matched = false;
        // The action itself, plus the static `default` node Cap/Cape carry.
        for (const wchar_t* n : { actionName, L"default" }) {
            if (!n) continue;
            IWzPropertyPtr p = pImg->item[n].GetUnknown();
            if (p) { SwapLayerOf(p, t, layer); matched = true; }
        }
        // Cash weapons nest one level deeper, under the numeric code of the weapon
        // type the sticker is worn over (e.g. 01702000.img/33/stand1/0/weapon).
        for (const std::wstring& name : ChildNames(pImg)) {
            if (name.empty() || !iswdigit(name[0])) continue;
            IWzPropertyPtr pType = pImg->item[name.c_str()].GetUnknown();
            if (!pType) continue;
            for (const wchar_t* n : { actionName, L"default" }) {
                if (!n) continue;
                IWzPropertyPtr p = pType->item[n].GetUnknown();
                if (p) { SwapLayerOf(p, t, layer); matched = true; }
            }
        }
        if (matched) return;

        // No node named for this action: an emotion-keyed img (Accessory). Those
        // hold one canvas per emotion, so walking every one of them is cheap.
        for (const std::wstring& name : ChildNames(pImg)) {
            if (name == L"info") continue;
            IWzPropertyPtr p = pImg->item[name.c_str()].GetUnknown();
            if (p) SwapLayerOf(p, t, layer);
        }
    } catch (...) {
        // Whatever landed in g_swaps is still undone by SwapOut.
    }
}

// =====================================================
// LOOK TINTS -- hair and eye colour (the window's Hair / Face tabs)
// =====================================================
// Hair is easy and eyes are not, and it is worth saying exactly why.
//
// HAIR. Character/Hair/000300NN.img holds ONE real canvas per layer and every
// per-action frame is a UOL pointing back at it: stand1/0/hair, walk1/0/hair,
// alert/0/hair, prone/0/hair and fly/0/hair all hold the string "../../default/hair".
// So three canvases cover the whole character -- `default/hair` plus `backDefault`'s
// two, `backHairBelowCap` and `backHair`. That last node is the trap: the equip walk
// visits { actionName, "default" } only, which reaches the front hair and not the
// back, and the result is a character whose ponytail is still the old colour.
//
// EYES. There is no iris layer to tint. A v83 Face img is one flat canvas per
// EXPRESSION containing eyes, eyebrows and mouth over transparency, and vanilla
// changes eye colour by swapping to a wholly different, separately drawn img. So a
// naive hue rotation of the canvas is wrong twice over:
//
//   * it recolours things that are not eyes. Measured over 117 default/face canvases:
//     a mean of 10 non-iris pixels per canvas are chromatic enough to move visibly
//     (brown brows/lashes at H~30, blue sclera shading at H~210-240), and smile /
//     angry / love carry 21-100 pure-red MOUTH pixels that a rotation turns green.
//   * it does nothing at all for the two most common eye colours. Colour 0 (Black)
//     is achromatic in 104 of 112 measured styles and colour 8 (White) in 102 of 109,
//     and TintPixel's hue rotation and chroma scale are both mathematical no-ops on
//     an S=0 pixel -- so a black-eyed player would see their whole face outline
//     brighten under the Value slider and their eyes never change.
//
// Thresholding the worn canvas cannot separate the iris either: those Black irises
// sit at S=0.000 exactly while 4-bit quantisation artefacts in the sclera reach
// S=1.000, so a saturation rule fails in both directions simultaneously.
//
// WHAT WORKS is to ask the artists. Eye colour is the HUNDREDS DIGIT of the face id,
// so the same style in another colour is one arithmetic step away, and DIFFING those
// siblings yields exactly the pixels an eye-colour change is allowed to touch -- for
// 00020000.img/default/face that is 10 pixels out of 416, the same 10 for every
// colour pair. Then the masked pixels are REPLACED from a chromatic reference and
// rotated from there, which is what makes the slider mean the same thing whatever
// colour the player happens to be wearing.
//
// Measured against a 7-variant ground truth: 1.5% of true iris pixels missed, 1.0%
// spurious (and those are achromatic, so hue and chroma are no-ops on them anyway),
// 91 of 117 canvases pixel-exact. On smile/angry the mask comes out empty, so those
// canvases are never cloned and the red mouth is never touched.
// The face family's hundreds digit is the native eye-colour variant. Keep the
// style suffix intact and compare three chromatic siblings within that family.
constexpr int kFaceRefOffsets[3] = { 200, 400, 600 };     // Red, Brown, Purple
constexpr int kIrisChannelDelta  = 34;    // 2 nibble steps in ARGB4444, the face format
constexpr float kIrisMinSat      = 0.20f;
constexpr float kIrisMinVal      = 0.15f;

// The only expression canvases that carry eye colour. Everything else (smile, angry,
// cry, love, every closed-eye pose) is byte-identical across all nine colour variants,
// so walking them would be pure cost -- 5 canvases instead of ~35.
const wchar_t* const kFaceIrisPaths[] = {
    L"default/face", L"blink/0/face", L"blink/2/face", L"wink/0/face", L"hit/0/face",
};

// A face style's iris mask plus the reference pixels the tint is generated FROM.
// Independent of the tint, so it is computed once per face id and survives every
// slider drag; the clone cache above it is what the tint invalidates.
struct FaceIrisPlan {
    int w = 0, h = 0;
    std::vector<uint8_t>  mask;   // 1 = iris
    std::vector<uint32_t> ref;    // the Red variant's pixels, ARGB
    bool Empty() const { return mask.empty(); }
};
std::unordered_map<int, std::vector<FaceIrisPlan>> g_facePlans;   // faceId -> per path

// Read any canvas into ARGB by cloning it first. Reading the ResMan-owned original
// through a lock would mean holding a lock on a process-global node; CopyEx into a
// scratch canvas is the path CloneTinted already proves out.
bool ReadCanvasPixels(IWzCanvasPtr pSrc, int& w, int& h, std::vector<uint32_t>& out) {
    if (!pSrc) return false;
    try {
        w = static_cast<int>(pSrc->width);
        h = static_cast<int>(pSrc->height);
        if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return false;
        IWzCanvasPtr tmp;
        PcCreateObject<IWzCanvasPtr>(L"Canvas", tmp, nullptr);
        if (!tmp) return false;
        tmp->Create(w, h, 0, CP_A8R8G8B8);
        tmp->CopyEx(0, 0, pSrc, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0);
        out.assign(static_cast<size_t>(w) * h, 0);
        if (!ReadPixelsLocked(tmp, w, h, out.data())) ReadPixelsSlow(tmp, w, h, out.data());
    } catch (...) {
        return false;
    }
    return true;
}

IWzPropertyPtr FaceImg(int faceId) {
    try {
        wchar_t path[80];
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"Character/Face/%08d.img", faceId);
        return IWzPropertyPtr(get_rm()->GetObjectA(path, vtEmpty, vtEmpty).GetUnknown());
    } catch (...) {
        return nullptr;
    }
}

// Walk a slash-separated node path. UOL nodes resolve natively on the way, which
// matters here: several imported face styles reach an expression through a uol
// rather than holding its canvas directly.
IWzPropertyPtr PropertyAtPath(IWzPropertyPtr pImg, const std::wstring& path) {
    if (!pImg) return nullptr;
    try {
        IWzPropertyPtr cur = pImg;
        size_t at = 0;
        while (at <= path.size()) {
            const size_t slash = path.find(L'/', at);
            const std::wstring seg =
                path.substr(at, slash == std::wstring::npos ? std::wstring::npos : slash - at);
            if (!seg.empty()) {
                IUnknownPtr pUnk = cur->item[seg.c_str()].GetUnknown();
                if (!pUnk) return nullptr;
                IWzPropertyPtr nxt;
                if (FAILED(pUnk.QueryInterface(__uuidof(IWzProperty), &nxt)) || !nxt)
                    return nullptr;
                cur = nxt;
            }
            if (slash == std::wstring::npos) break;
            at = slash + 1;
        }
        return cur;
    } catch (...) {
        return nullptr;
    }
}

// The canvas at a path, or null. Canvas is checked FIRST because a canvas also
// answers QueryInterface(IWzProperty) -- the same ordering trap SwapSubtree calls out.
IWzCanvasPtr CanvasAtPath(IWzPropertyPtr pImg, const wchar_t* path) {
    if (!pImg) return nullptr;
    try {
        const std::wstring full(path);
        const size_t cut = full.find_last_of(L'/');
        IWzPropertyPtr pParent = (cut == std::wstring::npos)
            ? pImg : PropertyAtPath(pImg, full.substr(0, cut));
        if (!pParent) return nullptr;
        IUnknownPtr pUnk = pParent->item[full.substr(cut + 1).c_str()].GetUnknown();
        if (!pUnk) return nullptr;
        IWzCanvasPtr cv;
        if (FAILED(pUnk.QueryInterface(__uuidof(IWzCanvas), &cv))) return nullptr;
        return cv;
    } catch (...) {
        return nullptr;
    }
}

// max |a-b| across A, R, G and B.
inline int ChannelDelta(uint32_t a, uint32_t b) {
    int d = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const int x = std::abs(static_cast<int>((a >> sh) & 0xFF) -
                               static_cast<int>((b >> sh) & 0xFF));
        if (x > d) d = x;
    }
    return d;
}

inline bool IsChromatic(uint32_t argb) {
    if (((argb >> 24) & 0xFF) == 0) return false;
    float h, s, v;
    RgbToHsv((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, h, s, v);
    return s >= kIrisMinSat && v >= kIrisMinVal;
}

const std::vector<FaceIrisPlan>& FacePlansFor(int faceId) {
    auto it = g_facePlans.find(faceId);
    if (it != g_facePlans.end()) return it->second;

    std::vector<FaceIrisPlan> plans(_countof(kFaceIrisPaths));
    const int base = faceId - 100 * ((faceId / 100) % 10);

    IWzPropertyPtr pWornImg = FaceImg(faceId);
    IWzPropertyPtr pRefImg[3];
    for (int r = 0; r < 3; ++r) pRefImg[r] = FaceImg(base + kFaceRefOffsets[r]);

    if (pWornImg && pRefImg[0] && pRefImg[1] && pRefImg[2]) {
        for (size_t i = 0; i < _countof(kFaceIrisPaths); ++i) {
            const wchar_t* path = kFaceIrisPaths[i];
            int w = 0, h = 0;
            std::vector<uint32_t> worn, ref[3];
            if (!ReadCanvasPixels(CanvasAtPath(pWornImg, path), w, h, worn)) continue;

            bool ok = true;
            for (int r = 0; r < 3 && ok; ++r) {
                int rw = 0, rh = 0;
                // Size equality is REQUIRED, not nice to have: 7 of 193 vanilla styles
                // ship a variant at a different size (and a different map/brow), and
                // indexing across those would scramble the mask. Skipping costs almost
                // nothing and cannot produce a wrong pixel.
                ok = ReadCanvasPixels(CanvasAtPath(pRefImg[r], path), rw, rh, ref[r]) &&
                     rw == w && rh == h;
            }
            if (!ok) continue;

            const size_t n = worn.size();
            std::vector<uint8_t> mask(n, 0);
            size_t hits = 0;
            for (size_t k = 0; k < n; ++k) {
                // Parenthesised: <windows.h> defines max() as a macro and would eat these.
                const int d = (std::max)(ChannelDelta(ref[0][k], ref[1][k]),
                              (std::max)(ChannelDelta(ref[0][k], ref[2][k]),
                                         ChannelDelta(ref[1][k], ref[2][k])));
                if (d < kIrisChannelDelta) continue;
                if (!IsChromatic(ref[0][k]) && !IsChromatic(ref[1][k]) && !IsChromatic(ref[2][k]))
                    continue;
                mask[k] = 1;
                ++hits;
            }
            if (!hits) continue;                 // nothing varies: not an iris canvas
            plans[i].w = w;
            plans[i].h = h;
            plans[i].mask.swap(mask);
            plans[i].ref = ref[0];               // Red: the colour the slider rotates FROM
        }
    }

    // ALL OR NOTHING, keyed on default/face. A partial plan is worse than none: if the
    // resting canvas is skipped but blink/wink/hit are not, the eyes sit their original
    // colour and then FLASH the tint mid-blink. That is not hypothetical -- face 20100
    // (style 00, colour 1) ships its default/face at 26x17 while its colour siblings are
    // 26x16, so the size guard drops exactly that one canvas and keeps the other four.
    //
    // A canvas whose own mask is legitimately empty is a different case and is left
    // alone: those are frames with the eyes CLOSED, which carry no iris to tint, so
    // nothing flickers.
    if (plans[0].Empty()) {
        for (FaceIrisPlan& p : plans) {
            p.mask.clear();
            p.ref.clear();
            p.w = p.h = 0;
        }
    }

    bool anyPlan = false;
    for (const FaceIrisPlan& p : plans) anyPlan = anyPlan || !p.Empty();
    if (!anyPlan) {
        // ~6% of installed face ids: an imported family duplicated one file into
        // several colour slots, so the siblings are byte-identical and there is
        // nothing to diff. Tinting the whole canvas instead is NOT an acceptable
        // fallback (see the header of this section), so the eye tint is simply a
        // no-op for those styles and says so once.
        LOG_ONCE("weapontint: face %d has no usable colour siblings; eye tint is "
                 "inert for this style", faceId);
    }
    return g_facePlans.emplace(faceId, std::move(plans)).first->second;
}

// Clone `pSrc` with only the masked pixels rewritten, sourced from the plan's Red
// reference and then rotated. Everything outside the mask keeps the WORN variant's
// own bytes -- which is what stops a style whose Black variant has brown eyebrows
// (00020020.img: 38 px, worst channel delta 204) from having them turned grey.
IWzCanvasPtr CloneIrisTinted(IWzCanvasPtr pSrc, const FaceIrisPlan& plan,
                             const WeaponTint& t) {
    if (!pSrc || plan.Empty()) return nullptr;
    IWzCanvasPtr dst;
    try {
        const int w = static_cast<int>(pSrc->width);
        const int h = static_cast<int>(pSrc->height);
        if (w != plan.w || h != plan.h) return nullptr;

        PcCreateObject<IWzCanvasPtr>(L"Canvas", dst, nullptr);
        if (!dst) return nullptr;
        dst->Create(w, h, 0, CP_A8R8G8B8);
        dst->CopyEx(0, 0, pSrc, CANVAS_ALPHATYPE::CA_OVERWRITE, 0, 0, 0, 0, 0, 0);
        if (!CopyCanvasProperty(pSrc, dst)) return nullptr;

        std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0);
        if (!ReadPixelsLocked(dst, w, h, px.data())) ReadPixelsSlow(dst, w, h, px.data());

        for (size_t k = 0; k < px.size() && k < plan.mask.size(); ++k) {
            if (!plan.mask[k]) continue;
            uint32_t v = plan.ref[k];
            TintPixel(v, t);
            px[k] = v;
        }
        WriteRuns(dst, w, h, px.data());
        dst->cx = pSrc->cx;
        dst->cy = pSrc->cy;
    } catch (...) {
        return nullptr;
    }
    return dst;
}

IWzCanvasPtr GetIrisTinted(IWzCanvasPtr pSrc, const FaceIrisPlan& plan, const WeaponTint& t) {
    if (!pSrc) return nullptr;
    if (!g_irisClonesValid || g_irisClonesKey != t.Key()) {
        g_irisClones.clear();
        g_irisClonesKey = t.Key();
        g_irisClonesValid = true;
    }
    IWzCanvas* raw = pSrc;
    auto it = g_irisClones.find(raw);
    if (it != g_irisClones.end()) return it->second;
    IWzCanvasPtr clone = CloneIrisTinted(pSrc, plan, t);
    g_irisClones.emplace(raw, clone);    // cache failures too, so they are not retried per frame
    return clone;
}

// Swap the iris-bearing canvases of the worn face for masked clones.
void SwapInFaceTintInto(int faceId, const WeaponTint& t, std::vector<Swap>& swaps) {
    if (faceId <= 0) return;
    const std::vector<FaceIrisPlan>& plans = FacePlansFor(faceId);
    IWzPropertyPtr pImg = FaceImg(faceId);
    if (!pImg) return;
    int swapped = 0;
    try {
        for (size_t i = 0; i < _countof(kFaceIrisPaths) && i < plans.size(); ++i) {
            if (plans[i].Empty()) continue;
            // Split the path so the PARENT can be recorded for the swap-back.
            std::wstring full = kFaceIrisPaths[i];
            const size_t cut = full.find_last_of(L'/');
            const std::wstring leaf = full.substr(cut + 1);
            IWzPropertyPtr pParent = (cut == std::wstring::npos)
                ? pImg : PropertyAtPath(pImg, full.substr(0, cut));
            if (!pParent) continue;

            IWzCanvasPtr pOrig;
            IUnknownPtr pUnk = pParent->item[leaf.c_str()].GetUnknown();
            if (!pUnk || FAILED(pUnk.QueryInterface(__uuidof(IWzCanvas), &pOrig)) || !pOrig)
                continue;

            IWzCanvasPtr pTinted = GetIrisTinted(pOrig, plans[i], t);
            if (!pTinted) continue;
            swaps.push_back(Swap{ pParent, leaf, pOrig });
            pParent->item[leaf.c_str()] = static_cast<IUnknown*>(pTinted);
            ++swapped;
        }
    } catch (...) {
    }
    LOG_ONCE_PER_ID(faceId,
        "weapontint: face %d iris path: %d canvases swapped (h=%d c=%d v=%d)",
        faceId, swapped, t.hue, static_cast<int>(t.chroma), static_cast<int>(t.bright));
}

void SwapInFaceTint(int faceId, const WeaponTint& t) {
    SwapInFaceTintInto(faceId, t, g_swaps);
}

// The face assembler keeps a direct reference to its source IWzCanvas.  Replacing
// the canvas in the parent property (which is enough for equip layers) therefore
// does not affect a face frame once that reference has been cached.  For the narrow
// native face-build call, repaint that exact source canvas instead, then put every
// changed pixel back before any other avatar can render from it.
struct FaceCanvasPatch {
    IWzCanvasPtr canvas;
    int w = 0, h = 0;
    std::vector<uint32_t> original;
};

void RestoreFacePatches(std::vector<FaceCanvasPatch>& patches) {
    for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
        if (it->canvas && it->w > 0 && it->h > 0 && !it->original.empty())
            WriteRuns(it->canvas, it->w, it->h, it->original.data());
    }
    patches.clear();
}

void PaintFaceIrisForBuild(int faceId, const WeaponTint& t,
                           std::vector<FaceCanvasPatch>& patches) {
    if (faceId <= 0) return;
    const std::vector<FaceIrisPlan>& plans = FacePlansFor(faceId);
    IWzPropertyPtr image = FaceImg(faceId);
    if (!image) return;
    for (size_t i = 0; i < plans.size() && i < _countof(kFaceIrisPaths); ++i) {
        const FaceIrisPlan& plan = plans[i];
        if (plan.Empty()) continue;
        IWzCanvasPtr canvas = CanvasAtPath(image, kFaceIrisPaths[i]);
        int w = 0, h = 0;
        std::vector<uint32_t> before;
        if (!ReadCanvasPixels(canvas, w, h, before) || w != plan.w || h != plan.h)
            continue;
        std::vector<uint32_t> after = before;
        bool changed = false;
        for (size_t px = 0; px < after.size() && px < plan.mask.size(); ++px) {
            if (!plan.mask[px]) continue;
            uint32_t tintPixel = plan.ref[px];
            TintPixel(tintPixel, t);
            if (after[px] != tintPixel) {
                after[px] = tintPixel;
                changed = true;
            }
        }
        if (!changed || WriteRuns(canvas, w, h, after.data()) <= 0) continue;
        FaceCanvasPatch patch;
        patch.canvas = canvas;
        patch.w = w; patch.h = h;
        patch.original.swap(before);
        patches.push_back(std::move(patch));
    }
}

struct FacePatchGuard {
    std::vector<FaceCanvasPatch> patches;
    ~FacePatchGuard() { RestoreFacePatches(patches); }
};

// Face.img has eight artist-authored eye colours per style.  The face assembler
// caches its original canvas before a WZ-node replacement can reach it, but it does
// honour the face id it is handed.  Use the closest native colour variant while the
// layer is built; this gives the rendered layer a genuinely different source rather
// than trying to edit the already-cached source canvas.
int NativeFaceForTint(int faceId, const WeaponTint& tint) {
    const int base = faceId - 100 * ((faceId / 100) % 10);
    int variant = 2; // red / pink, the reference hue for the Prism's hue slider
    const int h = tint.hue;
    if (tint.bright <= -70 || tint.chroma <= -75) {
        variant = 0; // black
    } else if (h < 30 || h >= 330) {
        variant = 2; // red / pink
    } else if (h < 80) {
        variant = 4; // brown / amber
    } else if (h < 155) {
        variant = 3; // green
    } else if (h < 210) {
        variant = 5; // aqua
    } else if (h < 265) {
        variant = 1; // blue
    } else if (h < 320) {
        variant = 6; // purple
    } else {
        variant = 7; // magenta
    }
    const int candidate = base + variant * 100;
    return FaceImg(candidate) ? candidate : faceId;
}

struct FaceIdGuard {
    CAvatar* avatar = nullptr;
    int original = 0;
    FaceIdGuard(CAvatar* a, int replacement) : avatar(a), original(a->m_avatarLook.nFace) {
        a->m_avatarLook.nFace = replacement;
    }
    ~FaceIdGuard() { if (avatar) avatar->m_avatarLook.nFace = original; }
};

// Hair: the three real canvases, reached through `default` and `backDefault`. The
// per-action frames are UOLs to default/hair, so no action walking is needed.
void SwapInHairTint(int hairId, const WeaponTint& t) {
    if (hairId <= 0) return;
    try {
        wchar_t path[80];
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"Character/Hair/%08d.img", hairId);
        IWzPropertyPtr pImg = get_rm()->GetObjectA(path, vtEmpty, vtEmpty).GetUnknown();
        if (!pImg) return;
        for (const wchar_t* n : { L"default", L"backDefault" }) {
            IWzPropertyPtr p = pImg->item[n].GetUnknown();
            if (p) SwapSubtree(p, t, 0, Layer::Body);
        }
    } catch (...) {
    }
}

void SwapOutList(std::vector<Swap>& swaps) {
    for (auto it = swaps.rbegin(); it != swaps.rend(); ++it) {
        try {
            if (it->parent && it->original) {
                it->parent->item[it->name.c_str()] = static_cast<IUnknown*>(it->original);
            }
        } catch (...) {
        }
    }
    swaps.clear();
}

void SwapOut() {
    SwapOutList(g_swaps);
}

// =====================================================
// WHOSE AVATAR IS THIS
// =====================================================
CAvatar* LocalAvatar() {
    auto* pUser = *reinterpret_cast<unsigned char**>(kAddr_CUserLocal_Instance);
    if (!pUser) return nullptr;
    return reinterpret_cast<CAvatar*>(pUser + kOff_AvatarInUser);
}

// Character name of the CUser an avatar belongs to, or "" .
//
// SAFETY: not every CAvatar has a CUser in front of it -- the prism window builds
// its own, and character select has others -- so `pAvatar - 0x88` can be arbitrary
// memory. Every read is SEH-guarded and the result must look like a MapleStory name
// (printable ASCII, 4..13 chars) to be returned at all. A junk read therefore
// yields "", which matches no entry in the tint table and simply means "no tint" --
// never a wrong colour.
bool NameOfAvatar(CAvatar* pAvatar, char* out, size_t cap) {
    if (!pAvatar || cap < 16) return false;
    out[0] = '\0';
    int len = 0;
    __try {
        auto* pUser = reinterpret_cast<unsigned char*>(pAvatar) - kOff_AvatarInUser;
        const char* p = *reinterpret_cast<char* const*>(pUser + kOff_CharacterNameInUser);
        if (reinterpret_cast<uintptr_t>(p) < 0x10000) return false;
        while (len < 13) {
            const char c = p[len];
            if (c == '\0') break;
            if (c < 0x20 || c > 0x7E) return false;
            out[len++] = c;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (len < 4) return false;      // shortest legal MapleStory name
    out[len] = '\0';
    return true;
}

// The worn Cash-weapon id off an avatar's look, filtered by the range the client
// actually honours: the equip-slot loop gates the sticker on `id / 100000 == 17`
// at 0x004129BE, so an id outside 17xxxxx never reaches Character/Weapon/.
//
// Only the WINDOW uses this, as the default target until an item is dropped on it;
// the render path takes every worn item from WornItemsOf instead.
//
// SEH leaf: `pAvatar` may not be an avatar at all.
int CashWeaponOf(CAvatar* pAvatar) {
    if (!pAvatar) return 0;
    int id = 0;
    __try { id = pAvatar->m_avatarLook.nWeaponStickerID; }
    __except (EXCEPTION_EXECUTE_HANDLER) { id = 0; }
    return (id > 0 && id / 100000 == 17) ? id : 0;
}

// Which tints apply to `pAvatar`, resolved per ITEM.
//
//   * forced scope  -- a preview avatar this DLL built
//   * local avatar  -- the player's own
//   * anything else -- looked up by character name in the map table
// In the first two cases the prism window's live slider values override the stored
// tint for the ONE item being dyed, so the rest of the outfit keeps its own colours
// while you drag.
struct TintScope {
    const TintMap* stored = nullptr;
    const TintMap* forced = nullptr;
    bool  preview = false;      // apply the window's live values on top
    bool  any = false;

    bool Lookup(int itemId, WeaponTint& out) const {
        if (preview && itemId == g_previewItemId) { out = g_preview; return true; }
        // A saved salon look may intentionally contain the identity tint. Check this
        // before the player's normal table so a neutral saved look does not inherit
        // their currently worn hair/eye colour in its thumbnail.
        if (forced) {
            auto it = forced->find(itemId);
            if (it != forced->end()) { out = it->second; return true; }
        }
        if (!stored) return false;
        auto it = stored->find(itemId);
        if (it == stored->end()) return false;
        out = it->second;
        return true;
    }
};

TintScope ScopeForAvatar(CAvatar* pAvatar) {
    TintScope s;
    const bool isForced = (g_forcedScope > 0
            && (g_forcedAvatar == nullptr || pAvatar == g_forcedAvatar));
    const bool isLocal  = pAvatar == LocalAvatar();
    if (isLocal) {
        std::lock_guard<std::mutex> lock(g_mtx);
        s.stored = &g_saved;
        // THE LIVE PREVIEW IS SCOPED TO THE WINDOW'S OWN AVATAR. Only an avatar this DLL
        // built inside a forced scope sees the slider values; the character standing in
        // the world keeps its SAVED colours until Confirm. Dragging a slider used to
        // recolour both at once, which made it impossible to compare a candidate colour
        // against what you are actually wearing -- and left the world character wearing a
        // colour that was never bought if the window was then cancelled.
        s.any = !g_saved.empty();
        return s;
    }

    if (pAvatar != nullptr && pAvatar == g_prismPreviewAvatar) {
        std::lock_guard<std::mutex> lock(g_mtx);
        s.stored = &g_saved;
        s.preview = g_previewActive;
        s.any = s.preview || !g_saved.empty();
        return s;
    }

    if (isForced) {
        std::lock_guard<std::mutex> lock(g_mtx);
        s.stored = &g_saved;
        s.forced = &g_forcedTints;
        // An unbound legacy scope belongs to another preview window and must not
        // consume the Coloring Prism's live slider state.
        s.preview = g_forcedAvatar != nullptr && g_previewActive;
        s.any = s.preview || !g_forcedTints.empty() || !g_saved.empty();
        return s;
    }

    char name[16];
    if (!NameOfAvatar(pAvatar, name, sizeof(name))) return s;

    std::lock_guard<std::mutex> lock(g_mtx);
    // Remember which avatar wears this name so a later tint change can repaint it
    // without a user-pool walk (this repo has no way to enumerate CUsers).
    g_avatarByName[name] = pAvatar;
    auto it = g_remoteTints.find(name);
    if (it == g_remoteTints.end() || it->second.empty()) return s;
    s.stored = &it->second;
    s.any = true;
    return s;
}

// Puts the WZ tree back however the guarded scope exits -- including through a
// C++ exception thrown out of the client's own COM wrappers. Leaving a tinted
// clone behind in a ResMan-owned node would be a permanent, process-global
// recolor of that weapon for every character and every tooltip.
struct SwapGuard {
    ~SwapGuard() { SwapOut(); }
};

// =====================================================
// THE HOOK
// =====================================================
using t_PrepareActionLayer = void(__thiscall*)(void*, int, int, int);
auto PrepareActionLayer_Orig =
    reinterpret_cast<t_PrepareActionLayer>(kAddr_CAvatar_PrepareActionLayer);

// Force the avatar to rebuild its action layers so a tint change shows NOW rather
// than at the next action change.
//
// THE CACHE IS THE WHOLE PROBLEM. PrepareActionLayer keeps the built frames per
// action and refuses to build them twice -- byte-verified on its normal path at
// 0x004545C3:
//     mov eax,[ebp-0x14]   ; layerSet + action*4 + 0x10, the cached frame array
//     mov eax,[eax]
//     cmp eax,esi
//     je  0x004545D5       ;   empty -> fall through and build
//     cmp [eax-4],esi
//     jne 0x00454660       ;   non-empty -> SKIP CActionMan::BuildAvatarActionFrames
// so once the player has stood in a given action once, asking for a reload reads
// nothing out of the WZ tree and the swapped-in tinted canvases are never seen.
// That is why a tint used to appear only after unequipping and re-equipping the
// weapon (which clears the layers), and why the prism's own preview avatar tinted
// correctly all along -- it is a fresh CAvatar whose caches are empty.
//
// So the cache is dropped first, for BOTH 0x5DC layer sets, and only then is the
// reload issued.
//
// CAvatar::SetMoveAction(nMA, bReload) is the client's own reload entry, and
// bReload is exactly the flag for this. Byte-verified at 0x004520F1:
//     cmp [esp+8],0        ; bReload
//     mov eax,[esp+4]      ; nMA
//     jne 0x0045210B       ; bReload != 0 -> SKIP the early-out below
//     cmp eax,[esi+0x4E8]  ; nMA == m_nMoveAction ?
//     je  0x00452191       ;   -> return, nothing to do
//     ...
//     push 0 / push 0x64 / push 6 / call [eax+0x14]   ; PrepareActionLayer(6,100,0)
// so passing the CURRENT move action with bReload=1 rebuilds without changing the
// animation.
//
// The previous attempt called CUser::OnAvatarModified instead. That does nothing
// here: the tint is not part of AvatarLook, so the look is byte-identical to the
// copy the client keeps at CAvatar+0x1C9 and it correctly concludes there is
// nothing to rebuild. Unequip/reequip DID repaint because that genuinely changes
// the look -- which is precisely why the bug looked like a refresh problem rather
// than a render one.
//
// CAvatar::SetMoveAction is called directly rather than through vtable slot 4, so
// CUser's override does not run: that one also reports the move action onward, and
// a purely cosmetic repaint has no business generating traffic.
void SehReloadActionLayer(CAvatar* pAvatar) {
    __try {
        auto* p = reinterpret_cast<unsigned char*>(pAvatar);
        const int nMA = *reinterpret_cast<int*>(p + kOff_MoveAction);
        auto ClearActionLayer =
            reinterpret_cast<void(__thiscall*)(void*, int)>(kAddr_CAvatar_ClearActionLayer);
        ClearActionLayer(pAvatar, 0);
        ClearActionLayer(pAvatar, 1);
        reinterpret_cast<void(__thiscall*)(void*, int, int)>(kAddr_CAvatar_SetMoveAction)(
            pAvatar, nMA, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// SEH leaf (C2712: __try may not share a function with objects that unwind).
int CurrentActionOf(CAvatar* pAvatar) {
    int action = -1;
    __try { action = pAvatar->GetCurrentAction(nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { action = -1; }
    return action;
}

// Every item this avatar is currently SHOWING, into `out`. anHairEquip already
// resolves cash-over-normal per slot, so it is the visible item in every case
// except the weapon -- slot 11 is deliberately excluded from that override and the
// Cash weapon lives separately in nWeaponStickerID (verified in the look builder at
// 0x004E7358). Both are collected here, so a player wearing a Cash hat over a
// normal one contributes only the hat that is actually drawn.
//
// SEH leaf: raw reads through a pointer that may not be an avatar at all.
int WornItemsOf(CAvatar* pAvatar, int* out, int cap) {
    int n = 0;
    __try {
        const int sticker = pAvatar->m_avatarLook.nWeaponStickerID;
        if (sticker > 0 && sticker / 100000 == 17 && n < cap) out[n++] = sticker;
        for (int slot = 0; slot < kAvatarEquipSlots && n < cap; ++slot) {
            const int id = pAvatar->m_avatarLook.anHairEquip[slot];
            if (id > 0) out[n++] = id;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

// Hair and face straight off an avatar's look. Hair is anHairEquip[0] (equip slot 0,
// the same 0x19 coloringprism.cpp's kAL_HairEquip0 names), face is the standalone
// nFace at +0x11.
//
// SEH leaf: raw reads through a pointer that may not be an avatar at all, so a junk
// read must yield 0/0 -- "no look tint" -- rather than fault mid-render.
bool SehLookIdsOf(CAvatar* pAvatar, int& hairId, int& faceId) {
    hairId = 0;
    faceId = 0;
    __try {
        hairId = pAvatar->m_avatarLook.anHairEquip[0];
        faceId = pAvatar->m_avatarLook.nFace;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hairId = 0;
        faceId = 0;
        return false;
    }
    return true;
}

namespace {

struct HairSwayScope {
    bool active;
    ~HairSwayScope() {
        if (active) {
            HairSway_EndLayer();
        }
    }
};

void CallPrepareActionLayerWithHairSway(void* pThis, CAvatar* pAvatar, int hairId,
                                        int nActionSpeed, int nWalkSpeed, int bKeyDown) {
    const bool bSway = hairId && HairSway_Applies(pAvatar, hairId);
    HairSwayScope hairSwayScope{bSway};
    if (bSway) {
        HairSway_BeginLayer(pAvatar, hairId);
    }
    PrepareActionLayer_Orig(pThis, nActionSpeed, nWalkSpeed, bKeyDown);
}

} // namespace

void __fastcall PrepareActionLayer_Hook(void* pThis, void* /*edx*/,
                                        int nActionSpeed, int nWalkSpeed, int bKeyDown) {
    auto* pAvatar = reinterpret_cast<CAvatar*>(pThis);

    int hairId = 0, faceId = 0;
    SehLookIdsOf(pAvatar, hairId, faceId);
    const TintScope scope = ScopeForAvatar(pAvatar);
    if (!scope.any) {
        CallPrepareActionLayerWithHairSway(pThis, pAvatar, hairId, nActionSpeed, nWalkSpeed, bKeyDown);
        return;
    }
    const int action = CurrentActionOf(pAvatar);
    if (action < 0) {
        CallPrepareActionLayerWithHairSway(pThis, pAvatar, hairId, nActionSpeed, nWalkSpeed, bKeyDown);
        return;
    }

    int items[kAvatarEquipSlots + 1];
    const int count = WornItemsOf(pAvatar, items, _countof(items));

    // The hair and face ids come off THIS avatar's own look rather than from the
    // server, because for a remote player that look is the only correct source --
    // the table only says what colour to apply, never to what.
    Ztl_bstr_t sAction;
    get_action_name_from_code(&sAction, action);

    {
        SwapGuard guard;                       // restores the tree on ANY exit path
        for (int i = 0; i < count; ++i) {
            // anHairEquip[0] IS the hair id, so it arrives here as a worn "item" too.
            // It must not go through the equip path: that classifies by id, and the
            // look tints are deliberately keyed by kind instead.
            if (items[i] == hairId) continue;
            WeaponTint t;
            if (scope.Lookup(items[i], t) && !t.IsIdentity())
                SwapInTintFor(items[i], sAction.GetBSTR(), t, Layer::Body);
            // An item's effect layers carry their OWN tint under the biased key, so a
            // glow can be recoloured independently of the item it hangs off -- which is
            // the whole point of the window's Effects tab.
            if (scope.Lookup(EffectTintKeyFor(items[i]), t) && !t.IsIdentity())
                SwapInTintFor(items[i], sAction.GetBSTR(), t, Layer::Effects);
        }
        WeaponTint tLook;
        if (hairId > 0 && scope.Lookup(kTintKey_Hair, tLook) && !tLook.IsIdentity())
            SwapInHairTint(hairId, tLook);
        // Face frames normally arrive through BuildFaceLayer, but the v83 client also
        // resolves them while preparing an avatar's action layers on some construction
        // paths (notably the stand preview used by Coloring Prism).  Keep the iris
        // canvases swapped for this whole build too.  The scoped SwapGuard restores the
        // shared WZ nodes before any other avatar can observe them.
        if (faceId > 0 && scope.Lookup(kTintKey_Face, tLook) && !tLook.IsIdentity())
            SwapInFaceTint(faceId, tLook);
        CallPrepareActionLayerWithHairSway(pThis, pAvatar, hairId, nActionSpeed, nWalkSpeed, bKeyDown);
    }
}

// =====================================================
// THE FACE HOOK
// =====================================================
// The eye tint used to do nothing, and this is why: the face is NOT built inside
// CAvatar::PrepareActionLayer. It has its own builder, 0x00453696, reached from
// CAvatar::SetEmotion at 0x00451D82 -- byte-verified by the two arguments it forwards,
//     00453704  push dword ptr [edi + 0x15]     ; AvatarLook.nFace  (look sits at +4)
//     00453707  push dword ptr [edi + 0x11]     ; AvatarLook.nSkin
//     0045370A  call 0x407a36
// so the swap window around PrepareActionLayer had already closed (or never opened) by
// the time anything read a face canvas. The tinted clones were built and thrown away.
//
// Hooking it is safe: nothing else in this repo owns 0x00453696, and it is a plain
// __thiscall void(int) (`ret 4` at 0x00453A26) whose ecx is the CAvatar.
//
// This also gets the refresh for free. The builder runs on every emotion change and on
// every blink (CAvatar::RegisterNextBlink @0x00453AA2 schedules them), so a face tint
// appears on its own within a second or two even without an explicit repaint -- and
// WeaponTint_RefreshLocalAvatar calls straight into this hook to make it immediate.
using t_BuildFaceLayer = void(__thiscall*)(void*, int);
auto BuildFaceLayer_Orig =
    reinterpret_cast<t_BuildFaceLayer>(kAddr_CAvatar_BuildFaceLayer);

void __fastcall BuildFaceLayer_Hook(void* pThis, void* /*edx*/, int nDuration) {
    auto* pAvatar = reinterpret_cast<CAvatar*>(pThis);
    const TintScope scope = ScopeForAvatar(pAvatar);
    if (scope.any) {
        int hairId = 0, faceId = 0;
        SehLookIdsOf(pAvatar, hairId, faceId);
        WeaponTint t;
        if (faceId > 0 && scope.Lookup(kTintKey_Face, t) && !t.IsIdentity()) {
            LOG_ONCE_PER_ID(faceId,
                "weapontint: BuildFaceLayer sees tinted face %d (h=%d c=%d v=%d)",
                faceId, t.hue, static_cast<int>(t.chroma), static_cast<int>(t.bright));
            const int nativeFace = NativeFaceForTint(faceId, t);
            FaceIdGuard faceIdGuard(pAvatar, nativeFace);
            BuildFaceLayer_Orig(pThis, nDuration);
            return;
        }
    }
    BuildFaceLayer_Orig(pThis, nDuration);
}

// =====================================================
// NETWORKING
// =====================================================
void Send(const COutPacket& o) {
    void* sock = *reinterpret_cast<void**>(kAddr_ClientSocket_Instance);
    if (!sock) return;
    reinterpret_cast<void(__thiscall*)(void*, const COutPacket&)>(kAddr_ClientSocket_Send)(sock, o);
}

// =====================================================
// ITEM-EFFECT LAYERS
// =====================================================
// A cape aura is not part of the avatar. An effect-layer renderer builds one
// IWzGr2DLayer per equipped item out of Effect/ItemEff.img and runs it with GA_REPEAT
// so it keeps its own Gr2D clock, which is the whole reason it is not an action layer: an
// avatar part is advanced by CAvatar::Update on the BODY's frame index, so its period
// would be forced to equal the current action's and a 12-frame aura would crawl when
// idle and race when walking.
//
// The consequence for tinting is that PrepareActionLayer never sees this art, so the
// hook above cannot reach it. This is the same swap-and-restore opened around that
// separate build, and it uses the item's EFFECT key: a cape's aura and a weapon's glow
// are one feature, one tab and one pair of database columns.
//
// NOT REENTRANT, and it does not need to be: the caller wraps one layer load, on the
// main thread, and such a renderer rebuilds only when the item, the action or the
// facing actually changed.
std::vector<Swap> g_itemEffSwaps;

bool BeginItemEffSwap(CAvatar* pAvatar, int itemId) {
    SwapOutList(g_itemEffSwaps);            // a previous build that never got closed
    if (!pAvatar || itemId <= 0) return false;
    const TintScope scope = ScopeForAvatar(pAvatar);
    if (!scope.any) return false;
    WeaponTint t;
    if (!scope.Lookup(EffectTintKeyFor(itemId), t) || t.IsIdentity()) return false;
    try {
        // THE WHOLE `effect` SUBTREE, not the one posture about to be loaded. 1013 of
        // the posture nodes in this img are UOLs into a sibling, so resolving the
        // posture path can land on an indirection with nothing under it and tint
        // nothing at all. Walking from `effect` down has no such hole and covers every
        // posture at once, which also means a turn or an action change reuses the same
        // swap. It is affordable precisely because these are small: a median of 24
        // canvases per entry and 121 at the very worst, against the few hundred an
        // item's own img would cost, and GetTinted caches every clone after the first.
        wchar_t path[64];
        _snwprintf_s(path, _countof(path), _TRUNCATE, L"Effect/ItemEff.img/%d/effect", itemId);
        IWzPropertyPtr pNode = get_rm()->GetObjectA(path, vtEmpty, vtEmpty).GetUnknown();
        if (!pNode) return false;
        // Everything below here IS effect art, so this is a Body walk: there is no
        // second half to keep apart, unlike an item's own img.
        SwapSinkTo sink(g_itemEffSwaps);
        SwapSubtree(pNode, t, 0, Layer::Body);
    } catch (...) {
        // Whatever landed in g_itemEffSwaps is still undone by the End call.
    }
    return !g_itemEffSwaps.empty();
}

} // namespace

// =====================================================
// PUBLIC API
// =====================================================
bool WeaponTint_BeginItemEffSwap(void* pAvatar, int itemId) {
    return BeginItemEffSwap(reinterpret_cast<CAvatar*>(pAvatar), itemId);
}

void WeaponTint_EndItemEffSwap() {
    SwapOutList(g_itemEffSwaps);
}

int WeaponTint_GetLocalCashWeaponId() {
    return CashWeaponOf(LocalAvatar());
}

int WeaponTint_GetLocalHairId() {
    int hair = 0, face = 0;
    SehLookIdsOf(LocalAvatar(), hair, face);
    return hair;
}

int WeaponTint_GetLocalFaceId() {
    int hair = 0, face = 0;
    SehLookIdsOf(LocalAvatar(), hair, face);
    return face;
}

WeaponTint WeaponTint_GetSavedFor(int itemId) {
    std::lock_guard<std::mutex> lock(g_mtx);
    auto it = g_saved.find(itemId);
    return (it == g_saved.end()) ? WeaponTint{} : it->second;
}

WeaponTint WeaponTint_GetEffectiveFor(int itemId) {
    if (g_previewActive && itemId == g_previewItemId) return g_preview;
    return WeaponTint_GetSavedFor(itemId);
}

bool WeaponTint_IsPreviewActive() { return g_previewActive; }

void WeaponTint_SetPreview(int itemId, const WeaponTint& t, bool active) {
    const WeaponTint before = WeaponTint_GetEffectiveFor(itemId);
    const int beforeId = g_previewItemId;
    g_preview = t;
    g_previewItemId = itemId;
    g_previewActive = active;
    if (beforeId != itemId || WeaponTint_GetEffectiveFor(itemId) != before) {
        // Drop the clones so the WINDOW's avatar rebuilds in the new colour. The world
        // avatar is deliberately NOT refreshed: the preview no longer reaches it (see
        // ScopeForAvatar), so repainting it would only cost a layer rebuild per slider step.
        ClearClones();
    }
}

void WeaponTint_AdoptOptimistic(int itemId, const WeaponTint& t) {
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        if (t.IsIdentity()) g_saved.erase(itemId);
        else                g_saved[itemId] = t;
    }
    // Confirm is the moment the world character is supposed to change, and now the only
    // one: the live preview never touched it. Repaint here rather than relying on the
    // window's teardown, whose before/after tints are equal by this point and so would
    // detect no change at all.
    ClearClones();
    WeaponTint_RefreshLocalAvatar();
}

void WeaponTint_BeginForcedScope() {
    ++g_forcedScope;
    g_forcedAvatar = nullptr;
    g_forcedTints.clear();
}

void WeaponTint_BeginForcedScope(void* pAvatar) {
    ++g_forcedScope;
    g_forcedAvatar = reinterpret_cast<CAvatar*>(pAvatar);
    g_forcedTints.clear();
}

void WeaponTint_BeginForcedScope(void* pAvatar, const WeaponTint& hairTint,
                                 const WeaponTint& faceTint) {
    WeaponTint_BeginForcedScope(pAvatar);
    // Keep both keys, including identity values. They must shadow the local
    // character's saved tint when this is a thumbnail render.
    g_forcedTints[kTintKey_Hair] = hairTint;
    g_forcedTints[kTintKey_Face] = faceTint;
}

void WeaponTint_EndForcedScope() {
    if (g_forcedScope <= 0) return;
    if (--g_forcedScope > 0) return;
    g_forcedScope = 0;
    g_forcedAvatar = nullptr;
    g_forcedTints.clear();
}

void WeaponTint_BindPreviewAvatar(void* pAvatar) {
    g_prismPreviewAvatar = reinterpret_cast<CAvatar*>(pAvatar);
}

void WeaponTint_UnbindPreviewAvatar(void* pAvatar) {
    if (g_prismPreviewAvatar == reinterpret_cast<CAvatar*>(pAvatar)) {
        SwapOutList(g_previewFaceSwaps);
        g_prismPreviewAvatar = nullptr;
    }
}

void WeaponTint_InstallPreviewFaceTint(void* pAvatar) {
    auto* avatar = reinterpret_cast<CAvatar*>(pAvatar);
    if (!avatar || avatar != g_prismPreviewAvatar) return;
    int hairId = 0, faceId = 0;
    if (!SehLookIdsOf(avatar, hairId, faceId) || faceId <= 0) return;
    const TintScope scope = ScopeForAvatar(avatar);
    WeaponTint tint;
    if (!scope.Lookup(kTintKey_Face, tint) || tint.IsIdentity()) return;
    // A new preview has no old retained nodes, but restore defensively in case a
    // failed build reused this allocation before its normal teardown ran.
    SwapOutList(g_previewFaceSwaps);
    SwapInFaceTintInto(faceId, tint, g_previewFaceSwaps);
}

void WeaponTint_RefreshLocalAvatar() {
    CAvatar* pAvatar = LocalAvatar();
    if (!pAvatar) return;
    SehReloadActionLayer(pAvatar);
    // +4E0 is the client's non-owning face-build sentinel.  Its constructor sets it
    // to null and BuildFaceLayer uses only null/non-null to decide whether to make a
    // new face layer.  Clear that sentinel before an explicit cosmetic refresh so a
    // confirmed eye tint does not wait for relogging to become visible.
    __try { *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(pAvatar) + 0x4E0) = nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    // The face sits outside the action layers, so the reload above does not touch it. Go
    // through the HOOK rather than the original, so the swap is applied; -1 is the duration
    // CAvatar::Init itself passes down this path.
    BuildFaceLayer_Hook(pAvatar, nullptr, -1);
}

WeaponTintResult WeaponTint_TakeLastResult() {
    std::lock_guard<std::mutex> lock(g_mtx);
    WeaponTintResult r = g_lastResult;
    g_lastResult = kTintResult_None;
    return r;
}

void WeaponTint_RequestSnapshot() {
    COutPacket o(kWeaponTintActionOpcode);
    o.Encode1(kAction_RequestSnapshot);
    Send(o);
}

void WeaponTint_SendApply(const WeaponTintTarget& target, const WeaponTint& t, int prismPos, int layer) {
    COutPacket o(kWeaponTintActionOpcode);
    o.Encode1(kAction_Apply);
    o.Encode1(static_cast<unsigned char>(target.invType));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(target.invPos)));
    o.Encode4(static_cast<unsigned int>(target.itemId));
    o.Encode2(static_cast<unsigned short>(t.hue));
    o.Encode1(static_cast<unsigned char>(t.chroma));
    o.Encode1(static_cast<unsigned char>(t.bright));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(prismPos)));
    o.Encode1(static_cast<unsigned char>(layer));
    Send(o);
}

void WeaponTint_SendRestore(const WeaponTintTarget& target, int prismPos, int layer) {
    COutPacket o(kWeaponTintActionOpcode);
    o.Encode1(kAction_Restore);
    o.Encode1(static_cast<unsigned char>(target.invType));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(target.invPos)));
    o.Encode4(static_cast<unsigned int>(target.itemId));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(prismPos)));
    o.Encode1(static_cast<unsigned char>(layer));
    Send(o);
}

// No target: the server reads the character's OWN hair/face, so there is nothing here
// for it to re-verify against and nothing the client could lie about.
void WeaponTint_SendApplyLook(int kind, const WeaponTint& t, int prismPos) {
    COutPacket o(kWeaponTintActionOpcode);
    o.Encode1(kAction_ApplyLook);
    o.Encode1(static_cast<unsigned char>(kind));
    o.Encode2(static_cast<unsigned short>(t.hue));
    o.Encode1(static_cast<unsigned char>(t.chroma));
    o.Encode1(static_cast<unsigned char>(t.bright));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(prismPos)));
    Send(o);
}

void WeaponTint_SendRestoreLook(int kind, int prismPos) {
    COutPacket o(kWeaponTintActionOpcode);
    o.Encode1(kAction_RestoreLook);
    o.Encode1(static_cast<unsigned char>(kind));
    o.Encode2(static_cast<unsigned short>(static_cast<short>(prismPos)));
    Send(o);
}

// Runs on the RECEIVE thread. Everything it writes is under g_mtx; the render
// side picks the change up on its next frame.
void WeaponTint_HandleSync(CompatInPacket* p) {
    if (!p) return;
    // The opcode sits at the packet's CURRENT offset, not necessarily 0.
    p->SetOffset(p->GetOffset() + 2);
    if (!p->CanRead(1)) return;
    const uint8_t subtype = p->Decode<uint8_t>();

    // Shared entry reader: itemId(4) hue(2) chroma(1) bright(1).
    auto readEntry = [&](TintMap& into) -> bool {
        if (!p->CanRead(8)) return false;
        const int itemId = static_cast<int>(p->Decode<uint32_t>());
        WeaponTint t;
        t.hue    = static_cast<short>(p->Decode<uint16_t>());
        t.chroma = static_cast<signed char>(p->Decode<uint8_t>());
        t.bright = static_cast<signed char>(p->Decode<uint8_t>());
        if (t.hue < 0 || t.hue > kTintHueMax) t.hue = 0;
        if (itemId > 0 && !t.IsIdentity()) into[itemId] = t;
        return true;
    };

    if (subtype == kResp_Snapshot) {
        // count + one entry per DYED item the local player owns.
        if (!p->CanRead(1)) return;
        const int count = p->Decode<uint8_t>();
        TintMap parsed;
        for (int i = 0; i < count; ++i) {
            if (!readEntry(parsed)) return;      // truncated: keep what we had
        }
        std::lock_guard<std::mutex> lock(g_mtx);
        if (parsed != g_saved) g_syncDirty = true;
        g_saved.swap(parsed);
        return;
    }

    if (subtype == kResp_RemoteTable) {
        // playerCount, then per player: name(u16 len + bytes), itemCount, entries.
        if (!p->CanRead(1)) return;
        const int players = p->Decode<uint8_t>();
        std::unordered_map<std::string, TintMap> parsed;
        for (int i = 0; i < players; ++i) {
            if (!p->CanRead(2)) return;
            const int len = p->Decode<uint16_t>();
            if (len < 1 || len > 13 || !p->CanRead(static_cast<size_t>(len) + 1)) return;
            std::string name(reinterpret_cast<const char*>(p->CurrentPublic()), len);
            p->SetOffset(p->GetOffset() + len);
            const int items = p->Decode<uint8_t>();
            TintMap m;
            for (int k = 0; k < items; ++k) {
                if (!readEntry(m)) return;
            }
            if (!m.empty()) parsed.emplace(std::move(name), std::move(m));
        }
        std::lock_guard<std::mutex> lock(g_mtx);
        // Repaint anyone whose tints actually moved, in EITHER direction -- a name
        // that dropped out of the table has been restored to vanilla and needs the
        // rebuild just as much as one that gained a colour.
        for (const auto& kv : parsed) {
            auto old = g_remoteTints.find(kv.first);
            if (old == g_remoteTints.end() || old->second != kv.second) {
                g_remoteDirty.push_back(kv.first);
            }
        }
        for (const auto& kv : g_remoteTints) {
            if (parsed.find(kv.first) == parsed.end()) g_remoteDirty.push_back(kv.first);
        }
        g_remoteTints.swap(parsed);
        return;
    }

    if (subtype == kResp_Result) {
        if (!p->CanRead(1)) return;
        const uint8_t code = p->Decode<uint8_t>();
        std::lock_guard<std::mutex> lock(g_mtx);
        g_lastResult = (code <= kTintResult_Failed)
                     ? static_cast<WeaponTintResult>(code)
                     : kTintResult_Failed;
        return;
    }
}

// Main thread, once per frame (the host's CWvsApp::CallUpdate hook). This is what
// makes a SERVER-driven tint change
// visible: an apply and a restore both land as a snapshot on the receive thread,
// which can only raise a flag. Without this the weapon kept its old colour until
// something else happened to rebuild its layers -- unequipping and re-equipping it,
// for instance.
void WeaponTint_Tick() {
    bool dirty = false;
    std::vector<std::string> remote;
    {
        std::lock_guard<std::mutex> lock(g_mtx);
        dirty = g_syncDirty;
        g_syncDirty = false;
        remote.swap(g_remoteDirty);
    }

    // --- other players -------------------------------------------------------
    // Their clones live in the same cache as ours, so any change there invalidates
    // it wholesale. Each affected avatar is then reloaded directly; a name we have
    // never rendered simply has no entry, and picks its tint up naturally the first
    // time the renderer asks for it.
    if (!remote.empty()) {
        ClearClones();
        for (const std::string& name : remote) {
            CAvatar* pAvatar = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_mtx);
                auto it = g_avatarByName.find(name);
                if (it == g_avatarByName.end()) continue;
                pAvatar = it->second;
            }
            // The pointer may be stale -- that player could have left the map and
            // the CUser been freed. Re-read the name off it and only touch it if it
            // still says what we recorded.
            char check[16];
            if (!NameOfAvatar(pAvatar, check, sizeof(check)) || name != check) {
                std::lock_guard<std::mutex> lock(g_mtx);
                g_avatarByName.erase(name);
                continue;
            }
            SehReloadActionLayer(pAvatar);
            // ...and their face, which is a separate layer the reload above does not
            // touch. Without this a remote player's eye colour would not appear until
            // their next blink. Safe to call here: the lock is already released, and
            // BuildFaceLayer_Hook takes it itself.
            BuildFaceLayer_Hook(pAvatar, nullptr, -1);
        }
    }

    // --- the local player ----------------------------------------------------
    if (!dirty) return;
    // While the prism window is open its sliders own the render; adopting the
    // server's value now would yank the preview out from under the player mid-drag.
    // The window drops the preview when it closes, and that path repaints itself.
    if (g_previewActive) return;
    ClearClones();
    WeaponTint_RefreshLocalAvatar();
}

void AttachWeaponTintMod() {
    // The face builder. Separate from PrepareActionLayer because the face is a separate
    // layer built by a separate function -- without this the eye tint is silently inert.
    ATTACH_HOOK(BuildFaceLayer_Orig, BuildFaceLayer_Hook);
    ATTACH_HOOK(PrepareActionLayer_Orig, PrepareActionLayer_Hook);
}
