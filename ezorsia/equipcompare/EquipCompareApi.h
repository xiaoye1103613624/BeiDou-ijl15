#pragma once

void AttachEquipCompareMod();

namespace EquipCompare {
// Call BEFORE SetItem::EnsureUiHooks so companion tip hits vanilla ShowItemToolTip.
void EnsureHooks();
// Reposition active compare tip to the right of set tip (if any), else right of hover.
void RelayoutActiveCompareTip();
// True while compare tip rewrites PrintValue (FusionAnvil Hyper breakdown must yield).
bool IsDeltaPrintValueActive();
}
