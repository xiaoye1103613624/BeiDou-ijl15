# IDA brief — GetSet+ApplyCaves + UiHooks+Layer+OnTick (2026-08-08c)

## Mandate

源码编译链直部署 Client_1。本轮在绿包 **4F11795E**（GetSet+ApplyCaves）上打开 **UiHooks + Layer + OnTick**；**Park OFF**；无 LoginPersist Early。

## Why Layer+OnTick (not UiHooks alone)

| Goal | Needs |
|------|--------|
| 图腾/徽章/副手 **图标显示** | `EnsureLayer` + `Paint` → **Layer=true**；驱动靠 **OnTick** |
| 图腾多座 / 防无限穿 / 满 4 拒第 5 | `WearFromDraggable` / `OnDropped` / bag dbl → **UiHooks=true**；座位坐标依赖 Layer 屏坐标 |
| ExtraRing −52/−53 主栏图标 | shoulders DrawHasItem（本轮不改 LoginPersist） |

Park 仍关：避免每 tick `park54/55` 与主栏坐标抖动。

## Source

| File | Change |
|------|--------|
| `equipaddon.cpp` | stamp `ADDON_GETSET_UI_LAYER_20260808`；GetSet+Apply+UiHooks+Layer+OnTick ON；Park OFF；teardown 调 `SideToolbar::DestroyForLogout` |
| `dllmain.cpp` | 仍 `BEIDOU_SIZE_TRIM_DLLMAIN`（无 LoginPersist） |
| `shoulders.cpp` | ExtraRing 52/53 保持 ON；无 Early LoginPersist |

## Build

```
_build_addon_getset_ui_layer.bat
```

Gate：无 `EquipAddonLoginPersist`；stub@AB30C=`53 E8`；GetSet BP54-62 + UiHooks OnDropped marker；size ≠ BAAD 1501184；硬上限 1490000。

## Enter / wear test

1. **冷杀全部 BeiDou** → 只启 Client_1 → select→enter A/B  
2. **红** → 立刻盖 `aside_4F11795E_rollback_*` 或 `golden\ijl15.ADDON_GETSET_CAVES_20260808.4F11795E.dll`  
3. **绿** → 看 log：`Sidecar SetItem hook OK (BP54-62)` + UiHooks OK；穿 −54/−55/−62 有 `Sidecar SetItem`；开 E 见 Addon LAYER；图腾满 4 拒第 5  
4. 双戒：主栏 −52/−53 图标（shoulders）；仍无则下一轮只修 ExtraRing draw

## Boot expect

`ShoulderSlots OK` → `LazyCompat bootstrap OK` → `Sidecar SetItem hook OK (BP54-62)` → UiHooks OK — **无** `EquipAddonLoginPersist`
