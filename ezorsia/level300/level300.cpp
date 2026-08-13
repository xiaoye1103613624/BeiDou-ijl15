// level300.cpp - Level field expansion byte(255) -> short(300).
// BeiDou.exe @ 0x400000 base. Every address was cross-checked against the
// live IDA database (BeiDou.exe) before being written here.
//
// GW_CharacterStat::Decode = sub_4E2A84:
//   Level Decode1 @ 0x4E2B20 -> Decode2; Tear_byte @ 0x4E2B2A -> LevelFakeTear
//   (mov cl,al @ 0x4E2B28 -> mov ecx,eax so the short reaches Tear)
// STAT_CHANGED OnStatChanged = sub_4E2FBA mask 0x10:
//   Decode1 @ 0x4E303E; Tear_byte @ 0x4E3048; same mov fix @ 0x4E3046
// Encode (GW_CharacterStat::Encode): Encode1 -> Encode2 at char-stat write sites.
//
// Architecture (same FakeTear + Fuse sentinel as maxhpmp, but ushort-width):
//   Tear_byte stores 2 encrypted bytes; Tear_short needs 4 and would collide
//   with the Level CS dword at +0x35 / Job at +0x39. LevelFakeTear stores a
//   raw ushort at [ptr] and returns checksum=kLevelFuseSentinel ('LVL3').
//   Fuse_byte is hooked: CS==sentinel returns *(unsigned short*)pTear;
//   otherwise original Fuse. Sentinel must NOT be 0 (skill UI CS==0 collisions).
// EXP Decode4 is intentionally left alone this round.

#include "stdafx.h"
#include "Level300Api.h"
#include "Memory.h"
#include "compat/hook.h"

#include <cstdio>
#include <cstring>

namespace {

void LvlLogWrite(const char* path, const char* line) {
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SetFilePointer(file, 0, nullptr, FILE_END);
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    // No FlushFileBuffers: skill UI Fuse is hot; sync flush stalls the UI thread.
    CloseHandle(file);
}

void LvlLog(const char* format, ...) {
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

    // Write beside EXE and beside ijl15.dll — cwd/shortcut confusion is common.
    char pathExe[MAX_PATH];
    GetModuleFileNameA(nullptr, pathExe, MAX_PATH);
    if (char* slash = strrchr(pathExe, '\\')) {
        *(slash + 1) = '\0';
    }
    strcat_s(pathExe, MAX_PATH, "level300_debug.txt");
    LvlLogWrite(pathExe, line);

    char pathDll[MAX_PATH];
    HMODULE self = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&LvlLog), &self) && self) {
        GetModuleFileNameA(self, pathDll, MAX_PATH);
        if (char* slash = strrchr(pathDll, '\\')) {
            *(slash + 1) = '\0';
        }
        strcat_s(pathDll, MAX_PATH, "level300_debug.txt");
        if (_stricmp(pathExe, pathDll) != 0) {
            LvlLogWrite(pathDll, line);
        }
    }

    OutputDebugStringA(line);
}

constexpr uintptr_t kAddr_CInPacket_Decode1   = 0x004065F3;
constexpr uintptr_t kAddr_CInPacket_Decode2   = 0x0042470C;
constexpr uintptr_t kAddr_CInPacket_Decode4   = 0x00406629;
constexpr uintptr_t kAddr_COutPacket_Encode1  = 0x00406549;
constexpr uintptr_t kAddr_COutPacket_Encode2  = 0x00427F74;
constexpr uintptr_t kAddr_ZtlSecureTear_byte  = 0x004E807A;
constexpr uintptr_t kAddr_ZtlSecureFuse_byte  = 0x0047465D;

using Decode4Fn = unsigned int(__thiscall*)(void* packet);

// Store raw int64 EXP at tear slot; CS sentinel=0 (Fuse_long hooked in maxhpmp).
static void __stdcall ExpDecodeStore(void* packet, void* tearPtr, unsigned int* csOut) {
    auto Decode4 = reinterpret_cast<Decode4Fn>(kAddr_CInPacket_Decode4);
    const unsigned int low = Decode4(packet);
    const unsigned int high = Decode4(packet);
    *reinterpret_cast<unsigned long long*>(tearPtr) =
            (static_cast<unsigned long long>(low)) |
            (static_cast<unsigned long long>(high) << 32);
    *csOut = 0;
}

