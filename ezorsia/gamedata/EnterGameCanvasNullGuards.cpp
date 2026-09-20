// EnterGameCanvasNullGuards — IDA-calibrated nullguard for enter-game CANVAS AV.
//
// Crash (2026-09-17 BeiDou-Client_S9):
//   EIP=CANVAS.DLL+0xED28 (mov eax,[esi] with ECX=this=0)
//   stack: sub_8DDB30 @ 0x8DE112 (KeyConfig / StatusBar quickSlot draw)
//   RecentWZ suspect_last=UI/StatusBar.img/base/quickSlot
//   getobj_fail empty — GrowthEnabled/Disabled GetObject succeeded; crash is later.
//
// Root cause: IWzCanvas::Copy (vtable+0x80) invoked with a NULL source canvas when
// KeyConfig/quickslotConfig/key/<id> GetItem returns empty. Copy then calls a
// Getcx/Getcy-style getter on the null source → AV at CANVAS+0xED28.
//
// Stock site @0x8DE0EC (Angel.exe / BeiDou.exe imagebase 0x400000), 38 bytes through
// call [edx+80h]; resume cmp eax,ebx @0x8DE112. Prior instruction sub esp,10 @0x8DE0E7
// allocates VARIANT scratch — skip path must add esp,10 then jmp 0x8DE124.

#include "stdafx.h"
#include "EnterGameCanvasNullGuardsApi.h"
#include "../Memory.h"

#include <iostream>

namespace {

constexpr DWORD kQuickSlotCopySite = 0x008DE0EC;
constexpr DWORD kQuickSlotCopyResume = 0x008DE112;
constexpr DWORD kQuickSlotCopySkip = 0x008DE124;
constexpr int kQuickSlotCopyBytes = 0x26; // 38

static bool ExpectBytes(DWORD va, const unsigned char* expected, size_t n) {
    __try {
        const auto* live = reinterpret_cast<const unsigned char*>(va);
        for (size_t i = 0; i < n; ++i) {
            if (live[i] != expected[i]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

__declspec(naked) void QuickSlotCopy_NullGuard() {
    __asm {
        cmp dword ptr [ebp - 0x14], 0
        je L_skip
        // Stock 0x8DE0EC..0x8DE111
        push dword ptr [ebp - 0x14]
        lea esi, [ebp - 0x94]
        movsd
        movsd
        add ecx, 2
        add edx, 2
        push ecx
        mov dword ptr [ebp - 0x20], edx
        push dword ptr [ebp - 0x20]
        mov edx, dword ptr [eax]
        movsd
        push eax
        mov dword ptr [ebp - 0x2C], eax
        movsd
        call dword ptr [edx + 0x80]
        jmp kQuickSlotCopyResume
    L_skip:
        add esp, 0x10
        jmp kQuickSlotCopySkip
    }
}

} // namespace

void AttachEnterGameCanvasNullGuards() {
    // push [ebp-14h] = FF 75 EC
    static const unsigned char kQuickHead[] = {0xFF, 0x75, 0xEC};
    if (ExpectBytes(kQuickSlotCopySite, kQuickHead, sizeof(kQuickHead))) {
        Memory::CodeCave(reinterpret_cast<void*>(&QuickSlotCopy_NullGuard),
                         kQuickSlotCopySite, kQuickSlotCopyBytes);
        const unsigned char op = *reinterpret_cast<volatile unsigned char*>(kQuickSlotCopySite);
        std::cout << "[EnterGameCanvasNullGuards] QuickSlotCopy @ 8DE0EC => "
                  << std::hex << static_cast<int>(op) << std::dec
                  << (op == 0xE9 ? " OK" : " FAIL") << std::endl;
    } else {
        std::cout << "[EnterGameCanvasNullGuards] QuickSlotCopy SKIP (bytes mismatch)"
                  << std::endl;
    }
}
