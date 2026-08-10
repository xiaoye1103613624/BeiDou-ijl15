#pragma once

void AttachSideToolbarMod();

namespace SideToolbar {
void EnsureHooks();
void OnTick();
// Login / char-select: destroy floating toolbar (not in CWvsContext A041FF list).
void DestroyForLogout();
}
