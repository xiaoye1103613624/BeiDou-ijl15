#pragma once

// Game window caption updater + limit-break sync (see windowtitle.cpp / LimitBreakBridge.cpp).

namespace WindowTitle {
void InstallEarlyUpdate();
void OnTick();
void SetLimitBreak(long long value);
long long GetLimitBreak();
// Set title value and rewrite client damage-cap memory (int imm + display double).
void ApplyLimitBreak(long long value);
void RegisterPacketHandler();
} // namespace WindowTitle
