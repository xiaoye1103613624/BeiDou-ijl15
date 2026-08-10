# IDA brief — GetSet+ApplyCaves only (2026-08-08b)

## Mandate

源码编译链直部署 Client_1。本轮**只开** `GetSet` + `ApplyCaves`；Layer/OnTick/UiHooks/Park = false。

## Why narrow

| Bad | SHA8 | Size | Cause |
|-----|------|------|-------|
| BAAD6396 | BAAD6396 | 1501184 | 全开 + polluted `dllmain` Early → Init 256 / avoid #35/#38 |
| Live pre | C482E183 | 1480704 | stub `F9 FF` + LoginPersist（非 F3F8） |

## Source

| File | Change |
|------|--------|
| `equipaddon.cpp` | stamp `ADDON_GETSET_CAVES_20260808`；gates GetSet+Apply ON，其余 OFF；`InstallLoginPersistEarly` = no-op |
| `dllmain.cpp` | `BEIDOU_SIZE_TRIM_DLLMAIN` 跳过 CrashDiag/VEH（贴 F3F8 体积） |
| `shoulders.cpp` | 已有 ExtraRing 52/53 + sidecar DrawHasItem 54/55 + bound `0xC2` |
| `CrashDiagStub.cpp` | 备用空 stub（本轮 SIZE_TRIM 不链） |

## Build

```
_build_addon_getset_caves_only.bat
```

- Frozen-obj + 重编 `dllmain` / `equipaddon` / `shoulders`
- Gate：无 `EquipAddonLoginPersist`；stub@AB30C=`53 E8`；GetSet 54–62 marker

## Deploy

| Item | Value |
|------|--------|
| Live SHA8 | **4F11795E** |
| Size | **1474048**（目标 1447424，Δ=+26624 — 必须冷启 A/B） |
| stub@AB30C | `53 E8` |
| LoginPersist | **无** |
| bak | `ijl15.dll.bak_before_GETSET_CAVES_20260808_152722` |
| F3F8 aside | `ijl15.dll.aside_F3F8D0F2_rollback_20260808_152722` |
| golden | `golden\ijl15.ADDON_GETSET_CAVES_20260808.4F11795E.dll` |

## Enter / wear test

1. 冷杀全部 BeiDou → 只启 Client_1 → select→enter A/B  
2. **红** → 立刻盖 `aside_F3F8D0F2_rollback_*` 或 golden F3F8  
3. **绿** → 看 `equipaddon_debug.log`：`Sidecar SetItem hook OK (BP54-62)`；穿 −54/−55/−62 有 `Sidecar SetItem`  
4. 本轮无 Layer/UiHooks：Addon 坞不画；图腾多座/无限穿需下一轮再开 UiHooks；双戒靠 shoulders ExtraRing

## Boot expect

`ShoulderSlots OK` → `LazyCompat bootstrap OK` → quiet — **无** `EquipAddonLoginPersist`
