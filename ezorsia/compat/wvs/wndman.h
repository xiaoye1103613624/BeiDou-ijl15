#pragma once

#include "../hook.h"
#include "wnd.h"
#include "ztl/ztl.h"

class CWndMan : public TSingleton<CWndMan, 0x00BEC20C> {
public:
    MEMBER_HOOK(int, 0x009E7D77, TranslateMessageImpl, UINT& msg, WPARAM& wParam, LPARAM& lParam, LRESULT* plResult);

    void SetFocus(IUIMsgHandler* pHandler) {
        reinterpret_cast<void(__thiscall*)(CWndMan*, IUIMsgHandler*)>(0x009E3264)(this, pHandler);
    }
};
