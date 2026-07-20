// maxhpmp.cpp - HP/MP/MaxHP/MaxMP expansion short(32767) -> int(99999).
// BeiDou.exe @ 0x400000 base. Every address below was cross-checked against
// the live IDA database for this exact binary (BeiDou_GMS_083.exe.i64,
// module BeiDou.exe, imagebase 0x400000) before being written here.
//
// Reference: GW_CharacterStat.md (Status: Working, 107 patches total)
//
// Architecture (hybrid FakeTear + Fuse hook - see reference doc for why a
// global Tear_short -> Tear_long redirect crashes with ZException):
//   1. FakeTear: HP/MP Tear call sites are redirected (PatchCall) from
//      ZtlSecureTear_short/_long to HPMP_FakeTear, which stores the raw
//      4-byte value at [ptr] and returns checksum=0 as a sentinel.
//   2. Fuse hook: ZtlSecureFuse_short AND ZtlSecureFuse_long are hooked
//      (Detours) globally. checksum==0 -> return *(int*)pTear (raw 4-byte).
//      Otherwise fall through to the original Fuse (encrypted, non-HP/MP).
//   3. FixMovsx: every movsx that would truncate the now-32-bit Fuse return
//      value back to 16 bits is patched to a same-size mov (+ NOP pad).
//   4. Decode2 -> Decode4: all HP/MP packet decode sites read 4 bytes
//      instead of 2 (server must send writeInt for these fields).
//   5. Clamping limits raised 30000 -> 99999; two 16-bit `cmp ax,si`
//      compares widened to 32-bit `cmp eax,esi`.
//   5b. FixTest: `test ax,ax` after HP Fuse widened to `test eax,eax` so
//       HP>=32768 is not treated as dead (blocks item/skill use).
//
// Do NOT globally redirect Tear_short -> Tear_long (see FAILED APPROACHES
// in the reference doc). Do NOT apply FakeFuse globally (breaks all other
// ZtlSecure<short>/<long> stats, which must remain encrypted).

#include "stdafx.h"
#include "MaxHpMpApi.h"
#include "Memory.h"
#include "compat/hook.h"

#include <cstdio>
#include <cstring>

