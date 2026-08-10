# IDA brief — source rebuild SetItem 54–62 (2026-08-08)

## Mandate

User: **改插件源码 → Release 编译 → 直部署 Client_1**。禁止 golden 二进制 FG 来回盖 / CD64_TEST 切换。

Cursor IDA MCP 仍挂 `v265.3` — **未要求用户切换**；取证用文件 ExpectBytes + live 日志。

## Root cause (source)

1. `equipaddon.cpp` 头注释写 kEnableAddon* ON，但 bootsafe 把门闩全关成 `false` → 新编译无 Sidecar Get/Set / Layer / bag dbl → 与 avoid #59 同类。
2. `shoulders` `kExtSlotBoundImm=0xC3`（−61）挡 −62 aux。
3. `DrawHasItem` BP54/55 读本地 `g_badge54`/`g_totem55`，库存已改走 sidecar → 主栏空画。
4. `FindFreeTotemBp` 仅 `GetItemIdAtBp`：SetItem 未落时永远返回 55 → 无限穿 UX。

## Source changes

| File | Change |
|------|--------|
| `ezorsia/equipaddon/equipaddon.cpp` | `kEnableAddonLayer/OnTick/UiHooks/GetSet/ApplyCaves=true`；Park=false；stamp `ADDON_SOURCE_SETITEM_5462_20260808`；`TotemIdAtBp` + sidecar 直读 |
| `ezorsia/shoulders/shoulders.cpp` | bound imm `0xC2`（−62）；DrawHasItem 54/55 → `ShadowZRefForBp`→sidecar |

## Build (合法路径)

```
_build_addon_source_setitem_5462.bat
```

- Frozen-obj link：`Release_enter_frozen` + 重编 `equipaddon` / `shoulders` / `sidetoolbar`
- **禁止** Client_1 VS Clean+Rebuild
- Post-link：`tools/preserve_execave_stub.ps1` 保留 stub@AB30C=`53 E8`

## Deploy

| Item | Value |
|------|--------|
| Live | `BeiDou-Client_1\ijl15.dll` |
| SHA8 | **BAAD6396** |
| Size | **1501184**（较 EXECAVE 1447424 **涨 ~52KB** — 进图红风险族；必须冷启 A/B） |
| stub@AB30C | `53 E8` |
| EXE | ACF10F63 未动 |
| bak | `ijl15.dll.bak_before_SOURCE_SETITEM_5462_20260808_150535`（前一版 **F3F8D0F2** FG） |
| golden | `golden\ijl15.ADDON_SOURCE_SETITEM_5462_20260808.BAAD6396.dll` |

## Enter gate / rollback

1. **冷杀**全部 BeiDou → 只启 Client_1 → select→enter A/B。
2. **红**（闪退/`0xC0000409`）：立刻把 bak（F3F8D0F2）盖回 live。
3. 绿后看 `equipaddon_debug.log`：
   - Init stamp = `ADDON_SOURCE_SETITEM_5462_20260808`
   - `Sidecar SetItem hook OK (BP54-62)`
   - 穿徽章/图腾/副手出现 `Sidecar SetItem slot=-54/-55…/-62`
   - 图腾 dblclick `bp=55..58` 递增，满 4 拒第 5
   - 双戒 −52/−53 主栏有图标

## Honesty

体积大于历史 EXECAVE 绿线；若进图红，问题在 **size-jump GS 族**，不是再盖 FG 字节能修的——回 bak 后需再压体积（更窄重编 / 拆 UiHooks 晚装）再源码迭代。
