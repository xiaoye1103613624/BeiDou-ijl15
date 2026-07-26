// Stamp: FIX_RING_UNEQUIP_PERSIST_20260727aj
//
// UI (this file): classic (38,101), BP23 park, layout and*→0, char HT extend, flag cave,
//   char equip **icon draw** loop extend to BP53 (fixed max=53 — NOT ForceDrawLoop).
// Wear/stats (shoulders.cpp — mirrored from 115/-20):
//   AbleToWear call-orig-then-OK for 112; is_correct 17|51; get_bodypart {17,51};
//   BP51 TSecType fail gates NOP'd (was real cause of red overlay + skipped -51 stats).
//   Rings 111: get_bodypart {52,53,12,13,15,16} ON; bag dblclick count restore (ai);
//   equipped unequip via native @4F0B89; login apply max 53 (aj).
//
// 2026-07-27aj — FIX_RING_UNEQUIP_PERSIST: unequip join bug + login apply 51→53.
// 2026-07-27ai — FIX_RING_EMPTY34: bag dblclick replaced classic while red3/4 empty.
//   PE pet-gate smash edi→1; restore count=6; prefer BP52/53 in get_bodypart order.
// 2026-07-27ah — FIX_DBLCLICK_UNEQUIP: equipped multi-BP → wear unequip; OccBp53.
// 2026-07-27ag — FIX_RING3_ICON: BP52 do52 must use Char [ecx-0x1A0], not [ebp-14h] UI.
// 2026-07-27af — FIX_E_HANG:
//   ae ForceNormal@7FEEC2 installed but special path skipped mov edx,[ebp-14h]
//   → cmp [ecx+edx],edi hang on E. Fixed in shoulders.cpp (keep draw max=53).
// 2026-07-26ae — FIX_AD_HANG:
//   ad imm 0x35 + flag1 ⇒ max 54 → BP54 OOB → 未响应. Loop-end now mov eax,53 (setne
//   replaced as a unit — NOT the forbidden setne→mov al,1 ForceDrawLoop).
// 2026-07-26ad — RING_34_ONESHOT:
//   ac: bind ON + [ebp-14h] cave, but red3 still red / red4 empty+multi-drag.
//   (1) imm 0x34 ⇒ max=52 when Character+0x5E8==0 (CUIEquip flag≠Character flag)
//       → BP53 never painted. Raise imm 0x32→0x35 (flag0⇒53; flag1⇒54 empty ok).
//   (2) DrawHasItem cave must set edi=item* for BP52/53 (continue mov esi,edi).
//       ac only fixed ZF from shadow → BP53 no real icon / slot looks empty.
// 2026-07-26w — weapon/shield draw fix:
//   pendant2_ui forces flag=1; draw uses GetSlotXY. Native right-column GetSlotXY is
//   +33 vs HitTest/labels → weapon icon landed on「盾牌」(GetSlotXY 137 vs label 104).
//   Fix: copy HitTest → GetSlotXY for BP 4/10/11 (weapon/shield/ear).
//   Shoulder → red8 (137,101). Extra rings BP52/53 → red3/4 (104,35)/(137,35) UI only.
//
// 2026-07-26x — six rings: get_bodypart×6 + AbleToWear 111 (rolled back in 26y).
//
// 2026-07-26y — classic ring draw restore:
//   Root cause: flag=1 draw reads GetSlotXY; native ring XY = HitTest+33 → BP13/16 at
//   x=170 off classic chrome (invisible); BP12/15 sit on neighbor labels (own slots
//   look empty). 26w Align included rings but 26x get_bodypart×6 could also park
//   equips on BP52/53 which draw-loop (≤51) never paints. Fix: hardcode classic
//   ring GetSlotXY←HitTest label coords; get_bodypart×6 OFF; Align only 4/10/11.
//
// 2026-07-26z — RING_ICON_ONESHOT (complete):
//   After y: DLL stamp confirmed loaded, but icons still missing because
//   (1) char draw loop @7FEFBC/7FEE54 still `add r,0x32` → max BP = 50+flag ≤51,
//       so −52/−53 never paint; (2) classic rings may be empty if server/client
//       parked items on 52/53 under 26x. Fix: raise draw imm 0x32→0x34 (max 52/53)
//       with ExpectBytes — same style as HitTest end BE23F4→BE240C. setne UNTOUCHED
//       (that was the forbidden ForceDrawLoop). Classic+extra rings: GetSlotXY +
//       HitTest + BE27E0 all on-canvas. get_bodypart×6 re-enabled in shoulders.cpp.
//
// Policy: NEVER ForceDrawLoop (setne→mov al,1) @ 0x007FEFB9. NEVER pet HT @ 801214/8013A3.

