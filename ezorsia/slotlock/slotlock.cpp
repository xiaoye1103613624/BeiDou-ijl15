// slotlock.cpp — thin gather/sort result hooks for ExpandItem (S9).
// Lock-slot feature removed (appended lock bytes left permanent gaps).
// Do NOT silently drop gather sends: pending+debounce latch made 集合 look dead
// when canSendPkg failed or invent busy stuck. Native SendGatherItem already
// rate-limits via canSendPkg(500ms) and invent busy @ +0x20A4.

#include "stdafx.h"
#include "SlotLockApi.h"
#include "compat/hook.h"

#include <cstdint>
#include <windows.h>

namespace SlotLock {
namespace {

constexpr uintptr_t kAddr_SendGatherItem = 0x00A08EE6;
constexpr uintptr_t kAddr_GatherResultHandler = 0x00A208FF;
constexpr uintptr_t kAddr_SortResultHandler = 0x00A2091C;

bool g_sortResultHooksAttached = false;

using SendGatherFn = void(__thiscall*)(void* pCtx, int invType);
SendGatherFn g_SendGatherOrig = reinterpret_cast<SendGatherFn>(kAddr_SendGatherItem);

using SortResultFn = int(__thiscall*)(void*, int);
SortResultFn g_GatherResultOrig =
    reinterpret_cast<SortResultFn>(kAddr_GatherResultHandler);
SortResultFn g_SortResultOrig =
    reinterpret_cast<SortResultFn>(kAddr_SortResultHandler);

void __fastcall SendGatherItem_PassThrough_hook(void* pCtx, void* /*edx*/, int invType) {
    // Only skip when native invent-busy is already set (same flag SendGather sets).
    // Never invent a second pending latch — that blocked all later 集合 clicks.
    if (pCtx) {
        __try {
            if (reinterpret_cast<DWORD*>(pCtx)[2089] != 0) {
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
    }
    if (g_SendGatherOrig) {
        g_SendGatherOrig(pCtx, invType);
    }
}

int __fastcall GatherResult_hook(void* pThis, void* /*edx*/, int a2) {
    return g_GatherResultOrig ? g_GatherResultOrig(pThis, a2) : 0;
}

int __fastcall SortResult_hook(void* pThis, void* /*edx*/, int a2) {
    return g_SortResultOrig ? g_SortResultOrig(pThis, a2) : 0;
}

void AttachSortResultHooks() {
    if (g_sortResultHooksAttached) {
        return;
    }
    g_sortResultHooksAttached = true;
    ATTACH_HOOK(g_GatherResultOrig, GatherResult_hook);
    ATTACH_HOOK(g_SortResultOrig, SortResult_hook);
}

void AttachGatherHook() {
    ATTACH_HOOK(g_SendGatherOrig, SendGatherItem_PassThrough_hook);
}

} // namespace

void EnsureHooks() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;
    AttachSortResultHooks();
    AttachGatherHook();
}

} // namespace SlotLock

void AttachSlotLockMod() {
    SlotLock::EnsureHooks();
}
