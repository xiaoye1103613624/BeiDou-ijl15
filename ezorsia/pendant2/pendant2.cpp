// Stamp: FIX_NO_BE27E0_KEYPOS_20260830
//   Root cause of 键盘错位: old code wrote Equip coords into 0xBE27E0
//   (CUIKeyConfig::s_aptKeyPos — IDA sub_8354E4 / EXE xrefs @833AC2,8354F8).
//   Fix: never write BE27E0; heal stock keypos if sentinel corrupted.
//
// UI (this file): classic layout, BP23 park, char HT extend, flag cave,
//   char equip icon draw loop fixed max BP55 (NOT ForceDrawLoop).
//   Pocket = Addon row3 (no main red9/10 wire).
//   NO BP33 draw cave. 109 shield = leave vanilla (no Option B coords).
// Wear/stats (shoulders.cpp): Occ OFF; ExtraRing Get/Set+UI ON; Addon Get/Set; apply max55.
//
// Mapping (UI red marks):
//   9 = 口袋 Po / BP33 / −33 → (104,200) right of 鞋子
//   10 = 辅助 134/135 only → (137,200); 109 盾 = vanilla native Si path
//   Badge BP54 + Totem1 BP55 live on Addon — parked off-panel on main Equip.
//
// Design: raise draw with ae-style `mov eax,55;nop` — NOT add-imm+flag (BP54 hang).
//
// Policy: NEVER ForceDrawLoop (setne→mov al,1) @ 0x007FEFB9. NEVER pet HT @ 801214/8013A3.
//   NEVER write 0xBE27E0 as an Equip table.

#include "stdafx.h"
#include "Pendant2Api.h"
#include "ShoulderApi.h"
#include "equipaddon/EquipAddonApi.h"
#include "Memory.h"
#include "INIReader.h"

#include <string>

