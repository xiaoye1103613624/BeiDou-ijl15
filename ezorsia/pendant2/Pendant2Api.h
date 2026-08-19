// Second pendant + extended equip UI (stamp ADDON_POLL_LAYER_20260731):
//   - No DllMain UI attach.
//   - When pendant2_ui=true after CField:
//       * AlignClassicDrawToHitTest + classic rings/medal/belt/shoulder
//       * GetSlotXY/HitTest/BE27E0: red3/4 (BP52/53); badge/totem parked (Addon)
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
// Always-safe: zero CUIEquip expand deltas (and r,0x21 → 0) so Equip stays
// classic narrow width even when draw-max is raised. Does NOT enable pendant2_ui.
void Pendant2EnsureNarrowEquipLayout();