#include "stdafx.h"
#include "Pendant2Api.h"
#include "Memory.h"
#include "INIReader.h"

#include <string>

namespace {

static constexpr bool kForceDrawLoopToBp51 = false; // NEVER true (setne→mov al,1)
// Safe: loop-end max fixed to 53 (always includes BP53, never BP54). Post-field only.
// ad used add-imm 0x35 which with flag1 became max=54 → hang.
static constexpr bool kExtendCharDrawLoopToBp53 = true;
static constexpr bool kAllowAttachAtDllMain = false;
static constexpr bool kPatchPetHitTestLoop = false; // NEVER true (was p/n mistake)

// Red-7: Row3 Col2 on classic 5×7 narrow grid (HitTest/label pitch 33).
static constexpr int kClassicPendant2X = 38;
static constexpr int kClassicPendant2Y = 101;

// Red-8: Row3 Col5 — right of 耳饰 (ear HitTest 104,101). Shoulder BP20.
static constexpr int kClassicShoulderX = 137;
static constexpr int kClassicShoulderY = 101;

// Red-3 / Red-4: Row1 Col4/Col5 — two extra ring slots (BP52/53). Keep classic 4 rings.
static constexpr int kClassicRing5X = 104;
static constexpr int kClassicRing5Y = 35;
static constexpr int kClassicRing6X = 137;
static constexpr int kClassicRing6Y = 35;

// Classic 戒指 (BP15/16) + 指环 (BP12/13) — HitTest label coords (pitch 33).
// Native GetSlotXY is +33X; with flag=1 that parks BP13/16 off-chrome (x=170).
static constexpr int kClassicRingBp15X = 104;
static constexpr int kClassicRingBp15Y = 68;
static constexpr int kClassicRingBp16X = 137;
static constexpr int kClassicRingBp16Y = 68;
static constexpr int kClassicRingBp12X = 104;
static constexpr int kClassicRingBp12Y = 167;
static constexpr int kClassicRingBp13X = 137;
static constexpr int kClassicRingBp13Y = 167;

// Belt / 轮回碑石 (113 → BP50 / −50). Native GetSlotXY was (0,0).
static constexpr int kClassicBeltX = 71;
static constexpr int kClassicBeltY = 167;

// Medal (BP49). Native GetSlotXY was (0,0) → top-left ghost.
static constexpr int kClassicMedalX = 5;
static constexpr int kClassicMedalY = 68;

// BP23 empty-label park (character GetSlotXY only). Keep off pet band (46,44).
// Native was (104,134); keep parked so empty「项链」chrome does not reappear.
static constexpr int kBp23ParkX = -128;
static constexpr int kBp23ParkY = -128;

extern "C" __declspec(dllexport) const char* Pendant2_GetStamp() {
    return "FIX_RING_UNEQUIP_PERSIST_20260727aj";
}

static constexpr int kPendant2BodyPart = 51;
static constexpr int kPendant2Index = 50; // classic tables: BP = index + 1
static constexpr int kPendant2Be27Index = 51; // BE27E0 indexed by bodypart number
static constexpr int kShoulderBodyPart = 20;
static constexpr int kShoulderIndex = 19;
static constexpr int kShoulderBe27Index = 20;
static constexpr int kRing5BodyPart = 52;
static constexpr int kRing5Index = 51; // past regular[50] into cash[1]
static constexpr int kRing5Be27Index = 52;
static constexpr int kRing6BodyPart = 53;
static constexpr int kRing6Index = 52; // cash[2]
static constexpr int kRing6Be27Index = 53;
static constexpr int kClassicRingBp12Index = 11; // 指环
static constexpr int kClassicRingBp13Index = 12; // 指环
static constexpr int kClassicRingBp15Index = 14; // 戒指
static constexpr int kClassicRingBp16Index = 15; // 戒指
static constexpr int kBeltBodyPart = 50;
static constexpr int kBeltIndex = 49;
static constexpr int kMedalBodyPart = 49;
static constexpr int kMedalIndex = 48;
static constexpr int kBp23Index = 22; // BP23 — empty「项链」ghost (native 104,134)

static constexpr DWORD kEquipSlotDraw_MovFlag = 0x007FDE8B;
static const DWORD kEquipSlotDraw_AfterFlag = 0x007FDE91;

static constexpr DWORD kCUIEquipSingleton = 0x00BED650;
static constexpr DWORD kCWvsContextSingleton = 0x00BE7918;
static constexpr int kCWvsExtraPendantOff = 0x387C;
static constexpr int kExtraPendantFlagOff = 0x5E8;

static constexpr DWORD kClassicHitTestTable = 0x00BE2260;   // BP = index+1
static constexpr DWORD kClassicGetSlotXyTable = 0x00BE2580; // BP = index+1
static constexpr DWORD kCuiEquipBe27Table = 0x00BE27E0;     // indexed by BP

// Character CUIEquip::GetBodyPartFromPoint (Client_1):
//   0x007FEC32: xor edx,edx / mov ecx, BE2264
//   0x007FEC6F: cmp ecx, BE23F4  (end after BP50)
//   → BE23FC = BP51; → BE240C = BP53 (extra rings)
static constexpr DWORD kCharHitTestFunc = 0x007FEC32;
static constexpr DWORD kCharHitTestEndCmp = 0x007FEC6F; // 81 F9 F4 23 BE 00
// End after BP53: BE2260 + 53*8 = BE2408; walk cmp uses +4 = BE240C
static constexpr BYTE kCharHitTestEndImmLo = 0x0C;
static constexpr BYTE kCharHitTestEndImmHi = 0x24; // BE240C

// Character equip ICON draw loop (Client_1 BeiDou.exe PE-verified — NOT pet):
//   @7FEFB9: setne al / add eax,0x32 / cmp [ebp+8],eax / jle
//   @7FEE54: setne cl / add ecx,0x32  (early max vs start)
//   maxBP = 0x32 + (flag?1:0) = 50 or 51. With pendant2 flag=1 → ≤51.
//   ad raised imm→0x35: flag0⇒53 OK, but flag1⇒**54** OOB hang.
//   ae: replace setne+add (6B) with `mov eax,53; nop` — always BP53, never 54.
//   This is NOT ForceDrawLoop (that was setne→mov al,1 leaving add 0x32 ⇒ max 51).
//   Pet cmp edx,0x32 @8013A3 is a different function — never touch.
static constexpr DWORD kCharDrawLoopAddMax = 0x007FEFBC; // 83 C0 xx (imm at +2)
static constexpr DWORD kCharDrawLoopAddMaxEarly = 0x007FEE54; // 83 C1 xx
static constexpr BYTE kCharDrawLoopImmNative = 0x32;
static constexpr BYTE kCharDrawLoopImmBp53 = 0x35; // early only: 53+flag (54 empty ok for entry)

// WRONG pet loop (document only — must stay unpatched):
//   0x00801214 cmp esi,0x30 / 0x008013A3 cmp edx,0x32
static constexpr DWORD kPetHitTestCmpBpMax = 0x00801214;
static constexpr DWORD kPetHitTestCmpLoopEnd = 0x008013A3;

// Expanded-layout delta masks (flag?0x21:0). Zero → stay on classic 175x304 math.
static constexpr DWORD kLayoutAndDraw = 0x007FDE9D;   // 83 E0 21
static constexpr DWORD kLayoutAndPetY = 0x00801A5F;   // 83 E1 21
static constexpr DWORD kLayoutAndPetX = 0x00801A77;   // 83 E0 21

struct EquipSlotPos {
    int x;
    int y;
};

static bool g_uiHooksAttached = false;
static bool g_pendant2UiEnabled = false;
static bool g_configLoaded = false;

static std::string ConfigIniBesideModule() {
    char dllPath[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&ConfigIniBesideModule),
            &self)
        || !self) {
        return "config.ini";
    }
    if (GetModuleFileNameA(self, dllPath, MAX_PATH) == 0) {
        return "config.ini";
    }
    std::string path(dllPath);
    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return "config.ini";
    }
    return path.substr(0, slash + 1) + "config.ini";
}

