#pragma once
#include "compat/WzLib/IWzProperty.h"

#include <map>
#include <vector>


// Shared state between the Effect_HP/Effect_Miss splices (damageskin.cpp) and
// the picker UI (damageskinpicker.cpp).

struct DamageSkinProp {
    IWzPropertyPtr pNoRed0;
    IWzPropertyPtr pNoRed1;
    IWzPropertyPtr pNoCri0;
    IWzPropertyPtr pNoCri1;

    // Optional unit-damage glyphs under the skin's NoCustom node.
    //   NoCustom/NoRed0/0 = "."  (decimal point)
    //   NoCustom/NoRed0/1 = "K"
    //   NoCustom/NoRed0/2 = "M"
    //   NoCustom/NoRed0/3 = "B"
    // (and the matching NoCri0 children for crit). bHasCustom is true
    // iff both NoRed0 and NoCri0 unit folders resolved 鈥?the picker's
    // "unit-only" filter uses this flag, and DrawPreview paints a
    // "1.8K" sample when the hovered skin has them.
    IWzPropertyPtr pNoCustomRed0;
    IWzPropertyPtr pNoCustomCri0;
    bool           bHasCustom = false;

    // In-game unit-damage wrappers. When bHasCustom is true we build
    // IWzProperty wrappers that union the base digits sheet (NoRed0 /
    // NoCri0) with the NoCustom glyphs, so Effect_HP's existing digit
    // loop can fetch "1", "8", "." and "K" from a single property.
    // Raw pointers 鈥?wrapper instances leak intentionally for DLL
    // lifetime (ownership is trivial since we never unload skins).
    void* pWrapNormal = nullptr;   // CUnitPropertyWrapper*
    void* pWrapCrit   = nullptr;
};

extern std::map<int, DamageSkinProp> g_mDamageSkinProp;
extern std::vector<int>               g_vSkinIds;
extern int                            g_nDamageSkin;

// Idempotent — safe to call repeatedly. Bulk-scans WZ on first call.
void LoadDamageSkin();

// Loads one skin by direct UOL (Effect/DamageSkin.img/<id> or BasicEff path).
// Used by the picker when bulk enumeration returns nothing.
void EnsureDamageSkinLoaded(int nID);

// Set the active skin id used by subsequent ShowDamage calls. 0 = default.
void SetActiveDamageSkin(int nID);

// Charid鈫抯kinId map populated from server broadcasts. Used by the attacker-
// attribution hook to pick the correct skin right before Effect_HP fires.
int  GetSkinForChar(int nCharId);
void SetSkinForChar(int nCharId, int nSkinId);

// Shop catalog + owned inventory, populated from server packets at login
// and kept fresh as things change. The picker UI reads from these.
struct DamageSkinCatalogEntry { int nID; long long llPrice; };
extern std::vector<DamageSkinCatalogEntry> g_vShopCatalog;
extern std::vector<int>                    g_vOwnedSkins;
extern int                                 g_nLocalCharId;  // my own cid, for broadcast filtering

// Outgoing requests 鈥?fire-and-forget; server replies via DAMAGE_SKIN_RESULT.
void Send_DamageSkinApply(int nSkinId);
void Send_DamageSkinPurchase(int nSkinId);

// Called once from AttachDamageSkinMod().
void AttachDamageSkinNet();

// Defined in damageskinpicker.cpp. Rebuilds m_vMyList / m_vShopList from
// g_vOwnedSkins / g_vShopCatalog if the picker is currently open. No-op
// otherwise. Called by the net packet handlers after server state moves.
void RefreshDamageSkinPicker();
