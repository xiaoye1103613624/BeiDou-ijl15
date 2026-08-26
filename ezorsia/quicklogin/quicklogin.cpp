// quicklogin.cpp — channel auto-focus + CLogin stability (GMS v083).
// Source: beidou_client_xiaoye playbook / quicklogin

#include "stdafx.h"
#include "QuickLoginApi.h"
#include "Memory.h"
#include "compat/hook.h"
#include "compat/wvs/wnd.h"
#include "compat/wvs/wndman.h"
#include "compat/wvs/wvsapp.h"
#include "compat/ztl/ztl.h"

// ============================================================================
// UI: auto-focus channel select after world pick
// ============================================================================

class CUIChannelSelect : public TSingleton<CUIChannelSelect, 0x00BEDA58> {
public:
    MEMBER_AT(IUIMsgHandler, 0x04, m_handler)
};

class CUIWorldSelect {
public:
    MEMBER_HOOK(void, 0x006283C1, UserLimitResult, int bOverUserLimit, int bPopulateLevel)
};

void CUIWorldSelect::UserLimitResult_hook(int bOverUserLimit, int bPopulateLevel) {
    UserLimitResult(this, bOverUserLimit, bPopulateLevel);

    auto* pChannelSelect = CUIChannelSelect::GetInstance();
    if (pChannelSelect && CWndMan::GetInstance()) {
        CWndMan::GetInstance()->SetFocus(&pChannelSelect->m_handler);
    }
}

// ============================================================================
// System: CLogin stability (heap sub-step + focus)
// ============================================================================

class CLogin {
public:
    MEMBER_AT(int, 0x23C, m_bSubStepChanged)
    MEMBER_AT(int, 0x238, m_nSubStep)
    MEMBER_HOOK(CLogin*, 0x005F3C59, Constructor)
};

CLogin* CLogin::Constructor_hook() {
    auto* ret = Constructor(this);

    this->m_bSubStepChanged = 0;
    this->m_nSubStep = 0;

    // Removed the keybd_event(VK_MENU) "ALT foreground bypass": synthesizing Alt
    // here could leave the key stuck down (auto-Alt / auto-jump). A plain
    // SetForegroundWindow is harmless — it just no-ops if the foreground is locked.
    if (CWvsApp::GetInstance()) {
        SetForegroundWindow(CWvsApp::GetInstance()->m_hWnd);
    }

    return ret;
}

void AttachQuickLoginMod() {
    ATTACH_HOOK(CUIWorldSelect::UserLimitResult, CUIWorldSelect::UserLimitResult_hook);
    ATTACH_HOOK(CLogin::Constructor, CLogin::Constructor_hook);
    std::cout << "[quicklogin] AttachQuickLoginMod OK" << std::endl;
}

void AttachAllowCashTradeMod() {
    // Allow Cash Items to be Traded (GMS v083)
    Memory::PatchNop(0x004F3FB8, 6);
    Memory::PatchNop(0x004F3FC4, 6);
    std::cout << "[quicklogin] AttachAllowCashTradeMod OK (NOP @4F3FB8/4F3FC4)" << std::endl;
}
