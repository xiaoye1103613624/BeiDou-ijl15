#pragma once

namespace SetItem {
void RegisterPacketHandler();
void EnsureHooks();
void EnsureUiHooks();
void RequestSetItemBonus(int setId);
void RedrawActivePanel();
const char* GetSetItemSkillBonusText(int setId);
bool IsSetEnabled(int setId);
bool IsSetDisabled(int setId);
int GetSkillBonusLevel(int skillId);
bool TryGetActiveSetTooltipRect(int& outX, int& outY, int& outW, int& outH);
} // namespace SetItem

class CUIToolTip;
class GW_ItemSlotEquip;

// When custom equip tip skips vanilla DrawToolTip_Equip, still refresh set companion.
void SetItem_OnEquipTipDrawn(CUIToolTip* tip, GW_ItemSlotEquip* pe);

// Addon tip ClearToolTip would HideSetTooltip; call before clearing custom tip when
// CUIEquip tip may still be visible (classic cover) so 套装 tip can rebind.
void SetItem_ReleaseCustomMainTipWithoutHidingSet(CUIToolTip* tip);