namespace {

static constexpr bool kForceDrawLoopToBp51 = false; // NEVER true
static constexpr bool kExtendCharDrawLoopToBp55 = true;
static constexpr bool kAllowAttachAtDllMain = false;
static constexpr bool kPatchPetHitTestLoop = false; // NEVER true

// Red-7: Row3 Col2
static constexpr int kClassicPendant2X = 38;
static constexpr int kClassicPendant2Y = 101;

// Red-8: Row3 Col5 — shoulder BP20
static constexpr int kClassicShoulderX = 137;
static constexpr int kClassicShoulderY = 101;

// Red-3 / Red-4: extra rings BP52/53
static constexpr int kClassicRing5X = 104;
static constexpr int kClassicRing5Y = 35;
static constexpr int kClassicRing6X = 137;
static constexpr int kClassicRing6Y = 35;

// Red-9 pocket / red-10 aux — wired by EquipAddon while Equip open (not here).
static constexpr int kClassicRed9X = 104;
static constexpr int kClassicRed9Y = 200;
static constexpr int kClassicRed10X = 137;
static constexpr int kClassicRed10Y = 200;
static constexpr int kOffPanelX = -2000;
static constexpr int kOffPanelY = -2000;
static constexpr int kClassicRingBp15X = 104;
static constexpr int kClassicRingBp15Y = 68;
static constexpr int kClassicRingBp16X = 137;
static constexpr int kClassicRingBp16Y = 68;
static constexpr int kClassicRingBp12X = 104;
static constexpr int kClassicRingBp12Y = 167;
static constexpr int kClassicRingBp13X = 137;
static constexpr int kClassicRingBp13Y = 167;

static constexpr int kClassicBeltX = 71;
static constexpr int kClassicBeltY = 167;
static constexpr int kClassicMedalX = 5;
static constexpr int kClassicMedalY = 68;

static constexpr int kBp23ParkX = -128;
static constexpr int kBp23ParkY = -128;

extern "C" __declspec(dllexport) const char* Pendant2_GetStamp() {
    return "FIX_NO_BE27E0_KEYPOS_20260830";
}

static constexpr int kPendant2BodyPart = 51;
static constexpr int kPendant2Index = 50;
static constexpr int kShoulderBodyPart = 20;
static constexpr int kShoulderIndex = 19;
static constexpr int kRing5BodyPart = 52;
static constexpr int kRing5Index = 51;
static constexpr int kRing6BodyPart = 53;
static constexpr int kRing6Index = 52;
static constexpr int kBadgeBodyPart = 54;
static constexpr int kBadgeIndex = 53; // HitTest/GetSlotXY: BP = index+1
static constexpr int kTotemBodyPart = 55;
static constexpr int kTotemIndex = 54;
static constexpr int kPocketBodyPart = 33;
static constexpr int kPocketIndex = 32;
static constexpr int kSubWeaponBodyPart = 10;
static constexpr int kSubWeaponIndex = 9;
static constexpr int kClassicRingBp12Index = 11;
static constexpr int kClassicRingBp13Index = 12;
static constexpr int kClassicRingBp15Index = 14;
static constexpr int kClassicRingBp16Index = 15;
static constexpr int kBeltBodyPart = 50;
static constexpr int kBeltIndex = 49;
static constexpr int kMedalBodyPart = 49;
static constexpr int kMedalIndex = 48;
static constexpr int kBp23Index = 22;

static constexpr DWORD kEquipSlotDraw_MovFlag = 0x007FDE8B;
static const DWORD kEquipSlotDraw_AfterFlag = 0x007FDE91;

static constexpr DWORD kCUIEquipSingleton = 0x00BED650;
static constexpr DWORD kCWvsContextSingleton = 0x00BE7918;
static constexpr int kCWvsExtraPendantOff = 0x387C;
static constexpr int kExtraPendantFlagOff = 0x5E8;

static constexpr DWORD kClassicHitTestTable = 0x00BE2260;
static constexpr DWORD kClassicGetSlotXyTable = 0x00BE2580;
static constexpr DWORD kClassicCashSlotXyTable = 0x00BE23F0; // flag==0 GetSlotXY
// FORBIDDEN as Equip table: 0x00BE27E0 = CUIKeyConfig::s_aptKeyPos
// (EXE xrefs 0x833AC2 / 0x8354F8; IDA sub_8354E4). Heal-only below.
static constexpr DWORD kAptKeyPosTable = 0x00BE27E0;
static constexpr int kAptKeyPosCount = 91; // main keyboard + bottom modifiers

// Character GetBodyPartFromPoint @ 0x7FEC6F end cmp.
// BP55 end: BE2260 + 55*8 + 4 = BE241C
static constexpr DWORD kCharHitTestFunc = 0x007FEC32;
static constexpr DWORD kCharHitTestEndCmp = 0x007FEC6F; // 81 F9 .. .. BE 00
static constexpr BYTE kCharHitTestEndImmLo = 0x1C;
static constexpr BYTE kCharHitTestEndImmHi = 0x24; // BE241C

static constexpr DWORD kCharDrawLoopAddMax = 0x007FEFBC;
static constexpr DWORD kCharDrawLoopAddMaxEarly = 0x007FEE54;
static constexpr BYTE kCharDrawLoopImmNative = 0x32;
static constexpr BYTE kCharDrawLoopImmBp55 = 0x37; // early entry gate

static constexpr DWORD kPetHitTestCmpBpMax = 0x00801214;
static constexpr DWORD kPetHitTestCmpLoopEnd = 0x008013A3;

static constexpr DWORD kLayoutAndDraw = 0x007FDE9D;
static constexpr DWORD kLayoutAndPetY = 0x00801A5F;
static constexpr DWORD kLayoutAndPetX = 0x00801A77;

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
    char* slash = strrchr(dllPath, '\\');
    if (!slash) {
        slash = strrchr(dllPath, '/');
    }
    if (!slash) {
        return "config.ini";
    }
    *(slash + 1) = '\0';
    return std::string(dllPath) + "config.ini";
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
    // Classic HitTest @BE2260 is only 50 entries; index>=50 is the cash table.
    // Extended BP51–55 HitTest lives in shoulders DLL table — do not smash cash.
    // GetBodyPartFromPoint walks the DLL table (shoulders redirect), so any
    // classic HT remap must also update g_hitTestExt or tip/dblclick miss.
    // Never route Equip remaps through kAptKeyPosTable / BE27E0.
    if (tableBase == kAptKeyPosTable) {
        return;
    }
    if (tableBase == kClassicHitTestTable) {
        Shoulder_SetExtHitTestSlot(index, x, y);
        if (index >= 50) {
            return;
        }
    }
    auto* slots = reinterpret_cast<EquipSlotPos*>(tableBase);
    DWORD oldProt = 0;
    if (!VirtualProtect(slots + index, sizeof(EquipSlotPos), PAGE_EXECUTE_READWRITE, &oldProt)) {
        return;
    }
    slots[index].x = x;
    slots[index].y = y;
    VirtualProtect(slots + index, sizeof(EquipSlotPos), oldProt, &oldProt);
}

