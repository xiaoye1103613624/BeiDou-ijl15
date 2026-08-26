// mr_skill_satellites.cpp — MapleRoot SkillEdits satellites (LtRb / FlashJump / Corsair ship).
// VAs IDA-confirmed on BeiDou.exe (same as MR SERVERNAME @ 0x400000):
//   LtRb_Eval cave   0x00953E2C (6)
//   FlashJump cave   0x0096BF0B
//   shipSkills hook  0x0096719D
//   velocity imm     0x0096C00A / 0x0096C021 / 0x0096C031
#include "stdafx.h"
#include "Memory.h"
#include "HyperSkillApi.h"

#include <cstdio>

namespace {

void HyperSatLog(const char* msg) {
    FILE* f = nullptr;
    if (fopen_s(&f, "beidou-hyperskill.log", "a") == 0 && f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
}

// --- Goose LT/RB eval (Avenger / Mortal Blow xbow) — YourStuff.h LtRb_Eval ---
const void* kLtRbReturn = reinterpret_cast<void*>(0x00953E5C);

void __declspec(naked) LtRb_Eval() {
    __asm {
        cmp dword ptr[ebp - 16], 4111005
        je Label_return_LtRb
        cmp dword ptr[ebp - 16], 3201005
        je Label_return_LtRb
        mov ecx, [ebp - 180]
        jmp Label_return_LtRb
        Label_return_LtRb:
        jmp dword ptr[kLtRbReturn]
    }
}

// --- Flash bounce 1054 — Activeskill.h FlashJumpAll ---
DWORD g_FlashJumpVar = 0x0096BF52;
DWORD g_FlashJumpRet = 0x0096BF12;

void applyVelocityChange() {
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFFFF);
    Memory::WriteInt(0x0096C021 + 3, 0x00000000);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFB50);
}

void restoreVelocityChange() {
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFD55);
    Memory::WriteInt(0x0096C021 + 3, 0x0000025E);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFD50);
}

void __declspec(naked) FlashJumpAll() {
    __asm {
        cmp eax, 0xD72A0C
        je applyDefault
        cmp eax, 1050
        je applyDefault
        cmp eax, 10001050
        je applyDefault
        cmp eax, 20001050
        je applyDefault
        cmp eax, 1054
        je applyOverride
        cmp eax, 10001054
        je applyOverride
        cmp eax, 20001054
        je applyOverride
        jmp dword ptr[g_FlashJumpRet]

        applyOverride:
        push ebp
        mov ebp, esp
        call applyVelocityChange
        mov esp, ebp
        pop ebp
        jmp fjvar

        applyDefault:
        push ebp
        mov ebp, esp
        call restoreVelocityChange
        mov esp, ebp
        pop ebp
        jmp fjvar

        fjvar:
        jmp dword ptr[g_FlashJumpVar]
    }
}

// NOTE 2026-08-22: GetSkillLevel/vertical/GetAttackSpeedDegree hooks were tried and REVERTED —
// they did not fix the hyper-skill cast crash and vertical/GetAttackSpeedDegree are active
// behavior changes that may themselves break casting. Keep the plugin at the proven-casting baseline.

// --- Battleship: allow skills while on ship — YourStuff.h shipSkills (MR whitelist) ---
DWORD g_ShipSkills = 0x0096719D;
DWORD g_ShipSkillsRet = 0x009671A3;
DWORD g_ShipSkillsAllow = 0x009673CF;

