#include "stdafx.h"
#include "UserInfoDetailApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace UserInfoDetail {
void RegisterPacketHandler();
void HandleServerPacket(CompatInPacket* packet);

void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kUserInfoExRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kUserInfoExRecvOpcode) {
                    return false;
                }
                HandleServerPacket(packet);
                return true;
            });
}
} // namespace UserInfoDetail
