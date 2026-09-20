#pragma once

// .text Memory::Write* patches that must run AFTER InitializeGr2D succeeds.
// Calling them from DllMain with UseVirtuProtect=true breaks Gr2D_DX8 FindScreenMode
// ("Failed in finding proper screen mode for Gr2D") on some hosts.
void ApplyDeferredBootPatches();

// Config wants VirtualProtect for patches; keep it OFF until after Gr2D, then restore.
void DeferredBoot_SetWantVirtuProtect(bool want);
bool DeferredBoot_GetWantVirtuProtect();
