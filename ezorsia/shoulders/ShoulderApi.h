#pragma once

// Shoulder 115 / BP20 / -20: path + bodypart + combat NOP + IsAbleToWear (call-orig).
// Second pendant 112 / BP51 / -51: mirrored from shoulder —
//   is_correct 17|51, get_bodypart {17,51}, AbleToWear call-orig-then-OK,
//   BP51 TSecType fail gates NOP'd (red overlay + combat/localstat skip).
// Also: belt 113 accepts BP50 (轮回碑石).
// Rings 111: is_correct 12/13/15/16/52/53; AbleToWear call-orig-then-OK;
//   get_bodypart {12,13,15,16,52,53} ON (RING_ICON_ONESHOT_20260726z) —
//   draw loop max fixed to BP53 in pendant2.cpp.
//   Shoulder UI → red8 in pendant2.cpp.
// Stamp: FIX_DBLCLICK_UNEQUIP_20260727ah —
//   −52/−53 bind + DrawHasItem/ForceNormal from ag; plus OnDoubleClicked equipped
//   path → wear(slot,−slot) unequip (multi-BP empty-search abort); BP53 occupancy
//   via g_ring53ZRef.
// Stamp: FIX_RING_EMPTY34_20260727ai —
//   bag dblclick: restore ring empty-search count (PE smash→1); get_bodypart
//   order {52,53,12,13,15,16}; server prefer empty −52/−53 over replace.
// Stamp: FIX_RING_UNEQUIP_PERSIST_20260727aj —
//   unequip: jmp native @4F0B89 (not call wear+join 4F0B98 mid-imm);
//   login apply: slot max 51→53; walk bound −51→−52 (not −53 raw).
void AttachShoulderSlotsFix();
