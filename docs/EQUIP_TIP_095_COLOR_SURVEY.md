# 095 是否有「装备属性分色展示」？

> 日期：2026-08-03（**live IDA MCP 复核**）  
> 结论先行：**官方 GMS095 没有我们 §10.3 那种「括号内按来源多色（白/橙/黄/紫…）」的 tip。**  
> 083 分色是私服自研方案；095 可借鉴的是潜能/星力/Growth tip **系统能力**与字体槽模型，不是色板。

## 0. 本次 MCP 会话

| 项 | 值 |
|----|-----|
| Cursor 默认 `user-ida-pro-mcp` | `:13338` → **083** `BeiDou.exe.i64`（勿当 095） |
| **095 MCP（本轮实际）** | **`:13337`** → `GMS_v95.0_U_DEVM.exe`（笔记旧写 `:13339`；以 `server_health` 为准） |
| IDB | `E:\资料\xiaoye\mxd学习\GMS_v95.0_U_DEVM.i64` |
| module / imagebase | `GMS_v95.0_U_DEVM.exe` / `0x400000` |
| Hex-Rays | ready |
| 083 live Client_1 | 保持绿 `6D612F01`；**本轮未部署 tip、未改 WZ** |

## 1. 文档证据（先前）

| 来源 | 内容 |
|------|------|
| `resource_doc/萧曳083改079冒险岛玩法.md` §10 | 分色为 **2026-08-01 私服评估**；锚点 ijl15 `EmitStatBreakdownLine` + 服务端 `computeBonus`；色板是**强制对照表**，非「从 095 抄来」 |
| `ADDON_095_IDA_BODY_PART_NOTES.md` | BodyPart / Equip Draw / CD 扩槽，**无 tip 分色** |
| `ADDON_083_ADAPT_FROM_095.md` | OccShadow/STATS，**无 PrintValue 多色** |

## 2. 095 关键 VA（live 反编译）

| VA | 符号 | 作用 |
|----|------|------|
| `0x891230` | `CUIToolTip::PrintValue` | 属性行格式化 → **固定** `AddInfoEx(21, 23, …)` |
| `0x88bac0` | `CUIToolTip::AddInfoEx` | 双上下文字体槽写入 `m_aLineInfo` |
| `0x88bda0` | `CUIToolTip::AddOptionInfo` | 潜能等单字体行 → `m_aOptionLineInfo` |
| `0x89e620` | `CUIToolTip::AddInfo` | 单上下文字体槽 |
| `0x881d40` | `CUIToolTip::GetFontByType` | `nType` → `m_pFontHL_*` / `m_pFontGen_*` / `m_pFontStan_*` |
| `0x8a0bd0` | `CUIToolTip::SetToolTip_Equip_Basic` | 基础属性 `PrintValue` + `AddInfoEx(0x15,0x16)` |
| `0x8a5670` | `CUIToolTip::SetToolTip_Equip` | 整装 tip（含标题区 `GetFontByType`） |
| `0x891c80` | `CUIToolTip::SetToolTip_ItemOption` | 潜能属性，大量 `PrintValue(3/4/5)` + `AddOptionInfo(0x15)` |
| `0x893f60` | `CUIToolTip::DrawToolTip_Equip` | 画装备 tip（图标 / Req / Growth 画布） |
| `0x89e8b0` | `CUIToolTip::DrawInfo` | 通用行绘制 |
| `0xba5aa4` / `0xba5ad4` | 字符串 | `ToolTip/Equip/GrowthDisabled` / `GrowthEnabled`（ctor `0x8839c0` 加载） |

### 2.1 `PrintValue` 行为（095）

- 参数 `nType` **0–5** = 格式模式（正负 StringPool `0x178C/0x178D`、` %d`、` %d%%`、拼接到属性名、自定义 format），对应 083 的 PT_INC / PT_VALUE / PT_PERCENT 一类，**不是颜色枚举**。
- case 0/1/2 最终：**`AddInfoEx(this, 21, 23, sProperty, sValue, …)`**  
  → `GetFontByType(21)=m_pFontStan_Prp`，`GetFontByType(23)=m_pFontStan_Num`  
  → **整行只有「属性名字体 + 数字字体」两色**，无括号内多段。
- case 3/4/5 → `AddOptionInfo(21, …)`（潜能旁路，仍单字体槽）。

### 2.2 `GetFontByType` 色板（有，但不是 §10.3）

095 字体槽比 083 更全（HL White/Gold/Orange/Gray/Blue/Violet/Green2/Excellent/Special；Gen White/Gray/Red/Orange/Purple/Green/Yellow/Blue；Stan_Prp/Dsc/Num；Skill_*）。  
用途是 **标题 / 品阶装饰 / 段落角色**，不是「同一属性括号里按砸卷/混沌/星力/潜能再拆色」。

