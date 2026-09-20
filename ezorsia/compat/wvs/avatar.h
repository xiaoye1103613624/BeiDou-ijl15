#pragma once
#include "compat/hook.h"
#include "ztl/ztl.h"
#include <windows.h>


// Real v83 layout: AvatarLook::operator= copies 52 slots (push 0x34), struct end 0x1C5.
// Declaring 60 was internally consistent with a wrong sizeof assert and overrun into
// neighbouring fields / heap (aItemEffectLayer). See coloring-prism INTEGRATION 4.2c.
#ifndef AVATAR_EQUIP_SLOTS
#define AVATAR_EQUIP_SLOTS 52
#endif

#pragma pack(push, 1)
struct AvatarLook : public ZRefCounted {
    unsigned char nGender;                      // +0x0C
    int nSkin;                                  // +0x0D
    int nFace;                                  // +0x11
    int nWeaponStickerID;                       // +0x15
    int anHairEquip[AVATAR_EQUIP_SLOTS];        // +0x19
    int anUnseenEquip[AVATAR_EQUIP_SLOTS];      // +0xE9
    int anPetID[3];                             // +0x1B9
};
#pragma pack(pop)
static_assert(sizeof(AvatarLook) == 0x1C5);


struct USERLAYER {
    enum POSTYPE {
        POS_BODY_ORIGIN = 0x0,
        POS_FACE_ORIGIN = 0x1,
        POS_CENTER = 0x2,
        POS_GROUND_ORIGIN = 0x3,
    };

    int bFixed;
    POSTYPE nPos;
    IWzGr2DLayerPtr pLayer;
};
static_assert(sizeof(USERLAYER) == 0xC);

struct ITEMEFFECTLAYER {
    int nItemID;
    int nAction;
    int bFlip;
    USERLAYER l;

    void Reset() {
        nItemID = 0;
        nAction = 0;
        l.pLayer = nullptr;
    }
};


class CAvatar {
public:
    struct CustomData {
        int bBlinking;
        POINT ptBodyRelMove;
        int nRidingChairID;
        ITEMEFFECTLAYER aItemEffectLayer[AVATAR_EQUIP_SLOTS];
    };

    virtual ~CAvatar() = 0;
    virtual int CanUseBaseHand() = 0;
    virtual int IsEvanJob() = 0;
    virtual void OnAvatarModified() = 0;
    virtual void SetMoveAction(int nMA, int bReload) = 0;
    virtual void PrepareActionLayer(int nActionSpeed, int nWalkSpeed, int bKeyDown) = 0;

    MEMBER_AT(AvatarLook, 0x4, m_avatarLook)
    MEMBER_AT(int, 0x460, m_bForcingAppearance)
    MEMBER_AT(CustomData*, 0x484, m_pCustomData) // Hijack m_bBlinking
    MEMBER_AT(int, 0x488, m_tNextBlink)
    MEMBER_AT(int, 0x4BC, m_nRidingVehicleID)
    MEMBER_AT(IWzVector2DPtr, 0x10B8, m_pBodyOrigin)
    MEMBER_AT(IWzGr2DLayerPtr, 0x10C8, m_pLayerUnderFace)

    // avatar.cpp
    MEMBER_HOOK(void, 0x0044FE6C, Constructor)
    MEMBER_HOOK(void, 0x0045011C, Destructor)
    MEMBER_HOOK(void, 0x00453AA2, RegisterNextBlink)
    MEMBER_HOOK(void, 0x00456E35, SetRidingVehicle, int nVehicleID)

    int GetCurrentAction(int* pnDir) {
        return reinterpret_cast<int(__thiscall*)(CAvatar*, int*)>(0x00451E4C)(this, pnDir);
    }
};