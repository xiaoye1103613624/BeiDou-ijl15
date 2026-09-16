# 窗口商城 · 坐骑一级 Tab

> 状态：已构建并同步 Client_S9  
> 适用：MapleStory v83 / BeiDou-ijl15 + BeiDou-Server_s9 窗口商城

## 原因

客户端 `cashshopwnd.cpp` 用静态 `kTabs`/`kCats` 画一级/二级 Tab，**不解析**服务端 `RESP_TAXONOMY=6`。仅 DB 有 tab 11 时游戏内不会出现「坐骑」。

## 改动

- `kTabs` 增加 `{11, 坐骑}`（GBK `\xD7\xF8\xC6\xEF`）
- `kCats` 增加 `11:1 坐骑` / `11:2 鞍具` / `11:3 坐骑道具`（不含空标注 11:0）
- 一级 Tab 改为 8 个：`kTabW=88` / `kTabPitch=90`

## 构建与同步

- 命令：`E:\project\BeiDou-ijl15\build_once.bat`
- 产物：`out\Release\ijl15.dll`
- PostBuild：`E:\MXD\BeiDou-Client_S9\ijl15.dll`

服务端灌货见 BeiDou-Server_s9 `docs/features/cashshop-mount/`。
