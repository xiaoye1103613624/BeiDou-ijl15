# 道具 Tip 底部显示 Item ID

> 状态：代码已闭环；Client_S9 同步待关客户端后覆盖  
> 适用客户端/协议版本：MapleStory v83（GMS）/ BeiDou-ijl15  
> 项目名称：BeiDou-ijl15  
> 作者/日期：BeiDou / 2026-09-15  
> 相关提交：（未提交，按用户要求）  
> 产物目录：docs/features/show-item-tip-id/

## 1. 背景与目标

- **问题**：悬停道具 tip 时不易确认真实 itemId（运营排查、对照 WZ、脚本调试）。
- **目标**：凡走装备/道具 tip 的路径，底部追加一行 ASCII：`ID: <itemId>`；可用 `config.ini` 关闭；缺省开启。
- **明确不做**：不改服务端 / WZ / gms-ui；不改套装/成长/潜能旁挂文案；不做游戏内开关 UI。

## 2. 影响范围

| 层级 | 路径/模块 | 变更类型 |
| --- | --- | --- |
| 服务端 | — | 无 |
| 脚本 | — | 无 |
| 管理端/前端 | — | 无 |
| WZ/XML | — | 无 |
| 客户端 IMG | — | 无 |
| 插件 | `setitem.cpp`、`equipcompare.cpp`、`Client.*`、`dllmain.cpp`、`config.ini` | 逻辑 + 配置 |
| DB/配置 | `optional.showItemTipId` | 默认 true |

## 3. 最终实现逻辑

### 3.1 主流程（2026-09-15 修复后）

```
装备 / 戒指（含 CUIEquip、CUIPetEquip 直调 SetToolTip_Equip）：
  SetToolTip_Equip
    → SetToolTip_Equip_Basic（FA / EquipCompare 链）
    → Hook_SetToolTipEquipBasic：AddInfoEx「ID: n」   ← 在 footer 预留之前
    → SetToolTip_Equip 量测橙色脚注文案并抬高 m_nHeight
    → MakeLayer / PrintLines / 画橙色脚注

消耗 / 其它 / 宠物（ShowItemToolTip → Pet/Bundle）：
  Hook_ShowItemToolTip：pending(tip, itemId)
    → SetToolTip_Pet / Bundle（无 PrintLines，早 MakeLayer）
    → Hook_MakeLayer：m_nHeight += 17，bake 后画布绘「ID: n」
```

**禁止**在 `Original_ShowItemToolTip` 返回后再 `AddInfoEx`（画布已烘焙，无效）。  
**禁止**在装备路径的 MakeLayer 时刻再 `AddInfoEx`（会落入 footer 预留区 → 与橙色文案重叠或间距失控）。  
`Hook_MakeLayer` 在 `TipInfoLineCount>0` 时只丢弃 pending，**不再**做晚追加回退。

### 3.2 根因（重叠 / 漏显）

| 现象 | 根因 |
| --- | --- |
| ID 与橙色「宿命剪刀 / 制炼书…」重叠或间距忽大忽小 | 初版在 `MakeLayer` 前 `AddInfoEx`。此时 `SetToolTip_Equip` **已**按脚注文案抬高 `m_nHeight`，再追加行会挤进预留带；脚注仍按旧预留 Y 绘制 → 重叠或空白不均。 |
| 普通装备槽、宠物装备槽无 ID | `CUIEquip` / `CUIPetEquip::OnMouseMove` **直调** `SetToolTip_Equip`，绕过 `ShowItemToolTip`，pending 从未设置。扩展槽走 `ShowItemToolTip` 故原先可见。 |
| 背包宠物 tip | `SetToolTip_Pet` 无 AddInfoEx/PrintLines，早 MakeLayer；仅靠 pending + 画布垫高绘制。 |

### 3.3 数据 / 协议 / 节点路径与语义

| 符号 | 地址（IDA / Angel·BeiDou v83） | 语义 |
| --- | --- | --- |
| `CUIToolTip::ShowItemToolTip` | `0x008F5B20` | 读 `a4+0xC` TSecType itemId，分发 Equip/Bundle/Pet |
| `CUIToolTip::SetToolTip_Equip_Basic` | `0x008ECA0C` | 装备属性行；**ID 追加点（footer 之前）** |
| `CUIToolTip::SetToolTip_Equip` | `0x008E8252` | Basic 后量测脚注并 MakeLayer |
| `CUIToolTip::MakeLayer` | `0x008F3141` | Pet/Bundle 早烘焙；装备路径已消费 pending |
| `CUIToolTip::AddInfoEx` | `0x008F39E1` | 追加一行并抬高 `m_nHeight`（fontH+4≈17） |

