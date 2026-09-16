# IDA 证据摘要（show-item-tip-id）

目标二进制：Angel.exe / BeiDou v83（MCP，2026-09-15）

| 查询 | 地址 | 符号（摘要） |
| --- | --- | --- |
| ShowItemToolTip | `0x008F5B20` | `CUIToolTip::ShowItemToolTip` |
| SetToolTip_Equip_Basic | `0x008ECA0C` | `CUIToolTip::SetToolTip_Equip_Basic` |
| SetToolTip_Equip | `0x008E8252` | `CUIToolTip::SetToolTip_Equip`（Basic → 脚注预留 → MakeLayer） |
| MakeLayer | `0x008F3141` | `CUIToolTip::MakeLayer` |
| AddInfoEx | `0x008F39E1` | `CUIToolTip::AddInfoEx` |

决策要点：

1. ShowItemToolTip 内按 itemId 大类调用 SetToolTip_Equip / Bundle / Pet，**本函数不直接 MakeLayer**。
2. AddInfoEx 必须在 MakeLayer 之前；装备路径须在 **footer 高度预留之前**（即 Basic 末尾），否则与橙色脚注重叠。
3. `SetToolTip_Equip` 顺序（IDA 2026-09-15 复核）：`SetToolTip_Equip_Basic` → `GetItemDesc` / `sub_8F4535` 量测 → `m_nHeight += measured`（及条件 `+=30`）→ `MakeLayer` → 橙色 `DrawTextA`（`Y ≈ height - measured - …`）。
4. itemId 读自 `a4 + 0xC`（TSecType），与仓库 `DecodeItemIdAt` 一致；`AddInfoEx` 以 `m_nHeight += fontH+4` 抬高。
5. 漏显：`CUIEquip::OnMouseMove`（`0x7FE9BB`）/ `CUIPetEquip::OnMouseMove`（`0x800F7B`）xref 直调 `SetToolTip_Equip`，绕过 ShowItemToolTip。
6. `SetToolTip_Pet` 仅由 ShowItemToolTip 调用；早 MakeLayer、无 PrintLines → 画布垫高路径。
6. Footer：`sub_8F4535` 量测后 `add [CUIToolTip+0x8], eax`（`m_nHeight`）；绘制时 Y = `m_nHeight - measured`。MakeLayer 之后再 `AddInfoEx` 会把行挤进该带。
