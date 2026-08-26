// slotlock.cpp — inventory gather debounce + sort/gather result hooks.
// Sort send is NOT debounced here (silent drop made 整理 look dead).

#include "stdafx.h"
#include "SlotLockApi.h"
#include "../invresize/InvResizeApi.h"
#include "compat/hook.h"

#include <cstdint>
#include <windows.h>

namespace SlotLock {
namespace {

constexpr uintptr_t kAddr_SendGatherItem = 0x00A08EE6;
constexpr uintptr_t kAddr_GatherResultHandler = 0x00A208FF;
constexpr uintptr_t kAddr_SortResultHandler = 0x00A2091C;
constexpr ULONGLONG kGatherDebounceMs = 800ull;

ULONGLONG g_lastGatherAt = 0;
bool g_gatherInventPending = false;
bool g_sortResultHooksAttached = false;

using SendGatherFn = void(__thiscall*)(void* pCtx, int invType);
SendGatherFn g_SendGatherOrig = reinterpret_cast<SendGatherFn>(kAddr_SendGatherItem);

using SortResultFn = int(__thiscall*)(void*, int);
SortResultFn g_GatherResultOrig =
    reinterpret_cast<SortResultFn>(kAddr_GatherResultHandler);
SortResultFn g_SortResultOrig =
    reinterpret_cast<SortResultFn>(kAddr_SortResultHandler);

void ClearGatherPending() {
    g_gatherInventPending = false;
    g_lastGatherAt = GetTickCount64();
}

bool GatherAllowSend(void* pCtx) {
    const ULONGLONG now = GetTickCount64();
    if (g_gatherInventPending) {
        if (g_lastGatherAt != 0 && now - g_lastGatherAt > 5000ull) {
            g_gatherInventPending = false;
        } else {
            return false;
        }
    }
    if (pCtx) {
        __try {
            if (reinterpret_cast<DWORD*>(pCtx)[2089] != 0) {
                return false;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    if (g_lastGatherAt != 0 && now - g_lastGatherAt < kGatherDebounceMs) {
        return false;
    }
    g_lastGatherAt = now;
    g_gatherInventPending = true;
    return true;
}

void __fastcall SendGatherItem_Debounce_hook(void* pCtx, void* /*edx*/, int invType) {
    if (!GatherAllowSend(pCtx)) {
        return;
    }
    if (g_SendGatherOrig) {
        g_SendGatherOrig(pCtx, invType);
    }
}

int __fastcall GatherResult_hook(void* pThis, void* /*edx*/, int a2) {
    const int result = g_GatherResultOrig ? g_GatherResultOrig(pThis, a2) : 0;
    ClearGatherPending();
    InvResize_ReinstallLiveBackground();
    return result;
}

int __fastcall SortResult_hook(void* pThis, void* /*edx*/, int a2) {
    const int result = g_SortResultOrig ? g_SortResultOrig(pThis, a2) : 0;
    InvResize_ReinstallLiveBackground();
    return result;
}

void AttachSortResultHooks() {
    if (g_sortResultHooksAttached) {
        return;
    }
    g_sortResultHooksAttached = true;
    ATTACH_HOOK(g_GatherResultOrig, GatherResult_hook);
    ATTACH_HOOK(g_SortResultOrig, SortResult_hook);
}

void AttachGatherDebounce() {
    ATTACH_HOOK(g_SendGatherOrig, SendGatherItem_Debounce_hook);
}

} // namespace

void EnsureHooks() {
    static bool attached = false;
    if (attached) {
        return;
    }
    attached = true;
    AttachSortResultHooks();
    AttachGatherDebounce();
}

} // namespace SlotLock

void AttachSlotLockMod() {
    SlotLock::EnsureHooks();
}
