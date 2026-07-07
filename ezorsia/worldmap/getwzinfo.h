#pragma once

#include <string>

#include "ztl/ztl.h"

IWzPropertyPtr OpenMapPropertyById(int mapId);

std::string GetMapById(int mapId);

std::string GetMobNameById(int mobId);

int GetMobLevelById(int mobId);

std::string GetNpcById(int npcId);
