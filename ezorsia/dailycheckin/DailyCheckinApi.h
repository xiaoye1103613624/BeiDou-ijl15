#pragma once

class CompatInPacket;

constexpr unsigned short kDailyCheckinOpcode = 0x17C;

void AttachDailyCheckinMod();

namespace DailyCheckin {
void HandleServerPacket(CompatInPacket* packet);
void RegisterPacketHandler();
void EnsureHooks();
}