__declspec(naked) static void Cave_ExpDecode_Login() {
    __asm {
        // ecx=CInPacket*, esi=GW_CharacterStat*
        lea eax, [esi + 0x99]
        push eax
        lea eax, [esi + 0x91]
        push eax
        push ecx
        call ExpDecodeStore
        mov ecx, edi
        mov eax, 0x004E2C88
        jmp eax
    }
}

__declspec(naked) static void Cave_ExpDecode_StatChanged() {
    __asm {
        lea eax, [esi + 0x99]
        push eax
        lea eax, [esi + 0x91]
        push eax
        push ecx
        call ExpDecodeStore
        mov ecx, edi
        mov eax, 0x004E31CE
        jmp eax
    }
}

// Store raw ushort (fits the 2-byte Level tear slot at +0x33).
// Sentinel MUST NOT be 0: skill UI Fuse_byte sites can legitimately see CS==0;
// treating those as Level FakeTear returned garbage (often ~200) and hung/crashed K.
constexpr unsigned int kLevelFuseSentinel = 0x4C564C33u; // 'LVL3'

static int g_lvlFuseLogLeft = 48;

__declspec(naked) static void LevelFakeTear_naked() {
    __asm {
        // ecx = level (ushort-wide), edx = tear ptr
        mov word ptr [edx], cx
        mov eax, kLevelFuseSentinel
        ret
    }
}

void* LevelFakeTearTarget() {
    // Prefer naked: exact maple Tear_byte ABI (__fastcall ECX=value, EDX=ptr).
    return CastHook(&LevelFakeTear_naked);
}

typedef int(__cdecl* t_ZtlSecureFuse_byte)(void* pTear, unsigned int checksum);

auto ZtlSecureFuse_byte = reinterpret_cast<t_ZtlSecureFuse_byte>(kAddr_ZtlSecureFuse_byte);

// Return full ushort in EAX when Level FakeTear sentinel; else original byte Fuse.
int __cdecl ZtlSecureFuse_byte_hook(void* pTear, unsigned int checksum) {
    // Guard against Detours trampoline mis-wire → infinite recurse → 0xC00000FD.
    static thread_local bool s_inFuse = false;
    if (s_inFuse) {
        return 0;
    }
    if (checksum == kLevelFuseSentinel) {
        const unsigned short v = *reinterpret_cast<unsigned short*>(pTear);
        if (g_lvlFuseLogLeft > 0) {
            --g_lvlFuseLogLeft;
            LvlLog("Fuse_byte LVL3 level=%u", static_cast<unsigned>(v));
        }
        return v;
    }
    s_inFuse = true;
    const int r = ZtlSecureFuse_byte(pTear, checksum) & 0xFF;
    s_inFuse = false;
    return r;
}

// BookFilter null-guard REMOVED (2026-07-24):
// Soft-skip when EDI==0 converted the K hang into EIP=0 flash-crash
// (Event Viewer 0xc0000005 offset 0, ~2s after "BookFilter SKIP").
// Call sites push book/char/out then call 0x75C7C5; skipping leaves the
// out-slot unfilled so later UI code calls through null. Root fix is
// ensuring the skill-book array slot is non-null (Skill.wz / UIWindow Skill /
// character skill roots), not papering over EDI==0.

struct CallPatch {
    uintptr_t addr;
    uintptr_t expectedTarget;
    void* newTarget;
    const char* label;
};

