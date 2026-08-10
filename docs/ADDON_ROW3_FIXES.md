# ADDON_ROW3_FIXES — unequip dupe + ExtraRing (on ROW3 base)

> **Date:** 2026-08-02  
> **Status:** **IMPLEMENTED / deploy** — stamp `ADDON_ROW3_FIXES_20260802`  
> **Parent:** [`ADDON_ROW3_POCKET_SI.md`](./ADDON_ROW3_POCKET_SI.md)  
> **Avoid:** [`ADDON_ENTER_CRASH_AVOID.md`](./ADDON_ENTER_CRASH_AVOID.md)

## Delta vs ROW3

| Fix | Change |
|-----|--------|
| Unequip dupe | Sidecar `TryUnequipBp` → **clear-only, server bag** (no optimistic `SetItem` bag) |
| ExtraRing −52/−53 | Get/Set + Apply + main UI draw/wear-occ **ON**; **Occ OFF**; **no −152** |
| Walk bound | Stay at −51 when ExtraRing ON (no native walk into TSec gap) |
| Shield 109 | Unchanged — vanilla main Si; Addon aux 134/135 only |
| Blind STAT | Server already injects −52…−61 via `STAT_CHANGED` / `forceSyncClientDisplayStats` |

## Live

| Field | Value |
|-------|-------|
| Stamp | `ADDON_ROW3_FIXES_20260802` |
| SHA256 | `60E3E02FFF100CE99C25D8404B1826B9CDC80BECEB6C1AF7D44AC1A75297717B` |
| Size | `1444864` |
| ROW3 bak | `ijl15.dll.bak_ADDON_ROW3_POCKET_SI_20260802` (`D2C538A9…`) — on Client_1 |
| STATS bak | `ijl15.dll.bak_ADDON_STATS_UNEQUIP_20260802` (`4AD0AA7A…`) |
| Build | `_build_addon_row3_fixes.bat` + `_static_enter_risk_row3_fixes.ps1` |
| Promote | close BeiDou → `_promote_row3_fixes.ps1` |

## User verify

1. 选角 → **进图**（新功能版必测）
2. 扩展栏卸装（纹章/安卓/心脏/图腾）→ 背包 **只出现一份**
3. 六戒：第 5/6 戒拖到主栏红 3/4 → 可穿、可见、卸装正常；属性靠服务端 STAT
4. 盾 109 主栏可穿；辅助座只接受 134/135
5. 穿/卸盲区槽后开 S 面板看四维刷新

## Rollback

```text
DLL: copy bak_ADDON_ROW3_POCKET_SI_20260802 → ijl15.dll
     VERSION=ADDON_ROW3_POCKET_SI_20260802
```
