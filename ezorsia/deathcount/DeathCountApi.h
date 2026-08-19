#pragma once

class CompatInPacket;

// S2C expedition death-count HUD. 0x3727 is USER_INFO_EX — do not reuse.
constexpr unsigned short kExpedDeathCountOpcode = 0x3728;
// SendOpcode.SET_FIELD — observe only, never consume.
constexpr unsigned short kSetFieldOpcode = 0x007D;

void DeathCount_OnPacket(CompatInPacket* packet);
void DeathCount_OnFieldChange();
void DeathCount_OnClientTick();

namespace DeathCount {
void RegisterPacketHandler();
}
