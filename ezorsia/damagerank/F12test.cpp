#include "F12test.h"
#include "uiDamageRank.h"
#include "beautyshop/BeautyShopApi.h"
#include "dailycheckin/DailyCheckinApi.h"
#include "storagebag/StorageBagApi.h"
#include "partybuffs/PartyBuffsApi.h"
#include "compat/hook.h"
#include "compat/wvs/field.h"
#include <windows.h>

namespace {
auto CFieldOnKey = CField::OnKey;

bool TryCloseCustomUiOnEscape() {
    if (BeautyShop_IsOpen()) {
        BeautyShop_CloseWindow();
        return true;
    }
    if (DailyCheckin_IsOpen()) {
        DailyCheckin_Close();
        return true;
    }
    if (BagWindow_IsOpen()) {
        BagWindow_Close();
        return true;
    }
    if (CUIDamageRank::IsPanelOpen()) {
        CUIDamageRank::ClosePanel();
        return true;
    }
    if (PartyBuffs_IsTrackerVisible()) {
        PartyBuffs_SetTrackerVisible(false);
        return true;
    }
    return false;
}

void __fastcall CFieldOnKey_hook(
        CField* pThis,
        void* /*edx*/,
        unsigned int wParam,
        int lParam) {
    const bool isKeyUp = (lParam & 0x80000000) != 0;
    const bool wasAlreadyDown = (lParam & 0x40000000) != 0;

    if (wParam == VK_ESCAPE && !isKeyUp && !wasAlreadyDown) {
        if (TryCloseCustomUiOnEscape()) {
            return;
        }
    }

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
