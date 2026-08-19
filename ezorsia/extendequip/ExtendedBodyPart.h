#pragma once
// ExtendedBodyPart registry — single source of truth for extended seats.
// Policy mirrors live RESTORED_ADDON_STATS_UNEQUIP_20260802 / FIX2 enter-safe flags:
//   ExtraRing Get/Set+UI ON; Addon Get/Set ON; Occ OFF;
//   Live WIREOFF: Equip-open XY wire OFF. Staged STRICT_WIRE: wire ON with strict gates.
//   NO BP33 draw cave; apply-max ≤ 55; cash rings never −152/−153.
// Phase 1–2: flag consumers OK; XY wire consumers must honor kEnableEquipOpenXyWire.

#include <cstddef>

namespace ExtEquip {

enum class UiSeat : unsigned char {
    MainEquip = 0,
    AddonDock = 1,
    HiddenPark = 2,
};

enum class DrawPolicy : unsigned char {
    Vanilla = 0,
    RemapXY = 1,       // GetSlotXY/HT only — never draw-skip punch
    AddonOverlay = 2,
    NeverDrawEnter = 3,
};

enum class StorageTier : unsigned char {
    NativeApplyMax55 = 0, // always native (≤55 / pocket −33 / shield −10)
    ShadowGetSet = 1,     // vanilla EXE: DLL ZRef; CD64 EXE: promote to native
    SidecarZRef = 2,      // vanilla EXE: sidecar; CD64 EXE: promote to native
};

enum class WireWhen : unsigned char {
    Always = 0,
    PostField = 1,
    EquipOpen = 2,
    GatedOff = 3, // ExtraRing until Phase-5 bisect
};

struct ExtendedBodyPart {
    int bp;                 // positive bodypart (== -slot)
    int prefix;             // itemId / 10000; 0 = multi/shared
    const char* islot;      // WZ code; may be empty
    UiSeat uiSeat;
    StorageTier storage;
    bool clientBlind;       // → server STAT_CHANGED
    DrawPolicy drawPolicy;
    WireWhen wireWhen;
    bool shadowGetSetOn;    // live: Addon ON, ExtraRing OFF
    int xyX;
    int xyY;
    bool hasCashAlias;      // cash = -(bp+100) when true
    const char* notes;
};

// Live policy table (STATS_UNEQUIP / FIX2). Order = AbleToWear prefix priority hint.
inline constexpr ExtendedBodyPart kBodyParts[] = {
    // Shoulder — native
    {20, 115, "Sh", UiSeat::MainEquip, StorageTier::NativeApplyMax55, false,
     DrawPolicy::RemapXY, WireWhen::Always, false, 137, 101, false, "shoulder red8"},
    // Pendant2 — native idle BP51
    {51, 112, "Pe", UiSeat::MainEquip, StorageTier::NativeApplyMax55, false,
     DrawPolicy::RemapXY, WireWhen::PostField, false, 0, 0, true, "pendant2 after CField"},
    // ExtraRing — Get/Set+main UI ON; Occ/−152 OFF (ROW3_FIXES)
    {52, 111, "Ri", UiSeat::MainEquip, StorageTier::ShadowGetSet, true,
     DrawPolicy::RemapXY, WireWhen::Always, true, 104, 35, true, "ExtraRing red3"},
    {53, 111, "Ri", UiSeat::MainEquip, StorageTier::ShadowGetSet, true,
     DrawPolicy::RemapXY, WireWhen::Always, true, 137, 35, true, "ExtraRing red4"},
    // Pocket −33 classic Equip; Aux −62 classic red10 (sidecar storage).
    {33, 116, "Po", UiSeat::MainEquip, StorageTier::NativeApplyMax55, false,
     DrawPolicy::RemapXY, WireWhen::EquipOpen, false, 104, 200, true, "classic pocket red9"},
    {10, 109, "Si", UiSeat::MainEquip, StorageTier::NativeApplyMax55, false,
     DrawPolicy::Vanilla, WireWhen::Always, false, 0, 0, true, "109 vanilla shield"},
    {62, 134, "Aw", UiSeat::MainEquip, StorageTier::SidecarZRef, true,
     DrawPolicy::RemapXY, WireWhen::Always, true, 137, 200, true, "classic aux −62"},
    {62, 135, "Aw", UiSeat::MainEquip, StorageTier::SidecarZRef, true,
     DrawPolicy::RemapXY, WireWhen::Always, true, 137, 200, true, "classic aux −62"},
    // Badge / Totem / Emblem / Android / Heart — Addon
    {54, 118, "Ba", UiSeat::AddonDock, StorageTier::ShadowGetSet, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, -2000, -2000, true, "badge"},
    {55, 120, "To", UiSeat::AddonDock, StorageTier::ShadowGetSet, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, -2000, -2000, true, "totem1"},
    {56, 120, "To", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "totem2 (−56/−156 cash)"},
    {57, 120, "To", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "totem3 (−57/−157 cash)"},
    {58, 120, "To", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "totem4 (−58/−158 cash)"},
    {59, 119, "Em", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "emblem never Si"},
    {60, 166, "Dr", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "android NOT BP21"},
    {61, 167, "Ht", UiSeat::AddonDock, StorageTier::SidecarZRef, true,
     DrawPolicy::AddonOverlay, WireWhen::Always, true, 0, 0, true, "heart NOT BP22"},
};

inline constexpr std::size_t kBodyPartCount = sizeof(kBodyParts) / sizeof(kBodyParts[0]);

// Phase-3 skeleton: Equip-open-only XY remaps (no BP33 draw cave).
struct XyRemap {
    int bp;
    int x;
    int y;
    int onlyPrefix; // 0 = always when Equip open; else require item prefix on that bp
    WireWhen when;
    const char* notes;
};

// No main-bar XY wire table — pocket/aux HitTest is Detours on GetBodyPartFromPoint.
inline constexpr XyRemap kEquipOpenXyRemaps[] = {
    {0, 0, 0, 0, WireWhen::GatedOff, "no main XY wire (classic HT hook)"},
};

inline constexpr std::size_t kEquipOpenXyRemapCount =
        sizeof(kEquipOpenXyRemaps) / sizeof(kEquipOpenXyRemaps[0]);

// Addon dock pixel seats (2×4) — view of uiSeat==AddonDock.
struct AddonSeatXy {
    int bp;
    int sx;
    int sy;
};

inline constexpr AddonSeatXy kAddonDockSeats[] = {
    {55, 6, 33},
    {56, 39, 33},
    {57, 72, 33},
    {58, 105, 33},
    {59, 6, 90},
    {60, 39, 90},
    {61, 72, 90},
    {54, 105, 90},
};

inline constexpr std::size_t kAddonDockSeatCount =
        sizeof(kAddonDockSeats) / sizeof(kAddonDockSeats[0]);

// --- Live feature flags (must match shoulders.cpp STATS/FIX2) ---
inline constexpr bool kExtraRingShadowsOn = true;
inline constexpr bool kAddonGetSetOn = true;
inline constexpr bool kOccShadowOn = false;
inline constexpr bool kRemapCashExtendedRings = false;
inline constexpr int kSidecarBpMin = 56;
inline constexpr int kSidecarBpMax = 62;
inline constexpr int kShadowGetSetMinBp = 52; // ExtraRing + Addon
inline constexpr int kShadowGetSetMaxBp = 62;
inline constexpr int kApplySlotMax = 55;
inline constexpr int kOffPanelX = -2000;
inline constexpr int kOffPanelY = -2000;

inline bool IsSidecarBp(int bp) {
    return bp >= kSidecarBpMin && bp <= kSidecarBpMax;
}

inline bool IsShadowGetSetBp(int bp) {
    if (!kAddonGetSetOn && !kExtraRingShadowsOn) {
        return false;
    }
    if (bp >= 54 && bp <= 62) {
        return kAddonGetSetOn;
    }
    if ((bp == 52 || bp == 53) && kExtraRingShadowsOn) {
        return true;
    }
    return false;
}

// Inventory position (negative) → client-blind for STAT_CHANGED.
inline bool IsClientBlindInventoryPos(int pos) {
    switch (pos) {
    case -52:
    case -53:
        return true;
    case -54:
    case -55:
    case -56:
    case -57:
    case -58:
    case -59:
    case -60:
    case -61:
    case -62:
        return true;
    case -154:
    case -155:
    case -156:
    case -157:
    case -158:
    case -159:
    case -160:
    case -161:
    case -162:
        return true;
    default:
        return false; // −10 / −33 never blind
    }
}

inline const ExtendedBodyPart* FindByBp(int bp) {
    for (std::size_t i = 0; i < kBodyPartCount; ++i) {
        if (kBodyParts[i].bp == bp) {
            return &kBodyParts[i];
        }
    }
    return nullptr;
}

inline const ExtendedBodyPart* FindByPrefix(int prefix) {
    for (std::size_t i = 0; i < kBodyPartCount; ++i) {
        if (kBodyParts[i].prefix == prefix) {
            return &kBodyParts[i];
        }
    }
    return nullptr;
}

inline bool PrefixOwnsBp(int prefix, int bp) {
    if (prefix == 109) {
        return bp == 10;
    }
    if (prefix == 134 || prefix == 135) {
        return bp == 62;
    }
    if (prefix == 111) {
        return bp == 12 || bp == 13 || bp == 15 || bp == 16
               || (kExtraRingShadowsOn && (bp == 52 || bp == 53));
    }
    if (prefix == 112) {
        return bp == 17 || bp == 51;
    }
    if (prefix == 120) {
        return bp >= 55 && bp <= 58;
    }
    const ExtendedBodyPart* e = FindByPrefix(prefix);
    return e && e->bp == bp;
}

inline bool IsSiAuxPrefix(int prefix) {
    return prefix == 134 || prefix == 135;
}

inline bool IsSiShieldPrefix(int prefix) {
    return prefix == 109;
}

} // namespace ExtEquip
