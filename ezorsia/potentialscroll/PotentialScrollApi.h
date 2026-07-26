#pragma once

// Hyper (20493*) / Potential (20494*) / classic 20497*/20498* /
// BonusPotential + Phase4 custom (20499*: 2049902, 2049910-2049915):
// bypass v083 scroll-vs-equip type match so USE_UPGRADE_SCROLL reaches the server.
//
// Phase11: Cash cubes 5062000/01/02/2100 — TryHandleCashCubeDrop intercepts
// inventory drop onto 背包装备栏 and sends USE_CASH_ITEM with equip slot.

void AttachPotentialScrollMod();

// Called from FusionAnvil CDraggableItem::OnDropped chain (owns that hook).
// Returns true if the drop was consumed (cube use packet sent).
bool PotentialScroll_TryHandleCashCubeDrop(
    int nItemTI, int nSlotPosition, void* pFrom, void* pTo, int rx, int ry);

namespace PotentialScroll {
void ApplyPatches();
}
