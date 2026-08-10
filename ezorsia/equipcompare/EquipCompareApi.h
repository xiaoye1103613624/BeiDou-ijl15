#pragma once

void AttachEquipCompareMod();

namespace EquipCompare {
// Call BEFORE SetItem::EnsureUiHooks so companion tip hits vanilla ShowItemToolTip.
void EnsureHooks();
// Reposition active compare tip: right of growth tip, else set tip, else hover.
void RelayoutActiveCompareTip();
// True while compare tip rewrites PrintValue (FusionAnvil Hyper breakdown must yield).
bool IsDeltaPrintValueActive();
}