// Stock CUIKeyConfig::s_aptKeyPos[0..90] from BeiDou.exe .data @ BE27E0.
// Sentinel: [2]=(48,66) Esc-row '1', [16]=(64,99) Tab, [42]=(38,165) Shift.
static const EquipSlotPos kStockAptKeyPos[kAptKeyPosCount] = {
    {0, 0}, {0, 0}, {48, 66}, {82, 66}, {116, 66}, {150, 66}, {184, 66}, {218, 66},
    {252, 66}, {286, 66}, {320, 66}, {354, 66}, {388, 66}, {422, 66}, {0, 0}, {0, 0},
    {64, 99}, {98, 99}, {132, 99}, {166, 99}, {200, 99}, {234, 99}, {268, 99}, {302, 99},
    {336, 99}, {370, 99}, {404, 99}, {438, 99}, {0, 0}, {22, 198}, {81, 132}, {115, 132},
    {149, 132}, {183, 132}, {217, 132}, {251, 132}, {285, 132}, {319, 132}, {353, 132},
    {387, 132}, {421, 132}, {14, 66}, {38, 165}, {472, 99}, {98, 165}, {132, 165},
    {166, 165}, {200, 165}, {234, 165}, {268, 165}, {302, 165}, {336, 165}, {370, 165},
    {0, 0}, {457, 165}, {0, 0}, {122, 198}, {233, 198}, {0, 0}, {82, 27}, {116, 27},
    {150, 27}, {184, 27}, {226, 27}, {260, 27}, {294, 27}, {328, 27}, {370, 27},
    {404, 27}, {0, 0}, {0, 0}, {548, 66}, {0, 0}, {582, 66}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {548, 99}, {0, 0}, {582, 99}, {514, 66}, {514, 99}, {0, 0},
    {0, 0}, {0, 0}, {438, 27}, {472, 27}, {461, 198}, {348, 198},
};

static void HealAptKeyPosIfCorrupted() {
    auto* live = reinterpret_cast<EquipSlotPos*>(kAptKeyPosTable);
    const bool ok = live[2].x == 48 && live[2].y == 66 && live[16].x == 64 && live[16].y == 99
        && live[42].x == 38 && live[42].y == 165;
    if (ok) {
        return;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(live, sizeof(EquipSlotPos) * kAptKeyPosCount, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return;
    }
    for (int i = 0; i < kAptKeyPosCount; ++i) {
        live[i] = kStockAptKeyPos[i];
    }
    VirtualProtect(live, sizeof(EquipSlotPos) * kAptKeyPosCount, oldProt, &oldProt);
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

// Extend character HitTest walk to BP55 end (BE241C) — PE-table path only.
// Native mov ecx loads BE2264 (&[0].y), not BE2260. If shoulders already
// redirected ecx to the DLL HT table, do not touch end (would re-enter cash).
static void PatchCharHitTestForBp55() {
    const DWORD tableImm = *reinterpret_cast<const DWORD*>(kCharHitTestFunc + 3);
    // Only native Y-pointer; shoulders uses DLL addr (neither BE2260 nor BE2264).
    if (tableImm != 0x00BE2264) {
        return;
    }
    static const unsigned char kFuncStart[] = {0x33, 0xD2, 0xB9, 0x64, 0x22, 0xBE, 0x00};
    if (!ExpectBytes(kCharHitTestFunc, kFuncStart, sizeof(kFuncStart))) {
        return;
    }
    // 81 F9 xx xx BE 00
    if (*reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp) != 0x81
        || *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 1) != 0xF9
        || *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 4) != 0xBE) {
        return;
    }
    const unsigned char b2 = *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 2);
    const unsigned char b3 = *reinterpret_cast<const unsigned char*>(kCharHitTestEndCmp + 3);
    // Accept native BE23F4, BP51 BE23FC, BP53 BE240C, or already BP55 BE241C
    const bool known =
        (b3 == 0x23 && (b2 == 0xF4 || b2 == 0xFC))
        || (b3 == 0x24 && (b2 == 0x0C || b2 == 0x1C));
    if (!known) {
        return;
    }
    if (b2 == kCharHitTestEndImmLo && b3 == kCharHitTestEndImmHi) {
        return;
    }
    Memory::WriteByte(kCharHitTestEndCmp + 2, kCharHitTestEndImmLo);
    Memory::WriteByte(kCharHitTestEndCmp + 3, kCharHitTestEndImmHi);
}

