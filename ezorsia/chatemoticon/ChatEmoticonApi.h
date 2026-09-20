#pragma once

// Public surface for the Kaentake-style overhead chat emoticon client feature.
// Mirrors DailyCheckinApi.h so the module can be wired into ModRegistry like any
// other builtin.

namespace ChatEmoticon {

// Registers the 0x17F inbound packet handler and the per-frame Tick (expiry /
// launcher button lifecycle). Called from ModRegistry::RegisterBuiltins().
void RegisterModule();

// True while the picker panel is open.
bool IsOpen();

// Toggle the picker panel (no-op if the launcher button has not been created
// yet, i.e. the player is not in-game).
void Toggle();

// Force-close the picker panel.
void Close();

} // namespace ChatEmoticon
