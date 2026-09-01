#pragma once

void AttachSideToolbarMod();

namespace SideToolbar {
void EnsureHooks();
void OnTick();
// Login / char-select: destroy floating toolbar (not in CWvsContext A041FF list).
void DestroyForLogout();
void RegisterPacketHandler();

/** Server SIDEBAR_CONFIG_SYNC (0x3733) — per ServerTool index. tip strings are GBK. */
void ResetServerToolConfigDefaults();
void ApplyServerToolConfig(int toolIndex, bool visible, const char* tipTitleGbk, const char* tipDescGbk);
void InvalidateUi();
}
