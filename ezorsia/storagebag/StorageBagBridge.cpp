#include "stdafx.h"
#include "StorageBagApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_storageBagHooksAttached = false;
} // namespace

namespace StorageBag {
void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kBagWindowRecvOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kBagWindowRecvOpcode) {
                    return false;
                }
                StorageBag::HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_storageBagHooksAttached) {
        return;
    }
    g_storageBagHooksAttached = true;
    AttachStorageBagMod();
}
} // namespace
