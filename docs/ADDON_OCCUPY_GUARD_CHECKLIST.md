# Addon 扩展栏 — 占用/显示/整理 回归清单

> **硬规则：** [`ADDON_FORBIDDEN_OPS.md`](./ADDON_FORBIDDEN_OPS.md) — 禁止再发明特殊 invent 包  
> **错误操作矩阵（防复发）：** [`ADDON_ERROR_OPS_MATRIX.md`](./ADDON_ERROR_OPS_MATRIX.md) · [`ADDON_REGRESSION_RECURRENCE_20260805.md`](./ADDON_REGRESSION_RECURRENCE_20260805.md)  
> 日期：2026-08-04 夜 · sidecar=存储 / 逻辑=原版 mode-2

## 0. 为何同类问题反复出现

对 sidecar 叠「特殊解锁」（空 invent、forceUpdate、dual/ghost mode-3、occupy 硬拒）每次只压一个症状，**必然制造下一个**（假死→闪退→戴不上→重登不同步→悬停卡死）。

**对策：** 只允许原版同形 mode-2 + enableActions；幽灵只在客户端清 arena。  
**自动化：** `gms-server/tools/guard_addon_error_ops.ps1` + `tools/guard_client1_execave.ps1`

## 1. 部署门禁

```powershell
powershell -File E:\pro\BeiDou-ijl15\tools\guard_client1_execave.ps1
powershell -File E:\pro\BeiDou-Server_xy\gms-server\tools\guard_addon_error_ops.ps1

# DLL 60A3E54F… 1447424；EXE ACF10F63…；PreferSend/SetItem/GetItem = 54–62
# 服务端：无 occupy 硬拒 / 无 ghost-sync；JVM 启动晚于 class mtime
```

改服务端后 **必须重启 gms-server** 再测。

## 2. 服务端（parity）

| 项 | 行为 |
|----|------|
| 穿/卸成功 | **mode-2**；落库 **−54…−62** |
| 座已占用 | **replace**（同经典），不硬拒 |
| source-null | **仅** enableActions（禁止装备 mode-3 ghost） |
| forceUpdate / 空 invent / dual mode-3 | **禁止** |
| 点装扩展座 | `EquipSlot.isAllowed` **exact** −bp（勿 cash 双减 100） |

## 3. 客户端

- Live：`60A3E54F` PacketSendOnly 54–62
- 源码：卸装 pending-ghost-heal（busy 清后仍亮 → ClearSidecar）；Addon 座允许 replace
- 未打进 live 的源码改动：下一次 **FA 零增长** 再同步；**当前以新 jar 服务端为主**

## 4. 手测（不过不许喊完成）

见 `ADDON_FORBIDDEN_OPS.md` 回归门禁 § 全 8 条 + 拖拽/双击替换徽章·图腾1·副手。
