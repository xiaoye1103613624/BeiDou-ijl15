#pragma once

#include <windows.h>

// Global click-to-front for overlapping CWnds (Equip / MiniMap / Inventory / …).
// Call on WM_LBUTTONDOWN before CWndMan::ProcessMouse so m_pMoveWnd belongs
// to the visual-top window under the cursor — not a covered neighbour.
void ClickRaise_OnLButtonDown();
