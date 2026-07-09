#include "stdafx.h"
#include "DamageRankInput.h"
#include "../storagebag/StorageBagApi.h"
#include "compat/hook.h"
#include "compat/wvs/wndman.h"

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
    const bool damageRankMouse =
            DamageRank_HandleMouseMessage(msg, wParam, lParam, plResult);
    const bool storageBagMouse =
            StorageBag_HandleMouseMessage(msg, wParam, lParam, plResult);

    const int result =
            CWndMan::TranslateMessageImpl(this, msg, wParam, lParam, plResult);

    if (damageRankMouse && msg == WM_LBUTTONDOWN) {
        ClearDamageRankFocus(this);
    }

    return result;
}

void AttachDamageRankInputHooks() {
    ATTACH_HOOK(CWndMan::TranslateMessageImpl, CWndMan::TranslateMessageImpl_hook);
}
