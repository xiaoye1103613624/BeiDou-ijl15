# AGENTS.md

## 唯一操作根（S8）

| 用途 | 路径 |
|------|------|
| 插件源码（本仓库） | `E:\pro\BeiDou-ijl15_S8` |
| 服务端 | `E:\pro\BeiDou-Server_S8` |
| 客户端 live | `E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_S8` |

旧 `BeiDou-ijl15` / `BeiDou-Client_1` / `BeiDou-Server_xy` 不再作为日常操作目标。

- 客户端 EXE / 本插件补丁：必须先 IDA（`user-ida-pro-mcp`），对照后再改。
- WZ / `.img`：仅 orange-wz / wzimg MCP，禁止 raw copy。
- 历史 SOP / avoid-list（只读归档）：`E:\pro\BeiDou-ijl15\docs\`

## 字符编码（强制）

v083 中文客户端 UI 使用 **GBK (CP936)**，不是 UTF-8。

- 插件 `.cpp/.h`：**禁止** UTF-8 中文源字面量直出游戏 UI。
- 固定文案：GBK `\xHH` 转义 + 注释（见 `setitem.cpp`、`combatpower.cpp`）。
- 服务端 UTF-8 入包：插件侧 `Utf8ToGbk` 后再渲染。
- 完整规范：`.cursor/rules/encoding-standards.mdc`；服务端封包见 BeiDou-Server `CLAUDE.md` 编码规范第 3 条。
