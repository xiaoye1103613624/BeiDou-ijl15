#pragma once

class CUIToolTip;
class GW_ItemSlotEquip;

namespace EquipGrowth {

void RegisterPacketHandler();

/** Cache/state only — no UI Detours. */

void EnsureHooks();

/**

 * Hover path: paint from local equip fields first; only optionally request 0x17D

 * when dynamic server-only data is missing (强化战斗%% / 成长明细分色).

 */

void RequestGrowthTip(int itemId);

/**

 * Prefer this from tip-drawn: builds/refreshes local tip from pe fields, then

 * maybe one enrichment request. enhance/itemLevel/scroll from GW_ItemSlotEquip.

 */

void OnHoverEquip(int itemId, int enhance, int itemLevel, int scrollLevel);

void RedrawActivePanel();

/** Drop negative (empty) cache so next hover re-requests after server/equip change. */

void InvalidateEmptyCache(int itemId);

/** Drop all cache for itemId (fingerprint change / hide after growth). */

void InvalidateCache(int itemId);

const char* GetGrowthTipText(int itemId);

/** Cached itemLevel for tip greying (0 if unknown). */
int GetGrowthTipItemLevel(int itemId);

/** True when server replied with non-empty growth tip OR local tip is non-empty. */

bool HasGrowthTip(int itemId);

/** True when a reply (empty or not) is already cached for itemId (+ fingerprint). */

bool IsGrowthTipResolved(int itemId);

bool TryGetActiveGrowthTooltipRect(int& outX, int& outY, int& outW, int& outH);

/** Compare-equip growth companion (second buffer), docked right of compare set/tip. */
void UpdateCompareCompanion(CUIToolTip* compareTip, int itemId, void* pe);
void HideCompareCompanion();
void RelayoutCompareCompanion(CUIToolTip* compareTip);
bool TryGetActiveCompareGrowthTooltipRect(int& outX, int& outY, int& outW, int& outH);

/** Growth flat bonus for tip color (statIdx 0=STR .. 14=Jump). Prefer cache; 0 if unknown. */

int GetGrowthBonusForStat(int itemId, int statIdx);

/** Flame (涅槃) flat bonus for tip green strip; 0 if unknown. */

int GetFlameBonusForStat(int itemId, int statIdx);

} // namespace EquipGrowth

/** Called only from SetItem AfterEquipTipDrawn / ShowItemToolTip path. */
void EquipGrowth_OnEquipTipDrawn(CUIToolTip* tip, GW_ItemSlotEquip* pe);
void EquipGrowth_OnEquipTipDrawnId(CUIToolTip* tip, int itemId);
void EquipGrowth_Hide();

