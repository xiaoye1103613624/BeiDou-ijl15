// MapEnterNullGuard — skip commodity check when CharacterData is null during map enter.
// Crash: sub_776020 → sub_A292BB @A292F9 push [edi+3Dh] with edi==0 (getobj_inflight map WZ).
// Uses a DLL-allocated trampoline (not EXE @9F6921) to avoid login E_POINTER regressions.

#include "stdafx.h"
#include "MapEnterNullGuardApi.h"
#include "../Memory.h"
#include "../bootlog/BootLog.h"

#include <cstdint>

namespace {

constexpr uintptr_t kSite = 0x00A292F9;
constexpr uintptr_t kContinue = 0x00A29300;
constexpr uintptr_t kExit = 0x00A29449;

static unsigned char* g_tramp = nullptr;
static bool g_installed = false;

static bool ExpectBytesVa(uintptr_t va, const unsigned char* expect, size_t n) {
    __try {
        const auto* p = reinterpret_cast<const unsigned char*>(va);
        for (size_t i = 0; i < n; ++i) {
            if (p[i] != expect[i]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void WriteJmpRel32(unsigned char* at, uintptr_t target) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(at);
    const auto rel = static_cast<int32_t>(target - (from + 5));
    at[0] = 0xE9;
    *reinterpret_cast<int32_t*>(at + 1) = rel;
}

static bool BuildTrampoline(unsigned char* tramp) {
    if (!tramp) {
        return false;
    }

    tramp[0] = 0x85;
    tramp[1] = 0xFF; // test edi,edi
    tramp[2] = 0x75;
    tramp[3] = 0x0B; // jnz +0x0B → push path at offset 0x0F
    tramp[4] = 0x59; // pop ecx
    WriteJmpRel32(tramp + 5, kExit);
    for (int i = 10; i < 15; ++i) {
        tramp[i] = 0x90;
    }
    tramp[15] = 0xFF;
    tramp[16] = 0x77;
    tramp[17] = 0x3D; // push [edi+3Dh]
    tramp[18] = 0x8D;
    tramp[19] = 0x47;
    tramp[20] = 0x39; // lea eax,[edi+39h]
    tramp[21] = 0x50; // push eax
    WriteJmpRel32(tramp + 22, kContinue);
    return true;
}

} // namespace

void AttachMapEnterNullGuard() {
    if (g_installed) {
        return;
    }

    static const unsigned char kExpectSite[12] = {
        0xFF, 0x77, 0x3D, 0x8D, 0x47, 0x39, 0x50, 0xE8, 0xD8, 0xB3, 0xA4, 0xFF};
    static const unsigned char kPatchedHead[5] = {0xE9, 0x23, 0xD6, 0xFC, 0xFF};

    if (ExpectBytesVa(kSite, kPatchedHead, sizeof(kPatchedHead))) {
        BootLog("MapEnterNullGuard already present @A292F9");
        g_installed = true;
        return;
    }
    if (!ExpectBytesVa(kSite, kExpectSite, sizeof(kExpectSite))) {
        BootLog("MapEnterNullGuard SKIP: ExpectBytes fail @A292F9");
        return;
    }

    if (!g_tramp) {
        g_tramp = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    }
    if (!g_tramp || !BuildTrampoline(g_tramp)) {
        BootLog("MapEnterNullGuard SKIP: trampoline alloc/build fail");
        return;
    }

    unsigned char site[12] = {};
    WriteJmpRel32(site, reinterpret_cast<uintptr_t>(g_tramp));
    for (int i = 5; i < 12; ++i) {
        site[i] = 0x90;
    }

    Memory::WriteByteArray(static_cast<DWORD>(kSite), site, sizeof(site));

    if (ExpectBytesVa(kSite, site, 5) && ExpectBytesVa(reinterpret_cast<uintptr_t>(g_tramp), g_tramp, 5)) {
        BootLog("MapEnterNullGuard OK @A292F9 → DLL tramp %p", g_tramp);
        g_installed = true;
    } else {
        BootLog("MapEnterNullGuard VERIFY FAIL after write");
    }
}
