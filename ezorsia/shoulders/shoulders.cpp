// Shoulder / Accessory-range (115 / BP20 / inventory -20).
// Also second pendant (112 / BP51 / -51) — mirrored from shoulder pattern.
// Also six rings (111 / BP52-53 / -52/-53) + badge (118 / BP54 / -54)
// + totem1 (120 / BP55 / −55) + android (166 / BP60) + heart (167 / BP61).
// BP21/22 = v083 pet seats (parked off main); Android/Heart = equipaddon sidecar.
// Stamp: ADDON_LOGIN_HOVER_SAFE_20260803 — classic Equip native unequip; PacketSlot −bp (no TOTEM2 cash).
//   ROW3 enter baseline + Early login persist + GetItem no cash alias.
//   ExtraRing −52/−53 Get/Set+UI ON; Occ OFF; NO −152 remap; Addon Get/Set ON.
//   NO ExtEquip include on enter path.
// 2026-08-02 — ADDON_SI_ENTER_SAFE: SI_STATS enter AV @535351 (write 0x2AE0,
//   set_stage). Disable Addon Combat/MainStat Occ again; keep Addon Get/Set/
//   Apply/UI + Si prefixes. Do NOT early-return ExtraRing bind (OCC_OFF bug).
// 2026-08-02 — ADDON_SI_STATS (rolled back Occ): sanitized Addon Occ ON caused
//   select→enter crash (not @77F99E). Empty-ZRef ring guard kept in caves for
//   when Occ re-enabled later.
// 2026-08-02 — ENTER_SAFE_UI / RULES_STATS / ENTER_FIX2 / CASH_SUB history kept.
// IDA BeiDou.exe imagebase 0x400000:
//   get_equip_data_path cmp 114 @ 0x005C973A  (83 F8 72; imm @ +2)
//   is_correct_bodypart @ 0x00460358
//   get_bodypart_from_item @ 0x004606A0
//   IsAbleToWear @ 0x004F2CEE  (must call original — cleans up via sub_4F5818)
//   combat skip -20 jz @ 0x0077F98C
//   BP51 TSecType gates (red + skip stats) — same class of fix as -20 NOP
//   g_aEquipSlotPos[50] @ 0x00BE2260 (BP = index+1); cash @ 0x00BE23F0
//
// 2026-07-27an — FIX_ENTER_INVALID: DISABLE am CashRingRemap (−152→−52).
//   Remap on login SetItem(cash bodypart 52) crashed enter-map (「数据无效」tip).
//   Cash rings stay on classic −112…/−116; −52/−53 remain non-cash only.
// 2026-07-27am — FIX_RING_CRASH: cash −152/−153 remap (superseded by an).
// 2026-07-27aq — FIX_RING34_RELOG_SYNC: DB keeps −52/−53 (like −20) but login
//   decode cmp esi,51 can still skip UI apply; force apply-max write + server
//   post-getCharInfo INVENTORY_OPERATION resync (equip path that draws).
// 2026-07-27aq — FIX_RING34_RELOG_SYNC: DB keeps −52/−53 (like −20) but login
//   decode cmp esi,51 can still skip UI apply; force apply-max write + server
//   post-getCharInfo INVENTORY_OPERATION resync (equip path that draws).
// 2026-07-27ap — FIX_RING34_PERSIST_SAFE: al caves-before-bound + apply max55
//   + an kRemapCashExtendedRings=false (no −152 remap). A/B rollback to aj
//   drops al → login shadows empty while DB −52/−53 still present.
// 2026-07-27an — FIX_ENTER_INVALID: DISABLE am CashRingRemap (−152→−52).
// 2026-07-27am — FIX_RING_CRASH: cash −152/−153 remap (superseded by an).
// 2026-07-27al — FIX_WEAR_PERSIST: caves BEFORE bound raise; apply max 55.
// 2026-07-27ak — BADGE_TOTEM_910: UI red 9/10 = BP54/55 (−54/−55).
// 2026-07-27aj — FIX_RING_UNEQUIP_PERSIST: unequip @4F0B89 + apply 51→53.
// (Prior history: see docs/装备槽修正-*.md and equip_slot_lessons.)

#include "stdafx.h"
#include "ShoulderApi.h"
#include "equipaddon/EquipAddonApi.h"
#include "Memory.h"
#include "compat/hook.h"

#include <initializer_list>

namespace {

// --- feature flags (STATS / FIX2 enter-safe; no ExtEquip registry) ---
static constexpr bool kPatchAccessoryPath = true;   // 114 -> 120 (badge 118 + totem 120 via Accessory path)
static constexpr bool kHookBodypart115 = true;      // 115 -> BP20; also 112/113/111/118/120
static constexpr bool kHookIsAbleToWear115 = true;  // call orig then force OK
static constexpr bool kNopCombatSkipNeg20 = true;   // shoulder stats into panel/combat
static constexpr bool kNopBp51TsecGates = true;     // second pendant: red + stats (mirror -20)
static constexpr bool kMoveShoulderUiTo8 = false;
// ExtraRing −52/−53: Get/Set + main UI ON; Occ/−152 OFF (Phase-5 enter-safe bisect).
// Do NOT early-return entire bind when OFF — Addon Get/Set still required.
// Crash class was shadows+−152 remap / Occ ON — not Get/Set+draw alone on ROW3.
static constexpr bool kEnableExtraRingsBp52_53 = true;
static constexpr bool kAcceptExtraRingBodyparts = true;
static constexpr bool kBindExtraRingInventory = true;
static constexpr bool kEnableCombatOccShadow = false;
static constexpr bool kEnableMainStatOccShadow = false;
static constexpr bool kEnableAddonSlotGetSet = true;
static constexpr bool kEnableAddonOccShadow = false;
static constexpr bool kEnableBadgeTotemBp54_55 = true;
// Vanilla SAFE_LAYOUT: NEVER raise apply-max past 55 (sidecar absorbs 56–62).
// CD64 EXE + kNativeCd64ExtendedSlots: apply-max 62, native ZRefs (Route A).
static constexpr bool kEnableAddonExtSlots = false; // hard OFF — do not re-enable without A/B
static constexpr bool kEnableAndroidHeartNative = true; // 166→BP60, 167→BP61
static constexpr bool kEnableEmblemBp59 = true;          // Emblem BP59
static constexpr bool kEnableTotem2to4 = true;           // Totem×4 BP55–58
static constexpr int kAndroidBodyPart = 60;
static constexpr int kHeartBodyPart = 61;
static constexpr bool kRemapCashExtendedRings = false;
// Route A (=「原生扩展」): DetectCd64Exe (push 0x700 @778F02) + this flag →
// UseNativeCd64Slots / NativeCd64Inventory: −52…−62 native ZRefs; Addon=UI only.
// HARD RULE 2026-08-04: EXE CD grow FORBIDDEN on live — flag stays true but
// Detect is false on vanilla ACF (0x640) ⇒ ExtraRing shadows + (gated) sidecar.
// Do not redeploy MINIMAL/CTOR EXE to Client_1; CD64_TEST only after user OK.
static constexpr bool kNativeCd64ExtendedSlots = true;
static constexpr int kOffPanelX = -2000;
static constexpr int kOffPanelY = -2000;
static constexpr unsigned char kExtSlotBoundImm = 0xC2;  // −62 (shadow/sidecar + aux)
static constexpr unsigned char kExtSlotBoundImmCd64 = 0xC1; // −63 (EXE CD64)
static constexpr unsigned char kApplySlotMaxImm = 55;       // vanilla hard cap
static constexpr unsigned char kApplySlotMaxImmCd64 = 62;   // product −62
static constexpr unsigned char kDrawLoopMaxImm = 55;        // Addon paints 54–62 even on CD64

// Runtime: heap slab push 0x700 @0x778F02 (vanilla 0x640).
// ADDON_NATIVE_CD64_DETECT_20260810 — require ALL three Route A EXE markers so a
// vanilla EXE can never false-positive into the native inventory path (avoid S2/S7):
//   slab    push 0x700 @778F02 (would read 68 00 07 00 00)
//   bound   GetItem cmp imm −51 → −63 (0xCD → 0xC1 @42834E)
//   apply   login apply-max 51 → 62 (0x33 → 0x3E @4E5D0C)
// A half-built CD64 EXE (slab only) falls back to shadow/sidecar — safe, not silent.
static bool DetectCd64Exe() {
    auto* p = reinterpret_cast<const unsigned char*>(0x00778F02);
    if (!(p[0] == 0x68 && p[1] == 0x00 && p[2] == 0x07 && p[3] == 0x00 && p[4] == 0x00)) {
        return false;
    }
    const unsigned char bound = *reinterpret_cast<const unsigned char*>(0x0042834E + 2);
    const unsigned char applyMax = *reinterpret_cast<const unsigned char*>(0x004E5D0C + 2);
    return bound == 0xC1 && applyMax == 0x3E;
}

static bool UseNativeCd64Slots() {
    // Require BOTH explicit flag AND cd64 EXE. Vanilla EXE ⇒ shadows/sidecar.
    return kNativeCd64ExtendedSlots && DetectCd64Exe();
}
static constexpr DWORD kGetEquipDataPath_cmp_v2_114_imm = 0x005C973A + 2;
// PE: cmp eax,114 / jle Accessory. Raise imm so 115–120 take Accessory path.
// Totem img must also resolve under Character/Accessory (hardlink/MCP); v083 has no Totem string.
static constexpr BYTE kEquipDataPathAccessoryMax = 0x78; // 120

static constexpr DWORD kCombatSkipSlotNeg20_Jz = 0x0077F98C; // 6-byte jz -> NOP

static constexpr DWORD kBp51CombatJl_77E427 = 0x0077E427; // 7C 57
static constexpr DWORD kBp51CombatJl_77E533 = 0x0077E533; // 7C 57
static constexpr DWORD kBp51CombatJl_77E602 = 0x0077E602; // 7C 4F
static constexpr DWORD kBp51CombatJlNear_77EE1D = 0x0077EE1D; // 0F 8C ..
static constexpr DWORD kBp51CombatJlNear_77F004 = 0x0077F004; // 0F 8C ..

static constexpr DWORD kBp51DrawRedJge = 0x007FEEB3; // 7D 09
static constexpr DWORD kBp51WearJlNear_4F1CCA = 0x004F1CCA; // 0F 8C a3 0e 00 00
static constexpr DWORD kBp51InvalidJge_A288A8 = 0x00A288A8; // 7D 05 → EB 05 (edi=0)

static constexpr DWORD kEquipSlotPosBase = 0x00BE2260;
static constexpr DWORD kEquipSlotPosCashBase = 0x00BE23F0;
static constexpr int kShoulderIndex = 19; // BP20
static constexpr int kShoulderUiX = 137;
static constexpr int kShoulderUiY = 101;

struct EquipSlotPos {
    int x;
    int y;
};

static auto is_correct_bodypart =
    reinterpret_cast<int(__cdecl*)(int, int, int)>(0x00460358);

static auto get_bodypart_from_item =
    reinterpret_cast<int(__cdecl*)(int, int, int*, int)>(0x004606A0);

using IsAbleToWearFn = int(__thiscall*)(void*, int, int, int, int, int, int, int, int, char, int, int);
static IsAbleToWearFn is_able_to_wear =
    reinterpret_cast<IsAbleToWearFn>(0x004F2CEE);

static int __cdecl is_correct_bodypart_hook(int nItemID, int nBodyPart, int nGender) {
    const int prefix = nItemID / 10000;
    if (prefix == 115) {
        return nBodyPart == 20 ? 1 : 0;
    }
    if (prefix == 112) {
        return (nBodyPart == 17 || nBodyPart == 51) ? 1 : 0;
    }
    if (prefix == 113) {
        return nBodyPart == 50 ? 1 : 0;
    }
    if (prefix == 111 && kAcceptExtraRingBodyparts) {
        if (nBodyPart == 12 || nBodyPart == 13 || nBodyPart == 15 || nBodyPart == 16
            || nBodyPart == 52 || nBodyPart == 53) {
            return 1;
        }
        return 0;
    }
    // Pocket 116 → BP33 (main red 9)
    if (prefix == 116) {
        return nBodyPart == 33 ? 1 : 0;
    }
    // True split: 109 shield → BP10 (−10); 134/135 aux → BP62 (−62 Addon).
    if (prefix == 109) {
        return nBodyPart == 10 ? 1 : 0;
    }
    if (prefix == 134 || prefix == 135) {
        return nBodyPart == 62 ? 1 : 0;
    }
    // Badge 118 → BP54 (Addon)
    if (prefix == 118 && kEnableBadgeTotemBp54_55) {
        return nBodyPart == 54 ? 1 : 0;
    }
    // Totem 120 → BP55 only (Totem2–4 deferred; never accept 56–58 in SAFE_LAYOUT)
    if (prefix == 120 && kEnableBadgeTotemBp54_55) {
        if (kEnableTotem2to4) {
            return (nBodyPart >= 55 && nBodyPart <= 58) ? 1 : 0;
        }
        return nBodyPart == 55 ? 1 : 0;
    }
    // Emblem 119 → BP59 (Addon sidecar)
    if (prefix == 119 && kEnableEmblemBp59) {
        return nBodyPart == 59 ? 1 : 0;
    }
    // Android 166 → BP60 (Addon sidecar — NOT pet BP21)
    if (prefix == 166 && kEnableAndroidHeartNative) {
        return nBodyPart == kAndroidBodyPart ? 1 : 0;
    }
    // Heart 167 → BP61 (Addon sidecar — NOT pet BP22)
    if (prefix == 167 && kEnableAndroidHeartNative) {
        return nBodyPart == kHeartBodyPart ? 1 : 0;
    }
    return is_correct_bodypart(nItemID, nBodyPart, nGender);
}

static int write_bodyparts(int* outBodyParts, bool writeAll, std::initializer_list<int> parts) {
    if (parts.size() == 0) {
        return 0;
    }
    outBodyParts[0] = *parts.begin();
    if (!writeAll) {
        return 1;
    }
    int i = 0;
    for (int part : parts) {
        outBodyParts[i++] = part;
    }
    return i;
}

static int __cdecl get_bodypart_from_item_hook(int nItemID, int nGender, int* aBodyParts, int bWriteAll) {
    const int prefix = nItemID / 10000;
    if (prefix == 115) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {20});
    }
    if (prefix == 112) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {17, 51});
    }
    if (prefix == 113) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {50});
    }
    if (prefix == 111 && kEnableExtraRingsBp52_53) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {52, 53, 12, 13, 15, 16});
    }
    if (prefix == 116) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {33});
    }
    if (prefix == 109) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {10});
    }
    if (prefix == 134 || prefix == 135) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {62});
    }
    if (prefix == 118 && kEnableBadgeTotemBp54_55) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {54});
    }
    if (prefix == 120 && kEnableBadgeTotemBp54_55) {
        if (kEnableTotem2to4) {
            return write_bodyparts(aBodyParts, bWriteAll != 0, {55, 56, 57, 58});
        }
        return write_bodyparts(aBodyParts, bWriteAll != 0, {55});
    }
    if (prefix == 119 && kEnableEmblemBp59) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {59});
    }
    if (prefix == 166 && kEnableAndroidHeartNative) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {kAndroidBodyPart});
    }
    if (prefix == 167 && kEnableAndroidHeartNative) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {kHeartBodyPart});
    }
    return get_bodypart_from_item(nItemID, nGender, aBodyParts, bWriteAll);
}

