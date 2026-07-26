#include "stdafx.h"
#include "SideToolbarApi.h"

namespace {
bool g_sideToolbarEnabled = false;
} // namespace

namespace SideToolbar {
// Do NOT create UI here. CField init is too early (login/boot white-screen risk).
// Do NOT attach another CField::OnKey detour here — bag/F12 already hook that address;
// stacking separate DetourAttach trampolines on the same target has caused hangs.
void EnsureHooks() {
    g_sideToolbarEnabled = true;
}

void OnTick() {
    if (!g_sideToolbarEnabled) {
        return;
    }
    // Delay a few frames after first in-game UserLocal::Update so ResMan/UI are fully up.
    static int s_ticks = 0;
    if (s_ticks < 45) {
        ++s_ticks;
        return;
    }
    AttachSideToolbarMod();
}
} // namespace SideToolbar
