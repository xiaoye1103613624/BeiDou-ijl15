#pragma once

class CompatInPacket;

constexpr unsigned short kDailyCheckinOpcode = 0x17C;

void AttachDailyCheckinMod();
// Request snapshot open (same effect as @签到 / @daily).
void DailyCheckin_RequestOpen();
void DailyCheckin_Close();
void DailyCheckin_Toggle();
bool DailyCheckin_IsOpen();

namespace DailyCheckin {
void HandleServerPacket(CompatInPacket* packet);
void RegisterPacketHandler();
void EnsureHooks();
}
