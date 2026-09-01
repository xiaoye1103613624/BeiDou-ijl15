#include "stdafx.h"
#include "ModRegistry.h"
#include "PacketDispatcher.h"
#include "WzBridge.h"
#include "../Client.h"
#include "../HpMpAlert.h"
#include "../WorldMapInfo.h"
#include "../damagerank/DamageRank.h"
#include "../damageskin/DamageSkinApi.h"
#include "../beautyshop/BeautyShopApi.h"
#include "../dailycheckin/DailyCheckinApi.h"
#include "../miraclecube/MiracleCubeApi.h"
#include "../storagebag/StorageBagApi.h"
#include "../fusionanvil/FusionAnvilApi.h"
#include "../higherstoragelist/HigherStorageListApi.h"
#include "../potentialscroll/PotentialScrollApi.h"
#include "../userinfodetail/UserInfoDetailApi.h"
#include "../dropitemaura/DropItemAuraApi.h"
#include "../setitem/SetItemApi.h"
#include "../partybuffs/PartyBuffsApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../slotlock/SlotLockApi.h"
#include "../sidetoolbar/SideToolbarApi.h"
#include "../equipaddon/EquipAddonApi.h"
#include "../cashshop/CashShopApi.h"
#include "../equipgrowth/EquipGrowthApi.h"
#include "../coloringprism/ColoringPrismApi.h"
#include "../weather/WeatherApi.h"
#include "../newchardice/NewCharDiceApi.h"
#include "../invexpand/InvExpandApi.h"
#include "../windowtitle/WindowTitleApi.h"
#include "../combatpower/CombatPowerApi.h"

namespace {
std::vector<CompatModule> g_modules;
bool g_initialized = false;
bool g_builtinsRegistered = false;

void RegisterHpMpAlertModule() {
    CompatModule module{};
    module.name = "HpMpAlert";
    module.onAttach = []() {
        RegisterHpMpAlertPacketHandler();
    };
    ModRegistry::RegisterModule(std::move(module));
}
} // namespace

void ModRegistry::RegisterModule(CompatModule module) {
    g_modules.push_back(std::move(module));
}

