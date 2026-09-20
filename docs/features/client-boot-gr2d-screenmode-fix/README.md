# 客户端启动崩溃修复：IWzGr2D::Initialize 早期 hook

## 1. 问题现象

`E:\MXD\BeiDou-Client_S9` 双击后秒退、无任何窗口。日志停在 `CWvsApp::Init BEGIN → InitializeGr2D BEGIN`：

```
[23:17:06.619] *** CRASH code=0xC0000005 stage=InitializeGr2D BEGIN at BeiDou.exe+0x5F7BA7 abs=0x009F7BA7 read target=0x00000370
[23:17:06.620]     Eip=0x009F7BA7 Eax=0x00000320 Ecx=0x001AF4AC
[23:17:06.626] *** CWvsApp::Init SEH code=0xC0000005 lastStage=InitializeGr2D BEGIN
[23:17:06.626] *** Init failed → ExitProcess(1) to avoid null-Input Run AV
```

## 2. 根因（IDA MCP + capstone + 日志三重印证）

### 2.1 hook 地址是对的，签名是错的

`rs_ui.log` 显示插件把早期 hook 装在 `Gr2D_DX8.dll + 0x30D3`：

```
[RS] early FindScreenMode hook OK @504030D3
[RS] early FSM enter this=001AF4DC mode=0429070C fs=800 600x16387 unused=0
[RS] early FindScreenMode -> windowed soft-ok only 800x600 mode=0429070C
```

- 0x0429070C 就是 crash log 里的 `IWzGr2D*` —— 也就是 `mode` 参数实际拿到的是 `this`。
- `fs=800 600x16387` 对应真实的 `uWidth=800`、`uHeight=600`、`VARIANT.vt=0x4003(=16387)`，说明参数整体错位一个槽。

IDA MCP 反编译 `CWvsApp::InitializeGr2D`（0x9F7A3B，`E:\download\v83.i64`，同版本 v83）：

```c
PcCreateObject::IWzPackage(a1: v4, a2: &dword_BF14EC, a3: 0);
v6 = (*(*dword_BF14EC + 12))(a1: dword_BF14EC, a2: 800, a3: 600,
                             a4..a7: VARIANT, a8..a11: VARIANT, a12..a15: VARIANT);  /*0x9f7b2a*/
v11 = (*(*dword_BF14EC + 80))(a1: dword_BF14EC, a2: 0xFF000000);                      /*0x9f7ba7 ← 崩溃点*/
```

对照本仓 `compat/WzLib/IWzGr2D.h`：

- `vtable+0x0C` = `raw_Initialize(uWidth, uHeight, vHwnd, vBPP, vRefreshRate)`；
- `vtable+0x50` = `put_backColor`（0xFF000000，黑色背景）—— 正是崩溃点。

COM 接口方法在 x86 上是 **STDMETHODCALLTYPE = `__stdcall`**：`this` 是**第一个栈参数**（不是 ECX），参数合计 15 个 DWORD = 0x3C 字节，被调者清栈。

capstone 对 `Gr2D_DX8.dll` 的校验（`evidence/` 有完整输出）：

- `0x504030d0 ret 0x14`（上一函数尾），`0x504030d3 mov eax,<scope>; call __EH_prolog; sub esp,0xDC` → **0x30D3 才是入口**；
- 结尾 `leave; ret 0x3c` → 参数区 0x3C 字节，与 IDA 的 15 参数吻合；
- 错误串 `Failed in finding proper screen mode for Gr2D` 的引用在 RVA `0x3533`，落在 0x30D3~0x3577 函数体内；
- `0x2D154` 起为 vtable，第 4 槽 = `0x504030D3`。

**结论**：插件用 `__fastcall(pThis, edx, SCREENMODE*, ...)` 去接一个 `__stdcall` 的 COM 方法，参数错位一槽，`mode` 实为 `this`，于是把 `800/600` 写进 `this+0/+4`（虚表指针）；紧接着 `put_backColor` 执行 `call [eax+0x50]` 时 `eax=800` → 读 `0x370` → AV → 插件 Init SEH → `ExitProcess(1)`。

### 2.2 修好签名后暴露的真实故障：BPP=16

按正确签名透传后，原生 `Initialize` 仍返回 `0x80004005`（E_FAIL，即 "Failed in finding proper screen mode" 分支）。临时调试日志显示实参：

```
hw(vt=16387, lVal=...) bpp(vt=3, lVal=0x10) rr(vt=3, lVal=0x0)
```

BPP 来自 `dword_BF1AC8`（见 IDA 反编译 `pvargSrc.lVal = dword_BF1AC8`），本机为 **16**。现代桌面/驱动通常枚举不到 `800x600x16`，Gr2D 找不到匹配模式即失败。

## 3. 修复内容（`ezorsia/compat/rs/rs.cpp`）

