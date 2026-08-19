#include "stdafx.h"
#include "DamageRankInput.h"
#include "../storagebag/StorageBagApi.h"
#ifndef GREEN_ENTER_BASELINE
#define GREEN_ENTER_BASELINE 0
#endif
#if !GREEN_ENTER_BASELINE
#include "../invresize/InvResizeApi.h"
#endif
#include "compat/hook.h"
#include "compat/wvs/wndman.h"
#include "../bootlog/CrashDiag.h"

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
    CrashDiag_SetMsgContext(GetCurrentThreadId(), msg, wParam, lParam);

    const bool damageRankMouse =
            DamageRank_HandleMouseMessage(msg, wParam, lParam, plResult);
    // Title-bar BAG button before inventory edge grips (grips live at y >= 50).
    const bool storageBagMouse =
            StorageBag_HandleMouseMessage(msg, wParam, lParam, plResult);
#if !GREEN_ENTER_BASELINE
    InvResize_HandleMouseMessage(msg, wParam, lParam, plResult);
#else
    (void)storageBagMouse;
#endif
    const int result =
            CWndMan::TranslateMessageImpl(this, msg, wParam, lParam, plResult);

    if (damageRankMouse && msg == WM_LBUTTONDOWN) {
        ClearDamageRankFocus(this);
    }

    CrashDiag_ClearMsgContext();
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