`SetToolTip_Equip` 内 `GetFontByType` 立即数统计（本轮 py 扫描）：以 **14(Gen_Orange)、10(Gen_White)** 为主，偶发 19/22 —— 仍是区块着色。

`SetToolTip_ItemOption` 内 **未**发现 `cmp grade → GetFontByType` 的品阶→字体切换热点；潜能数值仍走 `PrintValue` → Stan 字体。

### 2.3 Growth / Hyper

- Growth：**Wz 结点 + `IsGrowthItem` + `DrawToolTip_Equip` 里 canvas/`draw_number_by_image`**，是成长装备 UI，不是属性来源分色。
- Hyper：`ShowItemHyperUpgradeEffect` / `SendHyperUpgradeItemUseRequest` 等为升级特效/发包，**不是 tip 内多色条**。

## 3. 083 对照（原版 tip 颜色模型）— **2026-08-03 IDA MCP 再核**

| 项 | 值 |
|----|-----|
| IDB | `E:\mxd_soft\2.客户端\083\BeiDou-Clinet_原版未动\BeiDou.exe.i64` |
| module | `BeiDou.exe` / imagebase `0x400000`（**不是 095**） |

| VA | IDA 名（本轮已 rename） | 调用约定（asm） | ijl15 hook |
|----|-------------------------|-----------------|------------|
| `0x008ECA0C` | `CUIToolTip__SetToolTip_Equip_Basic` | `__thiscall`；`retn 4`；`(this, pe*)` | `SetToolTip_Equip_Basic_hook` |
| `0x008E7836` | `CUIToolTip__PrintValue` | `__thiscall`；`retn 10h`；`(this, nType, nValue, ZXString, bShowAlways)` → **仅** Basic 内 17 处 call | `Hook_PrintValue_HyperBreakdown` |
| `0x008F39E1` | `CUIToolTip__AddInfoEx` | `__thiscall`；双字体槽写入 line info | 经 `tooltip.h` 直调，不 Detour |
| `0x008ED0D2` | `CUIToolTip__DrawToolTip_Equip` | `__thiscall`；`(this, a2/layout, pe*)`；`mov ebx,ecx` | `DrawToolTip_Equip_hook` → `DrawTipColorStrips` |
| `0x008F36A1` | `CUIToolTip__GetFontByType` | `case 14→+0x460` / `15→+0x464` / `16→+0x468` / `2→+0x428` | 勿再写错成 0x464/0x468 当 14/15 |
| `0x008E8252` | `CUIToolTip__SetToolTip_Equip` | 调 Basic `0x8E8E67` + Draw `0x8E9EA2` | 不 hook |
| `0x008E7975` | `CUIToolTip__SetToolTip_Equip_Simple` | 调 Basic `0x8E7B1C` + Draw `0x8E8035` | 不 hook |

083 `PrintValue` → `AddInfoEx(14, 0x10, …)` 两字体槽；**同样做不到** §10.3 括号多来源色（§10.1 已要求自绘 `DrawTextA`）。

字体槽编号 **095≠083**（095 Stan 落在 21/22/23），移植时勿硬搬常量。

**源码修正（本轮）：** `tooltip.cpp` 中 `kOffFontTipLabel/Purple` 原为 `0x464/0x468`（实为 type 15/16），已改为 IDA 的 `0x460/0x464`（type 14/15）。钩点 VA 与笔记一致，无需改地址。

## 4. 对 083 tip 分色的建议（进图安全）

1. **明确：095 也没有 §10.3 分色** —— 不要从 095「搬一套分色引擎」。  
2. **继续走私服方案**：只在已有 `DrawToolTip_Equip` hook（`0x008ED0D2`）里画 `DrawTipColorStrips` / `EmitStatBreakdownLine`；色板用 §10.3 对照表 + 服务端 `computeBonus`。  
3. **只钩 Draw 路径，不上脏重链**：进图红根因是 tip DLL **整树重链**，不是分色绘制本身；上线须冻结绿 `6D612F01` obj / 二进制级只换 `tooltip.obj`，禁止再 MSBuild 全量 tip 上 live Client_1。  
4. **095 可另借（与分色解耦）**：Growth 旁挂 tip UI 形态、`SetToolTip_ItemOption` 潜能分行、`AddOptionInfo` 旁路；若要品阶色，用 095 `GetFontByType` **角色槽思路**自建 083 映射，仍不要塞进括号多段。

## 5. 状态

- live：保持绿 `6D612F01`；tip **不上 Client_1**，直到 tooltip-only 进图 A/B 通过。  
- 连 095：先 `server_health` 确认 `GMS_v95.0_U_DEVM.exe`；双开时默认 Cursor MCP 仍可能是 083，需 HTTP 打 095 实际端口（本轮 `:13337`）。
- **2026-08-03 晚（本轮）：** 083 IDA 再核钩点 VA **全部一致**；修正 `kOffFontTipLabel/Purple`→`0x460/0x464`；**未构建、未部署**（frozen 疑脏 / 进图红史）。