// CRITICAL: never early-return before calling the original (cleanup via sub_4F5818).
// Bodypart maps live in is_correct_bodypart / get_bodypart_from_item (product −bp).
// CD64: return orig result → vanilla level/job/stat. Addon wear is SendChange→server.
// Vanilla EXE: keep force-OK for extended prefixes (sidecar era; Client_1 frozen).
static int __fastcall is_able_to_wear_hook(
    void* pThis, void* /*edx*/,
    int gender, int level, int job,
    int str, int dex, int intl, int luk, int pop,
    char a10, int a11, int itemId) {
    const int r = is_able_to_wear(
        pThis, gender, level, job, str, dex, intl, luk, pop, a10, a11, itemId);
    const int prefix = itemId / 10000;
    // Shoulder / pendant2 / ExtraRing — historically force OK on both paths.
    if (prefix == 115 || prefix == 112 || prefix == 111) {
        return 1;
    }
    if (UseNativeCd64Slots()) {
        (void)r;
        return r;
    }
    if (kEnableBadgeTotemBp54_55 && (prefix == 118 || prefix == 120)) {
        return 1;
    }
    if (kEnableAndroidHeartNative && (prefix == 166 || prefix == 167)) {
        return 1;
    }
    if (kEnableEmblemBp59 && prefix == 119) {
        return 1;
    }
    // Pocket 116 / shield 109 / aux 134|135 (−62 Addon)
    if (prefix == 116 || prefix == 109 || prefix == 134 || prefix == 135) {
        return 1;
    }
    (void)r;
    return r;
}

static void PatchShoulderUiCoords() {
    auto* regular = reinterpret_cast<EquipSlotPos*>(kEquipSlotPosBase);
    auto* cash = reinterpret_cast<EquipSlotPos*>(kEquipSlotPosCashBase);
    DWORD oldProt = 0;
    VirtualProtect(regular, 50 * sizeof(EquipSlotPos), PAGE_EXECUTE_READWRITE, &oldProt);
    regular[kShoulderIndex].x = kShoulderUiX;
    regular[kShoulderIndex].y = kShoulderUiY;
    VirtualProtect(regular, 50 * sizeof(EquipSlotPos), oldProt, &oldProt);

    VirtualProtect(cash, 50 * sizeof(EquipSlotPos), PAGE_EXECUTE_READWRITE, &oldProt);
    cash[kShoulderIndex].x = kShoulderUiX;
    cash[kShoulderIndex].y = kShoulderUiY;
    VirtualProtect(cash, 50 * sizeof(EquipSlotPos), oldProt, &oldProt);
}

