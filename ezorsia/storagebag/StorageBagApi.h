#pragma once

class CompatInPacket;

constexpr unsigned short kBagWindowSendOpcode = 0x3724;
constexpr unsigned short kBagWindowRecvOpcode = 0x3725;

void AttachStorageBagMod();
void BagWindow_Toggle();
void BagWindow_Close();
bool BagWindow_IsOpen();
bool StorageBag_HandleMouseMessage(unsigned int& msg, unsigned long wParam, long lParam, long* plResult);
// Used by slotlock: right-click deposit when bag window is open. Returns true if consumed.
bool BagWindow_DepositFromInventory(int invType, int slot);

namespace StorageBag {
void HandleServerPacket(CompatInPacket* packet);
void RegisterPacketHandler();
void EnsureHooks();
}
