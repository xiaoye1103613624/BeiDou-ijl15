#pragma once

void AttachEquipCompareMod();

class CUIToolTip;

namespace EquipCompare {
// Call BEFORE SetItem::EnsureUiHooks so companion tip hits vanilla ShowItemToolTip.
void EnsureHooks();
// Reposition active compare tip: right of growth tip, else set tip, else hover.
void RelayoutActiveCompareTip();
// True while compare tip rewrites PrintValue (FusionAnvil Hyper breakdown must yield).
bool IsDeltaPrintValueActive();
// True for the side-by-side equipped compare tip instance (not the hover tip).
bool IsEquippedCompareTip(CUIToolTip* tip);
// True while Original_ShowItemToolTip is painting the compare tip (nested show).
bool IsShowingCompareTip();
// After hover set/growth companions are ready: show/relayout compare to their right.
void FlushPendingAfterCompanions(
        CUIToolTip* sourceTooltip,
        int sourceLeft,
        int sourceTop,
        void* sourceItem,
        int a6,
        int a7,
        int a8,
        unsigned int a9);
void ClearPendingCompare();
}
