#include "stdafx.h"
#include "ClickRaiseApi.h"
#include "UiLayerZ.h"

#include "compat/wvs/wnd.h"
#include "compat/wvs/wndman.h"
#include "compat/wvs/statusbar.h"
#include "compat/ztl/zcoll.h"

#include <climits>
#include <algorithm>
#include <cstdio>

namespace {

constexpr uintptr_t kAddr_GameHwnd = 0x00BEC33C; // CInputSystem* → HWND at *[0]
constexpr uintptr_t kAddr_WndList = 0x00BF1648;  // ZList<CWnd*>

// Online-level: rare raise events only (not every mouse move).
static void ClickRaiseLog(const char* msg) {
    OutputDebugStringA(msg);
}

int WndAbsLeft(CWnd* pWnd) {
    if (!pWnd) {
        return -1;
    }
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E03C5)(
            reinterpret_cast<char*>(pWnd) + 4);
}

int WndAbsTop(CWnd* pWnd) {
    if (!pWnd) {
        return -1;
    }
    return reinterpret_cast<int(__thiscall*)(void*)>(0x009E0447)(
            reinterpret_cast<char*>(pWnd) + 4);
}

bool GetClientCursor(POINT& out) {
    out.x = 0;
    out.y = 0;
    POINT pt{};
    if (!::GetCursorPos(&pt)) {
        return false;
    }
    void* pInput = nullptr;
    __try {
        pInput = *reinterpret_cast<void**>(kAddr_GameHwnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pInput = nullptr;
    }
    HWND hwnd = pInput ? *reinterpret_cast<HWND*>(pInput) : nullptr;
    if (!hwnd || !::ScreenToClient(hwnd, &pt)) {
        return false;
    }
    out = pt;
    return true;
}

int SafeLayerZ(CWnd* w) {
    if (!w || !w->m_pLayer) {
        return INT_MIN / 4;
    }
    __try {
        return w->m_pLayer->z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return INT_MIN / 4;
    }
}

int SafeHitTest(CWnd* w, int rx, int ry) {
    if (!w) {
        return 0;
    }
    __try {
        CCtrlWnd* ctrl = nullptr;
        return w->HitTest(rx, ry, &ctrl);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool IsStatusBar(CWnd* w) {
    return CUIStatusBar::IsInstantiated() && w == CUIStatusBar::GetInstance();
}

ZList<CWnd*>& WndList() {
    return *reinterpret_cast<ZList<CWnd*>*>(kAddr_WndList);
}

bool WindowContainsPoint(CWnd* w, const POINT& pt, int& outRx, int& outRy) {
    outRx = outRy = 0;
    if (!w || !w->m_pLayer) {
        return false;
    }

    int vis = 1;
    __try {
        vis = static_cast<int>(w->m_pLayer->visible);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!vis) {
        return false;
    }

    const int l = WndAbsLeft(w);
    const int t = WndAbsTop(w);
    if (l < -2000 || t < -2000) {
        return false;
    }

    int ww = w->m_width;
    int hh = w->m_height;
    __try {
        if (ww <= 0) {
            ww = w->m_pLayer->width;
        }
        if (hh <= 0) {
            hh = w->m_pLayer->height;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (ww <= 0 || hh <= 0) {
        return false;
    }
    if (pt.x < l || pt.y < t || pt.x >= l + ww || pt.y >= t + hh) {
        return false;
    }

    outRx = pt.x - l;
    outRy = pt.y - t;
    return true;
}

// Visual-top window under the cursor (HitTest != 0). Skip StatusBar chrome.
// Same-z tie → prefer later list entry (usually more recently opened/focused).
CWnd* FindTopmostUnderCursor(const POINT& pt) {
    CWnd* best = nullptr;
    int bestZ = INT_MIN;
    int bestIndex = -1;
    int index = 0;

    auto* pos = WndList().GetHeadPosition();
    while (pos) {
        CWnd* w = WndList().GetNext(pos);
        const int idx = index++;
        if (!w || IsStatusBar(w)) {
            continue;
        }

        int rx = 0;
        int ry = 0;
        if (!WindowContainsPoint(w, pt, rx, ry)) {
            continue;
        }
        if (SafeHitTest(w, rx, ry) == 0) {
            continue; // transparent hole — let windows underneath compete
        }

        const int z = SafeLayerZ(w);
        if (z > bestZ || (z == bestZ && idx >= bestIndex)) {
            bestZ = z;
            bestIndex = idx;
            best = w;
        }
    }
    return best;
}

// Max z among raiseable ordinary windows only (exclude modal/tip bands).
int MaxLayerZAmongWindows() {
    int maxZ = 0;
    auto* pos = WndList().GetHeadPosition();
    while (pos) {
        CWnd* w = WndList().GetNext(pos);
        if (!w || IsStatusBar(w)) {
            continue;
        }
        const int z = SafeLayerZ(w);
        if (z >= 0 && z < UiLayerZ::kModalFloorZ && z > maxZ) {
            maxZ = z;
        }
    }
    return maxZ;
}

// Push every other hit window under the click below `top`, so ProcessMouse
// (list or z) cannot pick MiniMap/Inventory when Equip was the visual top.
void DemoteOverlappingUnder(CWnd* top, const POINT& pt, int topZ) {
    if (!top) {
        return;
    }
    auto* pos = WndList().GetHeadPosition();
    while (pos) {
        CWnd* w = WndList().GetNext(pos);
        if (!w || w == top || IsStatusBar(w) || !w->m_pLayer) {
            continue;
        }
        int rx = 0;
        int ry = 0;
        if (!WindowContainsPoint(w, pt, rx, ry)) {
            continue;
        }
        if (SafeHitTest(w, rx, ry) == 0) {
            continue;
        }
        const int z = SafeLayerZ(w);
        // Never demote modals / tips sitting in reserved high bands.
        if (UiLayerZ::IsModalOrAbove(z) || z < topZ) {
            continue;
        }
        __try {
            w->m_pLayer->z = topZ - 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void FocusAndActivate(CWnd* w) {
    if (!w || !CWndMan::IsInstantiated()) {
        return;
    }
    // SetFocus also reorders the manager's focus/hit stack on this client.
    auto* handler = reinterpret_cast<IUIMsgHandler*>(reinterpret_cast<char*>(w) + 4);
    __try {
        CWndMan::GetInstance()->SetFocus(handler);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    __try {
        w->OnActivate(1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RaiseWindow(CWnd* w, const POINT& pt) {
    if (!w || !w->m_pLayer) {
        return;
    }

    const int curZ = SafeLayerZ(w);
    // Modal / tip-band windows: focus only — do not pull them into raise band
    // or climb past reserved tip layers.
    if (UiLayerZ::IsModalOrAbove(curZ)) {
        FocusAndActivate(w);
        return;
    }

    const int maxZ = MaxLayerZAmongWindows();
    int nextZ = curZ;
    if (nextZ <= maxZ) {
        nextZ = (std::min)(maxZ + 1, UiLayerZ::kRaiseMaxZ);
        __try {
            w->m_pLayer->z = nextZ;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        char buf[128];
        sprintf_s(buf, "[ClickRaise] raise z %d -> %d (maxRaise=%d)\n",
                  curZ, nextZ, UiLayerZ::kRaiseMaxZ);
        ClickRaiseLog(buf);
    }

    DemoteOverlappingUnder(w, pt, nextZ);
    FocusAndActivate(w);
}

} // namespace

void ClickRaise_OnLButtonDown() {
    POINT pt{};
    if (!GetClientCursor(pt)) {
        return;
    }
    CWnd* top = FindTopmostUnderCursor(pt);
    if (!top) {
        return;
    }
    RaiseWindow(top, pt);
}