// Raise icon-draw max to exactly BP55 (never 56). ae-style fixed mov.
// Pocket BP33 skip exception is owned by shoulders (BP33-only cave).
static void PatchCharDrawLoopForBp55() {
    if (!kExtendCharDrawLoopToBp55) {
        return;
    }
    const DWORD loopSetne = kCharDrawLoopAddMax - 3; // 7FEFB9
    static const unsigned char kFixedMax55[] = {0xB8, 0x37, 0x00, 0x00, 0x00, 0x90}; // mov eax,55; nop
    static const unsigned char kFixedMax53[] = {0xB8, 0x35, 0x00, 0x00, 0x00, 0x90};
    if (ExpectBytes(loopSetne, kFixedMax55, sizeof(kFixedMax55))) {
        // already
    } else if (ExpectBytes(loopSetne, kFixedMax53, sizeof(kFixedMax53))) {
        Memory::WriteByteArray(loopSetne, const_cast<unsigned char*>(kFixedMax55), sizeof(kFixedMax55));
    } else {
        static const unsigned char kExpectLoop32[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x32};
        static const unsigned char kExpectLoop34[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x34};
        static const unsigned char kExpectLoop35[] = {0x0F, 0x95, 0xC0, 0x83, 0xC0, 0x35};
        const bool ok32 = ExpectBytes(loopSetne, kExpectLoop32, sizeof(kExpectLoop32));
        const bool ok34 = ExpectBytes(loopSetne, kExpectLoop34, sizeof(kExpectLoop34));
        const bool ok35 = ExpectBytes(loopSetne, kExpectLoop35, sizeof(kExpectLoop35));
        if (!ok32 && !ok34 && !ok35) {
            return;
        }
        Memory::WriteByteArray(loopSetne, const_cast<unsigned char*>(kFixedMax55), sizeof(kFixedMax55));
    }

    // Early max: raise add-imm so start entry allows ≤55; loop-end fixed so no BP56 body.
    const unsigned char earlyImm = *reinterpret_cast<const unsigned char*>(kCharDrawLoopAddMaxEarly + 2);
    if (*reinterpret_cast<const unsigned char*>(kCharDrawLoopAddMaxEarly) == 0x83
        && *reinterpret_cast<const unsigned char*>(kCharDrawLoopAddMaxEarly + 1) == 0xC1
        && (earlyImm == 0x32 || earlyImm == 0x34 || earlyImm == 0x35)) {
        Memory::WriteByte(kCharDrawLoopAddMaxEarly + 2, kCharDrawLoopImmBp55);
    }
    (void)kCharDrawLoopImmNative;
}

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
    PatchTableSlot(kClassicGetSlotXyTable, kMedalIndex, kClassicMedalX, kClassicMedalY);
    PatchTableSlot(kClassicHitTestTable, kMedalIndex, kClassicMedalX, kClassicMedalY);

    PatchTableSlot(kClassicGetSlotXyTable, kBeltIndex, kClassicBeltX, kClassicBeltY);
    PatchTableSlot(kClassicHitTestTable, kBeltIndex, kClassicBeltX, kClassicBeltY);

    PatchTableSlot(kClassicGetSlotXyTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);
    PatchTableSlot(kClassicHitTestTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);

    PatchTableSlot(kClassicGetSlotXyTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    PatchTableSlot(kClassicHitTestTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    // flag==0 GetSlotXY reads BE23F0 — native BP20 is (71,233) off-panel.
    PatchTableSlot(kClassicCashSlotXyTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp15Index, kClassicRingBp15X, kClassicRingBp15Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp15Index, kClassicRingBp15X, kClassicRingBp15Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp16Index, kClassicRingBp16X, kClassicRingBp16Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp16Index, kClassicRingBp16X, kClassicRingBp16Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp12Index, kClassicRingBp12X, kClassicRingBp12Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp12Index, kClassicRingBp12X, kClassicRingBp12Y);

    PatchTableSlot(kClassicGetSlotXyTable, kClassicRingBp13Index, kClassicRingBp13X, kClassicRingBp13Y);
    PatchTableSlot(kClassicHitTestTable, kClassicRingBp13Index, kClassicRingBp13X, kClassicRingBp13Y);

    PatchTableSlot(kClassicGetSlotXyTable, kRing5Index, kClassicRing5X, kClassicRing5Y);
    PatchTableSlot(kClassicHitTestTable, kRing5Index, kClassicRing5X, kClassicRing5Y);

    PatchTableSlot(kClassicGetSlotXyTable, kRing6Index, kClassicRing6X, kClassicRing6Y);
    PatchTableSlot(kClassicHitTestTable, kRing6Index, kClassicRing6X, kClassicRing6Y);

    // Badge / Totem1 OFF main Equip (Addon paints them).
    PatchTableSlot(kClassicGetSlotXyTable, kBadgeIndex, kOffPanelX, kOffPanelY);
    PatchTableSlot(kClassicHitTestTable, kBadgeIndex, kOffPanelX, kOffPanelY);

    PatchTableSlot(kClassicGetSlotXyTable, kTotemIndex, kOffPanelX, kOffPanelY);
    PatchTableSlot(kClassicHitTestTable, kTotemIndex, kOffPanelX, kOffPanelY);

    // Pocket/red10: EquipAddon::WirePocketAndSiSlots (Equip-open only).
    // 109 shield: leave vanilla BP10 GetSlotXY/HT untouched.
    PatchTableSlot(kClassicGetSlotXyTable, kBp23Index, kBp23ParkX, kBp23ParkY);
    (void)kPocketBodyPart;
    (void)kSubWeaponBodyPart;
    (void)kPocketIndex;
    (void)kSubWeaponIndex;
    (void)kClassicRed9X;
    (void)kClassicRed9Y;
    (void)kClassicRed10X;
    (void)kClassicRed10Y;
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
    // KeyConfig heal is independent of pendant2_ui — old builds / other modules
    // may have left s_aptKeyPos poisoned before Equip hooks attach.
    HealAptKeyPosIfCorrupted();
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
    (void)kBadgeBodyPart;
    (void)kTotemBodyPart;
    (void)Pendant2_GetStamp();

    AlignClassicDrawToHitTest();
    FixZeroDrawSlots();
    HealAptKeyPosIfCorrupted();
    PatchCharHitTestForBp55();
    PatchCharDrawLoopForBp55();
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
        // Still try KeyConfig heal if field-enter attach was skipped (pendant2_ui off).
        static bool s_healedOnce = false;
        if (!s_healedOnce) {
            s_healedOnce = true;
            HealAptKeyPosIfCorrupted();
        }
        return;
    }
    PumpExtraPendantFlags();
    // Re-assert gold classic seats — races after other UI patches can wipe
    // GetSlotXY/HT. Do NOT write BE27E0 as Equip (KeyConfig s_aptKeyPos).
    // Dual-table: flag==0 → BE23F0, flag!=0 → BE2580 (native BP20=(71,233)).
    PatchTableSlot(kClassicGetSlotXyTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    PatchTableSlot(kClassicCashSlotXyTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    PatchTableSlot(kClassicHitTestTable, kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    Shoulder_SetExtHitTestSlot(kShoulderIndex, kClassicShoulderX, kClassicShoulderY);
    static DWORD s_lastDiagTick = 0;
    const DWORD now = GetTickCount();
    if (now - s_lastDiagTick > 5000) {
        s_lastDiagTick = now;
        Shoulder_ReassertUiAndDiag();
    }
    PatchTableSlot(kClassicGetSlotXyTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);
    PatchTableSlot(kClassicHitTestTable, kPendant2Index, kClassicPendant2X, kClassicPendant2Y);
    PatchTableSlot(kClassicGetSlotXyTable, kRing5Index, kClassicRing5X, kClassicRing5Y);
    PatchTableSlot(kClassicHitTestTable, kRing5Index, kClassicRing5X, kClassicRing5Y);
    PatchTableSlot(kClassicGetSlotXyTable, kRing6Index, kClassicRing6X, kClassicRing6Y);
    PatchTableSlot(kClassicHitTestTable, kRing6Index, kClassicRing6X, kClassicRing6Y);
    // Re-assert Addon park only; pocket/red10 gated inside WirePocketAndSiSlots.
    PatchTableSlot(kClassicGetSlotXyTable, kBadgeIndex, kOffPanelX, kOffPanelY);
    PatchTableSlot(kClassicHitTestTable, kBadgeIndex, kOffPanelX, kOffPanelY);
    Shoulder_SetExtHitTestSlot(kBadgeIndex, kOffPanelX, kOffPanelY);
    PatchTableSlot(kClassicGetSlotXyTable, kTotemIndex, kOffPanelX, kOffPanelY);
    PatchTableSlot(kClassicHitTestTable, kTotemIndex, kOffPanelX, kOffPanelY);
    Shoulder_SetExtHitTestSlot(kTotemIndex, kOffPanelX, kOffPanelY);
    EquipAddon::WirePocketAndSiSlots();
}

void Pendant2EnsureNarrowEquipLayout() {
    PatchClassicLayoutDeltas();
}