bool PatchCallWithVerify(const CallPatch& site) {
    const unsigned char op = *reinterpret_cast<unsigned char*>(site.addr);
    if (op != 0xE8) {
        LvlLog("SKIP %s @ 0x%08X: expected call opcode 0xE8, found 0x%02X",
               site.label, static_cast<unsigned int>(site.addr), op);
        return false;
    }
    const int rel = *reinterpret_cast<int*>(site.addr + 1);
    const uintptr_t currentTarget = site.addr + 5 + static_cast<uintptr_t>(rel);
    if (currentTarget != site.expectedTarget) {
        LvlLog("SKIP %s @ 0x%08X: expected call target 0x%08X, found 0x%08X",
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
        LvlLog("FAIL %s @ 0x%08X: verify mismatch after patch (target=0x%08X)",
               site.label, static_cast<unsigned int>(site.addr),
               static_cast<unsigned int>(afterTarget));
        return false;
    }
    LvlLog("OK   %s @ 0x%08X: call 0x%08X -> 0x%08X",
           site.label, static_cast<unsigned int>(site.addr),
           static_cast<unsigned int>(currentTarget),
           static_cast<unsigned int>(afterTarget));
    return true;
}

struct RawPatch {
    uintptr_t addr;
    unsigned char size;
    unsigned char expected[4];
    unsigned char value[4];
    const char* label;
};

bool PatchRawWithVerify(const RawPatch& site) {
    unsigned char before[4] = {};
    for (unsigned char i = 0; i < site.size; ++i) {
        before[i] = *reinterpret_cast<unsigned char*>(site.addr + i);
    }
    if (memcmp(before, site.value, site.size) == 0) {
        LvlLog("OK   %s @ 0x%08X: already patched", site.label, static_cast<unsigned int>(site.addr));
        return true;
    }
    if (memcmp(before, site.expected, site.size) != 0) {
        LvlLog("SKIP %s @ 0x%08X: unexpected bytes", site.label, static_cast<unsigned int>(site.addr));
        return false;
    }

    Memory::WriteByteArray(static_cast<DWORD>(site.addr), const_cast<unsigned char*>(site.value), site.size);

    unsigned char after[4] = {};
    for (unsigned char i = 0; i < site.size; ++i) {
        after[i] = *reinterpret_cast<unsigned char*>(site.addr + i);
    }
    if (memcmp(after, site.value, site.size) != 0) {
        LvlLog("FAIL %s @ 0x%08X: verify mismatch", site.label, static_cast<unsigned int>(site.addr));
        return false;
    }
    LvlLog("OK   %s @ 0x%08X: patched %u bytes", site.label, static_cast<unsigned int>(site.addr), site.size);
    return true;
}

bool g_attached = false;

} // namespace

