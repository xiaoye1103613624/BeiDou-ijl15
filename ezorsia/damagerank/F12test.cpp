#include "F12test.h"
#include "uiDamageRank.h"
#include "compat/hook.h"
#include "compat/wvs/field.h"
#include <windows.h>

namespace {
auto CFieldOnKey = CField::OnKey;

void __fastcall CFieldOnKey_hook(
        CField* pThis,
        void* /*edx*/,
        unsigned int wParam,
        int lParam) {
    const bool isKeyUp = (lParam & 0x80000000) != 0;
    const bool wasAlreadyDown = (lParam & 0x40000000) != 0;

    if (wParam == VK_F12 && !isKeyUp && !wasAlreadyDown) {
        auto& ui = CUIDamageRank::GetInstance();
        const bool wasVisible = ui.IsVisible();

        CUIDamageRank::ToggleByHotkey();

        if (!wasVisible && ui.IsVisible()) {
            CUIDamageRank::PlayUISound(L"MenuUp");
        }
        return;
    }

    CFieldOnKey(pThis, wParam, lParam);
}
} // namespace

void AttachF12test() {
    ATTACH_HOOK(CFieldOnKey, CFieldOnKey_hook);
}
