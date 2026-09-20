#include "pch.h"
#include "itemeff.h"
#include "weapontint.h"
#include "debug.h"
#include "compat/hook.h"
#include "compat/wvs/avatar.h"
#include "compat/wvs/iteminfo.h"
#include "compat/wvs/util.h"
#include "compat/wvs/wvsapp.h"
#include "ztl/ztl.h"

#include <map>

// CUser embeds CAvatar at +0x88 (IDA: CUser ctor lea edi,[esi+0x88]; call CAvatar::CAvatar).
#define AVATAR_OFFSET 0x88

namespace {

class CUser {
public:
    MEMBER_AT(CAvatar, AVATAR_OFFSET, m_CAvatar)

    inline int LoadLayer(Ztl_bstr_t bsUOL, int bLeft, USERLAYER& l, int* pnRepeatStartIndex) {
        return reinterpret_cast<int(__thiscall*)(CUser*, Ztl_bstr_t, int, USERLAYER&, int*)>(
                0x00941417)(this, bsUOL, bLeft, l, pnRepeatStartIndex);
    }
};

std::map<int, IWzPropertyPtr> g_mPropItemEffect;

// --- CustomData injection (hijack int m_bBlinking @ +0x484) -----------------------------
// Stock CAvatar stores an int blink flag at +0x484. We replace it with a heap CustomData*
// so aItemEffectLayer[] can live with the avatar. Blink sites that compared the int must
// be patched to dereference the pointer (same as kaentake avatar.cpp).

void __fastcall CAvatar_Constructor_Hook(CAvatar* pThis, void* /*edx*/) {
    CAvatar::Constructor(pThis);
    auto* p = new CAvatar::CustomData{};
    // MEMBER_AT(CustomData*, ...) expands set to `const CustomData*&` (macro pitfall);
    // write the hijacked blink slot directly.
    *reinterpret_cast<CAvatar::CustomData**>(reinterpret_cast<uintptr_t>(pThis) + 0x484) = p;
}

void __fastcall CAvatar_Destructor_Hook(CAvatar* pThis, void* /*edx*/) {
    auto** pp = reinterpret_cast<CAvatar::CustomData**>(reinterpret_cast<uintptr_t>(pThis) + 0x484);
    delete *pp;
    *pp = nullptr;
    CAvatar::Destructor(pThis);
}

void __fastcall CAvatar_RegisterNextBlink_Hook(CAvatar* pThis, void* /*edx*/) {
    if (auto* p = pThis->m_pCustomData) {
        p->bBlinking = 0;
    }
    if (CWvsApp::IsInstantiated()) {
        pThis->m_tNextBlink =
                CWvsApp::GetInstance()->m_tUpdateTime + (rand() % 3000) + 2000;
    }
}

// CAvatar::Update sites that compared [ebx+0x484] as int — now [ [ebx+0x484] ].
uintptr_t g_CAvatarUpdate_ret1 = 0x004534D2;
void __declspec(naked) CAvatar_Update_Hook1() {
    __asm {
        mov     ecx, [ebx + 0x484]
        cmp     [ecx], edi
        jmp     [g_CAvatarUpdate_ret1]
    }
}

uintptr_t g_CAvatarUpdate_ret2 = 0x0045361C;
void __declspec(naked) CAvatar_Update_Hook2() {
    __asm {
        mov     eax, [ebx + 0x484]
        mov     dword ptr [eax], 1
        jmp     [g_CAvatarUpdate_ret2]
    }
}

// --- ItemEff property cache + per-avatar layers ---------------------------------------

auto CItemInfo_IterateItemInfo = reinterpret_cast<int(__thiscall*)(CItemInfo*)>(0x005CA71C);

int __fastcall CItemInfo_IterateItemInfo_Hook(CItemInfo* pThis, void* /*edx*/) {
    IWzPropertyPtr pItemEff = get_rm()->GetObjectA(L"Effect/ItemEff.img").GetUnknown();
    if (pItemEff) {
        IEnumVARIANTPtr pEnum;
        try {
            pEnum = pItemEff->_NewEnum;
        } catch (...) {
            pEnum = nullptr;
        }
        while (pEnum) {
            Ztl_variant_t vNext;
            ULONG uCeltFetched = 0;
            if (FAILED(pEnum->Next(1, &vNext, &uCeltFetched)) || uCeltFetched == 0) {
                break;
            }
            Ztl_bstr_t sNext = V_BSTR(&vNext);
            const int nItemID = static_cast<int>(wcstol(sNext.GetBSTR(), nullptr, 10));
            IWzPropertyPtr pProp = pItemEff->item[sNext].GetUnknown();
            if (!pProp) {
                continue;
            }
            IWzPropertyPtr pEffect;
            IUnknownPtr pUnknown = pProp->item[L"effect"].GetUnknown();
            if (SUCCEEDED(pUnknown.QueryInterface(__uuidof(IWzProperty), &pEffect)) && pEffect) {
                g_mPropItemEffect[nItemID] = pEffect;
            }
        }
    }
    return CItemInfo_IterateItemInfo(pThis);
}

void UpdateItemEff(CUser* pUser) {
    if (!pUser) {
        return;
    }
    CAvatar* pAvatar = &pUser->m_CAvatar;
    auto* pCustom = pAvatar->m_pCustomData;
    if (!pCustom) {
        return;
    }

    for (int i = 0; i < AVATAR_EQUIP_SLOTS; ++i) {
        const int nItemID = pAvatar->m_avatarLook.anHairEquip[i];
        auto* pItemEffectLayer = &pCustom->aItemEffectLayer[i];
        auto search = g_mPropItemEffect.find(nItemID);
        if (search == g_mPropItemEffect.end()) {
            pItemEffectLayer->Reset();
            continue;
        }

        int bFlip = 0;
        try {
            bFlip = pAvatar->m_pLayerUnderFace->flip;
        } catch (...) {
            bFlip = 0;
        }
        const int nAction = pAvatar->GetCurrentAction(nullptr);
        if (pItemEffectLayer->nItemID == nItemID && pItemEffectLayer->nAction == nAction &&
            (!pItemEffectLayer->l.bFixed && pItemEffectLayer->bFlip == bFlip)) {
            continue;
        }
        pItemEffectLayer->nItemID = nItemID;
        pItemEffectLayer->nAction = nAction;
        pItemEffectLayer->bFlip = bFlip;

        Ztl_bstr_t sActionName;
        reinterpret_cast<Ztl_bstr_t*(__cdecl*)(Ztl_bstr_t*, int)>(0x004A8CE6)(&sActionName, nAction);
        IWzPropertyPtr pEffect = search->second;
        Ztl_variant_t vAction = pEffect->item[sActionName];
        wchar_t sUOL[1024];
        swprintf_s(sUOL, L"Effect/ItemEff.img/%d/effect/%ls", nItemID,
                   V_VT(&vAction) == VT_EMPTY ? L"default" : sActionName.GetBSTR());

        // Swap tinted clones into the WZ tree for the duration of LoadLayer only.
        WeaponTint_BeginItemEffSwap(pAvatar, nItemID);
        const bool bLoaded = pUser->LoadLayer(sUOL, bFlip, pItemEffectLayer->l, nullptr) != 0;
        WeaponTint_EndItemEffSwap();
        if (bLoaded && pItemEffectLayer->l.pLayer) {
            try {
                pItemEffectLayer->l.pLayer->Animate(GA_REPEAT);
            } catch (...) {
            }
        }
    }
}

auto CUser_UpdateAdditionalLayer = reinterpret_cast<void(__thiscall*)(CUser*)>(0x00940EB7);

void __fastcall CUser_UpdateAdditionalLayer_Hook(CUser* pThis, void* /*edx*/) {
    CUser_UpdateAdditionalLayer(pThis);
    UpdateItemEff(pThis);
}

auto CUser_OnAvatarModified = reinterpret_cast<void(__thiscall*)(void*)>(0x0092E916);

void __fastcall CUser_OnAvatarModified_Hook(void* pEcX, void* /*edx*/) {
    CUser_OnAvatarModified(pEcX);
    UpdateItemEff(reinterpret_cast<CUser*>(reinterpret_cast<uintptr_t>(pEcX) - AVATAR_OFFSET));
}

auto CUser_SetMoveAction = reinterpret_cast<void(__thiscall*)(void*, int, int)>(0x0092ECD1);

void __fastcall CUser_SetMoveAction_Hook(void* pEcX, void* /*edx*/, int nMA, int bReload) {
    CUser_SetMoveAction(pEcX, nMA, bReload);
    UpdateItemEff(reinterpret_cast<CUser*>(reinterpret_cast<uintptr_t>(pEcX) - AVATAR_OFFSET));
}

} // namespace

