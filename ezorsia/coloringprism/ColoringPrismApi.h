#pragma once

class CompatInPacket;

// Server -> client sync for WeaponTint / Coloring Prism (replaces legacy 0x184).
constexpr unsigned short kColoringPrismRecvOpcode = 0x372F;
// Client -> server action opcode (weapontint.cpp sends this; documented for hosts).
constexpr unsigned short kColoringPrismSendOpcode = 0x372E;

void AttachColoringPrismMod();
void AttachWeaponTintMod();
void WeaponTint_Tick();

// Window dispatch API (see coloringprism.h). Host cash/drop hooks call these.
bool ColorPrism_IsPrismItem(int nItemID);
void ColorPrism_OnUse(int nPOS, int nItemID);
bool ColorPrism_HandleItemDrop(void* pTo, int invType, int invPos);
bool ColorPrism_HandleSkillDrop(void* pTo, int skillId);

void ColoringPrism_OnTick();

namespace ColoringPrism {
void RegisterPacketHandler();
void EnsureHooks();
}