static void LoadPendant2UiFlagOnce() {
    if (g_configLoaded) {
        return;
    }
    g_configLoaded = true;
    INIReader reader(ConfigIniBesideModule());
    g_pendant2UiEnabled = reader.GetBoolean("optional", "pendant2_ui", false);
}

static void PatchTableSlot(DWORD tableBase, int index, int x, int y) {
    auto* slots = reinterpret_cast<EquipSlotPos*>(tableBase);
    DWORD oldProt = 0;
    if (!VirtualProtect(slots + index, sizeof(EquipSlotPos), PAGE_EXECUTE_READWRITE, &oldProt)) {
        return;
    }
    slots[index].x = x;
    slots[index].y = y;
    VirtualProtect(slots + index, sizeof(EquipSlotPos), oldProt, &oldProt);
}

static EquipSlotPos ReadTableSlot(DWORD tableBase, int index) {
    auto* slots = reinterpret_cast<EquipSlotPos*>(tableBase);
    return slots[index];
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

// Extend character HitTest walk from BP50 end (BE23F4) to BP53 end (BE240C).
static void PatchCharHitTestForBp51() {
    static const unsigned char kExpectEndCmp[] = {0x81, 0xF9, 0xF4, 0x23, 0xBE, 0x00};
    static const unsigned char kFuncStart[] = {0x33, 0xD2, 0xB9, 0x64, 0x22, 0xBE, 0x00};
    if (!ExpectBytes(kCharHitTestFunc, kFuncStart, sizeof(kFuncStart))) {
        return; // wrong client build — refuse to patch
    }
    // Accept native BE23F4, prior BP51 BE23FC, or already BP53 BE240C
    const unsigned char b2 = *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 2);
    const unsigned char b3 = *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 3);
    const bool nativeEnd = ExpectBytes(kCharHitTestEndCmp, kExpectEndCmp, sizeof(kExpectEndCmp));
    const bool bp51End = (b2 == 0xFC && b3 == 0x23);
    const bool bp53End = (b2 == kCharHitTestEndImmLo && b3 == kCharHitTestEndImmHi);
    if (!nativeEnd && !bp51End && !bp53End) {
        return;
    }
    if (bp53End) {
        return;
    }
    // Imm32 low bytes of cmp ecx, BE240C
    Memory::WriteByte(kCharHitTestEndCmp + 2, kCharHitTestEndImmLo);
    Memory::WriteByte(kCharHitTestEndCmp + 3, kCharHitTestEndImmHi);
}

