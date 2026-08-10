#pragma once

#include <string>

#include "ztl/ztl.h"

IWzPropertyPtr OpenMapPropertyById(int mapId);

std::string GetMapById(int mapId);

std::string GetMobNameById(int mobId);

int GetMobLevelById(int mobId);

struct MobCombatInfo {
    int level = 0;
    int paDamage = 0;
    int pdDamage = 0;
    int maDamage = 0;
    int mdDamage = 0;
    int acc = 0;
    int eva = 0;
    std::string elemAttr;
};

MobCombatInfo GetMobCombatInfoById(int mobId);

std::string GetNpcById(int npcId);
