#pragma once

class CompatInPacket;

// Extension weapontint path (Coloring Prism item 5782000).
// Recv 0x372F = WEAPON_TINT_SYNC; Send 0x372E = WEAPON_TINT_ACTION.
constexpr unsigned short kColoringPrismRecvOpcode = 0x372F;
constexpr unsigned short kColoringPrismSendOpcode = 0x372E;

// Legacy EquipDye opcodes (kept for reference; unused by weapontint path):
// constexpr unsigned short kLegacyColoringPrismRecv = 0x184;
// constexpr unsigned short kLegacyColoringPrismSend = 0x11D;

void AttachColoringPrismMod();
void AttachColoringPrismHostHooks();

void ColoringPrism_RequestOpen();
void ColoringPrism_OpenWindow();
void ColoringPrism_CloseWindow();
bool ColoringPrism_IsOpen();
void ColoringPrism_HandleServerPacket(CompatInPacket* packet);
void ColoringPrism_OnTick();

bool ColorPrism_IsPrismItem(int nItemID);
int ColorPrism_PeekCashItemId(int invPos);
void ColorPrism_OnUse(int nPOS, int nItemID);
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos);
bool ColorPrism_HandleSkillDrop(void* pTo, int skillId);

namespace ColoringPrism {
void RegisterPacketHandler();
void EnsureHooks();
}
