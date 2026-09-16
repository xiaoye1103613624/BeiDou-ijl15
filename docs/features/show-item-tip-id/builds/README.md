# 构建索引（show-item-tip-id）

## 2026-09-15 23:12（重叠/漏显续作：硬化 MakeLayer）

- 命令：`E:\project\BeiDou-ijl15\build_once.bat`
- 编译/链接：**成功** → `E:\project\BeiDou-ijl15\out\Release\ijl15.dll`
  - 大小：1877504 bytes
  - SHA256：`31124A5F92D228ECD8885E63766B269A83482D31B5F51A214B5B8A8F2D77B418`
  - 时间：2026-09-15 23:12:37
- 代码：去掉 `Hook_MakeLayer` 在 `TipInfoLineCount>0` 时的 `AddInfoEx` 回退（会落入 footer 预留带）。
- PostBuild 同步：`E:\MXD\BeiDou-Client_S9\ijl15.dll` **失败（共享违规）**
  - 占用进程：`BeiDou.exe` PID 29956（`E:\MXD\BeiDou-Client_S9\BeiDou.exe`）
  - 客户端现有 dll：1877504 bytes，2026-09-15 22:57:40
  - SHA256：`1CF4A91E10B0B73689EC7A41C66E6F78BA3644B2E8A04DFC7D2828F8FACE9AAF`（含 Basic 追加，但仍含危险 MakeLayer 回退）
  - 按约定**未强杀**；关闭客户端后执行：
    ```
    xcopy /Y "E:\project\BeiDou-ijl15\out\Release\ijl15.dll" "E:\MXD\BeiDou-Client_S9\"
    ```
- `preserve_execave_stub.ps1`：SKIP（stub@AB30C=CCCC，layout drift，与本次无关）

## 2026-09-15 22:51 / 22:57（重叠/漏显修复初版）

- 命令：`E:\project\BeiDou-ijl15\build_once.bat`
- 引入 `Hook_SetToolTipEquipBasic`（footer 预留前 `AddInfoEx`）+ Pet/Bundle 画布路径。
- 22:51 PostBuild 曾失败；22:57 左右客户端曾同步到当时产物（见上「客户端现有 dll」哈希）。

## 2026-09-15 16:32（初版功能）

- 产物：1876992 bytes；SHA256 `02ed8a033509681b3e9cface845fa870c5c4cc24000ff1c4884d86205b8cdc19`
- 初版在 MakeLayer 对装备 `AddInfoEx` → 与橙色脚注重叠。