// Raise character equip icon-draw max to exactly BP53 (never 54).
// PE-verified Client_1: `0F 95 C0 83 C0 32` @7FEFB9 / `83 C1 32` @7FEE54.
// Loop-end: replace setne+add with mov eax,53;nop — NOT ForceDrawLoop (mov al,1+add).
static void PatchCharDrawLoopForBp53() {
    if (!kExtendCharDrawLoopToBp53) {
        return;
    }
    const DWORD loopSetne = kCharDrawLoopAddMax - 3; // 7FEFB9
    // Already ae-fixed?
    static const unsigned char kFixedMax53[] = {0xB8, 0x35, 0x00, 0x00, 0x00, 0x90}; // mov eax,53; nop
    if (ExpectBytes(loopSetne, kFixedMax53, sizeof(kFixedMax53))) {
        // already fixed
    } else {
        // Native / ad / z forms of setne al; add eax, imm
        static const unsigned char kExpectLoop32[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x32};
        static const unsigned char kExpectLoop34[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x34};
        static const unsigned char kExpectLoop35[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x35};
        const bool ok32 = ExpectBytes(loopSetne, kExpectLoop32, sizeof(kExpectLoop32));
        const bool ok34 = ExpectBytes(loopSetne, kExpectLoop34, sizeof(kExpectLoop34));
        const bool ok35 = ExpectBytes(loopSetne, kExpectLoop35, sizeof(kExpectLoop35));
        if (!ok32 && !ok34 && !ok35) {
            return; // wrong VA / ForceDraw-smashed — refuse
        }
        // Write mov eax,53; nop over the 6-byte setne+add
        Memory::WriteByteArray(loopSetne, const_cast<unsigned char*>(kFixedMax53), sizeof(kFixedMax53));
    }

    // Early max: keep add-imm 0x35 so start-BP entry check allows ≤53 even if flag=0.
    // flag1⇒54 here only gates entry (start≪54); loop-end is fixed at 53 so no BP54 body.
    static const unsigned char kExpectEarlyAdd[] = {0x83, 0xC1, 0x32};
    static const unsigned char kAlreadyEarlyAdd35[] = {0x83, 0xC1, 0x35};
    static const unsigned char kAlreadyEarlyAdd34[] = {0x83, 0xC1, 0x34};
    if (ExpectBytes(kCharDrawLoopAddMaxEarly, kAlreadyEarlyAdd35, sizeof(kAlreadyEarlyAdd35))) {
        // already raised
    } else if (ExpectBytes(kCharDrawLoopAddMaxEarly, kAlreadyEarlyAdd34, sizeof(kAlreadyEarlyAdd34))) {
        Memory::WriteByte(kCharDrawLoopAddMaxEarly + 2, kCharDrawLoopImmBp53);
    } else if (ExpectBytes(kCharDrawLoopAddMaxEarly, kExpectEarlyAdd, sizeof(kExpectEarlyAdd))) {
        Memory::WriteByte(kCharDrawLoopAddMaxEarly + 2, kCharDrawLoopImmBp53);
    }
    (void)kCharDrawLoopImmNative;
}