static bool ExpectBytes(DWORD va, const unsigned char* expected, size_t n) {
    auto* p = reinterpret_cast<const unsigned char*>(va);
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static void PatchBp51TsecGatesLikeShoulder() {
    static const unsigned char kJl57[] = {0x7C, 0x57};
    static const unsigned char kJl4F[] = {0x7C, 0x4F};
    if (ExpectBytes(kBp51CombatJl_77E427, kJl57, sizeof(kJl57))) {
        Memory::PatchNop(kBp51CombatJl_77E427, 2);
    }
    if (ExpectBytes(kBp51CombatJl_77E533, kJl57, sizeof(kJl57))) {
        Memory::PatchNop(kBp51CombatJl_77E533, 2);
    }
    if (ExpectBytes(kBp51CombatJl_77E602, kJl4F, sizeof(kJl4F))) {
        Memory::PatchNop(kBp51CombatJl_77E602, 2);
    }

    static const unsigned char kJlNear77EE[] = {0x0F, 0x8C, 0x8F, 0x01, 0x00, 0x00};
    static const unsigned char kJlNear77F0[] = {0x0F, 0x8C, 0x1F, 0x01, 0x00, 0x00};
    if (ExpectBytes(kBp51CombatJlNear_77EE1D, kJlNear77EE, sizeof(kJlNear77EE))) {
        Memory::PatchNop(kBp51CombatJlNear_77EE1D, 6);
    }
    if (ExpectBytes(kBp51CombatJlNear_77F004, kJlNear77F0, sizeof(kJlNear77F0))) {
        Memory::PatchNop(kBp51CombatJlNear_77F004, 6);
    }

    if (ExpectBytes(kBp51DrawRedJge, (const unsigned char*)"\x7D\x09", 2)) {
        Memory::WriteByte(kBp51DrawRedJge, 0xEB);
    }

    static const unsigned char kJlNear4F1C[] = {0x0F, 0x8C, 0xA3, 0x0E, 0x00, 0x00};
    if (ExpectBytes(kBp51WearJlNear_4F1CCA, kJlNear4F1C, sizeof(kJlNear4F1C))) {
        Memory::PatchNop(kBp51WearJlNear_4F1CCA, 6);
    }

    if (ExpectBytes(kBp51InvalidJge_A288A8, (const unsigned char*)"\x7D\x05", 2)) {
        Memory::WriteByte(kBp51InvalidJge_A288A8, 0xEB);
    }
}

// --- RING_34_BIND + BADGE/TOTEM1: client inventory stores -52..-55 ---
// GetItem/SetItem/combat: cmp eax,-51 (CD) → raise to -55 (C9). HARD CAP.
// -52: native 8-byte gap before TSecType@Character+0x293.
// -53..-55: smash TSecType → DLL ZRef shadows.
// BP56–59: NOT wired (shadow tables reserved; ShadowZRefForBp returns null).
// BP21/22: pet seats (parked off main). Android/Heart BP60/61 = equipaddon sidecar.

static constexpr DWORD kGetItemBoundCmpImm = 0x0042834E + 2;   // 83 F8 CD
static constexpr DWORD kSetItemBoundCmpImm = 0x0047B0BF + 2;   // 83 F8 CD
static constexpr DWORD kCombatBoundCmpImm = 0x0077F879 + 2;   // 83 F8 CD

// Bound-site early remap: −152/−153 → −52/−53 *before* cash/normal split
// (cash path @ GetItem+0x42837A is NOT hooked by addr caves).
// DISABLED by kRemapCashExtendedRings (an): login SetItem(−152)→−52 desync/crash.
static constexpr DWORD kGetItemBoundSite = 0x0042834E; // 83 F8 CD 7D 09 83 F8 9C 0F 8D ..
static constexpr DWORD kGetItemBoundCont = 0x0042835C;
static constexpr DWORD kGetItemBoundFail = 0x004283EC;
static constexpr DWORD kSetItemBoundSite = 0x0047B0BF;
static constexpr DWORD kSetItemBoundCont = 0x0047B0CD;
static constexpr DWORD kSetItemBoundFail = 0x0047B169;

static constexpr DWORD kGetItemAddrShl = 0x0042838B;
static constexpr DWORD kGetItemAddrBack = 0x00428396;
static constexpr DWORD kSetItemAddrShl = 0x0047B10C;
static constexpr DWORD kSetItemAddrBack = 0x0047B117;

static constexpr DWORD kDrawHasItemMovEcx = 0x007FEEDB;
static constexpr DWORD kDrawHasItemBack = 0x007FEEE2;
static constexpr DWORD kDrawTestEsiJz = 0x007FEEC2; // 85 F6 74 15 8B 4D DC
static constexpr DWORD kDrawSpecialHasItemCont = 0x007FEECC;

alignas(8) static void* g_ring52ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_ring53ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_badge54ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_totem55ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_totem56ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_totem57ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_totem58ZRef[2] = {nullptr, nullptr};
alignas(8) static void* g_emblem59ZRef[2] = {nullptr, nullptr};
// Enter-safe: redirect bogus ZRef.p (<64K) here — never write into Char/TSec.
alignas(8) static void* g_emptyZRef[2] = {nullptr, nullptr};

static void* __cdecl Ring52_ZRefBase() { return &g_ring52ZRef[0]; }
static void* __cdecl Ring53_ZRefBase() { return &g_ring53ZRef[0]; }
static void* __cdecl Badge54_ZRefBase() { return &g_badge54ZRef[0]; }
static void* __cdecl Totem55_ZRefBase() { return &g_totem55ZRef[0]; }
static void* __cdecl Totem56_ZRefBase() { return &g_totem56ZRef[0]; }
static void* __cdecl Totem57_ZRefBase() { return &g_totem57ZRef[0]; }
static void* __cdecl Totem58_ZRefBase() { return &g_totem58ZRef[0]; }
static void* __cdecl Emblem59_ZRefBase() { return &g_emblem59ZRef[0]; }

// BP 52..53 → DLL ring ZRef*; BP54–62 → equipaddon sidecar (never native aEquipped).
static void* __cdecl ShadowZRefForBp(int bp) {
    switch (bp) {
    case 52: return &g_ring52ZRef[0];
    case 53: return &g_ring53ZRef[0];
    default:
        // Badge/totem1 + Addon 56–62: single sidecar store (not g_badge54/g_totem55).
        // Native aEquipped[54/55] aliases cash face/eye (−102/−103).
        if (bp >= 54 && bp <= 62) {
            return EquipAddon_SidecarZRefForBp(bp);
        }
        return nullptr;
    }
}

// Negative equip slot −52..−62 (cash already aliased) → ZRef*.
static void* __cdecl ShadowZRefForNegSlot(int nSlot) {
    if (nSlot >= -62 && nSlot <= -52) {
        return ShadowZRefForBp(-nSlot);
    }
    return nullptr;
}

static void ClearExtendedSlotShadows() {
    g_ring52ZRef[0] = g_ring52ZRef[1] = nullptr;
    g_ring53ZRef[0] = g_ring53ZRef[1] = nullptr;
    // Badge/totem1 live in equipaddon sidecar — clear local leftovers only.
    g_badge54ZRef[0] = g_badge54ZRef[1] = nullptr;
    g_totem55ZRef[0] = g_totem55ZRef[1] = nullptr;
    g_totem56ZRef[0] = g_totem56ZRef[1] = nullptr;
    g_totem57ZRef[0] = g_totem57ZRef[1] = nullptr;
    g_totem58ZRef[0] = g_totem58ZRef[1] = nullptr;
    g_emblem59ZRef[0] = g_emblem59ZRef[1] = nullptr;
}

// Remap cash extended ring slots then re-emit bound check (imm −59).
void __declspec(naked) GetItem_CashRingRemap_cave() {
    __asm {
        cmp eax, -152
        jne g_nr152
        mov eax, -52
    g_nr152:
        cmp eax, -153
        jne g_nr153
        mov eax, -53
    g_nr153:
        cmp eax, -59
        jge g_ok
        cmp eax, -100
        jge g_fail
    g_ok:
        push kGetItemBoundCont
        ret
    g_fail:
        push kGetItemBoundFail
        ret
    }
}

void __declspec(naked) SetItem_CashRingRemap_cave() {
    __asm {
        cmp eax, -152
        jne s_nr152
        mov eax, -52
    s_nr152:
        cmp eax, -153
        jne s_nr153
        mov eax, -53
    s_nr153:
        cmp eax, -59
        jge s_ok
        cmp eax, -100
        jge s_fail
    s_ok:
        push kSetItemBoundCont
        ret
    s_fail:
        push kSetItemBoundFail
        ret
    }
}

void __declspec(naked) GetItem_ExtraRingAddr_cave() {
    __asm {
        // FIX2: NEVER remap −152/−153 here (FIX_ENTER_INVALID / an). Cash remap
        // only via kRemapCashExtendedRings cave (kept false).
        // Addon cash mirrors −154..−162 only.
        cmp eax, -154
        jne get_not154
        mov eax, -54
    get_not154:
        cmp eax, -155
        jne get_not155
        mov eax, -55
    get_not155:
        cmp eax, -156
        jne get_not156
        mov eax, -56
    get_not156:
        cmp eax, -157
        jne get_not157
        mov eax, -57
    get_not157:
        cmp eax, -158
        jne get_not158
        mov eax, -58
    get_not158:
        cmp eax, -159
        jne get_not159
        mov eax, -59
    get_not159:
        cmp eax, -160
        jne get_not160
        mov eax, -60
    get_not160:
        cmp eax, -161
        jne get_not161
        mov eax, -61
    get_not161:
        cmp eax, -162
        jne get_not162
        mov eax, -62
    get_not162:
        // ExtraRing ON: shadows −62..−52 (Addon + rings + aux). Never native −53
        // (TSec smash @Char+0x293). −52 native gap is safe if shadow empty
        // (ApplyEquip chain miss wrote aEquipped[52] instead of g_ring52).
        cmp eax, -52
        jg native_get_direct
        cmp eax, -62
        jl native_get_direct
        // ShadowZRefForNegSlot is __cdecl and clobbers ecx — save CharacterData*
        // this or native fallback reads [ecx+0xEB] with garbage → AV @0xF2.
        push ecx
        push eax
        call ShadowZRefForNegSlot
        test eax, eax
        jz native_get_pop
        mov ecx, dword ptr [eax + 4]
        test ecx, ecx
        jnz get_shadow_hit
        // Empty shadow: −52 may fall back to native Char gap; −53..−62 return null
        // (native −53 is TSecType; −54+ alias cash face/eye).
        cmp dword ptr [esp], -52
        je native_get_pop
        pop eax
        pop eax
        xor ecx, ecx
        jmp dword ptr [kGetItemAddrBack]
    get_shadow_hit:
        pop eax
        pop eax
        jmp dword ptr [kGetItemAddrBack]
    native_get_pop:
        pop eax
        pop ecx
    native_get_direct:
        shl eax, 3
        sub ecx, eax
        mov ecx, dword ptr [ecx + 0xEB]
        jmp dword ptr [kGetItemAddrBack]
    }
}

void __declspec(naked) SetItem_ExtraRingAddr_cave() {
    __asm {
        // FIX2: no −152/−153 remap (see GetItem cave).
        cmp eax, -154
        jne set_not154
        mov eax, -54
    set_not154:
        cmp eax, -155
        jne set_not155
        mov eax, -55
    set_not155:
        cmp eax, -156
        jne set_not156
        mov eax, -56
    set_not156:
        cmp eax, -157
        jne set_not157
        mov eax, -57
    set_not157:
        cmp eax, -158
        jne set_not158
        mov eax, -58
    set_not158:
        cmp eax, -159
        jne set_not159
        mov eax, -59
    set_not159:
        cmp eax, -160
        jne set_not160
        mov eax, -60
    set_not160:
        cmp eax, -161
        jne set_not161
        mov eax, -61
    set_not161:
        cmp eax, -162
        jne set_not162
        mov eax, -62
    set_not162:
        // ExtraRing ON: Addon + rings + aux −62..−52 (same as GetItem).
        cmp eax, -52
        jg native_set_direct
        cmp eax, -62
        jl native_set_direct
        // Same ecx clobber as GetItem cave — restore this on native fallback.
        push ecx
        push eax
        call ShadowZRefForNegSlot
        test eax, eax
        jz native_set_pop
        mov ecx, eax
        pop eax
        pop eax
        jmp dword ptr [kSetItemAddrBack]
    native_set_pop:
        pop eax
        pop ecx
    native_set_direct:
        shl eax, 3
        sub ecx, eax
        add ecx, 0xE7
        jmp dword ptr [kSetItemAddrBack]
    }
}

// Force BP52..55 into 7FEEDB. Re-emit mov edx,[ebp-14h] on special path (af).
void __declspec(naked) DrawForceNormalBp52_53_cave() {
    __asm {
        cmp dword ptr [ebp + 8], 52
        jb native_test
        cmp dword ptr [ebp + 8], 55
        ja native_test
        xor esi, esi
        push kDrawHasItemMovEcx
        ret
    native_test:
        test esi, esi
        jz to_feedb
        mov ecx, dword ptr [ebp - 24h]
        mov edx, dword ptr [ebp - 14h]
        push kDrawSpecialHasItemCont
        ret
    to_feedb:
        push kDrawHasItemMovEcx
        ret
    }
}

// BP52–55 native Char ZRef (CD64): walker [ebp-20h]-0x1A0 == aEquipped[BP].p.
void __declspec(naked) DrawHasItem_Bp53_native_cave() {
    __asm {
        cmp dword ptr [ebp + 8], 52
        jb walk
        cmp dword ptr [ebp + 8], 55
        ja empty_hi
        mov ecx, dword ptr [ebp - 20h]
        test ecx, ecx
        jz empty_hi
        mov edi, dword ptr [ecx - 0x1A0]
        lea eax, [ecx - 0x1A0]
        mov dword ptr [ebp - 14h], eax
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    empty_hi:
        xor edi, edi
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    walk:
        mov ecx, dword ptr [ebp - 14h]
        test ecx, ecx
        jz empty_hi
        xor eax, eax
        cmp dword ptr [ecx], eax
        push kDrawHasItemBack
        ret
    }
}

// Legacy DLL shadows (pre-CD64 EXE only).
void __declspec(naked) DrawHasItem_Bp53_shadow_cave() {
    __asm {
        cmp dword ptr [ebp + 8], 52
        je do52
        cmp dword ptr [ebp + 8], 53
        je do53
        cmp dword ptr [ebp + 8], 54
        je do54
        cmp dword ptr [ebp + 8], 55
        je do55
        cmp dword ptr [ebp + 8], 55
        ja empty_hi
        jmp walk
    do52:
        // Prefer g_ring52; if ApplyEquip wrote native gap instead, read Char −52.
        call Ring52_ZRefBase
        mov edi, dword ptr [eax + 4]
        test edi, edi
        jnz do52_have
        mov ecx, dword ptr [ebp - 20h]
        test ecx, ecx
        jz do52_empty
        mov edi, dword ptr [ecx - 0x1A0]
        lea eax, [ecx - 0x1A0]
        mov dword ptr [ebp - 14h], eax
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    do52_have:
        lea ecx, [eax + 4]
        mov dword ptr [ebp - 14h], ecx
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    do52_empty:
        xor edi, edi
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    do53:
        call Ring53_ZRefBase
        mov edi, dword ptr [eax + 4]
        lea ecx, [eax + 4]
        mov dword ptr [ebp - 14h], ecx
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    do54:
        // Inventory is equipaddon sidecar (not local g_badge54 leftover).
        push 54
        call ShadowZRefForBp
        add esp, 4
        test eax, eax
        jz empty_hi
        mov edi, dword ptr [eax + 4]
        lea ecx, [eax + 4]
        mov dword ptr [ebp - 14h], ecx
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    do55:
        // Inventory is equipaddon sidecar (not local g_totem55 leftover).
        push 55
        call ShadowZRefForBp
        add esp, 4
        test eax, eax
        jz empty_hi
        mov edi, dword ptr [eax + 4]
        lea ecx, [eax + 4]
        mov dword ptr [ebp - 14h], ecx
        and dword ptr [ebp - 18h], 0
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    empty_hi:
        xor edi, edi
        xor eax, eax
        cmp edi, eax
        push kDrawHasItemBack
        ret
    walk:
        mov ecx, dword ptr [ebp - 14h]
        test ecx, ecx
        jz empty_hi
        xor eax, eax
        cmp dword ptr [ecx], eax
        push kDrawHasItemBack
        ret
    }
}

static constexpr DWORD kDblEquipEmptySearch = 0x004F0B58;
static constexpr DWORD kDblEquipSearchCont = 0x004F0B62;
static constexpr DWORD kDblEquipWearNative = 0x004F0B89;
static constexpr DWORD kDblEquipFailExit = 0x004EFD4C;
static constexpr DWORD kDblEquipOccCmp = 0x004F0B72;
static constexpr DWORD kDblEquipOccJoin = 0x004F0B7A;

static int g_ringDblClickRotateIdx = 0;

static bool __cdecl IsRingBodyPartListHead(int bp) {
    return bp == 52 || bp == 53 || bp == 12 || bp == 13 || bp == 15 || bp == 16;
}

static int __cdecl RingFullRotatePickIndex() {
    const int idx = g_ringDblClickRotateIdx % 6;
    g_ringDblClickRotateIdx = (g_ringDblClickRotateIdx + 1) % 1000000;
    return idx;
}
// PE: jl loop ; E9 rel32 fail. Jmp opcode is at 4F0B84 (NOT 4F0B85).
static constexpr DWORD kDblEquipAllFullJmp = 0x004F0B84;
static constexpr DWORD kWearFromDraggable = 0x004F1C2D;
static constexpr DWORD kOnDoubleClicked = 0x004EFD25;


// CDraggableItem::OnDoubleClicked — equipped (slot<0) must NOT run multi-BP
// empty-search. Primary unequip: SendChangeSlotPosition (same as drag→bag).
// wear() kept as secondary; multi-BP seats never fall into 6-BP all-full abort.
using OnDoubleClickedFn = int(__thiscall*)(void* pDraggable);
static OnDoubleClickedFn g_onDoubleClicked = reinterpret_cast<OnDoubleClickedFn>(kOnDoubleClicked);

using WearFromDraggableFn = int(__thiscall*)(void* pDraggable, int nSlot, int nBodyPart);
static WearFromDraggableFn g_wearFromDraggable =
    reinterpret_cast<WearFromDraggableFn>(kWearFromDraggable);

static constexpr DWORD kCWvsContextSingleton = 0x00BE7918;
static constexpr DWORD kOff_CharData_InCtx = 0x20B8;
static constexpr DWORD kAddr_CharacterData_GetItem = 0x004282F7;
static constexpr DWORD kAddr_SendChangeSlotPosition = 0x00A0900A;

struct ZRefItemOut {
    void* unused;
    void* pItem;
};

using CharacterData_GetItemFn =
    void(__thiscall*)(void* pCharData, ZRefItemOut* outRef, int nTI, int nPOS);
static auto CharacterData_GetItem =
    reinterpret_cast<CharacterData_GetItemFn>(kAddr_CharacterData_GetItem);

using SendChangeSlotPositionFn =
    void(__thiscall*)(void* pCtx, int nTI, int nOldPos, int nNewPos, int nCount);
static auto SendChangeSlotPosition =
    reinterpret_cast<SendChangeSlotPositionFn>(kAddr_SendChangeSlotPosition);

static void* GetLocalCharacterData() {
    void* pCtx = *reinterpret_cast<void**>(kCWvsContextSingleton);
    if (!pCtx) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(pCtx) + kOff_CharData_InCtx);
}