- **任意道具 ID**：`ReadAnyShowItemToolTipItemId` / `SafeGetItemId`（`id > 0`）。
- **仅装备 ID**（套装/成长）：保留 `ReadShowItemToolTipItemId`（`IsEquipItemId`），互不混用。
- **文案**：`sLine.Format("ID: %d", itemId)` 或 `sprintf_s`；装备走 `AddInfoEx(14, 15, …, 1, 1001)`。
- **编码**：纯 ASCII，无 GBK 风险。

### 3.4 模块与扩展点

| 点 | 说明 |
| --- | --- |
| Equip Basic hook | 覆盖背包装备、普通装备槽、宠物装备槽、对比 tip（Detours 最外层，在 FA/Compare 之后追加） |
| pending + MakeLayer | 仅 Pet/Bundle（`TipInfoLineCount==0`）垫高画布后绘制；装备已由 Basic 消费 pending；**禁止** lineCount>0 时晚 `AddInfoEx` |
| 旁挂 | String2 套装/成长等不走 Basic/pending → 无 ID 行 |
| 开关 | `Client::showItemTipId` ← `optional.showItemTipId`，缺省 `true` |

## 4. 资源与同步

- 无 WZ / 道具资源改动。
- 插件产物：`out/Release/ijl15.dll` → PostBuild `E:\MXD\BeiDou-Client_S9\`。
- PostBuild **不覆盖**已有客户端 `config.ini`；缺键仍靠代码 default=true。

## 5. 编码与 i18n

- Tip 行：ASCII `ID: n`。
- 日志：`[SetItem] EnsureUiHooks OK … showItemTipId=on|off`（上线级一次）。
- 无临时 DBG 日志。

## 6. 产物清单

| 类型 | 路径 | 说明 |
| --- | --- | --- |
| 文档 | README.md | 本文件 |
| 证据 | evidence/ida-addresses.md | IDA 地址与决策摘要 |
| 笔记 | notes/impl-notes.md | 实现与修复笔记 |
| 构建索引 | builds/README.md | dll 路径说明 |

## 7. 验证记录

- IDA：确认 `0x8F5B20` / `0x8ECA0C` / `0x8E8252` / `0x8F3141` / `0x8F39E1`；footer 量测 `sub_8F4535`→`m_nHeight+=` @`0x8E975B`；绘制 Y=`m_nHeight-var_2C`；`CUIEquip`/`CUIPetEquip::OnMouseMove` → `SetToolTip_Equip`。
- 构建：见 `builds/README.md`（23:12 编译成功；同步因 `BeiDou.exe` 占用未覆盖）。
- 客户端自测清单（须先同步最新 `ijl15.dll` 并重启客户端）：
  - [ ] 背包：装备 tip — 金锤子行与 `ID:` 与橙色脚注间距均匀、不重叠
  - [ ] 背包：消耗 / 其它 / 现金 / 宠物 → 底部 `ID:` 正确
  - [ ] **普通装备槽**（非扩展）悬停 → 有 `ID:`
  - [ ] **宠物装备槽**（CUIPetEquip）悬停 → 有 `ID:`
  - [ ] 扩展装备槽 / 商店 / 仓库 / storagebag / equipaddon / 商城预览
  - [ ] 套装：主 tip 有 ID，旁挂无 ID；成长旁挂无 ID
  - [ ] 对比：主 tip + 对比 tip 各显示对应 ID
  - [ ] `showItemTipId=false` 后全无；删键仍为开

## 8. 预留、风险与回滚

| 风险 | 缓解 |
| --- | --- |
| MakeLayer 后追加无效 | 装备走 Basic；Pet/Bundle 走 bake 前抬高 + bake 后画布 |
| 对比 pending 串扰 | tip 指针匹配；Basic 消费 pending；嵌套结束清 pending |
| FA tip 钩子链 | SetItem Basic hook 在 EquipCompare/FA 之后注册，Detours 最外 |
| 客户端 dll 占用 | 同步失败报告，不强杀 |

**回滚**：`showItemTipId=false`，或还原 `ijl15.dll`。无需 DB/WZ/服务端回滚。

## 9. 对外交流摘要

道具 tip 追加 `ID: itemId`（ASCII）。装备在 `SetToolTip_Equip_Basic` 末尾追加（避开橙色脚注预留）；Pet/Bundle 早 MakeLayer 时垫高画布绘制。普通装备槽与宠物装备槽因直调 Equip 路径，同样由 Basic hook 覆盖。配置 `optional.showItemTipId` 默认开。
