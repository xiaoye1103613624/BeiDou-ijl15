# ADDON_ROW3_WEAR_UNEQUIP — packet-only wear/unequip (on ROW3_FIXES)

> **Date:** 2026-08-02  
> **Status:** **IMPLEMENTED / deploy** — stamp `ADDON_ROW3_WEAR_UNEQUIP_20260802`  
> **Parent:** [`ADDON_ROW3_FIXES.md`](./ADDON_ROW3_FIXES.md)  
> **Avoid:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)

## Root cause

Client `INVENTORY_OPERATION` mode-2 is a **GetItem swap** (`SetItem(old, Get(new))` + `SetItem(new, Get(old))`), not a server-authored payload.

| Local mutation | Effect |
|----------------|--------|
| Wear **optimistic sidecar** fill (same pointer still in bag) | Swap puts “dest item” back into bag → **bag ghost + Addon icon** |
| Unequip **clear-only** before packet | Both sides null → bag never filled; next unequip reuses same dest → full / stuck |

## Delta vs ROW3_FIXES

| Fix | Change |
|-----|--------|
| Wear | SendChange only — **no** optimistic sidecar `SetItem` |
| Unequip | SendChange only — **no** clear-only; **no** optimistic bag |
| ExtraRing / Occ / −152 / red9/10 | Unchanged (enter-safe) |

## Live

| Field | Value |
|-------|-------|
| Stamp | `ADDON_ROW3_WEAR_UNEQUIP_20260802` |
| SHA256 | `946B035BD79D4202C223B1B402599155DB45F5FA5D2492D325A3048EFCFFA798` |
| Size | `1446400` |
| FIXES bak | `ijl15.dll.bak_ADDON_ROW3_FIXES_20260802` (`60E3E02F…`) |
| Build | `_build_addon_row3_wear_unequip.bat` + `_static_enter_risk_row3_wear_unequip.ps1` |
| Promote | close BeiDou → `_promote_row3_wear_unequip.ps1` |
| Staged | `ijl15.dll.ADDON_ROW3_WEAR_UNEQUIP_20260802.staged` on Client_1 |

## User verify

1. 选角 → **进图**
2. 扩展栏穿图腾/纹章/安卓/心脏 → **背包对应格消失**，仅 Addon 有一份
3. Addon **双击卸下** → 栏清空，背包 **只一份**
4. 口袋 −33 / 辅助 134|135 / 六戒主栏红3/4 同样穿卸
5. 盾 109 主栏；辅助座只 134/135

## Rollback

```text
DLL: copy bak_ADDON_ROW3_FIXES_20260802 → ijl15.dll
     VERSION=ADDON_ROW3_FIXES_20260802
```
