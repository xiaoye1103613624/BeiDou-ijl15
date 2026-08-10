# 为何同类 Bug 又回来了（2026-08-05 ~01:03）

> 对应投诉：图腾1/徽章假卸载 → 整理闪退；副手不显示 + 可无限穿。

## 1. 不是「没修」，是「修了没装进 live 客户端逻辑」

| 层 | 23:54 后状态 | 结果 |
|----|--------------|------|
| 服务端 class | 已去 occupy / ghost-sync；JVM 23:54 已加载 | `source-null … enableActions only`（out1.log 23:56） |
| live `ijl15` | 仍 **`60A3E54F`** 二进制 APPEND_ONLY | **无** `GHOST heal` / `pending-ghost-heal` / `ClearSidecar` 字符串 |
| 胖重建 | `F3411A69` / **1512960** / stub `02 6A` | **禁止**盖 Client_1（avoid #55）— 已标 `DO_NOT_DEPLOY` |
| 客户端日志 | `equipaddon_debug.log` 停在 **19:37** | 未证明冷启过 APPEND_ONLY 进程 |

**复现链：**

1. 客户端 sidecar 仍画 −54/−55（幽灵），DB/服务端槽已空（物品在背包 42/58）。
2. 卸装 → `source-null` → 仅 enableActions → **live DLL 不会 ClearSidecar** → 假卸载 / 连点。
3. 背包已与 UI 不同步 → **整理** 大批 mode-3/0 → 客户端闪退（空 invent 族 / 坏指针族）。
4. −62 在 DB（1352977）但 UI GetItem 空 → 无占用感 → **同一副手可反复 replace 穿**（背包堆 135xxxx）。

## 2. 历史错误操作如何再次被踩

| 曾禁操作 | 本次是否再现 | 说明 |
|----------|--------------|------|
| occupy 硬拒 | 服务端 **未**再现 | 23:56 日志已无 `equip reject occupy` |
| ghost-sync mode-3 | 服务端 **未**再现 | 已是 enableActions only |
| 空 `modifyInventory([])` | 整理路径仍有风险 | 已加 **mods.isEmpty 跳过** |
| 胖 DLL 盖 live | 差点 | GHOSTHEAL 链出 1512960 — **guard 已加 F3411A69 黑名单** |
| 乐观 clear-only | live 仍无 GHOST heal | 源码有、二进制无 → 幽灵残留 |

## 3. 正确修复（权威）

- **真卸装**：服务端有物 → mode-2 −bp→bag；SetItem 清 sidecar。
- **幽灵卸装**：服务端无物 → **仅** enableActions + 提示重开/小退；客户端 **GHOST heal ClearSidecar**（须零增长进 live，禁止胖链）。
- **副手显示**：冷启 APPEND_ONLY + 登录 Apply −62；占用靠 GetItem 非空（仍 **replace**，不恢复 occupy 硬拒）。
- **整理**：禁止空 invent；大包分片。

## 4. 守卫

```powershell
powershell -File E:\pro\BeiDou-Server_xy\gms-server\tools\guard_addon_error_ops.ps1
powershell -File E:\pro\BeiDou-ijl15\tools\guard_client1_execave.ps1
# FAIL: occupy / ghost-sync 字符串；JVM 旧于 class；DLL size≠1447424；F3411A69 胖包
```

## 5. 重测前必做

1. **重启 gms-server**（加载 01:33 class）  
2. **杀干净 BeiDou.exe** 后冷启（确认 debug.log 出现新 Init）  
3. 小退再进：看 out.log `login addon seats`（应有 −62=…；−54/−55 若在包则不应在 seats）  
4. 再测：真穿真卸徽章/图腾1/副手 → 整理不闪 → 副手显示且不可「空白连穿」