namespace {

// Logging - mirrors highershoplist.cpp's ShopLog helper.

void MhmLog(const char* format, ...) {
    char msg[512];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[640];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        *(slash + 1) = '\0';
    }
    strcat_s(path, MAX_PATH, "maxhpmp_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }
}

// Key function addresses (image base 0x400000).

constexpr uintptr_t kAddr_CInPacket_Decode2   = 0x0042470C;
constexpr uintptr_t kAddr_CInPacket_Decode4   = 0x00406629;
constexpr uintptr_t kAddr_ZtlSecureTear_short = 0x004E80EB; // __fastcall: ECX=value, EDX=ptr
constexpr uintptr_t kAddr_ZtlSecureTear_long  = 0x004165B1; // DO NOT call directly; FakeTear redirects only
constexpr uintptr_t kAddr_ZtlSecureFuse_short = 0x004746DD; // __cdecl(ptr, checksum) [HOOKED]
constexpr uintptr_t kAddr_ZtlSecureFuse_long  = 0x00416563; // __cdecl(ptr, checksum) [HOOKED]

// HPMP_FakeTear - replaces ZtlSecureTear_short/_long at HP/MP write sites.
// Call-site convention confirmed by disassembly: __fastcall(ECX=value, EDX=ptr).
// Stores the raw 4-byte value at [ptr] and returns checksum=0 (sentinel).

unsigned int __fastcall HPMP_FakeTear(int value, void* ptr) {
    // Zero-extend to 8 bytes so Fuse_long CS==0 can safely read int64
    // (shared sentinel with EXP FakeTear; HP always fits int32).
    *reinterpret_cast<int*>(ptr) = value;
    *(reinterpret_cast<int*>(ptr) + 1) = 0;
    return 0;
}

void* FakeTearTarget() {
    return CastHook(&HPMP_FakeTear);
}

// Global Fuse hooks (Detours) - checksum==0 sentinel means "raw HP/MP slot,
// read *(int*)pTear directly"; any other checksum falls through to the real
// (encrypted) Fuse so every other ZtlSecure<short>/<long> stat is untouched.

typedef int(__cdecl* t_ZtlSecureFuse)(void* pTear, unsigned int checksum);

auto ZtlSecureFuse_short = reinterpret_cast<t_ZtlSecureFuse>(kAddr_ZtlSecureFuse_short);
auto ZtlSecureFuse_long  = reinterpret_cast<t_ZtlSecureFuse>(kAddr_ZtlSecureFuse_long);

int __cdecl ZtlSecureFuse_short_hook(void* pTear, unsigned int checksum) {
    if (checksum == 0) {
        return *reinterpret_cast<int*>(pTear);
    }
    return ZtlSecureFuse_short(pTear, checksum);
}

int __cdecl ZtlSecureFuse_long_hook(void* pTear, unsigned int checksum) {
    if (checksum == 0) {
        const long long v = *reinterpret_cast<long long*>(pTear);
        if (v > 2147483647LL) {
            return 2147483647;
        }
        if (v < -2147483647LL) {
            return -2147483647;
        }
        return static_cast<int>(v);
    }
    return ZtlSecureFuse_long(pTear, checksum);
}

// PATCH CATEGORY 1 + 2: Decode2->Decode4 (13) and FakeTear (25) call sites.
// All 38 sites are 5-byte `call rel32` (0xE8). Every expectedTarget was
// computed from the live rel32 bytes and checked against Decode2
// (0x42470C), Tear_short (0x4E80EB), or Tear_long (0x4165B1).

struct CallPatch {
    uintptr_t addr;
    uintptr_t expectedTarget;
    void* newTarget;
    const char* label;
};

bool PatchCallWithVerify(const CallPatch& site) {
    const unsigned char op = *reinterpret_cast<unsigned char*>(site.addr);
    if (op != 0xE8) {
        MhmLog("SKIP %s @ 0x%08X: expected call opcode 0xE8, found 0x%02X",
               site.label, static_cast<unsigned int>(site.addr), op);
        return false;
    }
    const int rel = *reinterpret_cast<int*>(site.addr + 1);
    const uintptr_t currentTarget = site.addr + 5 + static_cast<uintptr_t>(rel);
    if (currentTarget != site.expectedTarget) {
        MhmLog("SKIP %s @ 0x%08X: expected call target 0x%08X, found 0x%08X",
               site.label, static_cast<unsigned int>(site.addr),
               static_cast<unsigned int>(site.expectedTarget),
               static_cast<unsigned int>(currentTarget));
        return false;
    }

    PatchCall(site.addr, site.newTarget);

    const unsigned char afterOp = *reinterpret_cast<unsigned char*>(site.addr);
    const int afterRel = *reinterpret_cast<int*>(site.addr + 1);
    const uintptr_t afterTarget = site.addr + 5 + static_cast<uintptr_t>(afterRel);
    if (afterOp != 0xE8 || afterTarget != reinterpret_cast<uintptr_t>(site.newTarget)) {
        MhmLog("FAIL %s @ 0x%08X: verify mismatch after patch (target=0x%08X)",
               site.label, static_cast<unsigned int>(site.addr),
               static_cast<unsigned int>(afterTarget));
        return false;
    }
    MhmLog("OK   %s @ 0x%08X: call 0x%08X -> 0x%08X",
           site.label, static_cast<unsigned int>(site.addr),
           static_cast<unsigned int>(currentTarget),
           static_cast<unsigned int>(afterTarget));
    return true;
}

// PATCH CATEGORY 4 + 5: FixMovsx (54) and the two 16-bit `cmp ax,si` fixes.
// Every expected/value byte sequence was read from the live image:
//   Register self-case   0F BF C0          -> 90 90 90          (movsx eax,ax)
//   Register non-self    0F BF <ModRM>     -> 8B <ModRM> 90     (mov reg,srcreg32)
//   Memory form          0F BF <ModRM><d8> -> 8B <ModRM><d8> 90 (mov reg,dword ptr[mem])
//   16-bit cmp fix       66 3B C6          -> 3B C6 90          (cmp eax,esi)

struct RawPatch {
    uintptr_t addr;
    unsigned char size;
    unsigned char expected[4];
    unsigned char value[4];
    const char* label;
};

const RawPatch kRawPatches[] = {
    // Inline FixMovsx after OnPartyResult / OnAttack Decode4 patches.
    { 0x00A3ECFA, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_PartyResult_MP" },
    { 0x00A3ED09, 3, { 0x0F, 0xBF, 0xC8 }, { 0x8B, 0xC8, 0x90 }, "FixMovsx_PartyResult_MaxMP" },
    { 0x0098065B, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_OnAttack_MP" },
    { 0x00980668, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_OnAttack_MaxMP" },

    // CUIStatusBar::Draw
    { 0x008D822D, 4, { 0x0F, 0xBF, 0x4D, 0xE0 }, { 0x8B, 0x4D, 0xE0, 0x90 }, "FixMovsx_UIStatusBar_MP" },
    { 0x008D8237, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UIStatusBar_HP" },
    // CUIStat::Draw
    { 0x008C5DC4, 4, { 0x0F, 0xBF, 0x4D, 0xD8 }, { 0x8B, 0x4D, 0xD8, 0x90 }, "FixMovsx_UIStat_HP" },
    { 0x008C5EB9, 4, { 0x0F, 0xBF, 0x4D, 0xD8 }, { 0x8B, 0x4D, 0xD8, 0x90 }, "FixMovsx_UIStat_MP" },
    // CUIPartyHP::Draw
    { 0x009203F7, 4, { 0x0F, 0xBF, 0x45, 0xE0 }, { 0x8B, 0x45, 0xE0, 0x90 }, "FixMovsx_UIPartyHP_HP" },
    // CUIStatDetail::Draw
    { 0x008C4139, 3, { 0x0F, 0xBF, 0xD8 }, { 0x8B, 0xD8, 0x90 }, "FixMovsx_UIStatDetail_HP_1" },
    { 0x008C4141, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UIStatDetail_HP_2" },
    { 0x008C418E, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UIStatDetail_HP_3" },
    { 0x008C419F, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UIStatDetail_MP" },
    // CStatWnd::UpdateAbilityButtons / sub_8CBDDB
    { 0x008CBF94, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_AbilityBtn_MaxHP_1" },
    { 0x008CC015, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_AbilityBtn_MaxMP_1" },
    { 0x008CC1CF, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_AbilityBtn_MaxHP_2" },
    { 0x008CC24F, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_AbilityBtn_MaxMP_2" },
    // CStatWnd::DrawStatValues / sub_8CC8DE
    { 0x008CC9DC, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawValues_MaxMP" },
    { 0x008CCA8A, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawValues_MaxHP" },
    // CStatWnd::DrawStatDetails / sub_8CD8A0
    { 0x008CD9CE, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawDetails_MaxMP_1" },
    { 0x008CDAE5, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawDetails_MaxHP_1" },
    { 0x008CDF25, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawDetails_MaxMP_2" },
    { 0x008CDFC0, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatWnd_DrawDetails_MaxHP_2" },
    // CWvsContext::TryRecovery
    { 0x00A02F88, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_TryRecovery_HP" },
    { 0x00A03180, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_TryRecovery_MaxMP" },
    // CWvsContext::CheckDarkForce
    { 0x00A2938D, 4, { 0x0F, 0xBF, 0x4D, 0xEC }, { 0x8B, 0x4D, 0xEC, 0x90 }, "FixMovsx_CheckDarkForce_HP_1" },
    { 0x00A29412, 3, { 0x0F, 0xBF, 0xCF }, { 0x8B, 0xCF, 0x90 }, "FixMovsx_CheckDarkForce_HP_2" },
    // CWvsContext::CheckDragonFury
    { 0x00A29535, 4, { 0x0F, 0xBF, 0x4D, 0xE8 }, { 0x8B, 0x4D, 0xE8, 0x90 }, "FixMovsx_CheckDragonFury_MP_1" },
    { 0x00A29586, 3, { 0x0F, 0xBF, 0xCF }, { 0x8B, 0xCF, 0x90 }, "FixMovsx_CheckDragonFury_MP_2" },
    // CWvsContext::SendAbilityMassUpRequest
    { 0x00A23C4A, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_SendAbilityMassUp_AP" },
    // CUserLocal::Update
    { 0x0094B096, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UserLocalUpdate_HP_1" },
    { 0x0094B230, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UserLocalUpdate_HP_2" },
    { 0x0094EA4C, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_UserLocalUpdate_MP_1" },
    { 0x0094BB78, 3, { 0x0F, 0xBF, 0xCE }, { 0x8B, 0xCE, 0x90 }, "FixMovsx_UserLocalUpdate_MP_2" },
    // CUserLocal::TryConsumePetHP / TryConsumePetMP
    { 0x0095BA30, 4, { 0x0F, 0xBF, 0x45, 0xF0 }, { 0x8B, 0x45, 0xF0, 0x90 }, "FixMovsx_TryConsumePetHP" },
    { 0x0095BC7B, 3, { 0x0F, 0xBF, 0xC3 }, { 0x8B, 0xC3, 0x90 }, "FixMovsx_TryConsumePetMP" },
    // CUserLocal::SetDamaged
    { 0x009584B6, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_SetDamaged_HP" },
    { 0x0095960C, 3, { 0x0F, 0xBF, 0xC9 }, { 0x8B, 0xC9, 0x90 }, "FixMovsx_SetDamaged_MaxMP" },
    // CUserLocal::DoActiveSkill (root cause of skill haywire)
    { 0x00967733, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_DoActiveSkill_HP" },
    { 0x0096774F, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_DoActiveSkill_MP" },
    // Equipment Stat Calculation / sub_77EC9F
    { 0x0077ED97, 3, { 0x0F, 0xBF, 0xC8 }, { 0x8B, 0xC8, 0x90 }, "FixMovsx_EquipCalc_MaxHP" },
    { 0x0077EDB3, 3, { 0x0F, 0xBF, 0xC8 }, { 0x8B, 0xC8, 0x90 }, "FixMovsx_EquipCalc_MaxMP" },
    // Stat Recalculation / sub_78D46C
    { 0x0078D8FA, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatRecalc_MaxHP" },
    { 0x0078D947, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_StatRecalc_MaxMP" },
    // CSkillInfo::CheckConsumeForActiveSkill
    { 0x00764401, 3, { 0x0F, 0xBF, 0xC8 }, { 0x8B, 0xC8, 0x90 }, "FixMovsx_CheckConsume_HP_1" },
    { 0x007644E4, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_CheckConsume_HP_2" },
    { 0x00764507, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_CheckConsume_MP" },
    // CField_Dojang::Update
    { 0x00554AFC, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_Dojang_HP_1" },
    { 0x00554B31, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_Dojang_HP_2" },
    // CSummoned::TryDoingHeal
    { 0x007A5B42, 3, { 0x0F, 0xBF, 0xC0 }, { 0x90, 0x90, 0x90 }, "FixMovsx_SummonedHeal_HP" },
    // Stat Formatting / sub_4E3238 (GW_CharacterStat::GetString)
    { 0x004E3371, 4, { 0x0F, 0xBF, 0x4D, 0xDC }, { 0x8B, 0x4D, 0xDC, 0x90 }, "FixMovsx_GetString_MaxMP" },
    { 0x004E3376, 4, { 0x0F, 0xBF, 0x4D, 0xD8 }, { 0x8B, 0x4D, 0xD8, 0x90 }, "FixMovsx_GetString_MaxHP" },
    { 0x004E337B, 4, { 0x0F, 0xBF, 0x4D, 0xD4 }, { 0x8B, 0x4D, 0xD4, 0x90 }, "FixMovsx_GetString_MP" },
    { 0x004E3380, 4, { 0x0F, 0xBF, 0x4D, 0xD0 }, { 0x8B, 0x4D, 0xD0, 0x90 }, "FixMovsx_GetString_HP" },

    // Category 5: 16-bit comparison fixes (sub_78D46C).
    // Instruction starts at the 0x66 operand-size prefix, not the opcode.
    { 0x0078D8E7, 3, { 0x66, 0x3B, 0xC6 }, { 0x3B, 0xC6, 0x90 }, "CmpFix_StatRecalc_MaxHP" },
    { 0x0078D934, 3, { 0x66, 0x3B, 0xC6 }, { 0x3B, 0xC6, 0x90 }, "CmpFix_StatRecalc_MaxMP" },

    // Category 5b: alive/can-act gates after HP Fuse.
    // Fuse returns full int in EAX, but `test ax,ax` + `jle` treats HP>=32768
    // as dead (SF from low 16 bits) — blocks item use / skill cast.
    // `test ax,ax` (66 85 C0) -> `test eax,eax; nop` (85 C0 90).
    { 0x00485C1C, 3, { 0x66, 0x85, 0xC0 }, { 0x85, 0xC0, 0x90 }, "FixTest_CanAct_HP" },
    { 0x00A09687, 3, { 0x66, 0x85, 0xC0 }, { 0x85, 0xC0, 0x90 }, "FixTest_CanActEx_HP" },
    { 0x0078D36C, 3, { 0x66, 0x85, 0xC0 }, { 0x85, 0xC0, 0x90 }, "FixTest_StatRecalc_HP" },
    { 0x00A02F5F, 3, { 0x66, 0x85, 0xC0 }, { 0x85, 0xC0, 0x90 }, "FixTest_TryRecovery_HP_1" },
    { 0x00A03155, 3, { 0x66, 0x85, 0xC0 }, { 0x85, 0xC0, 0x90 }, "FixTest_TryRecovery_HP_2" },

    // Remaining movsx after HP Fuse (truncates >=32768 incorrectly).
    { 0x0064286B, 3, { 0x0F, 0xBF, 0xF0 }, { 0x8B, 0xF0, 0x90 }, "FixMovsx_MobHPBar_HP" },
    { 0x0096AE9D, 3, { 0x0F, 0xBF, 0xC7 }, { 0x8B, 0xC7, 0x90 }, "FixMovsx_SkillHPPct_HP" },
};

bool PatchRawWithVerify(const RawPatch& site) {
    unsigned char before[4] = {};
    for (unsigned char i = 0; i < site.size; ++i) {
        before[i] = *reinterpret_cast<unsigned char*>(site.addr + i);
    }
    if (memcmp(before, site.expected, site.size) != 0) {
        MhmLog("SKIP %s @ 0x%08X: unexpected bytes", site.label, static_cast<unsigned int>(site.addr));
        return false;
    }

    Memory::WriteByteArray(static_cast<DWORD>(site.addr), const_cast<unsigned char*>(site.value), site.size);

    unsigned char after[4] = {};
    for (unsigned char i = 0; i < site.size; ++i) {
        after[i] = *reinterpret_cast<unsigned char*>(site.addr + i);
    }
    if (memcmp(after, site.value, site.size) != 0) {
        MhmLog("FAIL %s @ 0x%08X: verify mismatch", site.label, static_cast<unsigned int>(site.addr));
        return false;
    }
    MhmLog("OK   %s @ 0x%08X: patched %u bytes", site.label, static_cast<unsigned int>(site.addr), site.size);
    return true;
}

// PATCH CATEGORY 6: Clamping limits raised 30,000 -> 99,999.

struct IntPatch {
    uintptr_t addr;
    unsigned int expected;
    unsigned int value;
    const char* label;
};

const IntPatch kIntPatches[] = {
    { 0x0078D8D2, 30000, 99999, "Clamp_StatRecalc_LevelUp" }, // sub_78D46C
    { 0x0077F1A0, 30000, 99999, "Clamp_EquipCalc_Sum" },      // sub_77EC9F
    { 0x008CD657, 30000, 99999, "Clamp_StatWnd_AP_HP" },      // CStatWnd
    { 0x008CD6EB, 30000, 99999, "Clamp_StatWnd_AP_MP" },      // CStatWnd
};

bool PatchIntWithVerify(const IntPatch& site) {
    const unsigned int before = *reinterpret_cast<unsigned int*>(site.addr);
    if (before != site.expected) {
        MhmLog("SKIP %s @ 0x%08X: expected 0x%08X, found 0x%08X",
               site.label, static_cast<unsigned int>(site.addr), site.expected, before);
        return false;
    }
    Memory::WriteInt(static_cast<DWORD>(site.addr), site.value);
    const unsigned int after = *reinterpret_cast<unsigned int*>(site.addr);
    if (after != site.value) {
        MhmLog("FAIL %s @ 0x%08X: wrote 0x%08X, read back 0x%08X",
               site.label, static_cast<unsigned int>(site.addr), site.value, after);
        return false;
    }
    MhmLog("OK   %s @ 0x%08X: 0x%08X -> 0x%08X",
           site.label, static_cast<unsigned int>(site.addr), before, after);
    return true;
}

bool g_attached = false;

} // namespace

void AttachMaxHpMpMod() {
    if (g_attached) {
        return;
    }
    g_attached = true;

    MhmLog("AttachMaxHpMpMod begin (UseVirtuProtect=%d)", Memory::UseVirtuProtect ? 1 : 0);

    void* const decode4 = reinterpret_cast<void*>(kAddr_CInPacket_Decode4);
    void* const fakeTear = FakeTearTarget();

    const CallPatch kCallPatches[] = {
        // Category 1: Decode2 -> Decode4
        { 0x004E2B9A, kAddr_CInPacket_Decode2, decode4, "Decode_Login_HP" },
        { 0x004E2BAE, kAddr_CInPacket_Decode2, decode4, "Decode_Login_MaxHP" },
        { 0x004E2BC2, kAddr_CInPacket_Decode2, decode4, "Decode_Login_MP" },
        { 0x004E2BD6, kAddr_CInPacket_Decode2, decode4, "Decode_Login_MaxMP" },
        { 0x0077621A, kAddr_CInPacket_Decode2, decode4, "Decode_OnSetField_HP" },
        { 0x004E30DA, kAddr_CInPacket_Decode2, decode4, "Decode_StatChanged_HP" },
        { 0x004E30F4, kAddr_CInPacket_Decode2, decode4, "Decode_StatChanged_MaxHP" },
        { 0x004E310E, kAddr_CInPacket_Decode2, decode4, "Decode_StatChanged_MP" },
        { 0x004E3128, kAddr_CInPacket_Decode2, decode4, "Decode_StatChanged_MaxMP" },
        { 0x00A3ECF5, kAddr_CInPacket_Decode2, decode4, "Decode_PartyResult_MP" },
        { 0x00A3ED02, kAddr_CInPacket_Decode2, decode4, "Decode_PartyResult_MaxMP" },
        { 0x00980656, kAddr_CInPacket_Decode2, decode4, "Decode_OnAttack_MP" },
        { 0x00980663, kAddr_CInPacket_Decode2, decode4, "Decode_OnAttack_MaxMP" },

        // Category 2: FakeTear (raw 4-byte storage)
        { 0x004E2BA4, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_Login_HP" },
        { 0x004E2BB8, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_Login_MaxHP" },
        { 0x004E2BCC, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_Login_MP" },
        { 0x004E2BE0, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_Login_MaxMP" },
        { 0x00776224, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_OnSetField_HP" },
        { 0x004E30E4, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatChanged_HP" },
        { 0x004E30FE, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatChanged_MaxHP" },
        { 0x004E3118, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatChanged_MP" },
        { 0x004E3132, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatChanged_MaxMP" },
        { 0x007646F2, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_SkillConsume_HP" },
        { 0x0076470F, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_SkillConsume_MP" },
        { 0x00967B94, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_DoActiveSkill_HP" },
        { 0x00967BA2, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_DoActiveSkill_MP" },
        { 0x0078D914, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatRecalc_MaxHP" },
        { 0x0078D961, kAddr_ZtlSecureTear_short, fakeTear, "FakeTear_StatRecalc_MaxMP" },
        // Equipment Stat Calculation / sub_77EC9F Tear_long writes (CUIStatusBar
        // reads MaxHP/MaxMP via ZtlSecureFuse_long from these slots).
        { 0x0077ED9D, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxHP_Init" },
        { 0x0077EF78, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxHP_Loop1" },
        { 0x0077F0EF, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxHP_Loop2" },
        { 0x0077F164, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxHP_Pct" },
        { 0x0077F1AF, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxHP_Cap" },
        { 0x0077EDB9, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxMP_Init" },
        { 0x0077EFA7, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxMP_Loop1" },
        { 0x0077F11E, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxMP_Loop2" },
        { 0x0077F18E, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxMP_Pct" },
        { 0x0077F1CE, kAddr_ZtlSecureTear_long, fakeTear, "FakeTear_EquipCalc_MaxMP_Cap" },
    };

    int okCount = 0;
    int failCount = 0;

    for (const CallPatch& site : kCallPatches) {
        if (PatchCallWithVerify(site)) ++okCount; else ++failCount;
    }
    for (const RawPatch& site : kRawPatches) {
        if (PatchRawWithVerify(site)) ++okCount; else ++failCount;
    }
    for (const IntPatch& site : kIntPatches) {
        if (PatchIntWithVerify(site)) ++okCount; else ++failCount;
    }

    // Category 3: global Fuse hooks (Detours).
    const bool hookedShort = ATTACH_HOOK(ZtlSecureFuse_short, ZtlSecureFuse_short_hook);
    const bool hookedLong = ATTACH_HOOK(ZtlSecureFuse_long, ZtlSecureFuse_long_hook);
    MhmLog("Fuse hooks: short=%d long=%d", hookedShort ? 1 : 0, hookedLong ? 1 : 0);
    if (hookedShort) ++okCount; else ++failCount;
    if (hookedLong) ++okCount; else ++failCount;

    MhmLog("AttachMaxHpMpMod done: ok=%d fail=%d (expected total=107)", okCount, failCount);
}
