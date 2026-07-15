#pragma once

#include <vector>

class CompatInPacket;

// Party buff icons / HP% / EXP-meso tracker beside CUIPartyHP.
void AttachPartyBuffsMod();
void PartyBuffs_OnClientTick();
void PartyBuffs_UpdateSnapshot(
        int characterId,
        const std::vector<int>& sourceIds,
        const std::vector<int>& remainingTimes,
        const std::vector<int>& totalTimes);
void PartyBuffs_UpdateHpPercent(int characterId, int percent);
void PartyBuffs_SetTrackerVisible(bool visible);
void PartyBuffs_UpdateTracker(int characterId, unsigned long long exp, unsigned long long meso);
void PartyBuffs_UpdateCounts(int characterId, int count, const unsigned char* payload);

namespace PartyBuffs {
void RegisterPacketHandler();
void EnsureHooks();
bool HandleServerPacket(CompatInPacket* packet);
}