void ModRegistry::RegisterBuiltins() {
    // IMPORTANT: do NOT gate on g_modules.empty().
    // dllmain calls rs_register() at boot and pre-inserts "Resolution".
    // The old empty-check skipped EVERY feature module (PotentialScroll /
    // MiracleCube / FusionAnvil / Beauty / …) → cubes/scrolls dead + unstable.
    if (g_builtinsRegistered) {
        return;
    }
    g_builtinsRegistered = true;
    RegisterHpMpAlertModule();

    CompatModule windowTitle{};
    windowTitle.name = "WindowTitle";
    windowTitle.onAttach = []() {
        WindowTitle::RegisterPacketHandler();
    };
    windowTitle.onTick = []() {
        WindowTitle::OnTick();
    };
    ModRegistry::RegisterModule(std::move(windowTitle));

    CombatPower::RegisterModule();

    // Keep in sync with LazyCompatInit.cpp / dllmain.cpp. 0 = full late UI.
#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

#if !BISECT_DISABLE_LATE_UI_HOOKS
    // WorldMap tooltip — re-enabled for S9 (server WORLD_MAP_PLAYERS already wired).
    if (!Client::disableWorldMap) {
        CompatModule worldMap{};
        worldMap.name = "WorldMapInfo";
        worldMap.onAttach = []() {
            WorldMapInfo::RegisterPacketHandler();
            WorldMapInfo::AttachHooks();
        };
        worldMap.onTick = []() {
            WorldMapInfo::TickTooltip();
        };
        ModRegistry::RegisterModule(std::move(worldMap));
    } else {
        std::cout << "[ModRegistry] WorldMapInfo skipped (enter MapHelper bisect)" << std::endl;
    }

    CompatModule damageRank{};
    damageRank.name = "DamageRank";
    damageRank.onAttach = []() {
        DamageRank::RegisterPacketHandler();
        DamageRank::AttachHooks();
    };
    damageRank.onTick = []() {
        DamageRank::OnTick();
    };
    ModRegistry::RegisterModule(std::move(damageRank));

    CompatModule damageSkin{};
    damageSkin.name = "DamageSkin";
    damageSkin.onAttach = []() {
        DamageSkin::RegisterPacketHandler();
        DamageSkin::AttachHooks();
    };
    ModRegistry::RegisterModule(std::move(damageSkin));

    // InvResize module omitted — real InvResize::EnsureHooks enter-red vs green 6D612F01.

#endif

    CompatModule beautyShop{};
    beautyShop.name = "BeautyShop";
    beautyShop.onAttach = []() {
        BeautyShop::RegisterPacketHandler();
    };
    ModRegistry::RegisterModule(std::move(beautyShop));

    CompatModule dailyCheckin{};
    dailyCheckin.name = "DailyCheckin";
    dailyCheckin.onAttach = []() {
        DailyCheckin::RegisterPacketHandler();
    };
    ModRegistry::RegisterModule(std::move(dailyCheckin));

    CompatModule miracleCube{};
    miracleCube.name = "MiracleCube";
    miracleCube.onAttach = []() {
        MiracleCube::RegisterPacketHandler();
    };
    ModRegistry::RegisterModule(std::move(miracleCube));
    CompatModule coloringPrism{};
    coloringPrism.name = "ColoringPrism";
    coloringPrism.onAttach = []() {
        ColoringPrism::RegisterPacketHandler();
    };
    coloringPrism.onTick = []() {
        ColoringPrism_OnTick();
    };
    ModRegistry::RegisterModule(std::move(coloringPrism));


    CompatModule storageBag{};
    storageBag.name = "StorageBag";
    storageBag.onAttach = []() {
        StorageBag::RegisterPacketHandler();
    };
    ModRegistry::RegisterModule(std::move(storageBag));

    CompatModule fusionAnvil{};
    fusionAnvil.name = "FusionAnvil";
    fusionAnvil.onAttach = []() {
        // Packet hooks at DllMain bootstrap; UI hooks via FusionAnvil::EnsureHooks().
    };
    ModRegistry::RegisterModule(std::move(fusionAnvil));

    CompatModule higherStorageList{};
    higherStorageList.name = "HigherStorageList";
    higherStorageList.onAttach = []() {
        HigherStorageList::ApplyPatches();
    };
    ModRegistry::RegisterModule(std::move(higherStorageList));

    CompatModule potentialScroll{};
    potentialScroll.name = "PotentialScroll";
    potentialScroll.onAttach = []() {
        PotentialScroll::ApplyPatches();
    };
    ModRegistry::RegisterModule(std::move(potentialScroll));

#if !BISECT_DISABLE_LATE_UI_HOOKS
    CompatModule userInfoDetail{};
    userInfoDetail.name = "UserInfoDetail";
    userInfoDetail.onAttach = []() {
        UserInfoDetail::RegisterPacketHandler();
        UserInfoDetail::AttachHooks();
    };
    ModRegistry::RegisterModule(std::move(userInfoDetail));
#endif

    CompatModule dropItemAura{};
    dropItemAura.name = "DropItemAura";
    dropItemAura.onAttach = []() {
        // DISABLED: drop-grade aura caused error 38; server no longer sends grade byte.
        // DropItemAura::AttachHooks();
    };
    ModRegistry::RegisterModule(std::move(dropItemAura));

#if !BISECT_DISABLE_LATE_UI_HOOKS
    CompatModule setItem{};
    setItem.name = "SetItem";
    setItem.onAttach = []() {
        SetItem::RegisterPacketHandler();
        SetItem::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(setItem));

    // Growth companion tip. Re-enabled with SetItemUI after enter 0x8007000D bisect:
    // hover-lazy SetToolTip_String2 (does not load GrowthEnabled/Disabled canvases).
    // Config enableGrowthCompanion=false still gates paint/request inside EquipGrowth*.
    CompatModule equipGrowth{};
    equipGrowth.name = "EquipGrowth";
    equipGrowth.onAttach = []() {
        EquipGrowth::RegisterPacketHandler();
        EquipGrowth::EnsureHooks();
        if (!Client::enableGrowthCompanionTip) {
            std::cout << "[ModRegistry] EquipGrowth handlers on; UI gated (enableGrowthCompanion=false)"
                      << std::endl;
        }
    };
    ModRegistry::RegisterModule(std::move(equipGrowth));

    CompatModule partyBuffs{};
    partyBuffs.name = "PartyBuffs";
    partyBuffs.onAttach = []() {
        PartyBuffs::RegisterPacketHandler();
        // Draw/create hooks + subscribe must attach; packet-only left party buff UI blank.
        PartyBuffs::EnsureHooks();
    };
    partyBuffs.onTick = []() {
        PartyBuffs_OnClientTick();
    };
    ModRegistry::RegisterModule(std::move(partyBuffs));

    CompatModule equipCompare{};
    equipCompare.name = "EquipCompare";
    equipCompare.onAttach = []() {
        EquipCompare::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(equipCompare));

    CompatModule slotLock{};
    slotLock.name = "SlotLock";
    slotLock.onAttach = []() {
        SlotLock::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(slotLock));

    CompatModule sideToolbar{};
    sideToolbar.name = "SideToolbar";
    sideToolbar.onAttach = []() {
        // No UI at attach — create only from OnTick after enter-game.
        SideToolbar::RegisterPacketHandler();
    };
    sideToolbar.onTick = []() {
        SideToolbar::OnTick();
    };
    ModRegistry::RegisterModule(std::move(sideToolbar));

    CompatModule cashShopWindow{};
    cashShopWindow.name = "CashShopWindow";
    cashShopWindow.onAttach = []() {
        CashShopWindow::RegisterPacketHandler();
    };
    cashShopWindow.onTick = []() {
        CashShopWnd_Tick();
    };
    ModRegistry::RegisterModule(std::move(cashShopWindow));

    CompatModule invExpand{};
    invExpand.name = "InvExpand";
    invExpand.onAttach = []() {
        // UI Detour attached in LazyCompatInit EnsureHooks (Field enter).
    };
    ModRegistry::RegisterModule(std::move(invExpand));

    CompatModule weather{};
    weather.name = "Weather";
    weather.onAttach = []() {
        Weather_RegisterPacketHandlers();
        AttachWeatherMod();
        AttachWeatherWindMod();
    };
    ModRegistry::RegisterModule(std::move(weather));

    CompatModule newCharDice{};
    newCharDice.name = "NewCharDice";
    newCharDice.onAttach = []() {
        // Layout/packet hooks already applied from DllMain AttachNewCharDiceMod.
    };
    newCharDice.onTick = []() {
        NewCharDice::OnClientTick();
    };
    ModRegistry::RegisterModule(std::move(newCharDice));
#endif
}

