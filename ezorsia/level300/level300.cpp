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
//   raw ushort at [ptr] and returns checksum=0. Fuse_byte is hooked: CS==0
//   returns *(unsigned short*)pTear; otherwise original Fuse.
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
    FlushFileBuffers(file);
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

// Store raw ushort (fits the 2-byte Level tear slot at +0x33). CS sentinel=0.
 // Use naked stub so call-sites (__fastcall ECX=value, EDX=ptr) never hit a C++ prologue.
static int g_lvlFuseLogLeft = 48;

__declspec(naked) static void LevelFakeTear_naked() {
    __asm {
        // ecx = level (ushort-wide), edx = tear ptr
        mov word ptr [edx], cx
        xor eax, eax          // checksum sentinel = 0
        ret
    }
}

void* LevelFakeTearTarget() {
    // Prefer naked: exact maple Tear_byte ABI (__fastcall ECX=value, EDX=ptr).
    return CastHook(&LevelFakeTear_naked);
}

typedef int(__cdecl* t_ZtlSecureFuse_byte)(void* pTear, unsigned int checksum);

auto ZtlSecureFuse_byte = reinterpret_cast<t_ZtlSecureFuse_byte>(kAddr_ZtlSecureFuse_byte);

// Return full ushort in EAX when sentinel; callers that used only AL are FixMovsx'd.
int __cdecl ZtlSecureFuse_byte_hook(void* pTear, unsigned int checksum) {
    if (checksum == 0) {
        const unsigned short v = *reinterpret_cast<unsigned short*>(pTear);
        if (g_lvlFuseLogLeft > 0) {
            --g_lvlFuseLogLeft;
            LvlLog("Fuse_byte CS0 level=%u", static_cast<unsigned>(v));
        }
        return v;
    }
    return ZtlSecureFuse_byte(pTear, checksum) & 0xFF;
}

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

    LvlLog("AttachLevel300Mod begin (UseVirtuProtect=%d)", Memory::UseVirtuProtect ? 1 : 0);

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
        // Decode1 -> Decode2 (GW_CharacterStat::Decode + OnStatChanged LEVEL)
        { 0x004E2B20, kAddr_CInPacket_Decode1, decode2, "Decode_Login_Level" },
        { 0x004E303E, kAddr_CInPacket_Decode1, decode2, "Decode_StatChanged_Level" },

        // Tear_byte -> LevelFakeTear (raw ushort + CS=0)
        { 0x004E2B2A, kAddr_ZtlSecureTear_byte, fakeTear, "FakeTear_Login_Level" },
        { 0x004E3048, kAddr_ZtlSecureTear_byte, fakeTear, "FakeTear_StatChanged_Level" },

        // Encode1 -> Encode2 (character stat serialize)
        { 0x004E289B, kAddr_COutPacket_Encode1, encode2, "Encode_CharStat_Level" },
        { 0x004E2DB2, kAddr_COutPacket_Encode1, encode2, "Encode_StatMask_Level" },
    };

    // mov cl,al (8A C8) -> mov ecx,eax (8B C8): pass full Decode2 result into Tear
    // mov [ebp+x],al (88 45 xx) -> mov [ebp+x],eax (89 45 xx): keep ushort for Encode2 push
    // movzx r32, al (0F B6 xx) -> movzx r32, ax (0F B7 xx)
    // mov bl,al (8A D8) -> mov ebx,eax (8B D8); mov cl,al -> mov ecx,eax
    const RawPatch kRawPatches[] = {
        { 0x004E2B28, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "MovEcx_Login_Level" },
        { 0x004E3046, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "MovEcx_StatChanged_Level" },

        { 0x004E2893, 3, { 0x88, 0x45, 0x08 }, { 0x89, 0x45, 0x08 }, "StoreDword_Encode_CharStat_Level" },
        { 0x004E2DAA, 3, { 0x88, 0x45, 0x0C }, { 0x89, 0x45, 0x0C }, "StoreDword_Encode_StatMask_Level" },

        // FixMovsx: widen Level Fuse consumers that truncated AL
        { 0x00602DDD, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_itow_Level_1" },
        { 0x006077F3, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_itow_Level_2" },
        { 0x007212DC, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_1" },
        { 0x00721312, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_2" },
        { 0x0072134F, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_3" },
        { 0x0072260E, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_4" },
        { 0x00724468, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_5" },
        { 0x0072447D, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelReq_6" },
        { 0x00752C75, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_Shop_Level" },
        { 0x00764DB0, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_Skill_Level" },
        // EquipCalc (sub_77E227): Fuse level → EBX, then was re-truncated via mov cl,bl
        // before IsAbleToWear. Level 300 became 44 → reqLevel 200/250 equips paint red.
        { 0x0077E251, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_EquipCalc_Level" },
        { 0x0077E25D, 2, { 0x8A, 0xCB }, { 0x8B, 0xCB }, "FixMovEcx_EquipCalc_LevelAdd" },
        { 0x0077ECCB, 2, { 0x8A, 0xC8 }, { 0x8B, 0xC8 }, "FixMovEcx_EquipCalc_Level" },
        // IsAbleToWear cash-path level override: movzx eax,al → ax @ 0x4F2D7A
        { 0x004F2D7A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_IsAbleToWear_Level" },
        { 0x0078DAEB, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_ExpTableLookup_Level" },
        { 0x008C531A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_UIStat_Level" },
        { 0x008C58CF, 2, { 0x8A, 0xD8 }, { 0x8B, 0xD8 }, "FixMovEbx_UIStat_Level" },
        { 0x008CBE33, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatWnd_Level" },
        { 0x008D774C, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_Level_1" },
        { 0x008D81BC, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_Level_2" },
        // ★ 状态栏 LevelNo 数字：Fuse 后 movzx al → 把 300 截成 44 再 DrawNumberByImage
        { 0x008D8176, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_StatusBar_DrawLevel" },
        { 0x00A1FBB4, 3, { 0x0F, 0xB6, 0xF8 }, { 0x0F, 0xB7, 0xF8 }, "FixMovzx_Context_Level" },

        // Extra Fuse→AL truncations found via IDA (digit/UI/copy paths)
        { 0x00A1FC08, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_Context_Level2" },
        { 0x0076601C, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_76601C" },
        { 0x00822B87, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_822B87" },
        { 0x00877DFE, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_877DFE" },
        { 0x0087924B, 3, { 0x0F, 0xB6, 0xF0 }, { 0x0F, 0xB7, 0xF0 }, "FixMovzx_LevelExtra_87924B" },
        { 0x00882EFA, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_882EFA" },
        { 0x008AD0AB, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8AD0AB" },
        { 0x008AD155, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8AD155" },
        { 0x008AD200, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8AD200" },
        { 0x008CD1A6, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD1A6" },
        { 0x008CD1C6, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD1C6" },
        { 0x008CD1E6, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD1E6" },
        { 0x008CD24A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD24A" },
        { 0x008CD2C4, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD2C4" },
        { 0x008CD2E1, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD2E1" },
        { 0x008CD332, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD332" },
        { 0x008CD35C, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD35C" },
        { 0x008CD405, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD405" },
        { 0x008CD422, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD422" },
        { 0x008CD43F, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD43F" },
        { 0x008CD49C, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD49C" },
        { 0x008CD4BA, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD4BA" },
        { 0x008CD531, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD531" },
        { 0x008CD54A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD54A" },
        { 0x008CD599, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD599" },
        { 0x008CD5C2, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8CD5C2" },
        { 0x008E93CE, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_8E93CE" },
        { 0x009514FD, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_9514FD" },
        { 0x00958938, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_958938" },
        { 0x00A0EC35, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_A0EC35" },
        { 0x00A12413, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_A12413" },
        { 0x00A2041A, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_A2041A" },
        { 0x00A3E7D5, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_A3E7D5" },
        { 0x004E3367, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_4E3367" },
        { 0x004BBB33, 3, { 0x0F, 0xB6, 0xC0 }, { 0x0F, 0xB7, 0xC0 }, "FixMovzx_LevelExtra_4BBB33" },
    };

    for (const CallPatch& site : kCallPatches) {
        if (PatchCallWithVerify(site)) ++okCount; else ++failCount;
    }
    for (const RawPatch& site : kRawPatches) {
        if (PatchRawWithVerify(site)) ++okCount; else ++failCount;
    }

    const bool hookedFuse = ATTACH_HOOK(ZtlSecureFuse_byte, ZtlSecureFuse_byte_hook);
    LvlLog("Fuse_byte hook: %d", hookedFuse ? 1 : 0);
    if (hookedFuse) ++okCount; else ++failCount;

    // GetNextLevelExp / lookup helpers: cmp eax, 200 -> cmp eax, 300 (same 5-byte imm32)
    // Must stay in lockstep with NextLevel table length (301 slots incl. terminator).
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

    // Expected: 2 exp caves + 6 call + (23+N) raw + 1 fuse + 5 max-level
    LvlLog("AttachLevel300Mod done: ok=%d fail=%d", okCount, failCount);
}