// Keep flag=1 for draw bound, but do not apply FullBackgrnd (+33) layout deltas.
static void PatchClassicLayoutDeltas() {
    static const unsigned char kAndEax21[] = {0x83, 0xE0, 0x21};
    static const unsigned char kAndEcx21[] = {0x83, 0xE1, 0x21};
    if (ExpectBytes(kLayoutAndDraw, kAndEax21, sizeof(kAndEax21))) {
        Memory::WriteByte(kLayoutAndDraw + 2, 0x00);
    }
    if (ExpectBytes(kLayoutAndPetY, kAndEcx21, sizeof(kAndEcx21))) {
        Memory::WriteByte(kLayoutAndPetY + 2, 0x00);
    }
    if (ExpectBytes(kLayoutAndPetX, kAndEax21, sizeof(kAndEax21))) {
        Memory::WriteByte(kLayoutAndPetX + 2, 0x00);
    }
}

// Right-column weapon/shield/ear: native GetSlotXY = HitTest + 33. With pendant2
// flag=1, draw uses GetSlotXY on classic narrow chrome → weapon on「盾牌」.
// Copy HitTest → GetSlotXY for BP4/10/11 ONLY. Classic rings are hardcoded in
// FixZeroDrawSlots (BP12/13/15/16) — do not fold them into this copy list.
static void AlignClassicDrawToHitTest() {
    static const int kBodyParts[] = {4, 10, 11};
    for (int bp : kBodyParts) {
        const int idx = bp - 1;
        if (idx < 0 || idx >= 50) {
            continue;
        }
        const EquipSlotPos hit = ReadTableSlot(kClassicHitTestTable, idx);
        if (hit.x == 0 && hit.y == 0) {
            continue;
        }
        PatchTableSlot(kClassicGetSlotXyTable, idx, hit.x, hit.y);
    }
}

