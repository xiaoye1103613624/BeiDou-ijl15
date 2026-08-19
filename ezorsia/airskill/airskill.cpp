// airskill.cpp — allow most skills while airborne (GMS v083).
// Source: 扩展改动/跳跃释放技能/airskill.cpp (adapted to MXD hook helpers).

#include "stdafx.h"
#include "AirSkillApi.h"
#include "Memory.h"
#include "compat/hook.h"

#include <iostream>

// CUser stores a pointer at this+0x11A4 that points 12 bytes PAST the CVecCtrl base.
// CVecCtrl+0x110 (272) - ground flag: 0 = in air (or on ladder).
static DWORD GetVecCtrlBase(const void* pThis) {
    const DWORD ptr = *reinterpret_cast<const DWORD*>(
        reinterpret_cast<const BYTE*>(pThis) + 0x11A4);
    return ptr ? (ptr - 12) : 0;
}

static auto CUser_IsOnLadderOrRope =
    reinterpret_cast<BOOL(__thiscall*)(const void*)>(0x004ACD7B);

static bool IsOnLadder(const void* pThis) {
    return CUser_IsOnLadderOrRope(pThis) != FALSE;
}

class CAirSkill {
public:
    MEMBER_HOOK(int, 0x00950921, TryDoingMeleeAttack,
        int* a2, __int64 a3, void* a4, LONG a5, BYTE* a1, wchar_t** a6)

    MEMBER_HOOK(int, 0x009537D5, TryDoingShootAttack,
        __int64 a2, int a3, ULONG** a4, LONG a5, int a6)

    MEMBER_HOOK(int, 0x0095571F, TryDoingMagicAttack,
        __int64 a2, int a3, int a4)
};

class CTeleportAir {
public:
    MEMBER_HOOK(int, 0x00957B74, TryRegisterTeleport,
        void* pSkill, int** nSLV, char* sPortalName, int* sTargetPortalName, unsigned int bForced)
};

int CAirSkill::TryDoingMeleeAttack_hook(int* a2, __int64 a3, void* a4, LONG a5, BYTE* a1, wchar_t** a6) {
    DWORD vc = GetVecCtrlBase(this);
    int r = TryDoingMeleeAttack(this, a2, a3, a4, a5, a1, a6);
    if (r) {
        return r;
    }
    // Rush crashes when ground=1 is faked (null target dereference).
    if (a2 && (*a2 == 1121006 || *a2 == 1221007 || *a2 == 1321003)) {
        return r;
    }
    DWORD s110 = 0;
    if (vc) {
        s110 = *reinterpret_cast<DWORD*>(vc + 272);
        *reinterpret_cast<DWORD*>(vc + 272) = 1;
    }
    r = TryDoingMeleeAttack(this, a2, a3, a4, a5, a1, a6);
    if (vc) {
        *reinterpret_cast<DWORD*>(vc + 272) = s110;
    }
    return r;
}

int CAirSkill::TryDoingShootAttack_hook(__int64 a2, int a3, ULONG** a4, LONG a5, int a6) {
    int r = TryDoingShootAttack(this, a2, a3, a4, a5, a6);
    if (r) {
        return r;
    }
    if (IsOnLadder(this)) {
        return r;
    }
    DWORD vc = GetVecCtrlBase(this);
    DWORD s110 = 0;
    if (vc) {
        s110 = *reinterpret_cast<DWORD*>(vc + 272);
        *reinterpret_cast<DWORD*>(vc + 272) = 1;
    }
    r = TryDoingShootAttack(this, a2, a3, a4, a5, a6);
    if (vc) {
        *reinterpret_cast<DWORD*>(vc + 272) = s110;
    }
    return r;
}

int CAirSkill::TryDoingMagicAttack_hook(__int64 a2, int a3, int a4) {
    int r = TryDoingMagicAttack(this, a2, a3, a4);
    if (r) {
        return r;
    }
    if (IsOnLadder(this)) {
        return r;
    }
    DWORD vc = GetVecCtrlBase(this);
    DWORD s110 = 0;
    if (vc) {
        s110 = *reinterpret_cast<DWORD*>(vc + 272);
        *reinterpret_cast<DWORD*>(vc + 272) = 1;
    }
    r = TryDoingMagicAttack(this, a2, a3, a4);
    if (vc) {
        *reinterpret_cast<DWORD*>(vc + 272) = s110;
    }
    return r;
}

int CTeleportAir::TryRegisterTeleport_hook(
        void* pSkill, int** nSLV, char* sPortalName, int* sTargetPortalName, unsigned int bForced) {
    int r = TryRegisterTeleport(this, pSkill, nSLV, sPortalName, sTargetPortalName, bForced);
    if (r) {
        return r;
    }
    if (IsOnLadder(this)) {
        return r;
    }
    // Pass bForced=1 to skip ground check. Do NOT fake CVecCtrl+272 (ZRef crash).
    return TryRegisterTeleport(this, pSkill, nSLV, sPortalName, sTargetPortalName, 1);
}

void AttachAirSkillMod() {
    ATTACH_HOOK(CAirSkill::TryDoingMeleeAttack, CAirSkill::TryDoingMeleeAttack_hook);
    ATTACH_HOOK(CAirSkill::TryDoingShootAttack, CAirSkill::TryDoingShootAttack_hook);
    ATTACH_HOOK(CAirSkill::TryDoingMagicAttack, CAirSkill::TryDoingMagicAttack_hook);
    ATTACH_HOOK(CTeleportAir::TryRegisterTeleport, CTeleportAir::TryRegisterTeleport_hook);
    // Pirate 4th-job in-air ground checks inside TryDoingMeleeAttack.
    Memory::PatchNop(0x00950B79, 6); // Demolition   (5121004)
    Memory::PatchNop(0x00950BB0, 6); // Dragon Strike (5121006)
    Memory::PatchNop(0x00950BBB, 6); // Snatch       (5121005)
    Memory::PatchNop(0x00950BC6, 2); // Barrage      (5121007)
    std::cout << "[airskill] AttachAirSkillMod OK" << std::endl;
}
