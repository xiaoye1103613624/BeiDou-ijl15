#include "stdafx.h"
#include "DamageRankInput.h"
#include "compat/hook.h"
#include "compat/wvs/wndman.h"
#include "../storagebag/StorageBagApi.h"
#include "../invresize/InvResizeApi.h"
#include "../clickraise/ClickRaiseApi.h"

namespace {
static void ClearDamageRankFocus(CWndMan* wndMan) {
    if (!wndMan) {
        return;
    }

    using SetFocusFn = void(__thiscall*)(CWndMan*, void*);
    auto fnSetFocus = reinterpret_cast<SetFocusFn>(0x009E3264);
    fnSetFocus(wndMan, nullptr);
}
} // namespace

int CWndMan::TranslateMessageImpl_hook(
        UINT& msg,
        WPARAM& wParam,
        LPARAM& lParam,
        LRESULT* plResult) {
    // Inventory BAG button lives in the title-bar strip — CUIItem::OnMouseButton
    // never sees those clicks. StorageBag must handle them here (msg by-ref so
    // it can eat WM_LBUTTONDOWN/UP as WM_NULL). Was documented but never called.
    if (StorageBag_HandleMouseMessage(msg, wParam, lParam, plResult)) {
        return 0;
    }
    (void)InvResize_HandleMouseMessage(msg, wParam, lParam, plResult);

    const bool damageRankMouse =
            DamageRank_HandleMouseMessage(msg, wParam, lParam, plResult);

    // Raise the visual-top CWnd under the cursor before ProcessMouse picks
    // m_pMoveWnd — stops Equip/Inventory drags from moving MiniMap underneath.
    if (msg == WM_LBUTTONDOWN) {
        ClickRaise_OnLButtonDown();
    }

    const int result =
            CWndMan::TranslateMessageImpl(this, msg, wParam, lParam, plResult);

    if (damageRankMouse && msg == WM_LBUTTONDOWN) {
        ClearDamageRankFocus(this);
    }

    return result;
}

void AttachDamageRankInputHooks() {
    static bool s_attached = false;
    if (s_attached) {
        return;
    }
    s_attached = true;
    ATTACH_HOOK(CWndMan::TranslateMessageImpl, CWndMan::TranslateMessageImpl_hook);
}