static void FixZeroDrawSlots() {
    // Medal BP49, belt BP50, pendant2 BP51 — native GetSlotXY (0,0) ghosts top-left.
    PatchTableSlot(kClassicGetSlotXyTable, kMedalIndex, kClassicMedalX, kClassicMedalY);
    PatchTableSlot(kClassicHitTestTable, kMedalIndex, kClassicMedalX, kClassicMedalY);

    PatchTableSlot(kClassicGetSlotXyTable, kBeltIndex, kClassicBeltX, kClassicBeltY);
    PatchTableSlot(kClassicHitTestTable, kBeltIndex, kClassicBeltX, kClassicBeltY);

    PatchTableSlot(kClassicGetSlotXyTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);
    // Index 50 is past the 50-entry regular table into cash[0] — required so the
    // extended character HitTest walk's last entries and [edx*8+BE2260] with edx=50
    // read slot-7 (38,101).
    PatchTableSlot(kClassicHitTestTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);

    // Expanded/CUIEquip bodypart table (indexed by BP number, not index).
    PatchTableSlot(kCuiEquipBe27Table, kPendant2Be27Index, kClassicPendant2X, kClassicPendant2Y);

    // Shoulder BP20 → red8 (137,101), right of 耳饰.
    PatchTableSlot(kClassicGetSlotXyTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    PatchTableSlot(kClassicHitTestTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    PatchTableSlot(kCuiEquipBe27Table, kShoulderBe27Index, kClassicShoulderX, kClassicShoulderY);

    // Classic 戒指/指环: force GetSlotXY + HitTest + BE27E0 to label coords.
    // Native GetSlotXY is +33X; with flag=1 that parks BP13/16 off-chrome (x=170).
    // BP15/16=(104/137,68) 戒指; BP12/13=(104/137,167) 指环.
    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp15Index, kClassicRingBp15X, kClassicRingBp15Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp15Index, kClassicRingBp15X, kClassicRingBp15Y);
    PatchTableSlot(kCuiEquipBe27Table, 15, kClassicRingBp15X, kClassicRingBp15Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp16Index, kClassicRingBp16X, kClassicRingBp16Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp16Index, kClassicRingBp16X, kClassicRingBp16Y);
    PatchTableSlot(kCuiEquipBe27Table, 16, kClassicRingBp16X, kClassicRingBp16Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp12Index, kClassicRingBp12X, kClassicRingBp12Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp12Index, kClassicRingBp12X, kClassicRingBp12Y);
    PatchTableSlot(kCuiEquipBe27Table, 12, kClassicRingBp12X, kClassicRingBp12Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp13Index, kClassicRingBp13X, kClassicRingBp13Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp13Index, kClassicRingBp13X, kClassicRingBp13Y);
    PatchTableSlot(kCuiEquipBe27Table, 13, kClassicRingBp13X, kClassicRingBp13Y);

    // Extra rings BP52/53 → red3/4 (104,35)/(137,35). Overwrites cash[1]/2] like pendant2→cash[0].
    //   Draw loop imm raised to 0x35 so these BPs always paint (see PatchCharDrawLoopForBp53).
    PatchTableSlot(kClassicGetSlotXyTable, kRing5Index, kClassicRing5X, kClassicRing5Y);
    PatchTableSlot(kClassicHitTestTable, kRing5Index, kClassicRing5X, kClassicRing5Y);
    PatchTableSlot(kCuiEquipBe27Table, kRing5Be27Index, kClassicRing5X, kClassicRing5Y);

    PatchTableSlot(kClassicGetSlotXyTable, kRing6Index, kClassicRing6X, kClassicRing6Y);
    PatchTableSlot(kClassicHitTestTable, kRing6Index, kClassicRing6X, kClassicRing6Y);
    PatchTableSlot(kCuiEquipBe27Table, kRing6Be27Index, kClassicRing6X, kClassicRing6Y);

    // Hide BP23 empty「项链」chrome. HitTest[22] untouched (pet HT).
    PatchTableSlot(kClassicGetSlotXyTable, kBp23Index, kBp23ParkX, kBp23ParkY);
}

static void PumpExtraPendantFlags() {
    if (!g_pendant2UiEnabled) {
        return;
    }
    __try {
        void* ctx = *reinterpret_cast<void**>(kCWvsContextSingleton);
        if (ctx) {
            *reinterpret_cast<int*>(reinterpret_cast<char*>(ctx) + kCWvsExtraPendantOff) = 1;
        }
        void* ui = *reinterpret_cast<void**>(kCUIEquipSingleton);
        if (ui) {
            *reinterpret_cast<int*>(reinterpret_cast<char*>(ui) + kExtraPendantFlagOff) = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __declspec(naked) ForceExtraPendantFlag_cave() {
    __asm {
        mov dword ptr [esi + 0x5E8], 1
        mov eax, dword ptr [esi + 0x5E8]
        jmp dword ptr [kEquipSlotDraw_AfterFlag]
    }
}

static void AttachUiHooksOnce() {
    if (g_uiHooksAttached) {
        return;
    }
    LoadPendant2UiFlagOnce();
    if (!g_pendant2UiEnabled) {
        return;
    }
    (void)kForceDrawLoopToBp51;
    (void)kPatchPetHitTestLoop;
    (void)kPetHitTestCmpBpMax;
    (void)kPetHitTestCmpLoopEnd;
    (void)kMedalBodyPart;
    (void)kBeltBodyPart;
    (void)kPendant2BodyPart;
    (void)kShoulderBodyPart;
    (void)kRing5BodyPart;
    (void)kRing6BodyPart;
    (void)Pendant2_GetStamp();

    // Draw←HitTest BEFORE custom slot writes so weapon/shield/ear land on labels;
    // then overlay medal/belt/pendant2/shoulder/classic rings/ring5–6.
    AlignClassicDrawToHitTest();
    FixZeroDrawSlots();
    PatchCharHitTestForBp51();
    PatchCharDrawLoopForBp53();
    PatchClassicLayoutDeltas();

    Memory::CodeCave(ForceExtraPendantFlag_cave, kEquipSlotDraw_MovFlag, 6);
    PumpExtraPendantFlags();
    g_uiHooksAttached = true;
}

} // namespace

void AttachPendant2SlotsFix() {
    if (!kAllowAttachAtDllMain) {
        return;
    }
    AttachUiHooksOnce();
}

void EnsurePendant2AfterFieldEnter() {
    AttachUiHooksOnce();
}

void Pendant2OnClientTick() {
    if (!g_uiHooksAttached) {
        return;
    }
    PumpExtraPendantFlags();
}
