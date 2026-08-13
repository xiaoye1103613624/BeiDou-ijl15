#pragma once

void AttachEquipAddonMod();

extern "C" void* __cdecl EquipAddon_SidecarZRefForBp(int bp);

namespace EquipAddon {
void InstallLoginPersistEarly();
void EnsureHooks();
void OnTick();
void WirePocketAndSiSlots();
}