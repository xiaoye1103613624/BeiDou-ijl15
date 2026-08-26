#include "stdafx.h"
#include "LazyCompatInit.h"
#include "ModRegistry.h"
#include "rs/rs.h"
#include "../BossHP.h"
#include "../Client.h"
#include "../damageskin/DamageSkinApi.h"
#include "../beautyshop/BeautyShopApi.h"
#include "../dailycheckin/DailyCheckinApi.h"
#include "../miraclecube/MiracleCubeApi.h"
#include "../storagebag/StorageBagApi.h"
#include "../fusionanvil/FusionAnvilApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../setitem/SetItemApi.h"
#include "../setitem/equiptooltip_style.h"
#include "../slotlock/SlotLockApi.h"
#include "../sidetoolbar/SideToolbarApi.h"
#include "../cashshop/CashShopApi.h"
#include "../pendant2/Pendant2Api.h"
#include "../shoulders/ShoulderApi.h"
#include "../equipaddon/EquipAddonApi.h"
#include "../coloringprism/ColoringPrismApi.h"
#include "../invresize/InvResizeApi.h"
#include "../deathcount/DeathCountApi.h"
#include "../WorldMapInfo.h"

namespace {
constexpr DWORD kCFieldInit = 0x00528DBC;
constexpr DWORD kCUserLocalUpdate = 0x0094A144;

bool g_bootstrapInstalled = false;
bool g_compatInitialized = false;
bool g_clientTickHookInstalled = false;

void InstallClientTickHookOnce();

#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

void EnsureInitializedOnce() {
    if (g_compatInitialized) {
        return;
    }
    g_compatInitialized = true;

    ModRegistry::Initialize();

#if BISECT_DISABLE_LATE_UI_HOOKS
    InstallClientTickHookOnce();
    return;
#else
    if (!Client::disableWorldMap) {
        Client::WorldMap();
        try {
            WorldMapInfo::AttachHooks();
            std::cout << "[LazyCompat] WorldMapInfo::AttachHooks OK modules="
                      << ModRegistry::ModuleCount() << " disableWorldMap=0" << std::endl;
        } catch (...) {
            std::cout << "[LazyCompat] WorldMapInfo::AttachHooks FAILED" << std::endl;
        }
    } else {
        std::cout << "[LazyCompat] WorldMap disabled by config" << std::endl;
    }

    std::cout << "[LazyCompat] Ensure* tip/UI hooks begin" << std::endl;
#define LC_ENSURE(name, call) \
    do { \
        try { \
            call; \
            std::cout << "[LazyCompat] OK " name << std::endl; \
        } catch (...) { \
            std::cout << "[LazyCompat] FAIL " name << std::endl; \
        } \
    } while (0)
    LC_ENSURE("DamageSkin", DamageSkin::EnsureHooks());
    LC_ENSURE("ShoulderUnequip", Shoulder_RehookDblClickUnequipOutermost());
    LC_ENSURE("BeautyShop", BeautyShop::EnsureHooks());
    LC_ENSURE("DailyCheckin", DailyCheckin::EnsureHooks());
    // MiracleCube popup UI off by default (DisablePopup=1); packet path still registers.
    // Cash-cube drag uses PotentialScroll; optional EnsureHooks only if popup enabled later.
    LC_ENSURE("StorageBag", StorageBag::EnsureHooks());
    LC_ENSURE("FusionAnvilUI", FusionAnvil::EnsureHooks());
    LC_ENSURE("FusionAnvilTip", FusionAnvil::EnsureTooltipHooks());
    LC_ENSURE("EquipCompare", EquipCompare::EnsureHooks());
    LC_ENSURE("SlotLock", SlotLock::EnsureHooks());
    LC_ENSURE("SetItemUI", SetItem::EnsureUiHooks());
    LC_ENSURE("SideToolbar", SideToolbar::EnsureHooks());
    LC_ENSURE("CashShopWindow", CashShopWindow::EnsureHooks());
    LC_ENSURE("ColoringPrism", ColoringPrism::EnsureHooks());
    LC_ENSURE("InvResize", InvResize::EnsureHooks());
    LC_ENSURE("EquipAddon", EquipAddon::EnsureHooks());
    LC_ENSURE("Pendant2", EnsurePendant2AfterFieldEnter());
#undef LC_ENSURE
    std::cout << "[LazyCompat] Ensure* tip/UI hooks done" << std::endl;

    if (!Client::disableBossHP) {
        BossHP::Hook();
    } else {
        InstallClientTickHookOnce();
    }
#endif
}

void InstallClientTickHookOnce() {
    if (g_clientTickHookInstalled) {
        return;
    }
    g_clientTickHookInstalled = true;

    typedef void(__fastcall* UserLocalUpdate_t)(void* pThis, void* edx);
    static auto originalUpdate = reinterpret_cast<UserLocalUpdate_t>(kCUserLocalUpdate);

    UserLocalUpdate_t hook = [](void* pThis, void* edx) -> void {
        originalUpdate(pThis, edx);
        ModRegistry::OnClientTick();
        Pendant2OnClientTick();
        EquipAddon::OnTick();
    };

    Memory::SetHook(true, reinterpret_cast<void**>(&originalUpdate), hook);
}

void InstallBootstrapHookOnce() {
    if (g_bootstrapInstalled) {
        return;
    }
    g_bootstrapInstalled = true;

    // RefreshRate intentionally OFF here: writing IWzGr2D+0x84 caused E_FAIL on
    // this host's green slim ijl15 path. Re-enable only after local A/B.

#if !BISECT_DISABLE_LATE_UI_HOOKS
    FusionAnvil::EnsurePacketHooks();
    AttachEquipTooltipStyleHooks();
#endif

    typedef void(__fastcall* FieldInit_t)(void* pThis, void* edx);
    static auto originalFieldInit = reinterpret_cast<FieldInit_t>(kCFieldInit);

    FieldInit_t hook = [](void* pThis, void* edx) -> void {
        // Char-select → field: one flush before map+NPC canvas alloc (not mid/post).
        // IDA: CField::Init@0x528DBC (existing LazyCompat hook — no new VA).
        // 2026-08-21: mid/post FlushCachedObjects(0) during Init freed live UI/char
        // refs → SET_FIELD AV at 0xA292F9. Keep single pre-flush only.
        rs_resman_flush_cached(0);
        rs_on_enter_field();
        EnsureInitializedOnce();
        if (!Client::disableBossHP) {
            BossHP::OnFieldEnter();
        }
        originalFieldInit(pThis, edx);
    };

    Memory::SetHook(true, reinterpret_cast<void**>(&originalFieldInit), hook);
}
} // namespace

void LazyCompatInit::InstallBootstrapHook() {
    InstallBootstrapHookOnce();
}

void LazyCompatInit::EnsureInitialized() {
    EnsureInitializedOnce();
}
