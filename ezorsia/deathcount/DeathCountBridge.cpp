#include "stdafx.h"
#include "DeathCountApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace DeathCount {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kExpedDeathCountOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kExpedDeathCountOpcode) {
                    return false;
                }
                DeathCount_OnPacket(packet);
                return true; // swallow: vanilla has no handler
            });
}
} // namespace DeathCount
