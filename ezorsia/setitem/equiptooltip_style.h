#pragma once

class CUIToolTip;
class GW_ItemSlotEquip;

void AttachEquipTooltipStyleHooks();

// Custom equip tip (set-tip style canvas). OFF = full vanilla DrawToolTip_Equip.
// Incomplete custom canvas hid icon/REQ/trade/job/etc ? keep OFF until parity.
#ifndef EQUIP_TIP_STYLE_CUSTOM_CANVAS
#define EQUIP_TIP_STYLE_CUSTOM_CANVAS 0
#endif

// Remember hover tip screen position from ShowItemToolTip (before Draw).
void EquipTooltipStyle_NoteHoverPos(CUIToolTip* tip, int x, int y);

// Read last NoteHoverPos for tip (or any noted tip if tip is null / mismatch).
// Returns false if nothing noted.
bool EquipTooltipStyle_GetHoverPos(CUIToolTip* tip, int& outX, int& outY);

// Replace vanilla equip tip body with custom Dotum/ColonCenter canvas.
// Returns true if handled ? caller must NOT call vanilla DrawToolTip_Equip.
bool EquipTooltipStyle_TryDrawCustom(
        CUIToolTip* tip,
        GW_ItemSlotEquip* pe,
        int extraBottomPad);
