#pragma once

// Shared Gr2D layer-z bands for ClickRaise vs tips / modal dialogs.
// Relative order: raised panels < plugin overlays < modal < tip.
//
// Plugin overlays (DamageRank ~1000, equipaddon ~3000) keep their own constants
// and sit between raise max and modal floor — they are not raised by ClickRaise.

namespace UiLayerZ {

// ClickRaise may only bump ordinary CWnd layers up to this inclusive ceiling.
constexpr int kRaiseMaxZ = 500;

// CreateDlg / UtilDlgEx / menu popups — always above raised panels & overlays.
constexpr int kModalFloorZ = 8000;

// CUIToolTip::MakeLayer vanilla uses 0/10/210; pin tips above modals.
constexpr int kTipMinZ = 10000;

inline bool IsModalOrAbove(int z) {
    return z >= kModalFloorZ;
}

} // namespace UiLayerZ
