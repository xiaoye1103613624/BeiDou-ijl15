#pragma once

void AttachEquipCompareMod();

namespace EquipCompare {
// Call BEFORE SetItem::EnsureUiHooks so companion tip hits vanilla ShowItemToolTip.
void EnsureHooks();
}
