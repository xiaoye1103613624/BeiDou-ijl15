// Shoulder / Accessory-range (115 / BP20 / inventory -20).
// Also second pendant (112 / BP51 / -51) — mirrored from shoulder pattern.
// IDA BeiDou.exe imagebase 0x400000:
//   get_equip_data_path cmp 114 @ 0x005C973A  (83 F8 72; imm @ +2)
//   is_correct_bodypart @ 0x00460358
//   get_bodypart_from_item @ 0x004606A0
//   IsAbleToWear @ 0x004F2CEE  (must call original — cleans up via sub_4F5818)
//   combat skip -20 jz @ 0x0077F98C
//   BP51 TSecType gates (red + skip stats) — same class of fix as -20 NOP
//   g_aEquipSlotPos[50] @ 0x00BE2260 (BP = index+1); cash @ 0x00BE23F0
//
// 2026-07-25 re-enable (stable set only):
//   path 114->119, 115->BP20, combat -20 NOP, IsAbleToWear call-orig-then-OK
//   BP52/53 rings OFF; UI ⑬→⑧ OFF (native BP20 at ⑬)
//
// 2026-07-26u — second pendant mirrored from shoulder:
//   Root cause of red + missing -51 stats was NOT is_correct (native 112 already
//   accepts BP17|51 via ZF trampoline). Real gate: TSecType lock on bodypart
//   array[51] fails → CUIEquip sets red overlay flag; combat jl skips +STR/etc.
//   Shoulder BP20 never hits that gate (normal slot). Fix = same idea as -20 NOP:
//   neutralize BP51 failure branches + AbleToWear call-orig-then-OK for 112.
//
// 2026-07-26w — shoulder UI moved to red8 in pendant2.cpp (137,101).
//   Rings: is_correct accepts BP52/53; get_bodypart still native 4 (buffer risk).
//
// 2026-07-26x — six rings (attempted):
//   Enabled get_bodypart {12,13,15,16,52,53} + AbleToWear call-orig-then-OK.
//
// 2026-07-26y — FIX_RING_DRAW: get_bodypart×6 OFF again.
//   Draw loop @7FEFB8 only walks BP≤50/51 — BP52/53 never paint.
//   writeAll×6 also risks wrong slot binding. Keep is_correct + AbleToWear
//   for drag-to-red3/4; classic 4 rings must stay native get_bodypart×4.
//
// 2026-07-26z — RING_ICON_ONESHOT: get_bodypart×6 ON again.
//   pendant2 PatchCharDrawLoopForBp53 raises draw imm 0x32→0x34 (max BP53)
//   without touching setne (≠ ForceDrawLoop). writeAll buffers large (26x PE).
//
// 2026-07-26aa — RING_34_BIND: root cause of z failure (no icon / endless drag / no tip):
//   CharacterData::GetItem/SetItem treat -100<=slot<-51 as EMPTY (cmp -51 / jge fail).
//   So -52/-53 never land in client inventory → HitTest OK but slot always vacant.
//   Fix: raise bound -51→-53; -52 uses native 8-byte gap before TSecType@+0x293;
//   -53 redirects addr calc to DLL ZRef (avoids smashing TSecType[0]); draw has_item
//   for BP52/53 uses that storage so icons paint.
//
// 2026-07-26ab — FIX_EQUIP_UI_HANG: E→CUIEquip freeze under aa.
//   Root: DrawHasItem_Bp53_cave used MASM `[ebp-14]` (decimal) instead of `[ebp-14h]`
//   matching PE `8B 4D EC`. That cave runs on (nearly) every equip icon has_item check
//   → wrong stack read/write → client not responding. Also risky: GetItem/SetItem
//   bound −51→−53 + −53 shadow. Prefer OFF bind; keep z UI (draw imm 0x34, HT, ×6).
//
// 2026-07-26ac — RING_34_BIND_SAFE: re-enable inventory bind after ab E-open OK.
//   ONLY change vs ab: kBindExtraRingInventory=true with DrawHasItem cave already
//   fixed to `[ebp-14h]` (== PE 8B 4D EC). Still: −52 native gap; −53 g_ring53ZRef;
//   ExpectBytes gates; deploy out\Release only. NEVER ForceDrawLoop / pet HT / expand.
//
// 2026-07-26ad — RING_34_ONESHOT: ac left red3 red / red4 empty+multi-accept.
//   Root A: draw imm 0x34 ⇒ max=52+flag; Character+0x5E8 often 0 ⇒ BP53 never visited.
//   Root B: DrawHasItem cave set ZF from shadow but left edi=TSecType garbage; continue
//   does `mov esi,edi` then draws/skips from esi — BP53 no real icon; occupancy wrong.
//   Fix: imm 0x35 (flag0⇒53); cave sets edi=item* for BP52/53 + clear red [ebp-18h].
//
// 2026-07-26ae — FIX_AD_HANG: ad made Client_1 未响应 on open / E.
//   Root A: imm 0x35 + flag1 ⇒ max BP **54**; BP54 OOB walker / special `cmp [ecx+edx]`
//     → UI thread fault / 未响应. (pendant2 forces flag=1.)
//   Root B: ForceNormal VA was **7FEEC5** (jz imm byte) not **7FEEC2** (`85 F6 74 15`)
//     → ExpectBytes failed → ForceNormal never installed (red3/4 still broken under ad).
//   Fix: loop-end max fixed to 53 (mov eax,53; not ForceDrawLoop); ForceNormal@7FEEC2
//     for BP 52|53 only; has_item BP>53 → empty no-deref; keep edi=item* + red clear.
//
// 2026-07-27af — FIX_E_HANG: ae still froze E while drag/equip/stats seemed OK.
//   Root: ForceNormal@7FEEC2 finally installed, but special-path continuation jumped to
//     **7FEECC** (`xor eax,eax`) and **skipped** PE `mov edx,[ebp-14h]` @7FEEC9.
//     Native then does `cmp [ecx+edx],edi` (39 3C 11) with stale edx → AV/UI hang.
//     Hits any BP with esi≠0 (not only −52/−53). GetItem/SetItem paths never run this.
//   Fix: re-emit `mov edx,[ebp-14h]` before continue @7FEECC. Keep bind + max=53.
//
// 2026-07-27ag — FIX_RING3_ICON: af E OK; red3 tip/swap OK but **no icon**.
//   Root: PE loads edi=item* via `mov edi,[ecx-0x1A0]` (Char −52 gap) BEFORE test esi.
//     Continue `@7FEEE2` is `mov esi,edi` → blit uses that edi. af do52 instead did
//     `mov edi,[[ebp-14h]]` (UI ZRef walker at object+0xC) — never extended for BP52 —
//     so edi=0/junk while GetItem(−52)/HitTest still see the ring → tip+swap, empty cell.
//   Fix: do52 reload edi from `[ebp-20h]-0x1A0` (same as PE); do53 stays g_ring53ZRef.
//     Do NOT touch [ebp-14] decimal / draw imm / ForceNormal edx / pet HT / ForceDrawLoop.
//
// 2026-07-27ah — FIX_DBLCLICK_UNEQUIP: tip/swap/icon OK; **双击卸不下来**.
//   Root: CDraggableItem::OnDoubleClicked EQUIP path (get_bodypart writeAll) searches for an
//     *empty* BP among the list, then calls wear@4F1C2D. Hats (count=1) skip search and call
//     wear(slot, −slot) → unequip-to-bag. Rings/pendant return 4–6 BPs; when every listed BP
//     looks occupied the search `jmp`s to early-exit — **no unequip packet**. BP53 occupancy
//     also reads Char+0x293 (TSecType) not g_ring53ZRef → false "full" more often.
//   Fix: at empty-search entry @4F0B58, if m_nSlotPosition<0 → wear(slot, bodypart) like hat
//     (cash: bp = −slot−100). Occupancy cmp for edx==53 reads g_ring53ZRef. Inventory
//     double-click (slot≥0) still uses native empty-BP search.
//
// 2026-07-27ai — FIX_RING_EMPTY34: bag dblclick replaced classic ring while red3/4 empty.
//   Root A (client): after get_bodypart×6, PE pet-gate (BP0≤20) jumps to `push 1;pop edi`
//     before empty-search @4F0B58 → count smashed to 1 → wear BP12 only (= replace −12).
//     Same class of bug as second-pendant always sending −17 (server already reroutes).
//   Root B (server): old route only when dst==−12 && classicFull; any other replace dst
//     or partial classic left −52/−53 unused.
//   Fix: inventory path restore edi=6 for ring BP lists; get_bodypart order
//     {52,53,12,13,15,16} so empty 3/4 preferred; keep ah unequip + OccBp53.
//     Server: if ring dst occupied, prefer first empty among −52/−53 then classic 4.
//
// 2026-07-27aj — FIX_RING_UNEQUIP_PERSIST:
//   A) ai kept ah unequip call wear@4F1C2D then join 4F0B98 — but native after-call is
//      **4F0B9B** (`test eax,eax`); 4F0B98 is mid-`call` imm → unequip path corrupted.
//      Also empty-search for equipped rings (slot<0 missed) never unequips when 6 BPs full.
//      Fix: slot<0 → write bp to anBodyPart[0], ecx=0, **jmp 4F0B89** (native single-BP
//      wear / unequip). slot≥0: keep edi=6 ring restore + empty search @4F0B62.
//   B) DB already saves −52/−53 (inventoryitems.position INT). "Gone after relog" is
//      client apply: loops/filters stop at BP51 (`cmp *,51` / `cmp ebx,-51`). Raise
//      apply filters 51→53; avatar walk loop −51→−52 only (raw ZRef; −53 stays shadow).
//
// History: early-return IsAbleToWear for 115 skipped cleanup -> E_POINTER.
//          half-hook rings BP52/53 smashed get_bodypart buffer -> E_POINTER.
//          (Prior smash was likely AbleToWear skip / table realloc — not these frames.)

