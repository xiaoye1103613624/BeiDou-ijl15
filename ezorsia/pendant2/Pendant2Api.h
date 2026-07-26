// Second pendant: BeiDou v083 native 112 → BP17+BP51 (−51).
//
// Safe UI + wear/stats (stamp FIX_RING_UNEQUIP_PERSIST_20260727aj):
//   - No DllMain UI attach.
//   - When pendant2_ui=true after CField:
//       * AlignClassicDrawToHitTest: GetSlotXY←HitTest for BP4/10/11 only
//         (weapon/shield/ear; classic rings hardcoded separately)
//       * Classic ring GetSlotXY+HitTest+BE27E0: BP15/16 (104/137,68),
//         BP12/13 (104/137,167)
//       * GetSlotXY/HitTest/BE27E0: medal/belt/pendant2(38,101) / shoulder red8(137,101)
//         / ring5–6 red3–4 (104,35)/(137,35)
//       * BE27E0[12/13/15/16/20/51/52/53]; BP23 GetSlotXY parked; char HT → BE240C
//       * Char equip icon draw: loop-end mov eax,53 @7FEFB9 (ae); early add 0x35
//         — NOT add-imm 0x35 alone (ad: flag1⇒BP54 hang)
//       * Zero expanded-layout and*,0x21 (classic 175×304); flag cave
//   - Wear/stats in AttachShoulderSlotsFix (115/BP20/−20 + 112/BP51/−51
//     + 111 is_correct/AbleToWear; get_bodypart×6 ON, order 52/53 first)
//   - Inventory bind ON: GetItem/SetItem cmp−53; −53→g_ring53ZRef;
//     DrawHasItem: BP52 edi=`[ebp-20h]-0x1A0` (not UI `[ebp-14h]`); BP53 shadow;
//     ForceNormal@7FEEC2 + special-path `mov edx,[ebp-14h]` (ae omitted → E hang)
//   - Bag ring dblclick: restore empty-search count (PE smash→1) so red3/4 fill
//   - Equipped dblclick: jmp native wear@4F0B89 (aj); login apply max 53
//   - NEVER ForceDrawLoop (setne→mov al,1) / pet HT@801214
// Stamp: FIX_RING_UNEQUIP_PERSIST_20260727aj
#pragma once

void AttachPendant2SlotsFix();
void EnsurePendant2AfterFieldEnter();
void Pendant2OnClientTick();
