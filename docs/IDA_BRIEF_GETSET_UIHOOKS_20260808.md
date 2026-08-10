# IDA brief — GetSet+ApplyCaves + UiHooks only (2026-08-08c)

## Mandate

源码编译链直部署 Client_1。在绿包 **4F11795E**（GetSet+ApplyCaves）上打开 **UiHooks**；**Layer/OnTick/Park OFF**；无 LoginPersist Early。

## Pre-check (this session)

| Item | Result |
|------|--------|
| Live before deploy | **F3F8D0F2** 1447424（非 4F11795E；进程曾跑 SETITEM_FG） |
| Golden green | `golden\ijl15.ADDON_GETSET_CAVES_20260808.4F11795E.dll` 仍在 |
| Log | 近期无 `GETSET_CAVES` / `BP54-62`；多为 `SETITEM_FG` + `BP56-61` |

## Why UiHooks only (not Layer)

| Goal | This ship |
|------|-----------|
| 防无限穿 / 图腾满 4 拒第 5 / bag dbl→sidecar | **UiHooks ON**（Wear/Drop/Dbl） |
| Addon 坞图标显示 | **需 Layer+OnTick**；试开后产物 **1500672 ≈ BAAD 体积族** → 本轮 **故意不开** |
| ExtraRing −52/−53 | shoulders DrawHasItem（已 ON；无 Early LoginPersist） |

## Source

| File | Change |
|------|--------|
| `equipaddon.cpp` | stamp `ADDON_GETSET_UIHOOKS_20260808`；GetSet+Apply+UiHooks ON；Layer/OnTick/Park OFF |
| `dllmain.cpp` | `BEIDOU_SIZE_TRIM_DLLMAIN` |
| `shoulders.cpp` | ExtraRing 52/53 保持 |

## Build

```
_build_addon_getset_uihooks.bat
```

Gate：无 `EquipAddonLoginPersist`；stub@AB30C=`53 E8`；GetSet BP54-62 + UiHooks OnDropped；size ≠ 1501184；硬上限 1495000。

## Deployed

| Item | Value |
|------|--------|
| Live SHA8 | **6126421E** |
| Size | **1490432**（Δ vs F3F8 = +43008；须冷启 A/B） |
| stub@AB30C | `53 E8` |
| LoginPersist | **无** |
| bak | `ijl15.dll.bak_before_GETSET_UIHOOKS_20260808_153623` |
| rollback | `ijl15.dll.aside_4F11795E_rollback_20260808_153623` |
| golden | `golden\ijl15.ADDON_GETSET_UIHOOKS_20260808.6126421E.dll` |

试开 Layer+OnTick 曾出 **1500672**（≈BAAD）→ 本轮未部署。

## Enter / wear test

1. 冷杀全部 BeiDou → 只启 Client_1 → select→enter A/B  
2. **红** → 立刻盖 `aside_4F11795E_rollback_*` 或 golden **4F11795E**  
3. **绿** → log：`Sidecar SetItem hook OK (BP54-62)` + UiHooks OK；穿 −54/−55/−62 有 `Sidecar SetItem`；图腾第 5 件应拒  
4. 本轮仍无 Addon LAYER 画图标；显示下一轮再拆 Layer（需先解决体积）

## Boot expect

`ShoulderSlots OK` → `LazyCompat bootstrap OK` → `Sidecar SetItem hook OK (BP54-62)` → UiHooks OK — **无** `EquipAddonLoginPersist`
