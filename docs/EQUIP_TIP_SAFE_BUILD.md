# 装备 tip 分色 — 正确改法（禁止靠还原当方案）

还原绿 bak 只是**进图止血**，不是 tip 功能的交付。tip 要做成「进图绿 + 悬停分色」。

## 为什么以前会炸

`_build_equip_tip_drawonly.bat` 等脚本虽然删了 EquipGrowth 字符串，但仍：

1. `link Release\*.obj` **盲链整目录**（含残留 `PacketDispatcher.obj` 等）
2. 顺手重编 equipaddon / ModRegistry / Client / dllmain / sidetoolbar …
3. 静态门禁「无禁串」≠ 进图安全  
→ 任意角色选角进图 `0xC0000409` @ `ucrtbase+0x9d132`（与 SENDBUSY/TOTEM2/ENTEROK 同家族）

## 正确流程（三步）

```
[A] 冻结进图绿基线 obj
    → 用进图已验证的 DLL 对应的 Release obj 快照
    → 或：干净重编 ROW3 绿线 → 你进图确认 → 再 robocopy 成 Release_enter_frozen\

[B] 只编 tip
    → 仅 cl tooltip.cpp（§10.3 DrawTipColorStrips）
    → link = frozen\*.obj 里换掉 tooltip.obj（显式列表，禁止 Release\*.obj）
    → 输出只进 golden，默认不上 Client_1

[C] 你进图 A/B
    → 任意角色选角进图 + 悬停装备看分色
    → 绿：再手工/脚本部署 live
    → 红：立刻回 6D612F01，修 B 再试（不要再全树重链）
```

## 门禁（写进脚本）

| 禁止 | 必须 |
|------|------|
| `link … Release\*.obj` | 只从 `Release_enter_frozen\` 取 obj |
| 重编 ModRegistry / PacketDispatcher / equipgrowth / dllmain（为 tip） | 本轮只允许新 `tooltip.obj` |
| 自动 deploy Client_1 | 默认 `LIVE_DEPLOY=SKIPPED` |
| TOTEM2 / SENDBUSY / ENTEROK / DRAWONLY 再上 live | stamp 新名 + SHA 记录 |
| 未进图验证当修好 | 文档写「pending enter A/B」 |

## 和服务端的关系

- 潜能/星力/高版本槽 **数值**走服务端（已 alias / 登录映射）— tip 只是**展示色**
- chaos 累计只落库，**不要**再往登录封包尾加 14 short（曾导致解码错位）

## 脚本

- `_freeze_enter_green_objs.ps1` — 把当前 `ezorsia\Release\*.obj` 快照到 `Release_enter_frozen\`（**仅在进图绿构建刚验证后执行**）
- `_build_equip_tip_tooltip_only.bat` — 在冻结目录上只换 tooltip → golden
- **独立 sidecar（推荐主路径，绿 ijl15 不动）：** `equip_tip_sidecar\` + `_build_equip_tip_sidecar.bat` → `golden\equip_tip_sidecar.dll`（§10.3 子集分色；见该目录 README）
- ~~`golden\tip_color_sidecar\`~~ **deprecated** → 指向 `equip_tip_sidecar\`

## 冻结目录健康检查（必做）

`Release_enter_frozen\` 必须来自 **进图已绿** 的同一次构建 obj，且与 live SHA `6D612F01` 同源。

| 危险信号 | 处理 |
|----------|------|
| frozen `tooltip.obj` 时间戳晚于某次 tip 进图红尝试 | **作废**；从绿 bak 对应构建重冻，或不要用当前 frozen 出 DLL |
| 脚本门禁 `STATIC_TOOLTIP_ONLY=OK` 但未进图 | 仍 **禁止** 拷到 Client_1 |
| 任何 `link … Release\*.obj` / MSBuild 全树 tip | 禁止上 live |

**2026-08-03 ~19:30：** `EQUIP_TIP_TOOLTIP_ONLY` 进图红后已回滚 live=`6D612F01`。当时 frozen 可能已脏 — **在重冻绿 obj 之前不要再 golden→live**。源码可先按 083 IDA 修正（字体槽偏移等），构建仅写 golden 且标注 pending A/B。