void ItemEff_Invalidate(void* pAvatarRaw) {
    auto* pAvatar = reinterpret_cast<CAvatar*>(pAvatarRaw);
    if (!pAvatar) {
        return;
    }
    __try {
        auto* pCustom = pAvatar->m_pCustomData;
        if (!pCustom) {
            return;
        }
        for (int i = 0; i < AVATAR_EQUIP_SLOTS; ++i) {
            // -1 rather than 0: empty slot really is 0 and would still match.
            pCustom->aItemEffectLayer[i].nItemID = -1;
            pCustom->aItemEffectLayer[i].nAction = -1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void AttachItemEffectMod() {
    ATTACH_HOOK(CAvatar::Constructor, CAvatar_Constructor_Hook);
    ATTACH_HOOK(CAvatar::Destructor, CAvatar_Destructor_Hook);
    ATTACH_HOOK(CAvatar::RegisterNextBlink, CAvatar_RegisterNextBlink_Hook);
    PatchJmp(0x004534CC, CAvatar_Update_Hook1);
    PatchJmp(0x00453612, CAvatar_Update_Hook2);

    ATTACH_HOOK(CItemInfo_IterateItemInfo, CItemInfo_IterateItemInfo_Hook);
    ATTACH_HOOK(CUser_UpdateAdditionalLayer, CUser_UpdateAdditionalLayer_Hook);
    ATTACH_HOOK(CUser_OnAvatarModified, CUser_OnAvatarModified_Hook);
    ATTACH_HOOK(CUser_SetMoveAction, CUser_SetMoveAction_Hook);

    LOG_ONCE("[ColoringPrism] ItemEff + CustomData hooks attached");
}
