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
#include "../setitem/SetItemApi.h"
#include "../partybuffs/PartyBuffsApi.h"
#include "../equipcompare/EquipCompareApi.h"
#include "../slotlock/SlotLockApi.h"
#include "../sidetoolbar/SideToolbarApi.h"
#include "../equipaddon/EquipAddonApi.h"
#include "../cashshop/CashShopApi.h"
#include "../equipgrowth/EquipGrowthApi.h"
#include "../coloringprism/ColoringPrismApi.h"
#include "../invresize/InvResizeApi.h"
#include "../deathcount/DeathCountApi.h"
#include "../statdetail/StatDetailExtApi.h"
#include "../monsterbook/MonsterBookApi.h"
#include "../weather/WeatherApi.h"

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
    if (g_builtinsRegistered) {
        return;
    }
    g_builtinsRegistered = true;
    RegisterHpMpAlertModule();

#ifndef BISECT_DISABLE_LATE_UI_HOOKS
#define BISECT_DISABLE_LATE_UI_HOOKS 0
#endif

#if !BISECT_DISABLE_LATE_UI_HOOKS
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

    CompatModule coloringPrism{};
    coloringPrism.name = "ColoringPrism";
    coloringPrism.onAttach = []() {
        ColoringPrism::RegisterPacketHandler();
    };
    coloringPrism.onTick = []() {
        ColoringPrism_OnTick();
    };
    ModRegistry::RegisterModule(std::move(coloringPrism));

    CompatModule invResize{};
    invResize.name = "InvResize";
    invResize.onAttach = []() {
        InvResize::EnsureHooks();
    };
    invResize.onTick = []() {
        InvResize::OnTick();
    };
    ModRegistry::RegisterModule(std::move(invResize));

    CompatModule deathCount{};
    deathCount.name = "DeathCount";
    deathCount.onAttach = []() {
        DeathCount::RegisterPacketHandler();
    };
    deathCount.onTick = []() {
        DeathCount_OnClientTick();
    };
    ModRegistry::RegisterModule(std::move(deathCount));

#if !BISECT_DISABLE_LATE_UI_HOOKS
    CompatModule userInfoDetail{};
    userInfoDetail.name = "UserInfoDetail";
    userInfoDetail.onAttach = []() {
        UserInfoDetail::RegisterPacketHandler();
        UserInfoDetail::AttachHooks();
    };
    ModRegistry::RegisterModule(std::move(userInfoDetail));

    CompatModule setItem{};
    setItem.name = "SetItem";
    setItem.onAttach = []() {
        SetItem::RegisterPacketHandler();
        SetItem::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(setItem));

    CompatModule equipGrowth{};
    equipGrowth.name = "EquipGrowth";
    equipGrowth.onAttach = []() {
        EquipGrowth::RegisterPacketHandler();
        EquipGrowth::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(equipGrowth));

    CompatModule partyBuffs{};
    partyBuffs.name = "PartyBuffs";
    partyBuffs.onAttach = []() {
        PartyBuffs::RegisterPacketHandler();
        PartyBuffs::EnsureHooks();
    };
    partyBuffs.onTick = []() {
        PartyBuffs_OnClientTick();
    };
    ModRegistry::RegisterModule(std::move(partyBuffs));

    CompatModule statDetailExt{};
    statDetailExt.name = "StatDetailExt";
    statDetailExt.onAttach = []() {
        StatDetailExt::EnsureHooks();
    };
    ModRegistry::RegisterModule(std::move(statDetailExt));

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

    // Create UI only from OnTick / EnsureHooks after enter-field (avoid login crash).
    CompatModule sideToolbar{};
    sideToolbar.name = "SideToolbar";
    sideToolbar.onAttach = []() {
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

    CompatModule monsterBook{};
    monsterBook.name = "MonsterBook";
    monsterBook.onAttach = []() {
        MonsterBook_RegisterPacketHandler();
        MonsterBook_AttachHooks();
    };
    monsterBook.onTick = []() {
        MonsterBook_OnTick();
    };
    ModRegistry::RegisterModule(std::move(monsterBook));

    CompatModule weather{};
    weather.name = "Weather";
    weather.onAttach = []() {
        Weather_RegisterPacketHandlers();
        AttachWeatherMod();
        AttachWeatherWindMod();
    };
    ModRegistry::RegisterModule(std::move(weather));
#endif
}

void ModRegistry::Initialize() {
    if (g_initialized) {
        return;
    }

    WzBridge::Initialize();
    RegisterBuiltins();

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
    // LazyCompat's second SetHook on the same addr often fails.
    EquipAddon::EnsureHooks();
    EquipAddon::OnTick();
}

std::size_t ModRegistry::ModuleCount() {
    return g_modules.size();
}
