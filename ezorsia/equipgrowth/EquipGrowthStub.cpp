// Tip-clean stub â€?keep link symbols without growth packet/UI attach.
// Real EquipGrowth*.obj must NOT be linked (enter-red vs green 6D612F01).
#include "stdafx.h"
#include "EquipGrowthApi.h"

namespace EquipGrowth {
void RegisterPacketHandler() {}
void EnsureHooks() {}
void RequestGrowthTip(int) {}
void OnHoverEquip(int, int, int, int) {}
void RedrawActivePanel() {}
void InvalidateEmptyCache(int) {}
void InvalidateCache(int) {}
const char* GetGrowthTipText(int) { return ""; }
bool HasGrowthTip(int) { return false; }
bool IsGrowthTipResolved(int) { return true; }
bool TryGetActiveGrowthTooltipRect(int&, int&, int&, int&) { return false; }
int GetGrowthBonusForStat(int, int) { return 0; }
int GetFlameBonusForStat(int, int) { return 0; }
} // namespace EquipGrowth

void EquipGrowth_OnEquipTipDrawn(CUIToolTip*, GW_ItemSlotEquip*) {}
void EquipGrowth_OnEquipTipDrawnId(CUIToolTip*, int) {}
void EquipGrowth_Hide() {}