void AttachLevel300Mod() {
    if (g_attached) {
        return;
    }
    g_attached = true;

    LvlLog("AttachLevel300Mod begin sentinel=LVL3 (0x%08X) UseVirtuProtect=%d",
           kLevelFuseSentinel, Memory::UseVirtuProtect ? 1 : 0);

    int okCount = 0;
    int failCount = 0;

    // EXP Decode8 caves (must match server writeLong). Login=26B, StatChanged=24B.
    Memory::CodeCave(reinterpret_cast<void*>(&Cave_ExpDecode_Login), 0x004E2C6E, 26);
    Memory::CodeCave(reinterpret_cast<void*>(&Cave_ExpDecode_StatChanged), 0x004E31B6, 24);
    LvlLog("OK   ExpDecode8 caves installed");
    okCount += 2;

    void* const decode2 = reinterpret_cast<void*>(kAddr_CInPacket_Decode2);
    void* const encode2 = reinterpret_cast<void*>(kAddr_COutPacket_Encode2);
    void* const fakeTear = LevelFakeTearTarget();

    const CallPatch kCallPatches[] = {
        { 0x004E2B20, kAddr_CInPacket_Decode1, decode2, "Decode_Login_Level" },
        { 0x004E303E, kAddr_CInPacket_Decode1, decode2, "Decode_StatChanged_Level" },
        { 0x004E2B2A, kAddr_ZtlSecureTear_byte, fakeTear, "FakeTear_Login_Level" },
        { 0x004E3048, kAddr_ZtlSecureTear_byte, fakeTear, "FakeTear_StatChanged_Level" },
        { 0x004E289B, kAddr_COutPacket_Encode1, encode2, "Encode_CharStat_Level" },
        { 0x004E2DB2, kAddr_COutPacket_Encode1, encode2, "Encode_StatMask_Level" },
    };

    // SAFE FixMovzx set only: character Level tear (+0x33) consumers.
    // EXCLUDED (history / hang lessons):
    //   - BookFilter soft-skip, SkillTip [ebp-1]->[ebp-4], SanitizeSkillReqList
    //   - 8AD* / 8CD* SetupJob / UISkill index widen (byte Fuse -> tab/book index)
    //   - Fuse_byte checksum==0 sentinel (use LVL3 only)
    const RawPatch kRawPatches[] = {
        { 0x004E2B28, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "MovEcx_Login_Level" },
        { 0x004E3046, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "MovEcx_StatChanged_Level" },
        { 0x004E2893, 3, { 0x88, 0x45, 0x08 }, { 0x89, 0x45, 0x08 }, "StoreDword_Encode_CharStat_Level" },
        { 0x004E2DAA, 3, { 0x88, 0x45, 0x0C }, { 0x89, 0x45, 0x0C }, "StoreDword_Encode_StatMask_Level" },

        { 0x00602DDD, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_itow_Level_1" },
        { 0x006077F3, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_itow_Level_2" },
        { 0x007212DC, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_1" },
        { 0x00721312, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_2" },
        { 0x0072134F, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_3" },
        { 0x0072260E, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_4" },
        { 0x00724468, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_5" },
        { 0x0072447D, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_6" },
        { 0x00752C75, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_Shop_Level" },
        { 0x00764DB0, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_CharLevel_SkillConsume" },
        { 0x0077E251, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_EquipCalc_Level" },
        { 0x0077E25D, 2, { 0x8A, 0xCB }, { 0x8B, 0xCB }, "FixMovEcx_EquipCalc_LevelAdd" },
        { 0x0077ECCB, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "FixMovEcx_EquipCalc_Level" },
        { 0x004F2D7A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_IsAbleToWear_Level" },
        { 0x0078DAEB, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_ExpTableLookup_Level" },
        { 0x008C531A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_UIStat_Level" },
        { 0x008C58CF, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_UIStat_Level" },
        { 0x008CBE33, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatWnd_Level" },
        { 0x008D774C, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_Level_1" },
        { 0x008D81BC, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_Level_2" },
        { 0x008D8176, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_DrawLevel" },
        { 0x00A1FBB4, 3, { 0x0F, 0xB6, 0xF8 }, { 0x0F, 0xB7, 0xF8 }, "FixMovzx_Context_Level" },
    };

    for (const CallPatch& site : kCallPatches) {
        if (PatchCallWithVerify(site)) ++okCount; else ++failCount;
    }
    for (const RawPatch& site : kRawPatches) {
        if (PatchRawWithVerify(site)) ++okCount; else ++failCount;
    }

    const bool hookedFuse = ATTACH_HOOK(ZtlSecureFuse_byte, ZtlSecureFuse_byte_hook);
    LvlLog("Fuse_byte hook LVL3: %d", hookedFuse ? 1 : 0);
    if (hookedFuse) ++okCount; else ++failCount;

    struct Imm32CmpPatch {
        uintptr_t addr;
        unsigned int expectedImm;
        unsigned int newImm;
        const char* label;
    };
    const Imm32CmpPatch kMaxLevelCmps[] = {
        { 0x0078D16A, 200, 300, "GetNextLevelExp_max" },
        { 0x0078D237, 200, 300, "ExpHelper_max_A" },
        { 0x0078D277, 200, 300, "ExpHelper_max_B" },
        { 0x0078D301, 200, 300, "ExpHelper_max_C" },
        { 0x0078DAEE, 200, 300, "ExpPercent_max" },
    };
    for (const Imm32CmpPatch& site : kMaxLevelCmps) {
        const unsigned char op = *reinterpret_cast<unsigned char*>(site.addr);
        if (op != 0x3D) {
            LvlLog("SKIP %s @ 0x%08X: expected opcode 0x3D, found 0x%02X",
                   site.label, static_cast<unsigned int>(site.addr), op);
            ++failCount;
            continue;
        }
        const unsigned int before = *reinterpret_cast<unsigned int*>(site.addr + 1);
        if (before != site.expectedImm) {
            LvlLog("SKIP %s @ 0x%08X: expected imm %u, found %u",
                   site.label, static_cast<unsigned int>(site.addr),
                   site.expectedImm, before);
            ++failCount;
            continue;
        }
        Memory::WriteInt(static_cast<DWORD>(site.addr + 1), site.newImm);
        const unsigned int after = *reinterpret_cast<unsigned int*>(site.addr + 1);
        if (after != site.newImm) {
            LvlLog("FAIL %s @ 0x%08X: writeback mismatch", site.label,
                   static_cast<unsigned int>(site.addr));
            ++failCount;
        } else {
            LvlLog("OK   %s @ 0x%08X: imm %u -> %u", site.label,
                   static_cast<unsigned int>(site.addr), site.expectedImm, site.newImm);
            ++okCount;
        }
    }

    // Expected: 2 caves + 6 call + 26 raw + 1 fuse + 5 max-level = 40
    LvlLog("AttachLevel300Mod done: ok=%d fail=%d (safe-set expected~40, sentinel=LVL3)",
           okCount, failCount);
}
