#include "stdafx.h"
#include "InvResizeApi.h"
#include "../damagerank/DamageRankInput.h"

namespace {
bool g_hooksAttached = false;
} // namespace

namespace InvResize {
void EnsureHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    // Mouse grips share CWndMan::TranslateMessageImpl with StorageBag / DamageRank.
    AttachDamageRankInputHooks();
    AttachInvResizeMod();
}

void OnTick() {
    InvResize_Tick();
}
} // namespace