#include "stdafx.h"
#include "ShoulderApi.h"
#include "Memory.h"
#include "compat/hook.h"

#include <initializer_list>

namespace {

// --- feature flags (login-safe defaults) ---
static constexpr bool kPatchAccessoryPath = true;   // 114 -> 119
static constexpr bool kHookBodypart115 = true;      // 115 -> BP20; also 112/113/111
static constexpr bool kHookIsAbleToWear115 = true;  // call orig then force OK (115+112)
static constexpr bool kNopCombatSkipNeg20 = true;   // shoulder stats into panel/combat
static constexpr bool kNopBp51TsecGates = true;     // second pendant: red + stats (mirror -20)
// Shoulder UI coords live in pendant2 FixZeroDrawSlots (needs pendant2_ui + post-field).
static constexpr bool kMoveShoulderUiTo8 = false;
// ON (20260726z/aa): draw loop extended to BP53; writeAll frames hold ≥6 ints.
static constexpr bool kEnableExtraRingsBp52_53 = true;
static constexpr bool kAcceptExtraRingBodyparts = true; // is_correct 12/13/15/16/52/53
// ON (ac): aa froze E because cave used `[ebp-14]` (dec) ≠ PE `[ebp-14h]`.
// Cave below already uses 14h; ab confirmed E opens with bind OFF — safe to re-bind.
static constexpr bool kBindExtraRingInventory = true;

static constexpr DWORD kGetEquipDataPath_cmp_v2_114_imm = 0x005C973A + 2;
static constexpr BYTE kEquipDataPathAccessoryMax = 0x77; // 119

static constexpr DWORD kCombatSkipSlotNeg20_Jz = 0x0077F98C; // 6-byte jz -> NOP

// BP51 TSecType failure → skip stat apply (mirror kCombatSkipSlotNeg20_Jz).
// Each VA is the jl/jl-near immediately after `test eax,eax` on the lock HRESULT.
static constexpr DWORD kBp51CombatJl_77E427 = 0x0077E427; // 7C 57
static constexpr DWORD kBp51CombatJl_77E533 = 0x0077E533; // 7C 57
static constexpr DWORD kBp51CombatJl_77E602 = 0x0077E602; // 7C 4F
static constexpr DWORD kBp51CombatJlNear_77EE1D = 0x0077EE1D; // 0F 8C ..
static constexpr DWORD kBp51CombatJlNear_77F004 = 0x0077F004; // 0F 8C ..

// CUIEquip draw: on BP51 TSecType fail, `mov [ebp-18], 1` paints 0x40FF0000 red.
// `7D 09` (jge skip set-red) → `EB 09` so failure never raises the red flag.
static constexpr DWORD kBp51DrawRedJge = 0x007FEEB3; // 7D 09

// Wear/validate helpers that treat BP51 TSecType fail as "invalid".
static constexpr DWORD kBp51WearJlNear_4F1CCA = 0x004F1CCA; // 0F 8C a3 0e 00 00
static constexpr DWORD kBp51InvalidJge_A288A8 = 0x00A288A8; // 7D 05 → EB 05 (edi=0)

static constexpr DWORD kEquipSlotPosBase = 0x00BE2260;
static constexpr DWORD kEquipSlotPosCashBase = 0x00BE23F0;
static constexpr int kShoulderIndex = 19; // BP20
static constexpr int kShoulderUiX = 137;  // red8 (kept for optional DllMain path)
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
    // Pendant 112: accept main BP17 and second BP51 (shoulder-style explicit table).
    if (prefix == 112) {
        return (nBodyPart == 17 || nBodyPart == 51) ? 1 : 0;
    }
    // Belt / 轮回碑石 (113). v083 has no native belt wear path → red + wrong slot.
    if (prefix == 113) {
        return nBodyPart == 50 ? 1 : 0;
    }
    // Rings 111: native 12/13/15/16 + BP52/53 (red3/4).
    if (prefix == 111 && kAcceptExtraRingBodyparts) {
        if (nBodyPart == 12 || nBodyPart == 13 || nBodyPart == 15 || nBodyPart == 16
            || nBodyPart == 52 || nBodyPart == 53) {
            return 1;
        }
        return 0;
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
    // Mirror shoulder: own the bodypart list for 112 instead of relying on native only.
    if (prefix == 112) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {17, 51});
    }
    if (prefix == 113) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {50});
    }
    // Rings: BP52/53 first so empty-search fills red3/4 before classic / replace.
    // Do NOT realloc g_aEquipSlotPos.
    if (prefix == 111 && kEnableExtraRingsBp52_53) {
        return write_bodyparts(aBodyParts, bWriteAll != 0, {52, 53, 12, 13, 15, 16});
    }
    return get_bodypart_from_item(nItemID, nGender, aBodyParts, bWriteAll);
}

