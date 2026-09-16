# ClickRaise Z-Order：置顶不再遮挡 tip / 弹窗

> 状态：代码已闭环；Client_S9 已同步（2026-09-16 00:40）  
> 适用客户端/协议版本：MapleStory v83（GMS）/ BeiDou-ijl15  
> 项目名称：BeiDou-ijl15  
> 作者/日期：BeiDou / 2026-09-16  
> 相关提交：（未提交，按用户要求）  
> 产物目录：docs/features/clickraise-z-order/

## 1. 背景与目标

- **问题**：点击置顶（ClickRaise）抬高装备栏等 `CWnd` 的 layer z 后，装备 tip、套装 tip、NPC/YesNo 等弹窗被盖住。
- **目标**：ClickRaise 仍解决「拖 A 却动到 B」；tip 与 modal 始终画在已置顶面板之上。
- **明确不做**：不改 OS `HWND_TOPMOST`；不改 gms-ui / 服务端 / WZ。

## 2. 影响范围

| 层级 | 路径/模块 | 变更类型 |
| --- | --- | --- |
| 插件 | `clickraise/UiLayerZ.h`、`clickraise.cpp` | 硬顶 + modal 过滤 |
| 插件 | `compat/rs/rs.cpp` | MakeLayer tip z；CreateDlg / UtilDlgEx / RsPlaceAbsDlg modal z |
| 插件 | `equipgrowth/equipgrowth.cpp` | companion tip z 对齐 tip band |
| 客户端 | `BeiDou-Client_S9/ijl15.dll` | PostBuild 覆盖 |

## 3. 最终实现逻辑

### 3.1 分层 band（`UiLayerZ.h`）

| Band | 常量 | Z |
| --- | --- | --- |
| Raise | `kRaiseMaxZ` | ≤ 500 |
| Plugin overlays | 保持现状 | DamageRank ~1000、equipaddon ~3000 |
| Modal | `kModalFloorZ` | ≥ 8000 |
| Tip | `kTipMinZ` | ≥ 10000 |

相对顺序：`raised panels < overlays < modal < tip`。

### 3.2 ClickRaise

1. `MaxLayerZAmongWindows` 只统计 `0 ≤ z < kModalFloorZ`。
2. `RaiseWindow`：`nextZ = min(maxZ+1, kRaiseMaxZ)`；去掉无效的 `9000` 软顶。
3. 被点窗已在 modal/tip 带：只 `SetFocus` + `OnActivate`，不改 z。
4. `DemoteOverlappingUnder` 不 demote `z ≥ kModalFloorZ`。

### 3.3 Tip

- `CUIToolTip::MakeLayer_hook`：原版 CreateLayer 后若 `z < kTipMinZ` 则抬到 `kTipMinZ`（仍保留屏幕边缘 RelMove）。
- 套装 tip 经 `SetToolTip_String2` → MakeLayer，自动受益。
- `equipgrowth` companion 的 `kGrowthLayerZBoost` 改为 `kTipMinZ`。

### 3.4 弹窗

- `rs_CreateDlg_hook`：所有 CreateDlg 的 `z` 提到至少 `kModalFloorZ`。
- `CUtilDlgEx::CreateUtilDlgEx_hook`：`CreateWnd` z 从 10 → `kModalFloorZ`。
- `RsPlaceAbsDlg`（右键菜单 / Popup / UIMenu HD 路径）：z 从 10 → `kModalFloorZ`。

## 4. 资源与同步

- 构建：`build_once.bat` → `out/Release/ijl15.dll`
- 同步：`E:\MXD\BeiDou-Client_S9\ijl15.dll`（与产物同 SHA256）
- SHA256：`4EEECE858ED7D831F4D04FF560D830EA3EF2A7F3BFA2D2F11A8C2BA183C39801`

## 5. 验证清单（需重启客户端后手工点）

1. 重叠装备/小地图多次点击置顶 → 拖的是最上窗  
2. 置顶后悬停装备 → tip / 套装 tip / 对比 tip 不被挡  
3. 置顶后开 NPC 对话、YesNo、右键菜单、系统选项 → 弹窗在最上  
4. HD 分辨率下重复 2–3（RS clamp 仍正常）

## 6. 风险与回滚

- 全局抬高 CreateDlg z 可能改变极少数依赖「低 z 被盖」的表现（少见）。
- 回滚：还原 ClickRaise / MakeLayer_hook / CreateDlg·UtilDlgEx·RsPlaceAbsDlg z 三处即可。

## 7. 证据

见 `evidence/ida-tooltip-z.md`。
