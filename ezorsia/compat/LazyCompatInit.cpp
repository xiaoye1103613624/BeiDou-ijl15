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
#include "../setitem/SetItemApi.h"
#include "../setitem/equiptooltip_style.h"
#include "../dropitemaura/DropItemAuraApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../slotlock/SlotLockApi.h"
#include "../sidetoolbar/SideToolbarApi.h"
#include "../cashshop/CashShopApi.h"
#include "../pendant2/Pendant2Api.h"
#include "../shoulders/ShoulderApi.h"
#include "../equipaddon/EquipAddonApi.h"
#include "../vskill/VSkillCastApi.h"
#include "../bounce/BounceSkillApi.h"
#include "../coloringprism/ColoringPrismApi.h"
#include "../invexpand/InvExpandApi.h"
#include "../WorldMapInfo.h"
// EquipGrowth stays off FieldInit. InvResize hooks from ModRegistry onTick.
// BuffTimer auto-inits via BuffTimerAutoInit in BuffTimerMod.cpp (no explicit Attach).

namespace {
constexpr DWORD kCFieldInit = 0x00528DBC;
constexpr DWORD kCUserLocalUpdate = 0x0094A144;

bool g_bootstrapInstalled = false;
bool g_compatInitialized = false;
bool g_clientTickHookInstalled = false;

void InstallClientTickHookOnce();

// 0 = full late UI Attach/EnsureHooks (Skill.wz restored; K hang was Data).
#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

void EnsureInitializedOnce() {
    if (g_compatInitialized) {
        return;
    }
    g_compatInitialized = true;

    // RefreshRate is installed in InstallBootstrapHookOnce (login/char-select safe).
    ModRegistry::Initialize();

#if BISECT_DISABLE_LATE_UI_HOOKS
    // Minimal: ModRegistry onAttach still runs (packet handlers); no WorldMap/BossHP/UI Ensure*.
    InstallClientTickHookOnce();
    return;
#else
    // Force-skip WorldMap on FieldInit for enter 0x8007000D bisect (ring ends MapHelper).
    // Config disableWorldMap=true also works; keep hard skip until enter green.
    if (!Client::disableWorldMap) {
        std::cout << "[LazyCompat] SKIP WorldMap/WorldMapInfo (enter MapHelper bisect)" << std::endl;
    } else {
        std::cout << "[LazyCompat] WorldMap disabled by config" << std::endl;
    }

    // Tip / equip UI hooks FIRST — before BossHP Detours. Historically BossHP::HookInitField
    // re-DetourAttached CField::Init from inside this EnsureInitializedOnce (called from
    // FieldInit) and aborted the rest → SetItem / Potential / Hyper ★ tips never attached.
    std::cout << "[LazyCompat] Ensure* tip/UI hooks begin TIP_BOSSHP_MINIMAP_20260801" << std::endl;
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
    LC_ENSURE("ColoringPrism", ColoringPrism::EnsureHooks());
    // MiracleCube UI left off: UIWindow2 stub has no MiracleCube nodes (avoid cube UI hang).
    // Cash-cube drag still works via PotentialScroll packet path.
    // InvResize: deferred to ModRegistry onTick (not FieldInit).
    LC_ENSURE("StorageBag", StorageBag::EnsureHooks());
    LC_ENSURE("FusionAnvilUI", FusionAnvil::EnsureHooks());
    // Soft-disable FusionAnvil tooltip enter hooks: 2026-08-13 post-StatusBar crash
    // still throw_hr 0x8007000D @ set_stage with RecentWZ ending
    // UI/UIWindow.img/ToolTip/Equip/{Can,Cannot,Dot} + Item/Special/0900 + Map/MapHelper.
    // Keep packet UI; tip Draw/Set hooks re-enable after enter is green.
    // LC_ENSURE("FusionAnvilTip", FusionAnvil::EnsureTooltipHooks());
    std::cout << "[LazyCompat] SKIP FusionAnvilTip (enter 0x8007000D bisect)" << std::endl;
    LC_ENSURE("EquipCompare", EquipCompare::EnsureHooks());
    LC_ENSURE("SlotLock", SlotLock::EnsureHooks());
    // SetItem companion tip (套装属性). Was soft-disabled during enter 0x8007000D
    // bisect; MXD_dev keeps this on — re-enable so set bonus tooltip shows again.
    // Addresses unchanged (kDrawToolTipEquip=0x008ED0D2, kShowItemToolTip=0x008F5B20,
    // kToolTipClear=0x008E6E23; PE-verified on BeiDou.exe 2026-08-30).
    LC_ENSURE("SetItemUI", SetItem::EnsureUiHooks());
    LC_ENSURE("SideToolbar", SideToolbar::EnsureHooks());
    LC_ENSURE("CashShopWindow", CashShopWindow::EnsureHooks());
    LC_ENSURE("InvExpand", InvExpand::EnsureHooks());
    LC_ENSURE("EquipAddon", EquipAddon::EnsureHooks());
    LC_ENSURE("Pendant2", EnsurePendant2AfterFieldEnter());
#undef LC_ENSURE
    std::cout << "[LazyCompat] Ensure* tip/UI hooks done TIP_BOSSHP_MINIMAP_20260801" << std::endl;

    if (!Client::disableBossHP) {
        BossHP::Hook(); // owns UserLocal::Update (+ ModRegistry tick); no CField::Init Detour
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

    // Cap refresh to 60Hz — MUST run after InitializeGr2D (Gr2D* @0xBF14EC).
    // Calling RefreshRate in DllMain races PcCreateObject_IWzPackage → Gr2D E_FAIL
    // ("Failed in finding proper screen mode for Gr2D") on S9. MXD_dev keeps this OFF.
    // Client::RefreshRate();

#if !BISECT_DISABLE_LATE_UI_HOOKS
    // getCharInfo (inventory + equips) arrives on character login, before
    // CField::CField — equip decode + struct size must be ready here.
    FusionAnvil::EnsurePacketHooks();
    // Soft-disable equip tip style hooks at bootstrap (enter 0x8007000D bisect).
    // AttachEquipTooltipStyleHooks();
    std::cout << "[LazyCompat] SKIP AttachEquipTooltipStyleHooks (enter tip bisect)" << std::endl;
    // VSkillCast DISABLED: kIsHerosWillSkill=0x006ED5EC is WRONG on this EXE —
    // bytes there are a C++ ctor (vtable 0xAF04A8), not IsHerosWillSkill.
    // Detour at DllMain → boot AV @ 0x006ED5F1 (ECX=0 / bad this). Re-enable only
    // after IDA confirms the real IsHerosWillSkill VA.
    // AttachVSkillCastMod();
#endif
    // Beginner bounce 1050/1054: DoActiveSkill→DoBoundJump + FlashJump (PE-verified VAs).
    // Outside late-UI bisect so bounce still works if tip hooks are skipped.
    AttachBounceSkillMod();
    // DropItemAura DISABLED: do not insert/consume drop-grade byte (server also stopped sending it).
    // DropItemAura::AttachHooks();
    // EquipAddon UI hooks attach on first CField (EnsureHooks above).

    typedef void(__fastcall* FieldInit_t)(void* pThis, void* edx);
    static auto originalFieldInit = reinterpret_cast<FieldInit_t>(kCFieldInit);

    FieldInit_t hook = [](void* pThis, void* edx) -> void {
        // Origin/adjust_cy MUST be live before SideToolbar CreateWnd, or the icon
        // drifts ~rs_adjust_cy after LT bind (looks "not vertically centered").
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
