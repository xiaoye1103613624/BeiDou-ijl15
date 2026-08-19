#include "stdafx.h"
#include "StorageBagApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"
#include "../damagerank/DamageRankInput.h"

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
    // Inventory BAG button clicks go through CWndMan::TranslateMessageImpl
    // (DamageRankInput hook → StorageBag_HandleMouseMessage). Attach here too
    // so bag clicks work even if DamageRank::AttachHooks order/fail changes.
    AttachDamageRankInputHooks();
    AttachStorageBagMod();
}
} // namespace
