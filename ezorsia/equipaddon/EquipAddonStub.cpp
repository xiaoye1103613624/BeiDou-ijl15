#include "stdafx.h"
#include "EquipAddonApi.h"

void AttachEquipAddonMod() {}

extern "C" void* __cdecl EquipAddon_SidecarZRefForBp(int bp) {
  (void)bp;
  return nullptr;
}

namespace EquipAddon {
void InstallLoginPersistEarly() {}
void EnsureHooks() {}
void OnTick() {}
void WirePocketAndSiSlots() {}
}
