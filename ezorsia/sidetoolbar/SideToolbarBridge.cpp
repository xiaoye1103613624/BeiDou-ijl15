#include "stdafx.h"
#include "SideToolbarApi.h"
#include "../WorldMapInfo.h"
#include "../Client.h"
#include "compat/wvs/field.h"
#include <cstdio>

namespace {
bool g_sideToolbarEnabled = false;

void SidebarLog(const char* /*msg*/) {
    // tip-drawonly: no fopen(sidebar_debug) — absent from green 6D612F01; enter GS family.
}
} // namespace

namespace SideToolbar {
// Field-enter is late enough for ResMan. Creating only from OnTick was fragile:
// if BossHP's UserLocal::Update hook fails, ModRegistry::OnClientTick never runs
// and the toolbar is never created.
void EnsureHooks() {
    g_sideToolbarEnabled = true;
    char line[192];
    sprintf_s(line, "EnsureHooks: create now disableWorldMap=%d",
              Client::disableWorldMap ? 1 : 0);
    SidebarLog(line);
    // WorldMap force-attach soft-disabled (enter MapHelper / 0x8007000D bisect).
    SidebarLog("EnsureHooks: SKIP WorldMap force-attach (enter MapHelper bisect)");
    // Only create once in-field — login screen must stay clean.
    if (get_field()) {
        AttachSideToolbarMod();
    }
}

void OnTick() {
    if (!g_sideToolbarEnabled) {
        return;
    }
    // Login / char-select: never keep or recreate toolbar.
    if (!get_field()) {
        DestroyForLogout();
        return;
    }
    // Retry if first create failed (ResMan not ready yet).
    static int s_ticks = 0;
    if (s_ticks < 120) {
        ++s_ticks;
        if ((s_ticks % 15) == 0) {
            AttachSideToolbarMod();
        }
    }
}
} // namespace SideToolbar
