#pragma once

#include "cashshopwnd.h"

class CompatInPacket;

constexpr unsigned short kCashShopWindowSyncOpcode = kCashShopSyncOpcode;

namespace CashShopWindow {
void RegisterPacketHandler();
void EnsureHooks();
void HandleServerPacket(CompatInPacket* packet);
}
