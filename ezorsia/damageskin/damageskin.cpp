#include "stdafx.h"
#include "compat/hook.h"
#include "debug.h"
#include "DamageSkinData.h"
#include "compat/wvs/util.h"
#include "compat/wvs/Packet.h"
#include "compat/ClientAddresses.h"

#include <cstdarg>
#include <cstdio>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>
#include <windows.h>

// Forward decls — both defined further down, referenced by LoadDamageSkin
// (for wrappers) and AttachDamageSkinMod (for the net hooks).
void AttachDamageSkinNet();
void AttachUnitDamageRendering();


// Quiet in release builds. Flip the 0 to 1 to re-enable damageskin.txt
// trace logging for troubleshooting.
#define DAMAGESKIN_TRACE 0
#if DAMAGESKIN_TRACE
static FILE* g_pDmgLog = nullptr;
static void DmgLog(const char* fmt, ...) {
    if (!g_pDmgLog) {
        fopen_s(&g_pDmgLog, "damageskin.txt", "a");
        if (g_pDmgLog) {
            SYSTEMTIME st; GetLocalTime(&st);
            fprintf(g_pDmgLog,
                "\n=== DamageSkin session %04d-%02d-%02d %02d:%02d:%02d ===\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
    }
    if (!g_pDmgLog) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_pDmgLog, fmt, ap);
    va_end(ap);
    fprintf(g_pDmgLog, "\n");
    fflush(g_pDmgLog);
}
#else
static inline void DmgLog(const char*, ...) {}
#endif


// ===========================================================================
// Damage Skin — in-game renderer + network glue.
//
// At load time we enumerate Effect/DamageSkin.img/<id> (separate img, safe)
// or Effect/BasicEff.img/damageSkin/<id> (legacy merged path) into a map.
// At render time CAnimationDisplayer::Effect_HP picks a sprite pair based
// on lColorType:
//    lColorType == 1  -> Red  (normal hit) at this + 0x178 / 0x17C
//    lColorType == 2  -> Cri  (crit)       at this + 0x180 / 0x184
// The splices below replace the first instruction of each branch with a
// jump to a helper that, when a skin is active, writes the skin's sprite
// pair into the function's IWzProperty smart pointers (var_18 at
// [ebp-0x18] and var_20 at [ebp-0x20]) instead of the mob's defaults.
//
// Skin selection is driven by g_nDamageSkin. CMob::OnHit's splice sets it
// from the attacker's charId (looked up against g_mCharIdToSkin populated
// by server broadcasts) just before Effect_HP fires.
// ===========================================================================


// ---------------------------------------------------------------------------
// v83 addresses
// ---------------------------------------------------------------------------

// Effect_HP branch splices. v83 takes (lColorType, lY); player damage fires
// under lColorType==0 with lY==0 (normal) or lY==1 (crit).
static constexpr uintptr_t kSplice_Normal_Site = 0x00437DA2;
static constexpr uintptr_t kSplice_Crit_Site   = 0x00437D8C;
// Common merge point both branches jump to after writing var_18 / pushing var_20.
static constexpr uintptr_t kSplice_MergeRet    = 0x00437DB6;

// 6-byte patch site: `jz loc_4382F6` (near jz, 0F 84 XX XX XX XX) at
// 0x00438166 guards the crit "Effect" lookup block. We overwrite it with an
// unconditional `jmp rel32 + nop` so the Effect GetItem (which expects a
// NoCri1/Effect child the skin WZ doesn't provide under that name) is
// always skipped. Cost: crits lose the stock glow overlay the Effect
// animation produces; digits themselves still render fine with skin style.
static constexpr uintptr_t kPatch_SkipEffect_Site = 0x00438166;
static constexpr uintptr_t kPatch_SkipEffect_Tgt  = 0x004382F6;

// Damage-number clipping: Effect_HP has THREE hardcoded 0x39 / 0xD1 imm8s
// that jointly control the scratch canvas and sprite positioning. Only
// bumping the canvas height is NOT sufficient — the blit Y anchor is also
// hardcoded to 57, so tall sprites still clip at the top where (anchor -
// originY) goes negative.
//
// 0x0043805F  39 → 0x7F  : push 0x39  ← canvas Init() height (57 → 127)
//                          at 0x43805E, consumed by [ecx+0x2C] call.
// 0x00438355  39 → 0x64  : push 0x39  ← blit Y anchor inside canvas
//                          (57 → 100). ebx = anchor - jitter - originY,
//                          then blitted via [eax+0x80].
// 0x004383F6  D1 → 0xA6  : add eax, -47 ← canvas screen render top offset
//                          (-47 → -90). Keeps the anchor at the same
//                          absolute screen Y: lCenterTop - 90 + 100 =
//                          lCenterTop + 10 == old lCenterTop - 47 + 57.
//                          Net effect: sprite gets 100 rows of headroom
//                          above its origin (was 57) and 27 below (was 0).
static constexpr uintptr_t kPatch_CanvasH_Addr   = 0x0043805F;
static constexpr uint8_t   kPatch_CanvasH_Val    = 0x7F; // 57 → 127
static constexpr uintptr_t kPatch_BlitAnchor_Addr = 0x00438355;
static constexpr uint8_t   kPatch_BlitAnchor_Val  = 0x64; // 57 → 100
static constexpr uintptr_t kPatch_ScreenOfs_Addr  = 0x004383F6;
static constexpr uint8_t   kPatch_ScreenOfs_Val   = 0xA6; // -47 → -90 (signed imm8)

// Effect_Miss sprite splice. Effect_Miss at 0x00438A21 loads one of two
// sprites into eax: default miss ([esi+0x170] = NoRed0 property) at
// 0x00438A51, or alternate ([esi+0x180]) at 0x00438A49. Downstream calls
// `GetItem(L"Miss")` on it. Skin's NoRed0 has the Miss child, so we splice
// only the default-miss path and return skin's NoRed0 from our helper.
static constexpr uintptr_t kSplice_Miss_Site = 0x00438A51;
static constexpr uintptr_t kSplice_Miss_Ret  = 0x00438A57;

// CAnimationDisplayer sprite field offsets (from `this`).
static constexpr uintptr_t kOff_No0    = 0x170; // regular small
static constexpr uintptr_t kOff_No1    = 0x174; // regular large
static constexpr uintptr_t kOff_NoCri0 = 0x188; // crit small
static constexpr uintptr_t kOff_NoCri1 = 0x18C; // crit large

// sub_4027BE(ecx = IWzPropertyPtr*, push arg): smart-pointer assign.
// This is the _com_ptr_t assignment helper the engine uses to store
// IWzProperty* into stack locals (with AddRef/Release handling).
static auto SmartPtrAssign =
    reinterpret_cast<void(__thiscall*)(void* /*pDest*/, IUnknown* /*pSrc*/)>(0x004027BE);



// ---------------------------------------------------------------------------
// Loaded skins
// ---------------------------------------------------------------------------

// Shared with damageskinpicker.cpp via damageskin.h.
std::map<int, DamageSkinProp> g_mDamageSkinProp;
std::vector<int>              g_vSkinIds;
int                           g_nDamageSkin = 0;

void SetActiveDamageSkin(int nID) { g_nDamageSkin = nID; }


static bool g_bBulkScanDone = false;
static wchar_t g_damageSkinRoot[96] = L"";

static void BridgeLoadLog(const char* fmt, ...) {
    FILE* f = nullptr;
    fopen_s(&f, "damageskin_bridge.txt", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fflush(f);
    fclose(f);
}

static bool ResolveDamageSkinRootPath() {
    if (g_damageSkinRoot[0] != L'\0') {
        return true;
    }
    static const wchar_t* kPaths[] = {
        L"Effect/DamageSkin.img",
        L"Effect/BasicEff.img/damageSkin",
    };
    for (const wchar_t* path : kPaths) {
        try {
            Ztl_variant_t vNode = get_rm()->GetObjectA(const_cast<wchar_t*>(path));
            IWzPropertyPtr pRoot(get_unknown(vNode));
            if (!pRoot) continue;
            wcscpy_s(g_damageSkinRoot, path);
            BridgeLoadLog("LoadDamageSkin: root resolved %ls", path);
            return true;
        } catch (...) {}
    }
    BridgeLoadLog("LoadDamageSkin: no WZ root (DamageSkin.img / BasicEff damageSkin)");
    return false;
}

static bool PopulatePropFromSkinNode(IWzPropertyPtr pProp, int nID, DamageSkinProp& prop) {
    if (!pProp) return false;

    Ztl_variant_t vNoRed0, vNoRed1, vNoCri0, vNoCri1;
    if (SUCCEEDED(pProp->get_item(const_cast<wchar_t*>(L"NoRed0"), &vNoRed0)))
        prop.pNoRed0 = IWzPropertyPtr(vNoRed0.GetUnknown(false, false));
    if (SUCCEEDED(pProp->get_item(const_cast<wchar_t*>(L"NoRed1"), &vNoRed1)))
        prop.pNoRed1 = IWzPropertyPtr(vNoRed1.GetUnknown(false, false));
    if (SUCCEEDED(pProp->get_item(const_cast<wchar_t*>(L"NoCri0"), &vNoCri0)))
        prop.pNoCri0 = IWzPropertyPtr(vNoCri0.GetUnknown(false, false));
    if (SUCCEEDED(pProp->get_item(const_cast<wchar_t*>(L"NoCri1"), &vNoCri1)))
        prop.pNoCri1 = IWzPropertyPtr(vNoCri1.GetUnknown(false, false));

    Ztl_variant_t vCustom;
    if (SUCCEEDED(pProp->get_item(const_cast<wchar_t*>(L"NoCustom"), &vCustom))) {
        IWzPropertyPtr pCustom(vCustom.GetUnknown(false, false));
        if (pCustom) {
            bool bIsGlUnit = false;
            Ztl_variant_t vType;
            if (SUCCEEDED(pCustom->get_item(
                    const_cast<wchar_t*>(L"customType"), &vType))
                && vType.vt == VT_BSTR && vType.bstrVal)
            {
                bIsGlUnit = (wcscmp(vType.bstrVal, L"glUnit") == 0);
            }
            if (bIsGlUnit) {
                Ztl_variant_t vCR0, vCC0;
                if (SUCCEEDED(pCustom->get_item(const_cast<wchar_t*>(L"NoRed0"), &vCR0)))
                    prop.pNoCustomRed0 = IWzPropertyPtr(vCR0.GetUnknown(false, false));
                if (SUCCEEDED(pCustom->get_item(const_cast<wchar_t*>(L"NoCri0"), &vCC0)))
                    prop.pNoCustomCri0 = IWzPropertyPtr(vCC0.GetUnknown(false, false));
                prop.bHasCustom = prop.pNoCustomRed0 && prop.pNoCustomCri0;
            }
        }
    }

    if (!prop.pNoRed0 || !prop.pNoRed1 || !prop.pNoCri0 || !prop.pNoCri1) {
        return false;
    }
    g_mDamageSkinProp[nID] = prop;
    if (std::find(g_vSkinIds.begin(), g_vSkinIds.end(), nID) == g_vSkinIds.end()) {
        g_vSkinIds.push_back(nID);
    }
    return true;
}

static bool TryLoadSkinByDirectPath(int nID) {
    if (nID <= 0) return false;
    if (!ResolveDamageSkinRootPath()) return false;

    wchar_t path[128];
    _snwprintf_s(path, _countof(path), _TRUNCATE, L"%s/%d", g_damageSkinRoot, nID);
    try {
        Ztl_variant_t vNode = get_rm()->GetObjectA(path);
        IWzPropertyPtr pProp(get_unknown(vNode));
        if (!pProp) return false;
        DamageSkinProp prop;
        return PopulatePropFromSkinNode(pProp, nID, prop);
    } catch (...) {
        return false;
    }
}

void EnsureDamageSkinLoaded(int nID) {
    if (nID <= 0) return;
    if (g_mDamageSkinProp.find(nID) != g_mDamageSkinProp.end()) return;
    TryLoadSkinByDirectPath(nID);
}

static void PreloadCatalogSkins() {
    int loaded = 0;
    for (const auto& e : g_vShopCatalog) {
        if (e.nID <= 0) continue;
        if (g_mDamageSkinProp.find(e.nID) != g_mDamageSkinProp.end()) {
            ++loaded;
            continue;
        }
        if (TryLoadSkinByDirectPath(e.nID)) {
            ++loaded;
        }
    }
    if (!g_vShopCatalog.empty()) {
        BridgeLoadLog("LoadDamageSkin: catalog preload %d/%zu props",
                      loaded, g_vShopCatalog.size());
    }
}

void LoadDamageSkin() {
    if (g_bBulkScanDone) return;
    g_bBulkScanDone = true;
    DmgLog("LoadDamageSkin: begin");

    if (!ResolveDamageSkinRootPath()) {
        PreloadCatalogSkins();
        AttachUnitDamageRendering();
        return;
    }

    IWzPropertyPtr pDamageSkin;
    try {
        Ztl_variant_t vDamageSkin =
            get_rm()->GetObjectA(const_cast<wchar_t*>(g_damageSkinRoot));
        pDamageSkin = IWzPropertyPtr(get_unknown(vDamageSkin));
    } catch (...) {
        pDamageSkin = nullptr;
    }
    if (!pDamageSkin) {
        BridgeLoadLog("LoadDamageSkin: bulk scan root QI failed");
        PreloadCatalogSkins();
        AttachUnitDamageRendering();
        return;
    }

    IUnknownPtr pEnumUnknown;
    if (FAILED(pDamageSkin->get__NewEnum(&pEnumUnknown))) {
        BridgeLoadLog("LoadDamageSkin: get__NewEnum failed — using direct paths");
        PreloadCatalogSkins();
        AttachUnitDamageRendering();
        return;
    }
    IEnumVARIANTPtr pEnum(pEnumUnknown);
    if (!pEnum) {
        PreloadCatalogSkins();
        AttachUnitDamageRendering();
        return;
    }

    int nSeen = 0, nOK = 0;
    while (true) {
        VARIANT rgVar[1];
        ULONG uFetched = 0;
        if (FAILED(pEnum->Next(1, rgVar, &uFetched)) || uFetched == 0) break;
        ++nSeen;
        if (rgVar[0].vt != VT_BSTR || !rgVar[0].bstrVal) continue;

        int nID = wcstol(rgVar[0].bstrVal, nullptr, 10);
        if (nID <= 0) continue;
        Ztl_variant_t vProp;
        if (FAILED(pDamageSkin->get_item(rgVar[0].bstrVal, &vProp))) continue;
        IWzPropertyPtr pProp(vProp.GetUnknown(false, false));
        DamageSkinProp prop;
        if (PopulatePropFromSkinNode(pProp, nID, prop)) {
            ++nOK;
        }
    }

    BridgeLoadLog("LoadDamageSkin: bulk scan seen=%d loaded=%d props=%zu",
                  nSeen, nOK, g_mDamageSkinProp.size());
    if (nOK == 0) {
        PreloadCatalogSkins();
    }
    DmgLog("LoadDamageSkin: done — %d entries scanned, %d fully loaded", nSeen, nOK);
    AttachUnitDamageRendering();
}


// ---------------------------------------------------------------------------
// Branch splice helpers
//
// Each helper is the replacement for a single branch of Effect_HP's
// lColorType switch. It writes the selected sprite0 to var_18 (via the
// engine's smart-ptr assign helper) and pushes sprite1 onto the stack for
// the merge-point to consume into var_20.
//
// Stack shape expected by the merge point at 0x00437DB6:
//   top of stack = sprite0 (to be assigned to var_20)
//   var_18 already populated
// Registers: esi = CAnimationDisplayer*, ebp = frame pointer.
// ---------------------------------------------------------------------------

// Returns (large, small) sprite sheets.
// Never returns null; falls back to the mob's own sprites when the active
// damage-skin id is not in the loaded map.
struct SpritePair { IWzProperty* pLarge; IWzProperty* pSmall; };

// Regular player damage: substitute the mob's (no0, no1) with the skin's
// (NoRed0, NoRed1) when a skin is active.  For unit-damage skins, hand
// out the unit wrapper in BOTH slots so every glyph in the run renders
// at consistent (small) size — NoCustom only ships the NoRed0 variant.
static inline SpritePair PickNormal(void* pDisp) {
    if (g_nDamageSkin != 0) {
        auto it = g_mDamageSkinProp.find(g_nDamageSkin);
        if (it != g_mDamageSkinProp.end()) {
            if (it->second.bHasCustom && it->second.pWrapNormal) {
                auto* w = static_cast<IWzProperty*>(it->second.pWrapNormal);
                return { w, w };
            }
            return { it->second.pNoRed1, it->second.pNoRed0 };
        }
    }
    auto fields = reinterpret_cast<char*>(pDisp);
    return {
        *reinterpret_cast<IWzProperty**>(fields + kOff_No1),
        *reinterpret_cast<IWzProperty**>(fields + kOff_No0),
    };
}

// Crit player damage: substitute the mob's (NoCri0, NoCri1) with the skin's
// (NoCri0, NoCri1). Safe only because we also skip the Effect-block patch
// (see kPatch_SkipEffect_Site) so nothing downstream queries NoCri1/Effect.
static inline SpritePair PickCrit(void* pDisp) {
    if (g_nDamageSkin != 0) {
        auto it = g_mDamageSkinProp.find(g_nDamageSkin);
        if (it != g_mDamageSkinProp.end()) {
            if (it->second.bHasCustom && it->second.pWrapCrit) {
                auto* w = static_cast<IWzProperty*>(it->second.pWrapCrit);
                return { w, w };
            }
            return { it->second.pNoCri1, it->second.pNoCri0 };
        }
    }
    auto fields = reinterpret_cast<char*>(pDisp);
    return {
        *reinterpret_cast<IWzProperty**>(fields + kOff_NoCri1),
        *reinterpret_cast<IWzProperty**>(fields + kOff_NoCri0),
    };
}


// Both helpers follow the same shape: called with (pDisp, &var_18) via
// __stdcall, they write var_18 and return sprite0 for the merge point to
// push before jumping to 0x00437DB6.
static IWzProperty* __stdcall Normal_helper(void* pDisp, void* pVar18) {
    LoadDamageSkin();
    SpritePair sp = PickNormal(pDisp);
    SmartPtrAssign(pVar18, sp.pLarge);
    return sp.pSmall;
}

static IWzProperty* __stdcall Crit_helper(void* pDisp, void* pVar18) {
    LoadDamageSkin();
    SpritePair sp = PickCrit(pDisp);
    SmartPtrAssign(pVar18, sp.pLarge);
    return sp.pSmall;
}

// Effect_Miss helper: returns skin's NoRed0 (which carries the `Miss` child)
// when a skin is active, else the mob's own NoRed0 at [this+0x170].
static IWzProperty* __stdcall Miss_helper(void* pDisp) {
    LoadDamageSkin();
    if (g_nDamageSkin != 0) {
        auto it = g_mDamageSkinProp.find(g_nDamageSkin);
        if (it != g_mDamageSkinProp.end() && it->second.pNoRed0) {
            return it->second.pNoRed0;
        }
    }
    return *reinterpret_cast<IWzProperty**>(
        reinterpret_cast<char*>(pDisp) + kOff_No0);
}


// Each splice replaces the first instruction of its branch with a 5-byte
// rel32 jmp into the helper. Helper:
//   1. Writes the "large" sprite into var_18 via the engine's smart-ptr
//      assign (ecx = &var_18, pushed = large).
//   2. Returns "small" sprite in eax; we push it onto the stack and jmp
//      to 0x00437DB6, whose lea+call consumes it into var_20.

static void __declspec(naked) Normal_splice_hook() {
    __asm {
        lea     eax, [ ebp - 0x18 ]
        push    eax
        push    esi
        call    Normal_helper
        push    eax
        jmp     [ kSplice_MergeRet ]
    }
}

static void __declspec(naked) Crit_splice_hook() {
    __asm {
        lea     eax, [ ebp - 0x18 ]
        push    eax
        push    esi
        call    Crit_helper
        push    eax
        jmp     [ kSplice_MergeRet ]
    }
}

// Effect_Miss default-branch replacement. Original at 0x00438A51:
//     mov eax, [esi+170h]   (6 bytes)
// We overwrite the first 5 bytes with a jmp to this trampoline; the 6th
// byte becomes dead after our jmp. The helper returns the sprite in eax,
// matching what the original `mov` produced; merge at 0x00438A57.
static void __declspec(naked) Miss_splice_hook() {
    __asm {
        push    esi
        call    Miss_helper
        jmp     [ kSplice_Miss_Ret ]
    }
}


// ---------------------------------------------------------------------------
// Attach
// ---------------------------------------------------------------------------

// Overwrite the near jz at 0x00438166 with `jmp rel32 + nop` (6 bytes total)
// so Effect_HP always skips the crit Effect-block. Relative offset is
// target - (site + 5).
static void PatchSkipEffectBlock() {
    auto* p = reinterpret_cast<unsigned char*>(kPatch_SkipEffect_Site);
    DWORD old, restore;
    VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; // jmp rel32
    *reinterpret_cast<int32_t*>(p + 1) =
        static_cast<int32_t>(kPatch_SkipEffect_Tgt - (kPatch_SkipEffect_Site + 5));
    p[5] = 0x90; // nop (pad 6th byte of original near jz)
    VirtualProtect(p, 6, old, &restore);
}

// Patch a single imm8 byte. Logs before/after so we can verify from the log.
static void PatchImm8(uintptr_t uAddr, uint8_t uVal, const char* sLabel) {
    auto* p = reinterpret_cast<unsigned char*>(uAddr);
    unsigned char before = *p;
    DWORD old, restore;
    BOOL ok1 = VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old);
    *p = uVal;
    BOOL ok2 = VirtualProtect(p, 1, old, &restore);
    DmgLog("%s @0x%08X: %02X → %02X  VP(%d/%d)",
           sLabel, (unsigned)uAddr, before, *p, ok1, ok2);
}

// Expand the damage-number scratch canvas so tall skin sprites render fully.
// Patches all three hardcoded sites (canvas height, blit anchor, screen
// offset) at once so the anchor's absolute screen position is preserved.
static void PatchDamageCanvas() {
    PatchImm8(kPatch_CanvasH_Addr,    kPatch_CanvasH_Val,    "CanvasH  ");
    PatchImm8(kPatch_BlitAnchor_Addr, kPatch_BlitAnchor_Val, "BlitAnch ");
    PatchImm8(kPatch_ScreenOfs_Addr,  kPatch_ScreenOfs_Val,  "ScreenOfs");
}

void AttachDamageSkinMod() {
    DmgLog("AttachDamageSkinMod: begin (lazy-load on first splice fire)");
    PatchJmp(kSplice_Normal_Site, &Normal_splice_hook);
    PatchJmp(kSplice_Crit_Site,   &Crit_splice_hook);
    PatchJmp(kSplice_Miss_Site,   &Miss_splice_hook);
    PatchSkipEffectBlock();
    PatchDamageCanvas();
    AttachDamageSkinNet();
    DmgLog("Attached: Normal Crit Miss splices + SkipEffect + canvas patch + net");
}


// ===========================================================================
// NETWORK — shared state + outgoing requests (receive handled in Bridge)
// ===========================================================================

std::vector<DamageSkinCatalogEntry> g_vShopCatalog;
std::vector<int>                    g_vOwnedSkins;
int                                 g_nLocalCharId = 0;

static std::unordered_map<int, int> g_mCharIdToSkin;

int GetSkinForChar(int nCharId) {
    auto it = g_mCharIdToSkin.find(nCharId);
    return it == g_mCharIdToSkin.end() ? 0 : it->second;
}

void SetSkinForChar(int nCharId, int nSkinId) {
    if (nSkinId == 0) {
        g_mCharIdToSkin.erase(nCharId);
    } else {
        g_mCharIdToSkin[nCharId] = nSkinId;
    }
}

static void DmgSendPacket(CompatOutPacketBuilder& builder) {
    void* pSocket = *reinterpret_cast<void**>(ClientAddresses::kClientSocketPtr);
    if (!pSocket) {
        return;
    }
    CompatOutPacket packet = builder.Build();
    reinterpret_cast<void(__fastcall*)(void*, void*, CompatOutPacket*)>(
        ClientAddresses::kSendPacket)(pSocket, nullptr, &packet);
}

void Send_DamageSkinApply(int nSkinId) {
    CompatOutPacketBuilder p(CustomRecvOpcode::kDamageSkinApply);
    p.WriteInt(nSkinId);
    DmgSendPacket(p);
    DmgLog("Send Apply id=%d", nSkinId);
}

void Send_DamageSkinPurchase(int nSkinId) {
    CompatOutPacketBuilder p(CustomRecvOpcode::kDamageSkinPurchase);
    p.WriteInt(nSkinId);
    DmgSendPacket(p);
    DmgLog("Send Purchase id=%d", nSkinId);
}


// ===========================================================================
// Attacker attribution — CMob::OnHit brings us the attacker's char id as the
// first stack arg. We temporarily swap g_nDamageSkin to that attacker's skin
// for the duration of the call, so the Effect_HP splice (which reads
// g_nDamageSkin) renders each player's damage in THEIR skin, not ours.
// ===========================================================================

static constexpr uintptr_t kAddr_CMob_OnHit          = 0x00668B83;
static constexpr uintptr_t kAddr_CUserLocal_Instance = 0x00BEBF98;
static constexpr uintptr_t kOff_UserLocal_CharId     = 1130 * 4;  // 0x11A8

// CMob::OnHit(this, attackerCid, a3..a14) — __thiscall, 13 stack args
// (verified by `retn 34h` at function epilogue — 0x34 / 4 = 13). Hex-Rays
// shows 14 args in its decomp but that's Hex-Rays miscounting; the 14th
// entry in the mangled name corresponds to the `this` pointer in ecx.
typedef void(__thiscall* t_CMob_OnHit)(
    void*, unsigned int, int, int, int, int, int, int, int,
    int, int, int, int, int);
static t_CMob_OnHit CMob_OnHit_orig =
    reinterpret_cast<t_CMob_OnHit>(kAddr_CMob_OnHit);

static int ReadLocalCharId() {
    auto pUser = *reinterpret_cast<char**>(kAddr_CUserLocal_Instance);
    if (!pUser) return 0;
    __try {
        return *reinterpret_cast<int*>(pUser + kOff_UserLocal_CharId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void __fastcall CMob_OnHit_hook(
    void* pThis, void* /*edx*/,
    unsigned int attackerCid,
    int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10,
    int a11, int a12, int a13, int a14)
{
    int saved   = g_nDamageSkin;
    int localId = ReadLocalCharId();
    if (localId) g_nLocalCharId = localId;

    int attackerSkin = GetSkinForChar((int)attackerCid);
    if ((int)attackerCid == g_nLocalCharId) {
        // Local attacker: prefer the just-applied skin even if the
        // broadcast round-trip hasn't landed yet; otherwise the map entry.
        if (attackerSkin != 0) g_nDamageSkin = attackerSkin;
        // else leave g_nDamageSkin alone (already our selected id)
    } else {
        // Remote attacker (or unknown): use whatever the server told us.
        // 0 means "no skin registered" → renders default numbers.
        g_nDamageSkin = attackerSkin;
    }

    CMob_OnHit_orig(pThis, attackerCid, a3, a4, a5, a6, a7, a8, a9, a10,
                    a11, a12, a13, a14);
    g_nDamageSkin = saved;
}

void AttachDamageSkinNet() {
    ATTACH_HOOK(CMob_OnHit_orig, CMob_OnHit_hook);
    DmgLog("AttachDamageSkinNet: installed CMob::OnHit hook");
}


// ===========================================================================
// UNIT DAMAGE (NoCustom) rendering — in-game
//
// For skins that carry a NoCustom node we render damage values >= 1000 as
// "1.8K" / "1M" / "1B" instead of raw digits. Two pieces:
//
//   1. A property-wrapper that unions the skin's base digit sheet with
//      the NoCustom glyphs. Effect_HP's existing digit loop calls
//      IWzProperty::get_item(sheet, L"1") per char; our wrapper routes
//      '0'..'9' to the base sheet and '.'/'K'/'M'/'B' to the NoCustom
//      sheet. All other IWzProperty methods delegate to base so the
//      engine sees a "normal" property.
//
//   2. A splice at the `call Format` site inside Effect_HP (0x00437DFB)
//      that runs the real Format, then — if the active skin has
//      NoCustom AND damage >= 1000 — overwrites the produced ZXString
//      with the unit-form ("1.8K"). The digit loop iterates our
//      rewritten string and the wrapper feeds it the correct glyphs.
//
// The sprite-pair splice (Normal_helper / Crit_helper) is also updated
// to hand out the wrapper (in both small and large slots) whenever the
// active skin is a unit skin, so the glyph run renders at consistent
// size regardless of which digit index the engine is drawing.
// ===========================================================================

#include <atomic>

class CUnitPropertyWrapper : public IWzProperty {
public:
    CUnitPropertyWrapper(IWzPropertyPtr pBase, IWzPropertyPtr pCustom)
        : m_pBase(pBase), m_pCustom(pCustom) { m_nRef.store(1); }

    // IUnknown ---------------------------------------------------------
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown ||
            riid == __uuidof(IWzSerialize) ||
            riid == __uuidof(IWzProperty))
        {
            *ppv = static_cast<IWzProperty*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef()  override { return (ULONG)(++m_nRef); }
    ULONG __stdcall Release() override {
        // Wrappers are owned by the DamageSkinProp map and live for the
        // DLL's lifetime. If the engine's Release would take us to 0 we
        // clamp back to 1 — we never actually free.
        long n = --m_nRef;
        if (n <= 0) { m_nRef.store(1); n = 1; }
        return (ULONG)n;
    }

    // IWzSerialize -----------------------------------------------------
    HRESULT __stdcall get_persistentUOL(BSTR* pVal) override {
        return m_pBase ? m_pBase->get_persistentUOL(pVal) : E_FAIL;
    }
    HRESULT __stdcall raw_Serialize(IWzArchive* pArchive) override {
        return m_pBase ? m_pBase->raw_Serialize(pArchive) : E_FAIL;
    }

    // IWzProperty ------------------------------------------------------
    // Effect_HP's digit loop reads each byte of the damage string, does
    // (byte - '0') and stringifies the result. Negative / out-of-range
    // byte values break the lookup, so we can't encode '.' / 'K' / 'M'
    // / 'B' directly. Instead FormatUnitDamage emits bytes 48..61 so the
    // integer stringification lands on "0".."13". This wrapper routes:
    //   "10" -> NoCustom/0  (the decimal point)
    //   "11" -> NoCustom/1  (K)
    //   "12" -> NoCustom/2  (M)
    //   "13" -> NoCustom/3  (B)
    // Anything else falls through to the base digits sheet (which has
    // children "0".."9" like a normal skin).
    HRESULT __stdcall get_item(BSTR sPath, VARIANT* pvValue) override {
        __try {
            if (sPath && sPath[0] == L'1' &&
                sPath[1] >= L'0' && sPath[1] <= L'3' &&
                sPath[2] == 0 && m_pCustom)
            {
                const wchar_t customKey[2] = { sPath[1], 0 };
                BSTR b = SysAllocString(customKey);
                HRESULT hr = m_pCustom->get_item(b, pvValue);
                SysFreeString(b);
                return hr;
            }
            return m_pBase ? m_pBase->get_item(sPath, pvValue) : E_FAIL;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            DmgLog("Wrapper::get_item SEH path=\"%ls\"", sPath ? sPath : L"(null)");
            if (pvValue) VariantInit(pvValue);
            return E_FAIL;
        }
    }
    HRESULT __stdcall put_item(BSTR sPath, VARIANT v) override {
        return m_pBase ? m_pBase->put_item(sPath, v) : E_FAIL;
    }
    HRESULT __stdcall get__NewEnum(IUnknown** pVal) override {
        return m_pBase ? m_pBase->get__NewEnum(pVal) : E_FAIL;
    }
    HRESULT __stdcall get_count(unsigned int* pVal) override {
        return m_pBase ? m_pBase->get_count(pVal) : E_FAIL;
    }
    HRESULT __stdcall raw_Add(BSTR s, VARIANT v, VARIANT b) override {
        return m_pBase ? m_pBase->raw_Add(s, v, b) : E_FAIL;
    }
    HRESULT __stdcall raw_Remove(BSTR s) override {
        return m_pBase ? m_pBase->raw_Remove(s) : E_FAIL;
    }
    HRESULT __stdcall raw_Import(BSTR s) override {
        return m_pBase ? m_pBase->raw_Import(s) : E_FAIL;
    }

private:
    IWzPropertyPtr    m_pBase;
    IWzPropertyPtr    m_pCustom;
    std::atomic<long> m_nRef;
};

// Build wrappers for every loaded unit-damage skin. Called once from
// LoadDamageSkin after the main scan populates g_mDamageSkinProp.
static void BuildUnitWrappers() {
    int built = 0;
    for (auto& kv : g_mDamageSkinProp) {
        DamageSkinProp& prop = kv.second;
        if (!prop.bHasCustom) continue;
        if (prop.pWrapNormal || prop.pWrapCrit) continue;  // already built
        prop.pWrapNormal = new CUnitPropertyWrapper(prop.pNoRed0, prop.pNoCustomRed0);
        prop.pWrapCrit   = new CUnitPropertyWrapper(prop.pNoCri0, prop.pNoCustomCri0);
        ++built;
    }
    if (built) DmgLog("BuildUnitWrappers: %d unit skins wrapped", built);
}

// Decide whether the current Effect_HP call should render the damage
// as unit form. Reads g_nDamageSkin (the attacker-attributed global).
static bool ShouldRenderUnit(int damage) {
    if (damage < 1000) return false;
    if (g_nDamageSkin == 0) return false;
    auto it = g_mDamageSkinProp.find(g_nDamageSkin);
    return it != g_mDamageSkinProp.end() && it->second.bHasCustom;
}

// Format damage into the engine-compatible unit encoding.
//
// Effect_HP's digit loop does (byte - '0') on every character, so we must
// emit bytes in 48..61 such that the resulting "integer string" maps to a
// key our wrapper recognises:
//
//   digits 0..9   -> bytes 48..57      (keys "0".."9")
//   decimal '.'   -> byte 58 ':'       (key "10"  -> NoCustom/0)
//   unit K        -> byte 59 ';'       (key "11"  -> NoCustom/1)
//   unit M        -> byte 60 '<'       (key "12"  -> NoCustom/2)
//   unit B        -> byte 61 '='       (key "13"  -> NoCustom/3)
//
// Visual output therefore renders correctly (wrapper fetches the right
// glyph), while the engine's numeric machinery never sees a negative.
//
// Keeps one fractional digit when the next denomination isn't whole.
// Negative damage is not possible in Effect_HP (miss handled separately),
// but the formatter handles it defensively.
static void FormatUnitDamage(long long damage, char* out, size_t cap) {
    long long abs = damage < 0 ? -damage : damage;
    char unitByte = 0;
    long long divisor = 1;
    if (abs >= 1000000000LL)    { unitByte = '='; divisor = 1000000000LL; }
    else if (abs >= 1000000LL)  { unitByte = '<'; divisor = 1000000LL; }
    else                         { unitByte = ';'; divisor = 1000LL; }

    long long whole = abs / divisor;
    long long remainder = abs - whole * divisor;
    long long tenths = (remainder * 10) / divisor;

    // ':' is the encoded decimal point. snprintf treats it as a literal.
    if (tenths == 0) {
        _snprintf_s(out, cap, _TRUNCATE, "%lld%c", whole, unitByte);
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "%lld:%lld%c",
                    whole, tenths, unitByte);
    }
}

// Overwrite a ZXString<char> in place so the new string fits inside the
// existing allocation. The ZXString layout is:
//   _ZXStringData { long nRef; int nCap; int nByteLen; char buf[nCap]; }
//   _m_pStr points at buf (so data is at [_m_pStr - 12]).
// We update nByteLen and copy the new bytes. Returns false if the new
// string is longer than nCap (caller should bail in that case).
static bool OverwriteZXString(ZXString<char>* pZStr, const char* newStr) {
    if (!pZStr) return false;
    char* pBuf = *reinterpret_cast<char**>(pZStr);
    if (!pBuf) return false;
    int* pCap    = reinterpret_cast<int*>(pBuf - 8);
    int* pByteLen = reinterpret_cast<int*>(pBuf - 4);
    int newLen = (int)strlen(newStr);
    if (newLen + 1 > *pCap) return false;
    memcpy(pBuf, newStr, newLen + 1);
    *pByteLen = newLen;
    return true;
}

// Call target at 0x00437DFB originally was ZXString::Format; we redirect
// it to this thunk. Thunk calls the real Format, then — if unit damage
// applies — overwrites the produced string in-place.
typedef void(__cdecl* t_ZXStringFormat)(ZXString<char>*, const char*, int);
static auto RealZXStringFormat =
    reinterpret_cast<t_ZXStringFormat>(0x00445B4B);

static void __cdecl ZXStringFormat_hook(
    ZXString<char>* pRet, const char* fmt, int damage)
{
    // Always run the original first — our substitution is purely a
    // post-process.
    __try {
        RealZXStringFormat(pRet, fmt, damage);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DmgLog("ZXStringFormat_hook: real Format SEH for damage=%d", damage);
        return;
    }

    // Post-process only if this call is a unit-damage candidate.
    __try {
        if (!ShouldRenderUnit(damage)) return;
        char unitStr[32];
        FormatUnitDamage((long long)damage, unitStr, sizeof(unitStr));
        bool ok = OverwriteZXString(pRet, unitStr);
        DmgLog("Unit damage: %d -> \"%s\" (%s)",
               damage, unitStr, ok ? "ok" : "cap-too-small");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DmgLog("ZXStringFormat_hook: post-process SEH for damage=%d", damage);
    }
}

// Patch the `call Format` at 0x00437DFB (5-byte E8 rel32) to target our
// thunk instead. Reversible via PatchCall — no trampoline needed because
// we don't need to execute the original bytes elsewhere.
static void PatchEffectHPFormatCall() {
    constexpr uintptr_t kSite = 0x00437DFB;
    PatchCall(kSite, &ZXStringFormat_hook, 5);
    DmgLog("Patched Effect_HP Format call at 0x%08X", (unsigned)kSite);
}

// Public entry — called from AttachDamageSkinMod after BuildUnitWrappers.
void AttachUnitDamageRendering() {
    BuildUnitWrappers();
    PatchEffectHPFormatCall();
}


