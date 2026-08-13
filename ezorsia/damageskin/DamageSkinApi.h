#pragma once

namespace DamageSkin {
void RegisterPacketHandler();
// Intentionally empty at DllMain — see EnsureHooks().
void AttachHooks();
// Idempotent: installs Effect_HP / picker hooks on first field enter.
void EnsureHooks();
}

void AttachDamageSkinMod();
void AttachDamageSkinPickerMod();
void OpenDamageSkinPicker();