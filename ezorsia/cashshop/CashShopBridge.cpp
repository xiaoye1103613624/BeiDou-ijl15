#include "stdafx.h"
#include "cashshopwnd.h"
#include "CashShopApi.h"
#include "compat/PacketDispatcher.h"
#include "compat/wvs/Packet.h"

namespace {
bool g_cashShopHooksAttached = false;
} // namespace

namespace CashShopWindow {
void HandleServerPacket(CompatInPacket* packet) {
    if (!packet) {
        return;
    }
    ::CashShopWnd_HandleSync(reinterpret_cast<CInPacket*>(packet));
}

void RegisterPacketHandler() {
    PacketDispatcher::RegisterHandler(
            kCashShopWindowSyncOpcode,
            [](void* /*clientSocket*/, CompatInPacket* packet, unsigned short opcode) {
                if (opcode != kCashShopWindowSyncOpcode) {
                    return false;
                }
                HandleServerPacket(packet);
                return true;
            });
}

void EnsureHooks() {
    if (g_cashShopHooksAttached) {
        return;
    }
    g_cashShopHooksAttached = true;
}
} // namespace
