# BeiDou-ijl15

将主分支的两次Release视为v1和v2。

由于v2版本在部分机器上会导致游戏进入到登录界面的时候崩溃，而v1没有这个问题，因此本分支继承主分支的v1版本，并不再和主分支同步，单独开发。

本分支主要服务于BeiDou的客户端。

## 使用方法

已测试的开发工具 VS 2019，SDK 10，工具集 VS2019（v142）

使用 VS 打开时选择 **Release | x86** 生成解决方案。

生成产物：`out\Release\ijl15.dll`

### 自动部署（推荐）

生成成功后会 **Post-Build 自动覆盖** 到：

`E:\mxd_soft\2.客户端\083\beidou_client_xiaoye\BeiDou-Client_1\ijl15.dll`

（同时复制 `ezorsia\config.ini`；旧 DLL 会备份到客户端目录下 `_ijl15_backup\`）

| 方式 | 命令 / 操作 |
|------|-------------|
| VS 生成 | 选 Release\|x86，生成（Ctrl+Shift+B）即可覆盖客户端 |
| 一键编译+部署 | `deploy_ijl15.bat` |
| 客户端占用 DLL | `deploy_ijl15.bat /kill`（会结束 BeiDou.exe） |
| 只复制不编译 | `deploy_ijl15.bat /copyonly` 或再加 `/kill` |
| MSBuild | `msbuild ezorsia.sln /p:Configuration=Release /p:Platform=x86` |
| MSBuild 并杀端 | 同上再加 `/p:DeployKillClient=true` |
| 保存后自动编译部署 | `powershell -ExecutionPolicy Bypass -File .\watch_and_deploy.ps1`（可选 `-KillClient`） |

说明：

- 若客户端正在运行导致文件锁定，默认 **部署失败并提示**；需要时用 `/kill` 或 `DeployKillClient=true`。
- `deploy_ijl15.bat` / `watch_and_deploy.ps1` 编译时会带 `DeploySkipPostBuild=true`，避免 Post-Build 与脚本重复复制。
- 首次使用前仍建议把原版客户端 `ijl15.dll` 留一份备份（例如重命名为 `2ijl15.dll`）。

详细配置见 `ezorsia\config.ini`。

## 推荐服务端

北斗 https://github.com/SleepNap/BeiDou

## 更新记录

相比主分支 v1

- 支持中文输入/角色中文名(修复卡门问题)
- 修复滚轮乱飞的问题
- 修复ToolTip超出游戏窗口的问题
- 支持长键盘快捷键
- 交易中心居中
- 增加中文汉化
- 增加魔攻/魔防/命中/回避/跳跃 上限突破
- 增加BossHp百分比显示（在Boss血条的头像下方）
- 免密模式（须服务端支持，客户端仅解除密码限制）
- 调整聊天框文字位置

针对中文环境的一些调整：

1. 装备tooltip字体大小
2. 道具有效期字体大小
3. 修复有效期日期顺序
4. 聊天栏的选项汉化了远征队
5. 在不修改wz的情况下解决了Eqp，Etc汉化后游戏崩溃的问题，Use汉化后吃药没声音的问题
