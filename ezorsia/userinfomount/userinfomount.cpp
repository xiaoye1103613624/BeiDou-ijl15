// userinfomount.cpp — CUIUserInfo mount page: shoulder out of mount list + preview gate
//
// BeiDou.exe / GMS v83 @ image base 0x400000 — IDA MCP verified 2026-09-17.
// Pure Memory patches; no Detours hooks.
//
// SetAvatarInfo (0x00903E24):
//   cmp bodypart, 14h @ 0x903E90 → writes BP20 to +0x6D4 (mount page 3rd slot).
//   Patch imm 14h → 7Fh so BP20 falls through to ITEM LIST (+0x704).
//
// Preview gate @ 0x904044..0x904081:
//   Requires mount + saddle + IsItemSuitedForTamingMob(saddle, mount).
//   Cash/backport mounts (e.g. 1902242, bit 42) often fail → blank preview.
//   Redirect fail paths to create-preview @ 0x904087; keep mount==0 skip.

#include "stdafx.h"
#include "UserInfoMountApi.h"
#include "Memory.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr const char kStamp[] = "USERINFO_MOUNT_BP20_PREVIEW_20260917";

// CUIUserInfo::SetAvatarInfo — bodypart 20 imm in `cmp [ebp-10h], 14h`
constexpr DWORD kBp20CmpImm = 0x00903E93;

// jz targets when saddle missing / GetEquipItem null / suitability fail
constexpr DWORD kJzNoSaddle = 0x00904058;       // was → 0x9041A5; become → 0x904087
constexpr DWORD kJzNoSaddleItem = 0x0090406C;   // was → 0x9041A2; become → 0x904087
constexpr DWORD kJzUnsuited = 0x00904081;       // NOP so fail falls into 0x904087

void Log(const char* format, ...) {
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
    strcat_s(path, MAX_PATH, "userinfo_mount_debug.txt");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
        CloseHandle(file);
    }

    std::cout << "[UserInfoMount] " << msg << std::endl;
}

bool ExpectBytes(DWORD addr, const unsigned char* expected, int size, const char* label) {
    for (int i = 0; i < size; ++i) {
        const unsigned char found = *reinterpret_cast<unsigned char*>(addr + i);
        if (found != expected[i]) {
            Log("SKIP %s @ 0x%08X+%d: expected 0x%02X, found 0x%02X",
                label, addr, i, expected[i], found);
            return false;
        }
    }
    return true;
}

bool PatchBytes(DWORD addr, const unsigned char* expected, const unsigned char* value, int size,
                const char* label) {
    if (!ExpectBytes(addr, expected, size, label)) {
        return false;
    }
    Memory::WriteByteArray(addr, const_cast<unsigned char*>(value), size);
    for (int i = 0; i < size; ++i) {
        const unsigned char after = *reinterpret_cast<unsigned char*>(addr + i);
        if (after != value[i]) {
            Log("FAIL %s @ 0x%08X+%d: wrote 0x%02X, read 0x%02X",
                label, addr, i, value[i], after);
            return false;
        }
    }
    Log("OK   %s @ 0x%08X (%d bytes)", label, addr, size);
    return true;
}
} // namespace

void AttachUserInfoMountFix() {
    Log("ApplyPatches begin stamp=%s UseVirtuProtect=%d", kStamp,
        Memory::UseVirtuProtect ? 1 : 0);

    int ok = 0;
    int fail = 0;

    // 1) BP20 imm: 14h → 7Fh (loop only to 51; never matches)
    {
        const unsigned char expect[] = {0x14};
        const unsigned char value[] = {0x7F};
        if (PatchBytes(kBp20CmpImm, expect, value, 1, "bp20_cmp_imm_14_to_7f")) {
            ++ok;
        } else {
            ++fail;
        }
    }

    // 2) No saddle → create preview (rel32 = 0x29)
    //    0x904058: 0F 84 47 01 00 00  →  0F 84 29 00 00 00
    {
        const unsigned char expect[] = {0x0F, 0x84, 0x47, 0x01, 0x00, 0x00};
        const unsigned char value[] = {0x0F, 0x84, 0x29, 0x00, 0x00, 0x00};
        if (PatchBytes(kJzNoSaddle, expect, value, 6, "jz_no_saddle_to_preview")) {
            ++ok;
        } else {
            ++fail;
        }
    }

    // 3) GetEquipItem(saddle)==null → create preview (rel32 = 0x15)
    {
        const unsigned char expect[] = {0x0F, 0x84, 0x30, 0x01, 0x00, 0x00};
        const unsigned char value[] = {0x0F, 0x84, 0x15, 0x00, 0x00, 0x00};
        if (PatchBytes(kJzNoSaddleItem, expect, value, 6, "jz_no_saddle_item_to_preview")) {
            ++ok;
        } else {
            ++fail;
        }
    }

    // 4) Unsuited saddle → NOP jz (fall into preview)
    {
        const unsigned char expect[] = {0x0F, 0x84, 0x1B, 0x01, 0x00, 0x00};
        const unsigned char value[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        if (PatchBytes(kJzUnsuited, expect, value, 6, "nop_jz_unsuited_saddle")) {
            ++ok;
        } else {
            ++fail;
        }
    }

    Log("ApplyPatches done: ok=%d fail=%d stamp=%s", ok, fail, kStamp);
    (void)kStamp;
}

namespace UserInfoMount {
void ApplyPatches() {
    static bool applied = false;
    if (applied) {
        return;
    }
    applied = true;
    AttachUserInfoMountFix();
}

const char* GetStamp() {
    return kStamp;
}
} // namespace