1. **签名改为真实 `__stdcall` 15 槽**
   `RsGr2D::Initialize_t` = `(void* This, uWidth, uHeight, VARIANT vHwnd, VARIANT vBPP, VARIANT vRefreshRate)`，
   实现上用 15 个 `unsigned int` 槽表达（该文件包含链里没有 `VARIANT` 定义，避免引入 OAIdl 依赖），
   `__stdcall` 由编译器生成 `ret 0x3C`，栈平衡。
2. **hook 默认纯透传，绝不写内存**
   新增 `rs_init_args_plausible()` 闸门：`this` 为空/不可读、宽高不在 `[640,2560]/[480,1440]` 时直接透传并返回，
   把"参数错位写坏对象"从崩溃降级为无害透传。**任何情况下都不写 `this+0`（vptr）**。
3. **BPP 兜底重试**
   原生返回失败且 `vBPP.vt == VT_I4(3)`、`lVal != 32` 时，以 `bpp=32` 重试一次；仍失败则原样返回。
4. **`RsGr2D::ScreenResolution()` 不再调用该 COM 方法**
   旧实现以 6 参数（24 字节）调用一个需要 0x3C 字节参数的方法，必然栈失衡；
   改为直接改写 `m_screenMode`（`this+0x20`）的 present 参数并置 `D3DERR_DEVICENOTRESET`，
   与原先 "FindScreenMode miss" 的降级路径等价，且不会碰到 `+0` 的 vptr。

## 4. 顺带解除的构建阻塞（编码）

`Client.cpp`、`rs.cpp` 是 **UTF-8 无 BOM**，MSVC 在无 BOM 时按代码页 936 解析，中文行尾的 UTF-8 尾字节会吞掉换行，
导致行号错位与 `C2601/C1075` 等假语法错误（Client.cpp 报 `C1075: "{" 未找到匹配令牌`，实际括号是平衡的）。
给这两个文件补 UTF-8 BOM 后编译恢复正常。源码中 284 个文件为 UTF-8、14 个为 GBK，
因此**不能**在工程级统一加 `/utf-8`，只对本次涉及的 UTF-8 文件加 BOM。

## 5. 验证结果

- 构建：`build_once.bat` → `[OK] built + PostBuild deployed to BeiDou-Client_S9`。
- 启动：`[RS] early IWzGr2D::Initialize hook OK @504030D3` → `bpp=16 unsupported — retry at 32bpp` → `rc=0x00000000`。
- boot 日志：`STAGE CWvsApp::Init END`、`CWvsApp::Init returned 256`，无 `CRASH`、无 `ExitProcess(1)`。
- 进程存活，主窗口标题「北斗」（登录界面已显示）。

## 6. 分辨率链路（已修复）

引擎固定以 800x600 调 `IWzGr2D::Initialize`，而 dllmain 已把 `rs_width/rs_height` 置为登录尺寸（1280x720）。
`rs_switchToSize()` 原先用**期望值**判断"是否已匹配"，于是永远跳过切换，界面停在 800x600。
现已改为以 **Gr2D 真实后备缓冲尺寸**（`m_screenMode.nWidth/nHeight`）为准：

- `rs_switchToSize()`：先取 `get_gr()`，`grSame = (实际宽高 == 目标)`；`sameSize = grSame && 期望值相同`；只有 `!grSame` 才调 `ScreenResolution()`。
- `rs_set_stage_hook()`：登录/选角分支的恢复条件加入 `grMismatch`（实际尺寸 ≠ 登录尺寸），否则"期望值已相等"的假象会漏掉恢复；并把 `std::cout` 改为 `RsLogFlush`（GUI 进程无控制台，该日志此前不可见）。

验证：

```
[RS] ScreenResolution(1280x720)=0x00000000 prev=800x600 now=1280x720 pp=1280x720
[RS] Switched to 1280x720 adj=60 grChanged=1 loginUi=0
[RS] WorldMap center LT 307,98 screen=1280x720
```

## 7. 选角进图 CANVAS.DLL 空指针崩溃（已闭环 2026-09-17）

现象：`set_stage END` @ `CANVAS.DLL+0xED28`（ECX=0）。崩前 path-spy 常刷 GrowthEnabled/Disabled，但 **真因不是缺 Growth 节点**。

校正（见服务端文档 `docs/features/enter-game-growth-tip-crash/`）：

- Data/EN `UIWindow` 解密后 **已有** Growth 子树与 PNG；`getobj_fail` 为空。
- 崩溃栈在 `sub_8DDB30` @ `0x8DE112`：StatusBar/KeyConfig quickSlot 对 **空源画布** `IWzCanvas::Copy`。
- EN `StatusBar.img` 已补 `quickSlot32/08/26`；插件 `EnterGameCanvasNullGuards` 在 `0x8DE0EC` 对空源跳过 Copy。

勿再启用 `kEnableGrowthPathRewrite` / SoftFail（会 bounce 登录↔进图）。

## 8. 窗口模式与命中坐标（已修复/已验证）

### 8.1 启动即全屏

旧的窗口化 patch 被移除后没有替代，启动阶段只剩引擎默认（全屏）：

