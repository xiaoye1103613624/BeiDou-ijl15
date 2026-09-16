// OverheadIcons — hide field NPC QuestIcon + local-player QuestAlert bulb.
//
// IDA (Angel.exe / BeiDou client @ 0x400000) 2026-09-16:
//   CNpc::SetQuestList              0x006D1779
//     quest id ZArray @ +0xC8/CC/D0/D4
//     desired icon type @ +0xD8 ; cached @ +0xDC ; layer ZRef @ +0xEC
//     type 3 = none → ZRef clear via sub_428712(@+0xEC, 0)
//     StringPool SP_3190 loads UI/UIWindow.img/QuestIcon/%d
//   CWvsContext::UpdateAutoQuestAlertIcon  0x00A08D30
//     show → sub_95CED9 (Effect/BasicEff.img/QuestAlert on CUserLocal)
//     clear @ 0x95D238: UserLocal+0x317C=0, ZRef@+0x3180=0,
//                       WvsContext+0x379C/0x37BC=0
//   TSingleton<CUserLocal>::ms_pInstance   0x00BEBF98
// World-map QuestIcon markers are NOT hooked (plan: keep worldmap).

#include "stdafx.h"
#include "OverheadIconsApi.h"
#include "compat/hook.h"

#include <atomic>
#include <cstdio>

namespace {

std::atomic<bool> g_suppress{true};
bool g_hooksAttached = false;

constexpr uintptr_t kAddr_SetQuestList = 0x006D1779;
constexpr uintptr_t kAddr_UpdateAutoQuestAlertIcon = 0x00A08D30;
constexpr uintptr_t kAddr_ZArrayRemoveAll_U16 = 0x00500EE6;
constexpr uintptr_t kAddr_ZRefAssign = 0x00428712;
constexpr uintptr_t kAddr_CUserLocal_Instance = 0x00BEBF98;

constexpr int kNpcOff_QuestArr0 = 0xC8;
constexpr int kNpcOff_QuestArr1 = 0xCC;
constexpr int kNpcOff_QuestArr2 = 0xD0;
constexpr int kNpcOff_QuestArr3 = 0xD4;
constexpr int kNpcOff_IconDesired = 0xD8;
constexpr int kNpcOff_IconCached = 0xDC;
constexpr int kNpcOff_IconLayer = 0xEC;
constexpr int kIconTypeNone = 3;

constexpr int kUserOff_QuestAlertFlag = 0x317C;
constexpr int kUserOff_QuestAlertLayer = 0x3180;
constexpr int kCtxOff_QuestAlertA = 0x379C;
constexpr int kCtxOff_QuestAlertB = 0x37BC;

using SetQuestList_t = void(__thiscall*)(void* self);
using UpdateAutoQuestAlertIcon_t = void(__thiscall*)(void* self);
using ZArrayRemoveAll_t = void(__thiscall*)(void* self);
using ZRefAssign_t = int*(__thiscall*)(void* self, int newVal);

SetQuestList_t g_origSetQuestList =
        reinterpret_cast<SetQuestList_t>(kAddr_SetQuestList);
UpdateAutoQuestAlertIcon_t g_origUpdateAutoQuestAlertIcon =
        reinterpret_cast<UpdateAutoQuestAlertIcon_t>(kAddr_UpdateAutoQuestAlertIcon);

void OverheadLog(const char* msg) {
    // Keep quiet on hot paths; one-shot attach log only.
    static bool once = false;
    if (!once) {
        once = true;
        std::printf("[OverheadIcons] %s\n", msg);
    }
}

void ClearNpcQuestIcon(void* npc) {
    if (!npc) {
        return;
    }
    char* base = static_cast<char*>(npc);
    auto removeAll = reinterpret_cast<ZArrayRemoveAll_t>(kAddr_ZArrayRemoveAll_U16);
    removeAll(base + kNpcOff_QuestArr0);
    removeAll(base + kNpcOff_QuestArr1);
    removeAll(base + kNpcOff_QuestArr2);
    removeAll(base + kNpcOff_QuestArr3);

    *reinterpret_cast<int*>(base + kNpcOff_IconDesired) = kIconTypeNone;
    if (*reinterpret_cast<int*>(base + kNpcOff_IconCached) != kIconTypeNone) {
        *reinterpret_cast<int*>(base + kNpcOff_IconCached) = kIconTypeNone;
        auto zref = reinterpret_cast<ZRefAssign_t>(kAddr_ZRefAssign);
        zref(base + kNpcOff_IconLayer, 0);
    }
}

void ClearLocalQuestAlert(void* ctx) {
    void* user = *reinterpret_cast<void**>(kAddr_CUserLocal_Instance);
    if (!user || !ctx) {
        return;
    }
    char* u = static_cast<char*>(user);
    char* c = static_cast<char*>(ctx);
    *reinterpret_cast<int*>(u + kUserOff_QuestAlertFlag) = 0;
    auto zref = reinterpret_cast<ZRefAssign_t>(kAddr_ZRefAssign);
    zref(u + kUserOff_QuestAlertLayer, 0);
    *reinterpret_cast<int*>(c + kCtxOff_QuestAlertA) = 0;
    *reinterpret_cast<int*>(c + kCtxOff_QuestAlertB) = 0;
}

void __fastcall Hook_SetQuestList(void* self, void* /*edx*/) {
    if (!g_suppress.load(std::memory_order_relaxed)) {
        g_origSetQuestList(self);
        return;
    }
    ClearNpcQuestIcon(self);
}

void __fastcall Hook_UpdateAutoQuestAlertIcon(void* self, void* /*edx*/) {
    if (!g_suppress.load(std::memory_order_relaxed)) {
        g_origUpdateAutoQuestAlertIcon(self);
        return;
    }
    ClearLocalQuestAlert(self);
}

} // namespace

namespace OverheadIcons {

void SetSuppressFieldIcons(bool suppress) {
    g_suppress.store(suppress, std::memory_order_relaxed);
}

bool IsSuppressFieldIcons() {
    return g_suppress.load(std::memory_order_relaxed);
}

void AttachHooks() {
    if (g_hooksAttached) {
        return;
    }
    g_hooksAttached = true;
    ATTACH_HOOK(g_origSetQuestList, Hook_SetQuestList);
    ATTACH_HOOK(g_origUpdateAutoQuestAlertIcon, Hook_UpdateAutoQuestAlertIcon);
    OverheadLog("hooks attached (NPC QuestIcon + local QuestAlert suppressed)");
}

} // namespace OverheadIcons