static void* GetEquippedItemAt(int nSlot) {
    void* pChar = GetLocalCharacterData();
    if (!pChar) {
        return nullptr;
    }
    ZRefItemOut out{};
    __try {
        CharacterData_GetItem(pChar, &out, 1, nSlot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return out.pItem;
}

static int FindEmptyEquipBagSlot() {
    void* pChar = GetLocalCharacterData();
    if (!pChar) {
        return 0;
    }
    for (int s = 1; s <= 96; ++s) {
        ZRefItemOut out{};
        __try {
            CharacterData_GetItem(pChar, &out, 1, s);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (!out.pItem) {
            return s;
        }
    }
    return 0;
}

// CWvsContext busy + last-send tick (same indices as equipaddon CtxPrepareSend).
static constexpr int kCtxDw_SendBusy = 2089;
static constexpr int kCtxDw_LastSendTick = 2090;
static constexpr DWORD kAddr_GetUpdateTime = 0x00987257;
using GetUpdateTimeFn = DWORD(__cdecl*)();
static auto GetUpdateTime = reinterpret_cast<GetUpdateTimeFn>(kAddr_GetUpdateTime);

static bool CtxForceClearSendBusy(void* pCtx) {
    if (!pCtx) {
        return false;
    }
    __try {
        auto* dw = reinterpret_cast<DWORD*>(pCtx);
        if (dw[kCtxDw_SendBusy]) {
            dw[kCtxDw_SendBusy] = 0;
            return true;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool CtxPrepareSend(void* pCtx) {
    if (!pCtx) {
        return false;
    }
    // Extended unequip SendChange: always clear leftover Addon wear busy first.
    CtxForceClearSendBusy(pCtx);
    return true;
}

// Direct ITEM_MOVE unequip (src<0, dest>0) — bypasses wear occ/empty-search.
static bool TrySendUnequipToBag(int equippedSlot) {
    if (equippedSlot >= 0) {
        return false;
    }
    if (!GetEquippedItemAt(equippedSlot)) {
        return false;
    }
    const int dest = FindEmptyEquipBagSlot();
    if (dest <= 0) {
        return false;
    }
    void* pCtx = *reinterpret_cast<void**>(kCWvsContextSingleton);
    if (!pCtx) {
        return false;
    }
    if (!CtxPrepareSend(pCtx)) {
        return false;
    }
    __try {
        // Native wear/unequip uses nCount=-1 (see wear@4F1C2D → A0900A).
        // nCount=1 broke some classic Equip dblclick unequip paths.
        SendChangeSlotPosition(pCtx, 1, equippedSlot, dest, -1);
        return reinterpret_cast<DWORD*>(pCtx)[kCtxDw_SendBusy] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Only seats whose native dblclick empty-search truly aborts unequip.
// Classic rings/pendant/pet (12–17, 21–22) MUST stay native — packet hijack +
// `return 0` on failure made ALL those slots (and often everything) undroppable.
static bool NeedsPacketUnequipBp(int bp) {
    return bp == 20 || bp == 51 || (bp >= 52 && bp <= 55);
}

static int __fastcall OnDoubleClicked_EquippedUnequip_hook(void* pThis, void* /*edx*/) {
    if (!pThis) {
        return g_onDoubleClicked(pThis);
    }
    const int slot = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(pThis) + 0x1C);
    // Clear stuck SendBusy before unequip dblclick (Addon reject can leave busy).
    void* pCtx = *reinterpret_cast<void**>(kCWvsContextSingleton);
    CtxForceClearSendBusy(pCtx);
    // Bag / cash-tab double-click: leave to original (+ damageskin / anvil hooks).
    if (slot >= 0) {
        return g_onDoubleClicked(pThis);
    }
    int bp = -slot;
    if (slot <= -100) {
        bp -= 100;
    }
    if (bp <= 0) {
        return g_onDoubleClicked(pThis);
    }
    // Classic main Equip: native only (hat/weapon/rings/armor/…).
    if (!NeedsPacketUnequipBp(bp)) {
        return g_onDoubleClicked(pThis);
    }
    // Shoulder / pendant2 / ExtraRing shadows: try packet, then wear(), then native.
    if (TrySendUnequipToBag(slot)) {
        return 1;
    }
    int* pItemTI = reinterpret_cast<int*>(reinterpret_cast<char*>(pThis) + 0x18);
    const int oldTI = *pItemTI;
    *pItemTI = 1;
    const int r = g_wearFromDraggable(pThis, slot, bp);
    *pItemTI = oldTI;
    if (r != 0) {
        return r;
    }
    // CRITICAL: never swallow unequip — fall through to native OnDoubleClicked.
    return g_onDoubleClicked(pThis);
}

// Login apply @4E5D55: lea ecx,[eax+esi*8+0xE7] then ZRef-assign — bypasses
// CharacterData::SetItem. Rings BP52–53 → DLL shadows; badge/totem+Addon 54–62
// → equipaddon sidecar (chained outer cave). Never native aEquipped[54+] —
// IDA: 52-slot array; esi=54≡cash face, esi=55≡cash eye.
static constexpr DWORD kApplyEquipZRefLea = 0x004E5D55;
static constexpr DWORD kApplyEquipZRefCont = 0x004E5D5C; // call ZRef-assign

void __declspec(naked) ApplyEquipZRef_Shadow_cave() {
    __asm {
        // Rings only. BP54–62 handled by equipaddon outer cave → sidecar.
        cmp esi, 52
        jb a_native
        cmp esi, 53
        ja a_native
        push eax
        push esi
        call ShadowZRefForBp
        add esp, 4
        mov ecx, eax
        pop eax
        push kApplyEquipZRefCont
        ret
    a_native:
        lea ecx, [eax + esi * 8 + 0xE7]
        push kApplyEquipZRefCont
        ret
    }
}

static bool g_dblUnequipHooked = false;

static void InstallDblClickUnequipHook() {
    // Attach once as outermost. Call ONLY after damageskinpicker's OnDoubleClick
    // DetourAttach (first CField), or from DllMain when late UI hooks are off.
    // Do NOT Detach+re-Attach over a dual-ptr chain — that restores PE bytes and
    // can wipe the damageskin jmp. Do NOT attach the same detour twice (shared
    // trampoline → recursion on bag passthrough).
    if (g_dblUnequipHooked) {
        return;
    }
    g_onDoubleClicked = reinterpret_cast<OnDoubleClickedFn>(kOnDoubleClicked);
    if (Memory::SetHook(true, reinterpret_cast<void**>(&g_onDoubleClicked),
            CastHook(&OnDoubleClicked_EquippedUnequip_hook))) {
        g_dblUnequipHooked = true;
    }
}

// wear@4F1C2D validates via native aEquipped[bp] @ Char+bp*8+0x28B (+0xEB cash).
// BP52–61 live in DLL shadows/sidecar — native miss → cmp fails → wear returns 0.
// Drag unequip uses GetItem (shadowed); dblclick uses wear — redirect these reads.
static constexpr DWORD kWearOccA = 0x004F1FD2; // 8B 84 C1 8B 02 00 00
static constexpr DWORD kWearOccAJoin = 0x004F1FD9; // 3B C7
// B is NOT 4F2041 — that VA is mid-imm of the same insn (ExpectBytes always failed).
static constexpr DWORD kWearOccB = 0x004F203E; // 8B 84 C1 8B 02 00 00
static constexpr DWORD kWearOccBJoin = 0x004F2045; // 3B C7
static constexpr DWORD kWearOccCash = 0x004F20B8; // +0xEB
static constexpr DWORD kWearOccCashJoin = 0x004F20BF;

void __declspec(naked) Wear_OccShadow_cave_A() {
    __asm {
        cmp eax, 52
        jb natA
        cmp eax, 62
        ja natA
        push ecx
        push edx
        push eax
        call ShadowZRefForBp
        add esp, 4
        mov eax, dword ptr [eax + 4]
        pop edx
        pop ecx
        push kWearOccAJoin
        ret
    natA:
        mov eax, dword ptr [ecx + eax * 8 + 0x28B]
        push kWearOccAJoin
        ret
    }
}

void __declspec(naked) Wear_OccShadow_cave_B() {
    __asm {
        cmp eax, 52
        jb natB
        cmp eax, 62
        ja natB
        push ecx
        push edx
        push eax
        call ShadowZRefForBp
        add esp, 4
        mov eax, dword ptr [eax + 4]
        pop edx
        pop ecx
        push kWearOccBJoin
        ret
    natB:
        mov eax, dword ptr [ecx + eax * 8 + 0x28B]
        push kWearOccBJoin
        ret
    }
}

void __declspec(naked) Wear_OccShadow_cave_Cash() {
    __asm {
        cmp eax, 52
        jb natC
        cmp eax, 62
        ja natC
        push ecx
        push edx
        push eax
        call ShadowZRefForBp
        add esp, 4
        mov eax, dword ptr [eax + 4]
        pop edx
        pop ecx
        push kWearOccCashJoin
        ret
    natC:
        mov eax, dword ptr [ecx + eax * 8 + 0xEB]
        push kWearOccCashJoin
        ret
    }
}

// Combat/localstat loop @77F879: address aEquipped ZRef then cmp [edi+4],0.
// Bound raise alone still uses native Char math → −52/−53 gap empty → stats skip.
// Redirect −52..−55 to DLL shadows (same as GetItem/SetItem/Draw).
static constexpr DWORD kCombatOccSite = 0x0077F879;
static constexpr DWORD kCombatOccJoin = 0x0077F894; // cmp dword ptr [edi+4], 0

// Main primary-stat sum sub_77EC9F @77EDF2: positive BP loop.
// BP<=51 → normal aEquipped walker; BP>51 → cash walker — misses DLL shadows
// for BP52–55. Mirror Combat_OccShadow so STR/DEX/INT/LUK/HP/MP from
// −52/−53/−54/−55 feed the main 四维 the same way as detail combat.
//
// IMPORTANT (MAINSTAT_OCCSHADOW_EBPFIX): BeiDou.exe uses [ebp+0x10] / [ebp-4],
// NOT IDA's stale frame names arg_8@+0x34 / var_4@+0x20. Prior stamp's
// ExpectBytes (8B 45 34 … 8B 45 20) never matched → cave never installed.
static constexpr DWORD kMainStatOccSite = 0x0077EDF2;
static constexpr DWORD kMainStatOccJoin = 0x0077EDFA; // cmp dword ptr [eax+4], 0

void __declspec(naked) MainStat_OccShadow_cave() {
    __asm {
        // ADDON_SI_STATS: BP54–61 → shadows/sidecar.
        // ExtraRing OFF: BP52/53 must NOT use native cash walker (TSec smash).
        cmp ebx, 52
        je m_empty
        cmp ebx, 53
        je m_empty
        cmp ebx, 54
        jb m_native
        cmp ebx, 62
        ja m_native
        push ebx
        call ShadowZRefForBp
        add esp, 4
        test eax, eax
        jz m_native
        jmp m_sanitize
    m_empty:
        lea eax, g_emptyZRef
        jmp m_join
    m_native:
        // Original: mov eax,[ebp+10h]; jg keep; mov eax,[ebp-4]
        // Flags at entry are from `cmp ebx, 51` @77EDEF — re-check ebx.
        mov eax, dword ptr [ebp + 0x10] // cash/ext walker (live PE)
        cmp ebx, 0x33
        jg m_sanitize
        mov eax, dword ptr [ebp - 0x04] // normal aEquipped walker
    m_sanitize:
        // Drop TSec/garbage p (<64K) so cmp [eax+4],0 skips instead of AV.
        test eax, eax
        jz m_join
        mov ecx, dword ptr [eax + 4]
        test ecx, ecx
        jz m_join
        cmp ecx, 0x10000
        jae m_join
        lea eax, g_emptyZRef
    m_join:
        push kMainStatOccJoin
        ret
    }
}

void __declspec(naked) Combat_OccShadow_cave() {
    __asm {
        // ADDON_SI_STATS: −61..−54 (+ cash −161..−154) → shadows/sidecar.
        // ExtraRing OFF: −53/−52/−153/−152 → empty ZRef (never native TSec).
        cmp eax, -154
        jne c_n154
        mov eax, -54
    c_n154:
        cmp eax, -155
        jne c_n155
        mov eax, -55
    c_n155:
        cmp eax, -156
        jne c_n156
        mov eax, -56
    c_n156:
        cmp eax, -157
        jne c_n157
        mov eax, -57
    c_n157:
        cmp eax, -158
        jne c_n158
        mov eax, -58
    c_n158:
        cmp eax, -159
        jne c_n159
        mov eax, -59
    c_n159:
        cmp eax, -160
        jne c_n160
        mov eax, -60
    c_n160:
        cmp eax, -161
        jne c_n161
        mov eax, -61
    c_n161:
        cmp eax, -162
        jne c_n162
        mov eax, -62
    c_n162:
        // Ring seats: ExtraRing OFF — skip native entirely (enter AV root).
        cmp eax, -52
        je c_use_empty
        cmp eax, -53
        je c_use_empty
        cmp eax, -152
        je c_use_empty
        cmp eax, -153
        je c_use_empty
        cmp eax, -54
        jg c_fallback
        cmp eax, -62
        jl c_cashpath
        // ShadowZRefForNegSlot is __cdecl — keep eax (slot); edi becomes ZRef*.
        push eax
        call ShadowZRefForNegSlot
        mov edi, eax
        pop eax
        test edi, edi
        jz c_fallback
        // edi already absolute ZRef* — do NOT sub (join is past native sub).
        jmp c_sanitize
    c_fallback:
        cmp eax, -51
        jl c_cashpath
        jmp c_normal
    c_cashpath:
        // Native cash: mov edi,[ebp+18]; lea ecx,[eax*8+198]; sub edi,ecx
        // Join target is 77F894 (cmp [edi+4]) which is AFTER sub @77F892 —
        // must apply sub here or cash slots read base+4 as ZRef.p (enter AV).
        mov edi, dword ptr [ebp + 18h]
        lea ecx, [eax * 8 + 0x198]
        sub edi, ecx
        jmp c_sanitize
    c_normal:
        mov edi, dword ptr [ebp + 14h]
        mov ecx, eax
        shl ecx, 3
        sub edi, ecx
    c_sanitize:
        // read target 0x69 ⇒ ZRef.p==1. Never write into [edi+4] (may be TSec).
        test edi, edi
        jz c_use_empty
        mov ecx, dword ptr [edi + 4]
        test ecx, ecx
        jz c_join
        cmp ecx, 0x10000
        jae c_join
    c_use_empty:
        lea edi, g_emptyZRef
    c_join:
        push kCombatOccJoin
        ret
    }
}

void __declspec(naked) DblClick_EquippedUnequip_cave() {
    __asm {
        mov eax, dword ptr [ebp - 18h]
        test eax, eax
        jz do_search
        mov edx, dword ptr [eax + 1Ch]
        test edx, edx
        jns do_search
        // slot < 0 → single-BP unequip (aj). Force ItemTI=EQUIPPED for wear.
        mov dword ptr [eax + 18h], 1
        mov eax, edx
        neg edx
        cmp eax, -100
        jg bp_ready
        sub edx, 100
    bp_ready:
        test edx, edx
        jle do_search
        mov dword ptr [ebp - 124h], edx
        xor ecx, ecx
        mov eax, 0x004F0B89
        push eax
        ret
    do_search:
        lea eax, [ebp - 124h]
        mov edx, dword ptr [eax]
        cmp edx, 52
        je ring_restore
        cmp edx, 53
        je ring_restore
        cmp edx, 12
        je ring_restore
        cmp edx, 13
        je ring_restore
        cmp edx, 15
        je ring_restore
        cmp edx, 16
        je ring_restore
        jmp search_go
    ring_restore:
        cmp dword ptr [eax + 4], 0
        je search_go
        mov edi, 6
    search_go:
        xor ecx, ecx
        test edi, edi
        jle to_fail
        mov eax, 0x004F0B62
        push eax
        ret
    to_fail:
        mov eax, 0x004EFD4C
        push eax
        ret
    }
}

// Occupancy: BP52–59 read shadows, not Char gap / TSecType.
// CRITICAL: empty-search loop keeps anBodyPart cursor in eax and index in ecx.
// After occupied: `inc ecx; add eax,4; cmp ecx,edi; jl`. Must NOT clobber eax/ecx
// (call ZRefBase returns in eax). pop does not alter ZF from cmp [zref+4],0.
void __declspec(naked) DblClick_OccBp53_cave() {
    __asm {
        // BP52–62: shadows (52–55) + equipaddon sidecar (56–62).
        cmp edx, 52
        jb native_occ
        cmp edx, 62
        ja native_occ
        push eax
        push ecx
        push edx              // preserve bodypart (edx is volatile across call)
        push edx              // arg BP
        call ShadowZRefForBp
        add esp, 4
        cmp dword ptr [eax + 4], 0
        pop edx
        pop ecx
        pop eax
        push 0x004F0B7A
        ret
    native_occ:
        cmp dword ptr [esi + edx * 8 + 0x28B], 0
        push 0x004F0B7A
        ret
    }
}

// All 6 ring BPs occupied: PE @4F0B85 jmp fail. For rings, rotate ecx and wear.
// Wear entry @4F0B89 uses push [ebp+ecx*4-124h] — ecx is bodypart list index.
void __declspec(naked) DblClick_RingFullRotate_cave() {
    __asm {
        lea eax, [ebp - 124h]
        mov edx, dword ptr [eax]
        push edx
        call IsRingBodyPartListHead
        add esp, 4
        test eax, eax
        jz fail_native
        call RingFullRotatePickIndex
        mov ecx, eax
        push kDblEquipWearNative
        ret
    fail_native:
        push kDblEquipFailExit
        ret
    }
}

// HitTest @BE2260 is ONLY 50 slots (BP1–50); cash table begins immediately at
// BE23F0. Writing index 51+ (BP52+) smashes cash hat/face coords → dual tooltips,
// "hat moved", drag-to-3/4 fail, unequip weirdness. Use a DLL-local HT table.
// Size 60: indices 0..59 cover BP1..BP60; walk end stays at BP55 (Addon-only
// BP56–59 are not main-Equip hit targets).
alignas(8) static EquipSlotPos g_hitTestExt[60]{};
static bool g_hitTestExtReady = false;

static void RestoreCashEquipSlotPosFromDefaults() {
    // Pristine BeiDou-Client cash table @BE23F0 (first 6); full 50 not required
    // if we no longer overflow HT into cash — still repair runtime corruption.
    static constexpr DWORD kCash = 0x00BE23F0;
    static const EquipSlotPos kCashDefault[6] = {
        {38, 35},
        {38, 68},
        {71, 101},
        {104, 101},
        {38, 134},
        {38, 167},
    };
    auto* cash = reinterpret_cast<EquipSlotPos*>(kCash);
    DWORD oldProt = 0;
    if (!VirtualProtect(cash, sizeof(kCashDefault), PAGE_EXECUTE_READWRITE, &oldProt)) {
        return;
    }
    for (int i = 0; i < 6; ++i) {
        cash[i] = kCashDefault[i];
    }
    VirtualProtect(cash, sizeof(kCashDefault), oldProt, &oldProt);
}

// HitTest + draw coords for BP52–53 (rings) at DllMain. Badge/totem/android/heart
// /emblem live on Addon — main Equip coords forced off-panel.
// NO ForceDrawLoop / NO flag cave (those + early HT historically → login ALL_IDLE).
static void PatchExtendedSlotHitTestAtDllMain() {
    static constexpr DWORD kHitTestTable = 0x00BE2260;
    static constexpr DWORD kGetSlotXyTable = 0x00BE2580;
    static constexpr DWORD kBe27Table = 0x00BE27E0;
    static constexpr DWORD kCharHitTestFunc = 0x007FEC32;
    static constexpr DWORD kCharHitTestEndCmp = 0x007FEC6F;
    // PE: 33 D2 B9 64 22 BE 00 → mov ecx, BE2264 (= &aEquipSlotPos[0].y).
    // Walker uses *(ecx-1)=X, *ecx=Y, then ecx+=8. End cmp is BE23F4 (= &[50].y).
    // Writing &table[0].x here shifts every hitbox by 4 bytes → hat dead, wrong tips.
    static constexpr DWORD kCharHitTestTableImm = 0x007FEC35; // imm32 of mov ecx, imm

    auto patchSlot = [](DWORD table, int index, int x, int y) {
        auto* slots = reinterpret_cast<EquipSlotPos*>(table);
        DWORD oldProt = 0;
        if (!VirtualProtect(slots + index, sizeof(EquipSlotPos), PAGE_EXECUTE_READWRITE, &oldProt)) {
            return;
        }
        slots[index].x = x;
        slots[index].y = y;
        VirtualProtect(slots + index, sizeof(EquipSlotPos), oldProt, &oldProt);
    };

    // Build DLL HT: copy native BP1–50, then BP51–55 at indices 50–54.
    if (!g_hitTestExtReady) {
        auto* pe = reinterpret_cast<EquipSlotPos*>(kHitTestTable);
        for (int i = 0; i < 50; ++i) {
            g_hitTestExt[i] = pe[i];
        }
        // Native BP20 HT is (71,233) — below the equip panel. Icon draws at
        // red-8 (137,101) via GetSlotXY; tip/dblclick use this HitTest walk.
        // Must remap here: pendant2 only writes PE BE2260, which GetBodyPart
        // no longer reads after the DLL-table redirect.
        g_hitTestExt[kShoulderIndex] = {kShoulderUiX, kShoulderUiY}; // BP20
        // Android/Heart: hide from main Equip (Addon owns these seats).
        g_hitTestExt[20] = {kOffPanelX, kOffPanelY}; // BP21
        g_hitTestExt[21] = {kOffPanelX, kOffPanelY}; // BP22
        // BP51 (index 50): do not read pe[50] (cash[0]); pendant2 red7.
        g_hitTestExt[50] = {38, 101};
        g_hitTestExt[51] = {104, 35};    // BP52 red3
        g_hitTestExt[52] = {137, 35};    // BP53 red4
        // BP54/55: was main red 9/10 — moved to Addon (off-panel on main).
        g_hitTestExt[53] = {kOffPanelX, kOffPanelY}; // BP54 badge
        g_hitTestExt[54] = {kOffPanelX, kOffPanelY}; // BP55 totem1
        g_hitTestExt[55] = {0, 0};
        // Main red 9 pocket (historical 9/10 GetSlotXY remap). BP10 left vanilla.
        g_hitTestExt[32] = {104, 200}; // BP33 pocket
        g_hitTestExtReady = true;
    } else {
        // Re-assert shoulder if pendant2 / other patches race after first build.
        g_hitTestExt[kShoulderIndex] = {kShoulderUiX, kShoulderUiY};
        g_hitTestExt[20] = {kOffPanelX, kOffPanelY};
        g_hitTestExt[21] = {kOffPanelX, kOffPanelY};
        g_hitTestExt[53] = {kOffPanelX, kOffPanelY};
        g_hitTestExt[54] = {kOffPanelX, kOffPanelY};
        g_hitTestExt[32] = {104, 200};
    }

    // Point ecx at first Y (PE BE2264 style), end at Y of index 55 (past BP55).
    const DWORD ourBase = reinterpret_cast<DWORD>(&g_hitTestExt[0]);
    const DWORD ourEcxY = ourBase + 4;
    const DWORD ourEnd = ourBase + 55 * 8 + 4;
    Memory::WriteInt(kCharHitTestTableImm, static_cast<unsigned int>(ourEcxY));
    if (*reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp) == 0x81
        && *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 1) == 0xF9) {
        Memory::WriteInt(kCharHitTestEndCmp + 2, static_cast<unsigned int>(ourEnd));
    }

    // GetSlotXY has room past 50. BE27 uses BP as index. Never write PE HT[50+].
    // Shoulder (BP20): keep draw tables in sync with DLL HitTest red-8.
    patchSlot(kGetSlotXyTable, kShoulderIndex, kShoulderUiX, kShoulderUiY);
    patchSlot(kBe27Table, kShoulderIndex + 1, kShoulderUiX, kShoulderUiY);
    patchSlot(kGetSlotXyTable, 20, kOffPanelX, kOffPanelY); // BP21 android
    patchSlot(kBe27Table, 21, kOffPanelX, kOffPanelY);
    patchSlot(kGetSlotXyTable, 21, kOffPanelX, kOffPanelY); // BP22 heart
    patchSlot(kBe27Table, 22, kOffPanelX, kOffPanelY);
    patchSlot(kGetSlotXyTable, 50, 38, 101);
    patchSlot(kGetSlotXyTable, 51, 104, 35);
    patchSlot(kBe27Table, 52, 104, 35);
    patchSlot(kGetSlotXyTable, 52, 137, 35);
    patchSlot(kBe27Table, 53, 137, 35);
    // Badge/totem OFF main Equip (Addon paints them).
    patchSlot(kGetSlotXyTable, 53, kOffPanelX, kOffPanelY);
    patchSlot(kBe27Table, 54, kOffPanelX, kOffPanelY);
    patchSlot(kGetSlotXyTable, 54, kOffPanelX, kOffPanelY);
    patchSlot(kBe27Table, 55, kOffPanelX, kOffPanelY);
    // Main red 9 pocket (GetSlotXY/BE27). BP10 untouched — 109 vanilla;
    // EquipAddon wires 134/135 → red10 only while Equip open.
    patchSlot(kGetSlotXyTable, 32, 104, 200);
    patchSlot(kBe27Table, 33, 104, 200);

    RestoreCashEquipSlotPosFromDefaults();
    (void)kCharHitTestFunc;
}

// Clear shadows at the start of each login inventory apply so stale pointers from a
// previous character session cannot mask a skipped SetItem (looks like “unequipped”).
void __declspec(naked) LoginApply_ClearShadows_cave() {
    __asm {
        pushad
        call ClearExtendedSlotShadows
        popad
        // stolen: cmp esi, 1  (83 FE 01) — raiseApplyMax sites sit after this
        cmp esi, 1
        push 0x004E5D06
        ret
    }
}

static void PatchLoginApplyClearShadows() {
    // Just before first cmp esi,51 block: at 4E5D03 is `cmp esi,1` (83 FE 01).
    // Steal 5 bytes? 83 FE 01 is only 3. Use site of `cmp esi,51` itself as dual patch:
    // we already raise imm; also clear shadows once per apply entry via jmp cave that
    // re-emits cmp esi,55 and jg.
    static constexpr DWORD kApplyEntry = 0x004E5D03; // 83 FE 01
    static const unsigned char kCmpEsi1[] = {0x83, 0xFE, 0x01};
    if (!ExpectBytes(kApplyEntry, kCmpEsi1, sizeof(kCmpEsi1))) {
        return;
    }
    // 5-byte cave needs more room — patch the 6-byte run: 83 FE 01 C6 45 FC is messy.
    // Instead clear on every SetItem(-52..-55) first write per session via flag below.
}

static bool g_clearedShadowsThisApply = false;

static void __cdecl MaybeClearShadowsOnExtendedSet(int slot) {
    const int n = (slot <= -100) ? (slot + 100) : slot;
    if (n > -52 || n < -55) {
        return;
    }
    if (!g_clearedShadowsThisApply) {
        ClearExtendedSlotShadows();
        g_clearedShadowsThisApply = true;
    }
}

// Reset clear-gate when leaving field / on DllMain bind.
static void ResetShadowClearGate() {
    g_clearedShadowsThisApply = false;
}

// Native draw skip @7FEE89: bp==14 || (bp>20 && bp<=48).
// ENTER_FIX3: do NOT install BP33-only cave. Live ENTER_SAFE_POCKET still crashed
// select→enter with 0xC0000409 (ucrtbase fastfail) while pocket 116 was equipped;
// drawing −33 in sub_7FEC81 during enter is unsafe. Keep vanilla upper=48.
// (SHIELD 48→32 also unsafe — painted BP34–48.) Restore imm=0x30 if a prior
// session left widen=0x20 in this process image (fresh EXE is already 0x30).
static constexpr bool kEnableBp33DrawCave = false; // ENTER-UNSAFE — keep false
static constexpr DWORD kDrawSkipUpper = 0x007FEE89; // cmp [ebp+8], imm8 ; jle skip

static void EnsureVanillaDrawSkipUpper48() {
    auto* p = reinterpret_cast<unsigned char*>(kDrawSkipUpper);
    // Only rewrite imm when still the cmp form (not an E9 cave from older DLL).
    if (p[0] == 0x83 && p[1] == 0x7D && p[2] == 0x08 && p[4] == 0x0F && p[5] == 0x8E) {
        if (p[3] != 0x30) {
            Memory::WriteByte(kDrawSkipUpper + 3, 0x30);
        }
    }
}

static void PatchDrawLoopAllowPocketBp33() {
    if (!kEnableBp33DrawCave) {
        EnsureVanillaDrawSkipUpper48();
        return;
    }
}

// Draw loop-end @7FEFB9: ae-style fixed max (NOT ForceDrawLoop setne→mov al,1).
// Keep max=55 so BP56–59 are Addon-only (main Equip must not paint them).
// Install at DllMain so BP52–55 icons work even if pendant2 UI attaches later.
static void PatchCharDrawMaxAtDllMain() {
    static constexpr DWORD kLoopSetne = 0x007FEFB9;
    static unsigned char kFixedMax[] = {0xB8, kDrawLoopMaxImm, 0x00, 0x00, 0x00, 0x90};
    static const unsigned char kFixedMax53[] = {0xB8, 0x35, 0x00, 0x00, 0x00, 0x90};
    static const unsigned char kFixedMax55[] = {0xB8, 0x37, 0x00, 0x00, 0x00, 0x90};
    if (!ExpectBytes(kLoopSetne, kFixedMax, sizeof(kFixedMax))) {
        if (ExpectBytes(kLoopSetne, kFixedMax53, sizeof(kFixedMax53))
            || ExpectBytes(kLoopSetne, kFixedMax55, sizeof(kFixedMax55))) {
            Memory::WriteByteArray(kLoopSetne, kFixedMax, sizeof(kFixedMax));
        } else {
            static const unsigned char kExpect32[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x32};
            static const unsigned char kExpect34[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x34};
            static const unsigned char kExpect35[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x35};
            if (ExpectBytes(kLoopSetne, kExpect32, sizeof(kExpect32))
                || ExpectBytes(kLoopSetne, kExpect34, sizeof(kExpect34))
                || ExpectBytes(kLoopSetne, kExpect35, sizeof(kExpect35))) {
                Memory::WriteByteArray(kLoopSetne, kFixedMax, sizeof(kFixedMax));
            }
        }
    }
    // Early entry add-imm @7FEE54: allow start ≤55 (loop-end still caps body).
    static constexpr DWORD kEarlyAdd = 0x007FEE54;
    const unsigned char earlyImm = *reinterpret_cast<const unsigned char*>(kEarlyAdd + 2);
    if (*reinterpret_cast<const unsigned char*>(kEarlyAdd) == 0x83
        && *reinterpret_cast<const unsigned char*>(kEarlyAdd + 1) == 0xC1
        && (earlyImm == 0x32 || earlyImm == 0x34 || earlyImm == 0x35 || earlyImm == 0x37
            || earlyImm == 0x3B)) {
        Memory::WriteByte(kEarlyAdd + 2, kDrawLoopMaxImm);
    }
    PatchDrawLoopAllowPocketBp33();
}

static void PatchExtraRingInventoryBind() {
    // ENTER_SAFE_UI: never skip this whole bind when Addon Get/Set is ON —
    // OCC_OFF early-return dropped Get/Set caves with ExtraRing OFF.
    const bool wantAddon = kEnableAddonSlotGetSet || kEnableAddonOccShadow;
    const bool wantRings = kBindExtraRingInventory && kEnableExtraRingsBp52_53;
    if (!wantAddon && !wantRings) {
        return;
    }

    const bool nativeCd64 = UseNativeCd64Slots();
    ClearExtendedSlotShadows();

    // Route A / CD64: EXE already has bound −63 + grown aEquipped. Do NOT install
    // shadow Get/Set caves (they steal −52…−62 into DLL ZRefs). Leave/poke bound −63.
    if (nativeCd64) {
        auto isBoundImm = [](unsigned char b) {
            return b == 0xCD || b == 0xCB || b == 0xCC || b == 0xC9 || b == 0xC5 || b == 0xC1
                || b == 0xC3;
        };
        const unsigned char bGet = *reinterpret_cast<const unsigned char*>(kGetItemBoundCmpImm);
        if (isBoundImm(bGet)) {
            Memory::WriteByte(kGetItemBoundCmpImm, kExtSlotBoundImmCd64); // −63
        }
        const unsigned char bSet = *reinterpret_cast<const unsigned char*>(kSetItemBoundCmpImm);
        if (isBoundImm(bSet)) {
            Memory::WriteByte(kSetItemBoundCmpImm, kExtSlotBoundImmCd64);
        }
        // Skip shadow addr / cash-remap caves entirely below.
    } else {
    // IMPORTANT (al): install address caves *before* raising bound past −51.
    // ak wrote bound→−55 first; if cave ExpectBytes failed, −53/−54/−55 hit native
    // ZRef math and smash TSecType @Char+0x293 → wear/unequip + login shadows break.
    // am remap caves OFF (an): −152→−52 on login cash apply → enter-map crash.
    static const unsigned char kGetBound14[] = {
        0x83, 0xF8, 0xCD, 0x7D, 0x09, 0x83, 0xF8, 0x9C, 0x0F, 0x8D, 0x90, 0x00, 0x00, 0x00};
    static const unsigned char kSetBound14[] = {
        0x83, 0xF8, 0xCD, 0x7D, 0x09, 0x83, 0xF8, 0x9C, 0x0F, 0x8D, 0x9C, 0x00, 0x00, 0x00};
    auto bound14Match = [](DWORD va, const unsigned char* expectCd) {
        auto* p = reinterpret_cast<const unsigned char*>(va);
        if (p[0] != 0x83 || p[1] != 0xF8) {
            return false;
        }
        const unsigned char imm = p[2];
        if (imm != 0xCD && imm != 0xCB && imm != 0xCC && imm != 0xC9 && imm != 0xC3
            && imm != 0xC5 && imm != 0xC1) {
            return false;
        }
        for (size_t i = 3; i < 14; ++i) {
            if (p[i] != expectCd[i]) {
                return false;
            }
        }
        return true;
    };
    const bool getBoundAlready = (*reinterpret_cast<const unsigned char*>(kGetItemBoundSite) == 0xE9);
    const bool setBoundAlready = (*reinterpret_cast<const unsigned char*>(kSetItemBoundSite) == 0xE9);
    bool getBoundCaved = false;
    bool setBoundCaved = false;
    if (kRemapCashExtendedRings) {
        getBoundCaved = getBoundAlready;
        setBoundCaved = setBoundAlready;
        if (!getBoundAlready && bound14Match(kGetItemBoundSite, kGetBound14)) {
            Memory::CodeCave(GetItem_CashRingRemap_cave, kGetItemBoundSite, 14);
            getBoundCaved = (*reinterpret_cast<const unsigned char*>(kGetItemBoundSite) == 0xE9);
        }
        if (!setBoundAlready && bound14Match(kSetItemBoundSite, kSetBound14)) {
            Memory::CodeCave(SetItem_CashRingRemap_cave, kSetItemBoundSite, 14);
            setBoundCaved = (*reinterpret_cast<const unsigned char*>(kSetItemBoundSite) == 0xE9);
        }
    }
    // When remap OFF: always poke bound imm (ignore leftover am E9 — need clean PE).

    static const unsigned char kGetAddr[] = {
        0xC1, 0xE0, 0x03, 0x2B, 0xC8, 0x8B, 0x89, 0xEB, 0x00, 0x00, 0x00};
    static const unsigned char kSetAddr[] = {
        0xC1, 0xE0, 0x03, 0x2B, 0xC8, 0x81, 0xC1, 0xE7, 0x00, 0x00, 0x00};
    const bool getAlreadyJmp = (*reinterpret_cast<const unsigned char*>(kGetItemAddrShl) == 0xE9);
    const bool setAlreadyJmp = (*reinterpret_cast<const unsigned char*>(kSetItemAddrShl) == 0xE9);
    bool getCaved = getAlreadyJmp;
    bool setCaved = setAlreadyJmp;
    // Addon seats need Get/Set caves even when ExtraRing 52/53 is OFF.
    if (kEnableAddonSlotGetSet || kEnableExtraRingsBp52_53) {
        if (ExpectBytes(kGetItemAddrShl, kGetAddr, sizeof(kGetAddr))) {
            Memory::CodeCave(GetItem_ExtraRingAddr_cave, kGetItemAddrShl, sizeof(kGetAddr));
            getCaved = (*reinterpret_cast<const unsigned char*>(kGetItemAddrShl) == 0xE9);
        }
        if (ExpectBytes(kSetItemAddrShl, kSetAddr, sizeof(kSetAddr))) {
            Memory::CodeCave(SetItem_ExtraRingAddr_cave, kSetItemAddrShl, sizeof(kSetAddr));
            setCaved = (*reinterpret_cast<const unsigned char*>(kSetItemAddrShl) == 0xE9);
        }
    }

    // Raise bound only when caves absorb extended slots (−62 Addon+aux / −55 rings).
    const unsigned char boundImm = (getCaved && setCaved)
        ? (kEnableAddonSlotGetSet ? kExtSlotBoundImm /* −62 */ : static_cast<unsigned char>(0xC9) /* −55 */)
        : 0xCC; /* −52 only — never unlock shadow slots without caves */
    auto isBoundImm = [](unsigned char b) {
        return b == 0xCD || b == 0xCB || b == 0xCC || b == 0xC9 || b == 0xC5 || b == 0xC1
            || b == 0xC3;
    };
    if (!getBoundCaved) {
        const unsigned char bGet = *reinterpret_cast<const unsigned char*>(kGetItemBoundCmpImm);
        if (isBoundImm(bGet)) {
            Memory::WriteByte(kGetItemBoundCmpImm, boundImm);
        }
    }
    if (!setBoundCaved) {
        const unsigned char bSet = *reinterpret_cast<const unsigned char*>(kSetItemBoundCmpImm);
        if (isBoundImm(bSet)) {
            Memory::WriteByte(kSetItemBoundCmpImm, boundImm);
        }
    }
    } // !nativeCd64 shadow/sidecar bind
    // NEVER poke kCombatBoundCmpImm. Occ cave replaces cmp@77F879; poking the
    // imm then failing the cave leaves −55 split with native Char ZRef math →
    // TSecType-as-ZRef (p often 1) → enter AV @77F99E.

    // Combat ZRef → Addon −61..−54 (sanitized). Full ring Occ stays behind flag.
    if ((kEnableCombatOccShadow || kEnableAddonOccShadow) && !nativeCd64) {
        auto* combatP = reinterpret_cast<const unsigned char*>(kCombatOccSite);
        const bool combatAlready = (combatP[0] == 0xE9);
        const bool combatMatch = (combatP[0] == 0x83 && combatP[1] == 0xF8
            && (combatP[2] == 0xCD || combatP[2] == 0xCB || combatP[2] == 0xCC
                || combatP[2] == 0xC9 || combatP[2] == 0xC5 || combatP[2] == 0xC1
                || combatP[2] == 0xC3)
            && combatP[3] == 0x7D && combatP[4] == 0x0C);
        if (!combatAlready && combatMatch) {
            Memory::CodeCave(Combat_OccShadow_cave, kCombatOccSite, 27);
        }
    }

    // Main 四维 → Addon BP54–61 (sanitized). Ring BP52–53 Occ stays OFF.
    if ((kEnableMainStatOccShadow || kEnableAddonOccShadow) && !nativeCd64) {
        static const unsigned char kMainStatOcc[] = {
            0x8B, 0x45, 0x10, // mov eax, [ebp+10h]  cash walker
            0x7F, 0x03,       // jg short +3
            0x8B, 0x45, 0xFC  // mov eax, [ebp-4]    normal walker
        };
        auto* mainP = reinterpret_cast<const unsigned char*>(kMainStatOccSite);
        const bool mainAlready = (mainP[0] == 0xE9);
        if (!mainAlready && ExpectBytes(kMainStatOccSite, kMainStatOcc, sizeof(kMainStatOcc))) {
            Memory::CodeCave(MainStat_OccShadow_cave, kMainStatOccSite, sizeof(kMainStatOcc));
        }
    }

    // Ring draw / dblclick / wear-occ caves only when ExtraRing bind is ON.
    // FIX2: ExtraRing OFF — skip these (STATS enter path). Badge/totem draw via
    // Addon layer + apply shadows BP54–55, not these ring caves.
    if (wantRings) {
        static const unsigned char kTestEsi[] = {0x85, 0xF6, 0x74, 0x15, 0x8B, 0x4D, 0xDC};
        if (ExpectBytes(kDrawTestEsiJz, kTestEsi, sizeof(kTestEsi))) {
            Memory::CodeCave(DrawForceNormalBp52_53_cave, kDrawTestEsiJz, sizeof(kTestEsi));
        }

        static const unsigned char kDrawHas[] = {0x8B, 0x4D, 0xEC, 0x33, 0xC0, 0x39, 0x01};
        if (ExpectBytes(kDrawHasItemMovEcx, kDrawHas, sizeof(kDrawHas))) {
            Memory::CodeCave(nativeCd64 ? DrawHasItem_Bp53_native_cave : DrawHasItem_Bp53_shadow_cave,
                             kDrawHasItemMovEcx, sizeof(kDrawHas));
        }

        static const unsigned char kEmptySearch[] = {
            0x33, 0xC9, 0x85, 0xFF, 0x0F, 0x8E, 0xEA, 0xF1, 0xFF, 0xFF};
        if (ExpectBytes(kDblEquipEmptySearch, kEmptySearch, sizeof(kEmptySearch))) {
            Memory::CodeCave(DblClick_EquippedUnequip_cave, kDblEquipEmptySearch, sizeof(kEmptySearch));
        }

        // CD64: leave PE occ (grown aEquipped). Shadow cave would see empty DLL ZRefs.
        if (!nativeCd64) {
            static const unsigned char kOccCmp[] = {
                0x83, 0xBC, 0xD6, 0x8B, 0x02, 0x00, 0x00, 0x00};
            if (ExpectBytes(kDblEquipOccCmp, kOccCmp, sizeof(kOccCmp))) {
                Memory::CodeCave(DblClick_OccBp53_cave, kDblEquipOccCmp, sizeof(kOccCmp));
            }
        }

        static const unsigned char kAllFullJmp[] = {0xE9, 0xC3, 0xF1, 0xFF, 0xFF};
        if (ExpectBytes(kDblEquipAllFullJmp, kAllFullJmp, sizeof(kAllFullJmp))) {
            Memory::CodeCave(DblClick_RingFullRotate_cave, kDblEquipAllFullJmp, sizeof(kAllFullJmp));
        }

        // wear() aEquipped[bp] occupancy — required for dblclick unequip of shadows.
        if (!nativeCd64) {
            static const unsigned char kWearOcc[] = {
                0x8B, 0x84, 0xC1, 0x8B, 0x02, 0x00, 0x00};
            static const unsigned char kWearOccCashBytes[] = {
                0x8B, 0x84, 0xC1, 0xEB, 0x00, 0x00, 0x00};
            if (ExpectBytes(kWearOccA, kWearOcc, sizeof(kWearOcc))) {
                Memory::CodeCave(Wear_OccShadow_cave_A, kWearOccA, sizeof(kWearOcc));
            }
            if (ExpectBytes(kWearOccB, kWearOcc, sizeof(kWearOcc))) {
                Memory::CodeCave(Wear_OccShadow_cave_B, kWearOccB, sizeof(kWearOcc));
            }
            if (ExpectBytes(kWearOccCash, kWearOccCashBytes, sizeof(kWearOccCashBytes))) {
                Memory::CodeCave(Wear_OccShadow_cave_Cash, kWearOccCash, sizeof(kWearOccCashBytes));
            }
        }
    }

    // Login apply direct ZRef write → shadows (cold-start persist).
    if (!nativeCd64) {
        static const unsigned char kApplyLea[] = {
            0x8D, 0x8C, 0xF0, 0xE7, 0x00, 0x00, 0x00};
        if (ExpectBytes(kApplyEquipZRefLea, kApplyLea, sizeof(kApplyLea))) {
            Memory::CodeCave(ApplyEquipZRef_Shadow_cave, kApplyEquipZRefLea, sizeof(kApplyLea));
        }
    }

    // Login/apply: cmp esi,N / jg skip.
    // Vanilla SAFE_LAYOUT: hard-cap 55 (sidecar 56–62). CD64: 62 (native −62).
    static constexpr DWORD kApplySlotMaxA = 0x004E5D0C;
    static constexpr DWORD kApplySlotMaxB = 0x004E5DCC;
    const unsigned char applyMaxImm = nativeCd64 ? kApplySlotMaxImmCd64 : kApplySlotMaxImm;
    auto raiseApplyMax = [applyMaxImm](DWORD cmpVa) {
        auto* p = reinterpret_cast<unsigned char*>(cmpVa);
        if (p[0] != 0x83 || p[1] != 0xFE) {
            return;
        }
        // jg (0x7F) or ja (0x77) — keep branch, only widen imm
        if (p[3] != 0x7F && p[3] != 0x77) {
            return;
        }
        // Vanilla: clamp prior TOTEM4 poke (0x3B=59) back to 55.
        // CD64: allow 62 (EXE phase2 also writes 0x3E).
        Memory::WriteByte(cmpVa + 2, applyMaxImm);
    };
    raiseApplyMax(kApplySlotMaxA);
    raiseApplyMax(kApplySlotMaxB);

    // Avatar/equip walk bound.
    // Shadows: keep −51 so walk doesn't read TSec gap. CD64: −63 with grown arrays.
    static constexpr DWORD kWalkBoundCmp = 0x004E5AFA; // 83 FB CD
    auto* walkP = reinterpret_cast<unsigned char*>(kWalkBoundCmp);
    if (walkP[0] == 0x83 && walkP[1] == 0xFB
        && (walkP[2] == 0xCD || walkP[2] == 0xCC || walkP[2] == 0xC1 || walkP[2] == 0xC3)) {
        Memory::WriteByte(kWalkBoundCmp + 2,
                          nativeCd64 ? kExtSlotBoundImmCd64
                                     : (kEnableExtraRingsBp52_53 ? 0xCD /* -51 */ : 0xCC /* -52 */));
    }

    PatchCharDrawMaxAtDllMain();
    PatchExtendedSlotHitTestAtDllMain();

    (void)kDblEquipWearNative;
    (void)kDblEquipFailExit;
    (void)kDblEquipOccJoin;
    (void)kDblEquipSearchCont;
    (void)kDblEquipAllFullJmp;
    (void)kEnableBadgeTotemBp54_55;
    (void)kEnableAddonExtSlots;
    (void)kEnableAndroidHeartNative;
    (void)kEnableEmblemBp59;
    (void)kEnableTotem2to4;
    (void)&Totem56_ZRefBase;
    (void)&Totem57_ZRefBase;
    (void)&Totem58_ZRefBase;
    (void)&Emblem59_ZRefBase;
}

} // namespace

void AttachShoulderSlotsFix() {
    if (kPatchAccessoryPath) {
        Patch1(kGetEquipDataPath_cmp_v2_114_imm, kEquipDataPathAccessoryMax);
    }

    if (kNopCombatSkipNeg20) {
        Memory::PatchNop(kCombatSkipSlotNeg20_Jz, 6);
    }

    if (kNopBp51TsecGates) {
        PatchBp51TsecGatesLikeShoulder();
    }

    if (kMoveShoulderUiTo8) {
        PatchShoulderUiCoords();
    }

    if (kHookBodypart115) {
        ATTACH_HOOK(is_correct_bodypart, is_correct_bodypart_hook);
        ATTACH_HOOK(get_bodypart_from_item, get_bodypart_from_item_hook);
    }

    if (kHookIsAbleToWear115) {
        ATTACH_HOOK(is_able_to_wear, is_able_to_wear_hook);
    }

    PatchExtraRingInventoryBind();
    // OnDoubleClicked unequip: install AFTER damageskin via Shoulder_RehookDblClickUnequipOutermost.
}


void Shoulder_RehookDblClickUnequipOutermost() {
    InstallDblClickUnequipHook();
}

void Shoulder_SetExtHitTestSlot(int index, int x, int y) {
    if (index < 0 || index >= 60) {
        return;
    }
    // Ensure table exists + GetBodyPart points at it (idempotent).
    PatchExtendedSlotHitTestAtDllMain();
    g_hitTestExt[index].x = x;
    g_hitTestExt[index].y = y;
}

extern "C" __declspec(dllexport) const char* Shoulder_GetStamp() {
    return "ADDON_WEAR_FIX_20260804";
}

bool Shoulder_UseNativeCd64Slots() {
    return UseNativeCd64Slots();
}

extern "C" void* __cdecl Shoulder_ShadowZRefForBp(int bp) {
    if (bp == 52 || bp == 53) {
        return ShadowZRefForBp(bp);
    }
    return nullptr;
}
