#pragma once

class CUIToolTip;
class GW_ItemSlotEquip;

namespace SetItem {
void RegisterPacketHandler();
void EnsureHooks();
void EnsureUiHooks();
void RequestSetItemBonus(int setId);
void RedrawActivePanel();
const char* GetSetItemSkillBonusText(int setId);
bool IsSetEnabled(int setId);
int GetSkillBonusLevel(int skillId);
bool TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH);
// Opposite-band set tip (cash while hovering normal, or vice versa).
bool TryGetActiveAltSetTooltipRect(int& outX, int& outY, int& outW, int& outH);
// Compare-equip companions (second buffer): docked right of the compare tip.
void UpdateCompareCompanion(CUIToolTip* compareTip, int itemId);
void HideCompareCompanion();
void RelayoutCompareCompanion(CUIToolTip* compareTip);
bool TryGetActiveCompareSetTooltipRect(int& outX, int& outY, int& outW, int& outH);
} // namespace SetItem

// When custom equip tip skips vanilla DrawToolTip_Equip, still refresh set companion.
void SetItem_OnEquipTipDrawn(CUIToolTip* tip, GW_ItemSlotEquip* pe);
void SetItem_ReleaseCustomMainTipWithoutHidingSet(CUIToolTip* tip);
