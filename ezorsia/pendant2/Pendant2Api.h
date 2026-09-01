// Second pendant + extended equip UI (stamp ADDON_POLL_LAYER_20260731):
//   - No DllMain UI attach.
//   - When pendant2_ui=true after CField:
//       * AlignClassicDrawToHitTest + classic rings/medal/belt/shoulder
//       * GetSlotXY/HitTest only (NEVER BE27E0/KeyConfig); badge/totem parked (Addon)
//       * Char HT end → BE241C; draw loop-end mov eax,55 (NOT ForceDrawLoop)
//       * Classic PE HitTest[≥50] not written (cash overflow fix)
//       * HitTest remaps also sync shoulders DLL table (tip/dblclick)
//   - Wear/stats/bind in AttachShoulderSlotsFix (shoulders.cpp)
//   - EquipAddon::OnTick via ModRegistry + Pendant2OnClientTick (poll-layer)
//   - NEVER ForceDrawLoop / pet HT@801214 / incomplete cd64 auto-enable
#pragma once

void AttachPendant2SlotsFix();
void EnsurePendant2AfterFieldEnter();
void Pendant2OnClientTick();
void Pendant2EnsureNarrowEquipLayout();
