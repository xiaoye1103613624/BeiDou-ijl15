#pragma once
#include "Client.h"
#include "ztl/ztl.h"

inline IWzGr2DPtr& get_gr() {
    return *reinterpret_cast<IWzGr2DPtr*>(0x00BF14EC);
}

inline IWzResManPtr& get_rm() {
    return *reinterpret_cast<IWzResManPtr*>(0x00BF14E8);
}

inline IWzNameSpacePtr& get_root() {
    return *reinterpret_cast<IWzNameSpacePtr*>(0x00BF14E0);
}

inline int get_int32(Ztl_variant_t v, int nDefault) {
    Ztl_variant_t vInt;
    if (V_VT(&v) == VT_EMPTY || V_VT(&v) == VT_ERROR || FAILED(ZComAPI::ZComVariantChangeType(&vInt, &v, 0, VT_I4))) {
        return nDefault;
    }
    return V_I4(&vInt);
}

IUnknownPtr* __cdecl get_unknown_hook(IUnknownPtr* result, Ztl_variant_t& v);
inline IUnknownPtr get_unknown(Ztl_variant_t v) {
    IUnknownPtr pUnk;
    Ztl_variant_t mutableV = v;
    get_unknown_hook(std::addressof(pUnk), mutableV);
    return pUnk;
}

// Resolution lives in Client::m_nGameWidth/Height (patched at startup).
// Do NOT read 0x009F707E/7082 — those are code-section immediates; the old
// get_screen_height() used 0x009F7082 (width+4) and returned garbage Y.
inline int get_screen_width() {
    return Client::m_nGameWidth;
}

inline int get_screen_height() {
    return Client::m_nGameHeight;
}