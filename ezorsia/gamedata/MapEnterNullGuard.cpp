// MapEnterNullGuard — skip commodity check when CharacterData is null during map enter.
// Crash: sub_776020 → sub_A292BB @A292F9 push [edi+3Dh] with edi==0 (getobj_inflight map WZ).
// Uses a DLL-allocated trampoline (not EXE @9F6921) to avoid login E_POINTER regressions.
//
// Site patch overwrites push/lea/push/call (12 bytes @A292F9..A29304). The trampoline
// must re-issue that call itself, then resume at A29305 (cmp ax, 84h). Jumping back to
// A29300 is wrong — those bytes are NOPs after the patch (caused 0xC000001D on enter).

#include "stdafx.h"
#include "MapEnterNullGuardApi.h"
#include "../Memory.h"
#include "../bootlog/BootLog.h"

#include <cstdint>

namespace {

constexpr uintptr_t kSite = 0x00A292F9;
constexpr uintptr_t kCallTarget = 0x004746DD; // sub_4746DD (ZXString/commodity helper)
constexpr uintptr_t kContinue = 0x00A29305;   // cmp ax, 84h (AFTER overwritten call)
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

static void WriteCallRel32(unsigned char* at, uintptr_t target) {
    const uintptr_t from = reinterpret_cast<uintptr_t>(at);
    const auto rel = static_cast<int32_t>(target - (from + 5));
    at[0] = 0xE8;
    *reinterpret_cast<int32_t*>(at + 1) = rel;
}

static bool BuildTrampoline(unsigned char* tramp) {
    if (!tramp) {
        return false;
    }

    // Site reached by JMP (not CALL).
    //   +0  test edi,edi
    //   +2  jnz  +5          ; edi!=0 → body at +9
    //   +4  jmp kExit        ; edi==0 → clean epilogue (no pop!)
    //   +9  push [edi+3Dh]
    //  +12  lea eax,[edi+39h]
    //  +15  push eax
    //  +16  call sub_4746DD  ; must live here — site call bytes are patched away
    //  +21  jmp kContinue    ; A29305 cmp ax / pop / pop / ...
    tramp[0] = 0x85;
    tramp[1] = 0xFF; // test edi,edi
    tramp[2] = 0x75;
    tramp[3] = 0x05; // jnz +5 → tramp+9
    WriteJmpRel32(tramp + 4, kExit);
    tramp[9] = 0xFF;
    tramp[10] = 0x77;
    tramp[11] = 0x3D; // push [edi+3Dh]
    tramp[12] = 0x8D;
    tramp[13] = 0x47;
    tramp[14] = 0x39; // lea eax,[edi+39h]
    tramp[15] = 0x50; // push eax
    WriteCallRel32(tramp + 16, kCallTarget);
    WriteJmpRel32(tramp + 21, kContinue);
    return true;
}

} // namespace

void AttachMapEnterNullGuard() {
    // HARD OFF 2026-09-02: DLL VirtualAlloc tramp @A292F9 caused enter-game
    // crashes all evening (AV@3 / illegal-insn / privileged-insn). Do not patch.
    // Keep function as no-op so any stray caller cannot re-enable the site write.
    BootLog("MapEnterNullGuard HARD-OFF (no patch @A292F9)");
    g_installed = true;
}