void ModRegistry::Initialize() {
    if (g_initialized) {
        return;
    }

    WzBridge::Initialize();
    RegisterBuiltins();

    // No fopen(sidebar_debug) here — green 6D612F01 has no that path; file I/O on
    // FieldInit attach correlated with enter GS families.
    for (const auto& module : g_modules) {
        if (!module.onAttach) {
            continue;
        }
        try {
            module.onAttach();
            std::cout << "[ModRegistry] attached " << module.name << std::endl;
        } catch (...) {
            std::cout << "[ModRegistry] attach FAILED " << module.name << std::endl;
        }
    }

    if (!Client::disablePacketHook) {
        PacketDispatcher::InstallHook(true);
    }
    // Rebind after InstallHook so DirectHandler survives dispatcher init order.
    EquipGrowth::RegisterPacketHandler();
    g_initialized = true;
}

void ModRegistry::OnClientTick() {
    for (const auto& module : g_modules) {
        if (module.onTick) {
            module.onTick();
        }
    }
    // EquipAddon must tick here: BossHP already owns UserLocal::Update;
    // LazyCompat's second SetHook on the same addr often fails, so its
    // EquipAddon::OnTick never ran (equipaddon_debug.log only had EnsureHooks).
    EquipAddon::EnsureHooks();
    EquipAddon::OnTick();
}

std::size_t ModRegistry::ModuleCount() {
    return g_modules.size();
}
