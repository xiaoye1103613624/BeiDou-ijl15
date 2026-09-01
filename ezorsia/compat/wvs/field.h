#pragma once
#include "compat/hook.h"
#include "wvs/stage.h"
#include "wvs/wnd.h"
#include "ztl/ztl.h"


class CMapLoadable : public CStage {
public:
    MEMBER_AT(IWzPropertyPtr, 0x2C, m_pPropFieldInfo)
    // Field BODY (not the same object as m_pPropFieldInfo). info/link maps keep +0x2C
    // on the linking map while +0x30 becomes the target's body.
    MEMBER_AT(IWzPropertyPtr, 0x30, m_pPropField)
    MEMBER_AT(RECT, 0xF0, m_rcViewRange)
    MEMBER_HOOK(void, 0x00641EF1, RestoreViewRange) // resolution.cpp / rs.cpp

    // weather.cpp (S9 IDA: LoadMap call @0x00529BC6, Update call @0x00534B0F)
    MEMBER_HOOK(void, 0x00639B3D, LoadMap)
    MEMBER_HOOK(void, 0x00641C28, Update)
    MEMBER_HOOK(void, 0x0063A100, RestoreTile)
    MEMBER_HOOK(void, 0x0063AA7E, RestoreObj)
    MEMBER_HOOK(void, 0x0063CBBA, RestoreBack)
    MEMBER_HOOK(void*, 0x0063CD4E, MakeBack, int nIndex, void* pProp)

    // lamps.cpp (MakeObj call @0x0063ACB5 inside RestoreObj)
    MEMBER_HOOK(void*, 0x0063AD16, MakeObj, int nLayer, IWzProperty* pObjProp)
};


class CField : public CMapLoadable {
public:
    MEMBER_AT(IWzGr2DLayerPtr, 0x15C, m_pLayerObject)
    MEMBER_AT(IWzGr2DLayerPtr, 0x174, m_pLayerBack)
    MEMBER_AT(IWzGr2DLayerPtr, 0x180, m_pLayerFront)
    // ShowMobHPTag stores gage layer at this+0x1E4 (IDA: *((_DWORD*)this+121)).
    MEMBER_AT(IWzGr2DLayerPtr, 0x1E4, m_pLayerHPTag)
    MEMBER_AT(ZRef<CWnd>, 0x1C8, m_pClock) // ZRef<CClock>

    inline static auto OnKey =
        reinterpret_cast<void(__thiscall*)(CField*, unsigned int, int)>(0x00529968);
};


// weather.cpp: capture NPC layers for night tint (S9 IDA: call @0x006D97A3)
class CNpcPool {
public:
    MEMBER_HOOK(void, 0x006D9993, OnNpcEnterField, void* pPacket)
};


inline CField* get_field() {
    return reinterpret_cast<CField*(__cdecl*)()>(0x00437A0C)();
}