// CRITICAL: never early-return before calling the original. IsAbleToWear always
// releases internal state via sub_4F5818(0) when a11 != 0; skipping that yields
// HRESULT E_POINTER (0x80004003) on field enter.
static int __fastcall is_able_to_wear_hook(
    void* pThis, void* /*edx*/,
    int gender, int level, int job,
    int str, int dex, int intl, int luk, int pop,
    char a10, int a11, int itemId) {
    const int r = is_able_to_wear(
        pThis, gender, level, job, str, dex, intl, luk, pop, a10, a11, itemId);
    const int prefix = itemId / 10000;
    // Shoulder template: call-orig-then-OK (cleanup done; force wearable).
    if (prefix == 115) {
        return 1;
    }
    // Second pendant: same pattern as shoulder (was wrongly `&& r` only).
    if (prefix == 112) {
        return 1;
    }
    // Extra rings BP52/53: same pattern (avoids red / reject on non-native slots).
    if (prefix == 111) {
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

// Mirror shoulder -20 NOP: BP51 TSecType fail must not red-tint or drop stats.
static void PatchBp51TsecGatesLikeShoulder() {
    // Combat / localstat: short jl after failed lock
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

    // Combat: near jl (6 bytes)
    static const unsigned char kJlNear77EE[] = {0x0F, 0x8C, 0x8F, 0x01, 0x00, 0x00};
    static const unsigned char kJlNear77F0[] = {0x0F, 0x8C, 0x1F, 0x01, 0x00, 0x00};
    if (ExpectBytes(kBp51CombatJlNear_77EE1D, kJlNear77EE, sizeof(kJlNear77EE))) {
        Memory::PatchNop(kBp51CombatJlNear_77EE1D, 6);
    }
    if (ExpectBytes(kBp51CombatJlNear_77F004, kJlNear77F0, sizeof(kJlNear77F0))) {
        Memory::PatchNop(kBp51CombatJlNear_77F004, 6);
    }

    // CUIEquip icon draw: never set red overlay flag for BP51 lock fail
    if (ExpectBytes(kBp51DrawRedJge, (const unsigned char*)"\x7D\x09", 2)) {
        Memory::WriteByte(kBp51DrawRedJge, 0xEB); // jge → jmp (always clear-red path)
    }

    // Equip validate: don't abort on BP51 TSecType fail
    static const unsigned char kJlNear4F1C[] = {0x0F, 0x8C, 0xA3, 0x0E, 0x00, 0x00};
    if (ExpectBytes(kBp51WearJlNear_4F1CCA, kJlNear4F1C, sizeof(kJlNear4F1C))) {
        Memory::PatchNop(kBp51WearJlNear_4F1CCA, 6);
    }

    // Helper that returned edi=1 (invalid) on BP51 lock fail → always edi=0
    if (ExpectBytes(kBp51InvalidJge_A288A8, (const unsigned char*)"\x7D\x05", 2)) {
        Memory::WriteByte(kBp51InvalidJge_A288A8, 0xEB);
    }
}

// --- RING_34_BIND: client inventory actually stores -52/-53 ---
// GetItem @42834E / SetItem @47B0BF / combat @77F879: cmp eax,-51 (CD) rejects -52/-53
// into the empty-return path. Raise imm to -53 (CB).
// -52 lands in the 8-byte gap before TSecType@Character+0x293 (safe).
// -53 would smash TSecType[0] — redirect addr calc to g_ring53ZRef.
// Draw has_item for BP53 reads the shadow (walk would hit TSecType).

static constexpr DWORD kGetItemBoundCmpImm = 0x0042834E + 2;   // 83 F8 CD
static constexpr DWORD kSetItemBoundCmpImm = 0x0047B0BF + 2;   // 83 F8 CD
static constexpr DWORD kCombatBoundCmpImm = 0x0077F879 + 2;   // 83 F8 CD

// GetItem: shl/sub/mov ecx,[ecx+0xEB] @42838B → item* in ecx; continue @428396
static constexpr DWORD kGetItemAddrShl = 0x0042838B;
static constexpr DWORD kGetItemAddrBack = 0x00428396;

// SetItem: shl/sub/add ecx,0xE7 @47B10C → &ZRef in ecx; continue @47B117
static constexpr DWORD kSetItemAddrShl = 0x0047B10C;
static constexpr DWORD kSetItemAddrBack = 0x0047B117;

// Draw has_item BP!=51: 8B 4D EC 33 C0 39 01 @7FEEDB → continue @7FEEE2
// PE displacement EC = -0x14. MASM MUST use `ebp-14h` (NOT decimal `ebp-14`).
static constexpr DWORD kDrawHasItemMovEcx = 0x007FEEDB;
static constexpr DWORD kDrawHasItemBack = 0x007FEEE2;
// test esi,esi / jz 7FEEDB starts at **7FEEC2** (85 F6 74 15 8B 4D DC).
// ad wrongly used 7FEEC5 (the jz rel8 imm) → ExpectBytes miss → cave never installed.
static constexpr DWORD kDrawTestEsiJz = 0x007FEEC2; // 85 F6 74 15 8B 4D DC
// After re-emitting mov ecx,[ebp-24h] AND mov edx,[ebp-14h]. PE:
//   7FEEC6: mov ecx,[ebp-24h] / 7FEEC9: mov edx,[ebp-14h] / 7FEECC: xor eax,eax
//   7FEED0: cmp [ecx+edx],edi (39 3C 11). ae jumped here without edx → E hang.
static constexpr DWORD kDrawSpecialHasItemCont = 0x007FEECC;

// ZRef layout matches native: SetItem writes via base+0xE7, GetItem reads base+0xEB (=base+4).
alignas(8) static void* g_ring53ZRef[2] = {nullptr, nullptr}; // [0]=pad, [1]=item*

static void* __cdecl Ring53_ZRefBase() {
    return &g_ring53ZRef[0];
}

void __declspec(naked) GetItem_ExtraRingAddr_cave() {
    __asm {
        cmp eax, -53
        jne native_get
        call Ring53_ZRefBase
        mov ecx, dword ptr [eax + 4]
        jmp dword ptr [kGetItemAddrBack]
    native_get:
        shl eax, 3
        sub ecx, eax
        mov ecx, dword ptr [ecx + 0xEB]
        jmp dword ptr [kGetItemAddrBack]
    }
}

void __declspec(naked) SetItem_ExtraRingAddr_cave() {
    __asm {
        cmp eax, -53
        jne native_set
        call Ring53_ZRefBase
        mov ecx, eax
        jmp dword ptr [kSetItemAddrBack]
    native_set:
        shl eax, 3
        sub ecx, eax
        add ecx, 0xE7
        jmp dword ptr [kSetItemAddrBack]
    }
}

// Force BP52/53 into 7FEEDB so DrawHasItem cave runs (esi often TSecType junk).
// Overwrites test/jz + `mov ecx,[ebp-24h]` (7 bytes) at 7FEEC2; special path re-emits
// mov ecx AND mov edx (PE @7FEEC9) before continue @7FEECC.
// ONLY 52|53 — never BP≥54 (OOB). ad used ≥52 and wrong VA.
void __declspec(naked) DrawForceNormalBp52_53_cave() {
    __asm {
        cmp dword ptr [ebp + 8], 52
        jb native_test
        cmp dword ptr [ebp + 8], 53
        ja native_test
        xor esi, esi
        push kDrawHasItemMovEcx
        ret
    native_test:
        test esi, esi
        jz to_feedb
        mov ecx, dword ptr [ebp - 24h]
        mov edx, dword ptr [ebp - 14h] // PE @7FEEC9; ae omitted → cmp [ecx+edx] hang
        push kDrawSpecialHasItemCont
        ret
    to_feedb:
        push kDrawHasItemMovEcx
        ret
    }
}

// MUST use `[ebp-14h]` matching PE `8B 4D EC`. Decimal `[ebp-14]` = aa E-freeze.
// Continue @7FEEE2 is `mov esi,edi` / `setne al` / `xor edi,edi` / `cmp esi,edi` / jnz draw.
// So for BP52/53 we MUST leave edi=item* and ZF from `cmp edi,0` — fixing only ZF (ac)
// left edi as TSecType garbage ⇒ no icon / wrong occupancy on red4.
// BP52: PE `mov edi,[ecx-0x1A0]` (ecx=[ebp-20h] TSecType walker) already has Char −52
// item* BEFORE ForceNormal. Native 7FEEDB does NOT reload edi from [ebp-14h] — only ZF.
// Loading edi from UI walker [ebp-14h] (af) wiped the good pointer → tip OK, icon gone.
// BP>53: empty, no walker deref (belt for any max>53 slip).
void __declspec(naked) DrawHasItem_Bp53_cave() {
    __asm {
        cmp dword ptr [ebp + 8], 52
        je do52
        cmp dword ptr [ebp + 8], 53
        je do53
        cmp dword ptr [ebp + 8], 53
        ja empty_hi
        jmp walk
    do52:
        // Same as PE @7FEE90: mov edi,[ecx-0x1A0] with ecx = [ebp-20h].
        mov ecx, dword ptr [ebp - 20h]
        mov edi, dword ptr [ecx - 1A0h]
        and dword ptr [ebp - 18h], 0
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

// OnDoubleClicked EQUIP empty-BP search @4F0B58 (after get_bodypart writeAll).
// Native: xor ecx / test edi / jle fail / lea bodyparts / … cmp [esi+bp*8+0x28B],0
// Single-BP wear (hats / unequip): @4F0B89 push anBodyPart[ecx]; this; push slot; call wear
//   → return @4F0B9B test eax,eax. (ah wrongly joined 4F0B98 = mid-call imm.)
static constexpr DWORD kDblEquipEmptySearch = 0x004F0B58; // 33 C9 85 FF 0F 8E …
static constexpr DWORD kDblEquipSearchCont = 0x004F0B62;  // lea eax,[ebp-124h]
static constexpr DWORD kDblEquipWearNative = 0x004F0B89;  // push [ebp+ecx*4-124h] …
static constexpr DWORD kDblEquipFailExit = 0x004EFD4C;
static constexpr DWORD kDblEquipOccCmp = 0x004F0B72;      // 83 BC D6 8B 02 00 00 00
static constexpr DWORD kDblEquipOccJoin = 0x004F0B7A;     // after 8-byte cmp (74 0D)

// Equipped double-click: skip empty-BP search → native single-BP wear @4F0B89 (unequip).
// Inventory rings: restore edi=6 so empty-search walks {52,53,12,13,15,16}.
void __declspec(naked) DblClick_EquippedUnequip_cave() {
    __asm {
        mov eax, dword ptr [ebp - 18h] // CDraggableItem*
        test eax, eax
        jz do_search
        mov eax, dword ptr [eax + 1Ch] // m_nSlotPosition
        test eax, eax
        jns do_search // inventory (≥0): empty-BP walk (ring count fix below)
        // bodypart = (slot <= -100) ? (-slot - 100) : (-slot)
        mov edx, eax
        neg edx
        cmp eax, -100
        jg bp_ready
        sub edx, 100
    bp_ready:
        // Reuse native hat path: anBodyPart[0]=bp, ecx=0 → wear(slot, bp) unequip.
        mov dword ptr [ebp - 124h], edx
        xor ecx, ecx
        mov eax, 0x004F0B89
        push eax
        ret
    do_search:
        // If anBodyPart looks like our ring list, restore edi=6 (undo push1;pop edi).
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
        // Hook writes 6 ints; PE may have left them intact while zeroing count.
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

// Empty-BP occupancy: BP53 must read g_ring53ZRef, not Char+0x293 (TSecType).
void __declspec(naked) DblClick_OccBp53_cave() {
    __asm {
        cmp edx, 53
        jne native_occ
        call Ring53_ZRefBase
        cmp dword ptr [eax + 4], 0
        mov eax, 0x004F0B7A
        push eax
        ret
    native_occ:
        cmp dword ptr [esi + edx * 8 + 0x28B], 0
        mov eax, 0x004F0B7A
        push eax
        ret
    }
}

static void PatchExtraRingInventoryBind() {
    if (!kBindExtraRingInventory || !kEnableExtraRingsBp52_53) {
        return;
    }

    static const unsigned char kCmpCd[] = {0x83, 0xF8, 0xCD};
    if (ExpectBytes(kGetItemBoundCmpImm - 2, kCmpCd, 3)) {
        Memory::WriteByte(kGetItemBoundCmpImm, 0xCB); // -53
    }
    if (ExpectBytes(kSetItemBoundCmpImm - 2, kCmpCd, 3)) {
        Memory::WriteByte(kSetItemBoundCmpImm, 0xCB);
    }
    if (ExpectBytes(kCombatBoundCmpImm - 2, kCmpCd, 3)) {
        Memory::WriteByte(kCombatBoundCmpImm, 0xCB);
    }

    static const unsigned char kGetAddr[] = {
        0xC1, 0xE0, 0x03, 0x2B, 0xC8, 0x8B, 0x89, 0xEB, 0x00, 0x00, 0x00};
    if (ExpectBytes(kGetItemAddrShl, kGetAddr, sizeof(kGetAddr))) {
        Memory::CodeCave(GetItem_ExtraRingAddr_cave, kGetItemAddrShl, sizeof(kGetAddr));
    }

    static const unsigned char kSetAddr[] = {
        0xC1, 0xE0, 0x03, 0x2B, 0xC8, 0x81, 0xC1, 0xE7, 0x00, 0x00, 0x00};
    if (ExpectBytes(kSetItemAddrShl, kSetAddr, sizeof(kSetAddr))) {
        Memory::CodeCave(SetItem_ExtraRingAddr_cave, kSetItemAddrShl, sizeof(kSetAddr));
    }

    // 85 F6 74 15 8B 4D DC — test/jz + mov ecx,[ebp-24h] (7 bytes; special path re-emits mov)
    static const unsigned char kTestEsi[] = {0x85, 0xF6, 0x74, 0x15, 0x8B, 0x4D, 0xDC};
    if (ExpectBytes(kDrawTestEsiJz, kTestEsi, sizeof(kTestEsi))) {
        Memory::CodeCave(DrawForceNormalBp52_53_cave, kDrawTestEsiJz, sizeof(kTestEsi));
    }

    static const unsigned char kDrawHas[] = {0x8B, 0x4D, 0xEC, 0x33, 0xC0, 0x39, 0x01};
    if (ExpectBytes(kDrawHasItemMovEcx, kDrawHas, sizeof(kDrawHas))) {
        Memory::CodeCave(DrawHasItem_Bp53_cave, kDrawHasItemMovEcx, sizeof(kDrawHas));
    }

    // Double-click unequip: 33 C9 85 FF 0F 8E EA F1 FF FF (10 bytes)
    static const unsigned char kEmptySearch[] = {
        0x33, 0xC9, 0x85, 0xFF, 0x0F, 0x8E, 0xEA, 0xF1, 0xFF, 0xFF};
    if (ExpectBytes(kDblEquipEmptySearch, kEmptySearch, sizeof(kEmptySearch))) {
        Memory::CodeCave(DblClick_EquippedUnequip_cave, kDblEquipEmptySearch, sizeof(kEmptySearch));
    }

    // Occupancy: 83 BC D6 8B 02 00 00 00 (8 bytes)
    static const unsigned char kOccCmp[] = {
        0x83, 0xBC, 0xD6, 0x8B, 0x02, 0x00, 0x00, 0x00};
    if (ExpectBytes(kDblEquipOccCmp, kOccCmp, sizeof(kOccCmp))) {
        Memory::CodeCave(DblClick_OccBp53_cave, kDblEquipOccCmp, sizeof(kOccCmp));
    }

    // Login/apply filters: cmp esi,51 / jg skip — raise to 53 so BP52/53 applied.
    // @4E5D0C and @4E5DCC (two similar blocks). Expect 83 FE 33.
    static constexpr DWORD kApplySlotMaxA = 0x004E5D0C;
    static constexpr DWORD kApplySlotMaxB = 0x004E5DCC;
    static const unsigned char kCmpEsi33[] = {0x83, 0xFE, 0x33};
    if (ExpectBytes(kApplySlotMaxA, kCmpEsi33, 3)) {
        Memory::WriteByte(kApplySlotMaxA + 2, 0x35); // 53
    }
    if (ExpectBytes(kApplySlotMaxB, kCmpEsi33, 3)) {
        Memory::WriteByte(kApplySlotMaxB + 2, 0x35);
    }

    // Avatar/equip walk: cmp ebx,-51 / jge loop — include −52 only (native ZRef gap).
    // Do NOT go to −53 here (raw esi walk would hit TSecType; −53 uses g_ring53ZRef).
    static constexpr DWORD kWalkBoundCmp = 0x004E5AFA; // 83 FB CD
    static const unsigned char kCmpEbxCd[] = {0x83, 0xFB, 0xCD};
    if (ExpectBytes(kWalkBoundCmp, kCmpEbxCd, 3)) {
        Memory::WriteByte(kWalkBoundCmp + 2, 0xCC); // -52
    }
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
}

extern "C" __declspec(dllexport) const char* Shoulder_GetStamp() {
    return "FIX_RING_UNEQUIP_PERSIST_20260727aj";
}
