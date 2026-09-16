# IDA：CUIToolTip::MakeLayer 默认 z

- 函数：`CUIToolTip::MakeLayer` @ `0x8F3141`
- 反编译要点：`IWzGr2D::CreateLayer` 的 z 参数为
  - 路径 A：`a6 != 0 ? 210 : 10`
  - 路径 B：`a6 != 0 ? 0xD2 : 0`（即 210 / 0）
- 结论：原版 tip 层 z ∈ {0, 10, 210}，远低于 ClickRaise 抬升后的面板 z（可到数千）。
- 注释里旧的「9000 以下避开 tip」与原版 tip 带不符；本修复以 `UiLayerZ::kTipMinZ = 10000` 显式抬 tip。

相关地址：

| 符号 | 地址 |
| --- | --- |
| `CUIToolTip::MakeLayer` | `0x8F3141` |
| `CDialog::CreateDlg` | `0x4EDA94` |
| Wnd list `ZList<CWnd*>` | `0x00BF1648` |
