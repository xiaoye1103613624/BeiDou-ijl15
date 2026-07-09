#include "stdafx.h"
#include "DailyCheckinApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_dailyCheckinHooksAttached = false;
} // namespace

namespace DailyCheckin {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kDailyCheckinOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kDailyCheckinOpcode) {
                    return false;
                }
                DailyCheckin::HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_dailyCheckinHooksAttached) {
        return;
    }
    g_dailyCheckinHooksAttached = true;
    AttachDailyCheckinMod();
}
} // namespace