```cpp
// 旧代码（Client::UpdateResolution）
if (WindowedMode) {
    unsigned char forced_window[] = { 0xb8, 0x00, 0x00, 0x00, 0x00 }; // "force window mode"
    Memory::WriteByteArray(0x009F7A9B, forced_window);
}
```

但 `0x009F7A9B` 的 `mov eax,[dword_BF1AC8]` 是 **BPP** 来源（IDA：Initialize 的 BPP 实参来自 `dword_BF1AC8`；hook 实测 `bpp(vt=3,lVal=0x10)`=16），
置 0 会让 `IWzGr2D::Initialize` 失败（`0x8876086C D3DERR_INVALIDCALL`）——所以不能恢复该 patch。

修复：在 `Initialize` hook **成功返回后**用真实 `this` 调 `RsGr2D::ForceWindowedFlag()`（`put_fullScreen(0)` + `m_screenMode.nWindowed/bFullScreen`），不动 BPP。
验证：窗口 `806x629`、样式 `0x14ca0000`（有标题栏/边框，非 `WS_POPUP`，屏幕 2560x1600），日志 `early Initialize — windowed flag applied (BPP untouched)`。

### 8.1.1 窗口模式启动桌面闪一下（已修复 2026-09-17）

现象：`WindowedMode=true` 时打开客户端，系统桌面分辨率先变一下再变回。

根因：`rs_EarlyInitialize_hook` 在登录尺寸（1280x720）枚举失败后仍试 **1920x1080** 等全屏候选 → `OK at 1920` → 再 `ScreenResolution` realign 到 1280；中间一次 `ChangeDisplaySettings` 可见。

修复（`rs.cpp` `rs_EarlyInitialize_hook`）：

1. `WindowedMode` 下在首次 `CallNativeInit` 前 `ApplyPresentSize(login)`（预写 `nWindowed=1`）。
2. 窗口候选只保留：登录尺寸 → 引擎原始 → 1280/1024/800 → **桌面原生兜底**；禁止 1920/1366 等非桌面中间档。
3. 成功后 `ForceWindowedFlag` + 若尺寸≠登录则 `ScreenResolution` 归位（只 patch present，不改系统桌面模式）。

验证日志（本机桌面可能超过布局上限，如 3200x1440）：

```
[RS] early Initialize desk=3200x1440 login=1280x720 max=2560x1440
[RS] early Initialize allow desk over-max 3200x1440 …
[RS] early Initialize OK at 3200x1440
[RS] early Initialize realign 3200x1440 -> 1280x720 (login, windowed)
```

注意：桌面超过 `RS_SCREEN_*_MAX` 时必须仍允许 Initialize（否则候选被静默跳过 → `Failed in finding proper screen mode` 直接 ExitProcess）。1920/1366 保留为最后兜底。

不再出现「小档全失败且无 desk/1920 尝试」导致无法启动。

### 8.2 点不到 NPC / 丢道具（命中死区）

`Client::UpdateResolution()` 里这两行注释写着 "moves all interactable UI elements"：

```cpp
Memory::WriteInt(dwCursorVectorVPos + 2, -(m_nGameHeight / 2)); // 0x0059A15D
Memory::WriteInt(dwCursorVectorHPos + 2, -(m_nGameWidth  / 2)); // 0x0059A169
```

IDA 校准 `CInputSystem::SetCursorVectorPos @0x59A0CB`：

```c
v8 = (*(*v7 + 144))(a1: v7, a2: a2 - 400, a3: a3 - 300, …);   /*0x59a172*/
```

即鼠标屏幕坐标经 `-W/2, -H/2` 转成视图向量；`SetCursorPos @0x59A887` / `UpdateMouse @0x59AAFE` 还各有一组钳制上限。
这些值只在 `UpdateResolution()` 按登录尺寸写一次，**rs 的 Origin 切换后从不更新** → HD 下 800x600 之外出现点击/丢弃死区
（`rs.cpp` 既有注释亦记载 "unclickable/drop-dead zones past the old 800x600 island"）。

修复：新增 `RsApplyInputHitTestCoords()`，在 `rs_callUpdateResolution()` 与 `rs_afterSuccessfulSwitch()` 后按运行时真实宽高重写这 6 处 immediate。
**状态：代码已落地，尚未进图实测**（被 8.3 的崩溃阻塞）。

### 8.3 进图 CANVAS 闪退（已闭环，见 §7）

先前「UTF-16 搜不到 = 缺节点」判断有误（img 字符串加密）。2026-09-17 用 orange-wz + IDA 校正后，
按 §7 完成 EN StatusBar 补齐与 `0x8DE0EC` nullguard；Growth path-rewrite 仍保持关闭。

## 9. 回滚

- 旧 dll 备份：`E:\MXD\BeiDou-Client_S9\ijl15.dll.bak_20260917`。
- 源码回退：`git checkout -- ezorsia/compat/rs/rs.cpp ezorsia/compat/rs/rs.h ezorsia/bootlog/LoadTrace.cpp`（Client.cpp 的 BOM 需单独处理）。
