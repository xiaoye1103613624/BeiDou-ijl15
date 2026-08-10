#pragma once

constexpr int kFusionAnvilItemId = 5900000;

void AttachFusionAnvilMod();
void AttachFusionAnvilPacketHooks();
void AttachFusionAnvilUiHooks();
void AttachFusionAnvilTooltipHooks();
void FusionAnvil_BindDrawToolTipEquipTarget(void** outPtr);
#ifdef __cplusplus
extern "C" {
#endif
/** tip 深绿：statIdx 0..14，无记录则 0。 */
short FusionAnvil_GetChaosStat(void* pe, int statIdx);
#ifdef __cplusplus
}
#endif
void AttachFusionAnvilItemIconHooks();

namespace FusionAnvil {
// Struct patch + equip decode — must run at DllMain (before getCharInfo).
void EnsurePacketHooks();
// Dialog / avatar / cash-item hooks — deferred until first map entry.
void EnsureHooks();
// Equip tooltip transmog line + corner skin icon — deferred with UI hooks.
void EnsureTooltipHooks();
// Equip-slot grid corner skin badge — DISABLED (naked hook @0x81DEE9 crashes backpack).
// Transmog display: tooltip.cpp only.
void EnsureItemIconHooks();
}
