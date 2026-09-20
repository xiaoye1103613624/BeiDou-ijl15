#pragma once

// ItemEff (Effect/ItemEff.img cape auras) + CAvatar::CustomData injection for aItemEffectLayer.
// Owned by Coloring Prism; wraps LoadLayer with WeaponTint_Begin/EndItemEffSwap.

void AttachItemEffectMod();

// Force the next UpdateItemEff to rebuild every slot on this avatar (SEH-safe).
void ItemEff_Invalidate(void* pAvatarRaw);