bool ShipSkillAllowed(int id) {
    switch (id) {
    case 1050: case 10001050: case 20001050:
    case 1054: case 10001054: case 20001054:
    case 1001: case 1002: case 1005: case 1001003:
    case 1101007: case 1101006: case 1111007: case 1111002:
    case 1121010: case 1121011: case 1121000: case 1121002:
    case 1201007: case 1201006: case 1211004: case 1211006:
    case 1211008: case 1211009: case 1211003: case 1211005:
    case 1211007: case 1221004: case 1221012: case 1221000:
    case 1221002: case 1221003: case 1301007: case 1301006:
    case 1301005: case 1301004: case 1311008: case 1311007:
    case 1320008: case 1321007: case 1321010: case 1320009:
    case 1321000: case 1321002: case 2001002: case 2001003:
    case 2101001: case 2101003: case 2111005: case 2121008:
    case 2121004: case 2121002: case 2121000: case 2201001:
    case 2201003: case 2211005: case 2221008: case 2221004:
    case 2221002: case 2221000: case 2301003: case 2301004:
    case 2311001: case 2311003: case 2311002: case 2321009:
    case 2321005: case 2321004: case 2321002: case 2321000:
    case 3001003: case 3101004: case 3111002: case 3121008:
    case 3121009: case 3121000: case 3121002: case 3201004:
    case 3211002: case 3221006: case 3221008: case 3221000:
    case 3221002: case 4001003: case 4101004: case 4111001:
    case 4111002: case 4121009: case 4121000: case 4121006:
    case 4201003: case 4211005: case 4211003: case 4221008:
    case 4221000: case 4221004: case 5001005: case 10000012:
    case 10001005: case 10001002: case 10001001: case 11111001:
    case 11101002: case 11001001: case 11101003: case 11001004:
    case 11111007: case 11121204: case 12101005: case 12001004:
    case 12111004: case 12001002: case 12001001: case 12101000:
    case 12101001: case 13111005: case 13101002: case 13001002:
    case 13111004: case 13101003: case 13001004: case 13101006:
    case 14001005: case 14001003: case 14101003: case 14111000:
    case 15001003: case 15100004: case 15111001: case 15101002:
    case 15001004: case 15111006: case 15101006: case 15111005:
    case 20000012: case 20001002: case 20001005: case 20001001:
    case 21121000: case 21121008: case 21100005: case 21111005:
    case 21101003: case 21111001: case 21120007: case 21000000:
    case 13121008: case 12111002: case 2211004: case 2111004:
    case 2311005: case 5221014: case 5221018: case 5121009:
    case 5121000: case 5221000: case 5221010: case 5121008:
    case 5211006: case 5221016:
    // hyper buffs / bard
    case 1131109: case 1231109: case 1331109: case 11131109: case 21131109:
    case 2131006: case 2231006: case 2331006: case 12131006:
    case 3131007: case 3231007: case 13131007:
    case 4131031: case 4231031: case 14131031:
    case 5131033: case 5231033: case 15131033:
    case 7101101: case 7101102: case 7101103: case 7101104:
    case 7101105: case 7101106: case 7101107: case 7101108:
        return true;
    default:
        return false;
    }
}

void __declspec(naked) ShipSkills_Hook() {
    __asm {
        push eax
        push esi
        call ShipSkillAllowed
        add esp, 4
        test al, al
        pop eax
        jne ship_allow
        cmp esi, 5211002
        jmp dword ptr[g_ShipSkillsRet]
        ship_allow:
        jmp dword ptr[g_ShipSkillsAllow]
    }
}

void InstallCorsairShipMods() {
    Memory::WriteByte(0x00967235, 0x75);
    Memory::WriteByte(0x00967191 + 1, 0x84);
    Memory::SetHook(true, reinterpret_cast<void**>(&g_ShipSkills), reinterpret_cast<void*>(&ShipSkills_Hook));
}

} // namespace

void AttachMrSkillSatellites() {
    // 2026-08-26: beginner Root bounce skills 1050/1054 need FlashJumpAll (MR YourStuff.h
    // NINJA BOUNCE). LtRb + Corsair stay off — they were correlated with 0001002 hang and
    // are unrelated to bounce.
    HyperSatLog("[hyperskill] AttachMrSkillSatellites (FlashJump only for 1050/1054)");
    Memory::CodeCave(reinterpret_cast<void*>(&FlashJumpAll), 0x0096BF0B, 0);
    // MR YourStuff.h:3573 — nop the code path that kills the hyper 1054 bounce.
    Memory::PatchNop(0x0096C073, 6);
    // MR YourStuff.h:3576-3578 — install-time FlashJump default momentum baseline.
    Memory::WriteInt(0x0096C00A + 1, 0xFFFFFD55);
    Memory::WriteInt(0x0096C021 + 3, 0x0000025E);
    Memory::WriteInt(0x0096C031 + 1, 0xFFFFFD50);
    HyperSatLog("[hyperskill] AttachMrSkillSatellites OK (FlashJump; LtRb/Corsair still off)");
}
