# 逆向证据：Gr2D_DX8!IWzGr2D::Initialize

## 1. IDA MCP（本机 http://127.0.0.1:13337/mcp，IDB `E:\download\v83.i64`，模块 `` `Angel.exe ``，imagebase 0x400000，同 v83 版本）

`tools/call: decompile("0x9f7a3b")` 输出（节选）：

```c
void __thiscall CWvsApp::InitializeGr2D(CWvsApp *this)
{
  ...
  PcCreateObject::IWzPackage(a1: v4, a2: &dword_BF14EC, a3: 0);              /*0x9f7a78*/
  v22.lVal = *(this + 1);                                                    /*0x9f7a98*/
  v22.vt = 0x4003;                                                           /*0x9f7aa6*/
  pvargSrc.vt = 3;                                                           /*0x9f7aac*/
  pvargSrc.lVal = dword_BF1AC8;                                              /*0x9f7ab2*/
  v6 = (*(*dword_BF14EC + 12))(                                              /*0x9f7b2a*/
         a1: dword_BF14EC, a2: 800, a3: 600,
         a4: *&pvarg.vt, a5: pvarg.decVal.Hi32, a6: pvarg.lVal, a7: pvarg.cyVal.Hi,
         a8: *&pvargDest.vt, a9: pvargDest.decVal.Hi32, a10: pvargDest.lVal, a11: pvargDest.cyVal.Hi,
         a12: *&v19.vt, a13: v19.decVal.Hi32, a14: v19.lVal, a15: v19.cyVal.Hi);
  if ( v6 < 0 ) _com_issue_errorex(...);                                     /*0x9f7b38*/
  ...
  v11 = (*(*dword_BF14EC + 80))(a1: dword_BF14EC, a2: 0xFF000000);           /*0x9f7ba7 ← 崩溃点*/
  if ( v11 < 0 ) _com_issue_errorex(...);                                    /*0x9f7bb5*/
}
```

对照 `ezorsia/compat/WzLib/IWzGr2D.h`（`IWzGr2D : IUnknown`，全部 `__stdcall`）：

| vtable 槽 | 偏移 | 方法 |
|---|---|---|
| 0..2 | 0x00/0x04/0x08 | IUnknown: QueryInterface / AddRef / Release |
| 3 | **0x0C** | `raw_Initialize(uWidth, uHeight, VARIANT vHwnd, VARIANT vBPP, VARIANT vRefreshRate)` |
| 20 | **0x50** | `put_backColor(unsigned int puColor)`（实参 0xFF000000） |

`lookup_funcs("0x009F7BA7")` → `?InitializeGr2D@CWvsApp@@IAEXXZ` @0x9f7a3b, size 0x25f（崩溃点确实在该函数内）。

## 2. capstone 反汇编 `E:\MXD\BeiDou-Client_S9\Gr2D_DX8.dll`（基址 0x50400000，raw==rva）

```
0x504030d0  ret 0x14                      ← 上一函数尾（说明 0x30D3 才是入口）
0x504030d3  mov eax, 0x5042ae00
0x504030d8  call 0x5042a5f0               ← __EH_prolog
0x504030dd  sub esp, 0xdc
0x504030e3  push ebx
0x504030e4  push esi
0x504030e5  push edi
0x504030e6  xor edi, edi
0x504030e8  cmp dword ptr [ebp + 0xc], edi     ← 参数 [ebp+C] 非零检查（uWidth/uHeight 区）
0x504030f4  cmp dword ptr [ebp + 0x10], edi
0x504030fd  test byte ptr [ebp + 0x15], 0x40
0x50403101  mov ebx, dword ptr [ebp + 8]       ← [ebp+8] = this（CWzGr2D*）
0x50403107  mov dword ptr [ebx + 0x98], eax
0x50403156  cmp dword ptr [ebx + 0x90], edi    ← +0x90 = m_pD3DDevice
0x5040317a  lea esi, [ebx + 0x20]              ← +0x20 = m_screenMode
0x50403180  mov ecx, ebx
0x50403187  call 0x50402e1b                    ← 子函数（枚举适配器模式）
...
0x50403533  push 0x5043631c                    ← "Failed in finding proper screen mode for Gr2D"
0x50403538  call 0x504050bc
0x50403546  mov eax, 0x80004005                 ← E_FAIL 返回
0x50403576  leave
0x50403577  ret 0x3c                            ← 参数区 0x3C 字节 = 15 DWORD
```

其它扫描结果：

- 导出表只有 `DllCanUnloadNow`(0x1066)、`DllGetClassObject`(0x1072) —— COM 类厂，接口方法不经导出。
- `0x2D154` 起为 vtable：`0x504013ca, 0x5040138c, 0x5040139d, 0x504030d3, 0x504035b6, ...`（第 4 槽即 Initialize）。
- 特征 `B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 81 EC DC 00 00 00` 在 dll 内唯一命中 `0x30D3`。
- 错误字符串 UTF-16 位于 RVA `0x3631c`，唯一引用点 `0x3533`（函数体内）。

## 3. 运行日志对照

修复前（`rs_ui.log`）：

```
[RS] early FindScreenMode hook OK @504030D3
[RS] early FSM enter this=001AF4DC mode=0429070C fs=800 600x16387 unused=0   ← 错位一槽
[RS] early FindScreenMode -> windowed soft-ok only 800x600 mode=0429070C     ← 800/600 写进 this+0（vptr）
```

修复后（`rs_ui.log`）：

```
[RS] early IWzGr2D::Initialize hook OK @504030D3
[RS] early IWzGr2D::Initialize enter this=042B9404 800x600 bpp=16 win=1
[RS] early Initialize bpp=16 unsupported — retry at 32bpp
[RS] early IWzGr2D::Initialize rc=0x00000000
```

`client_boot.log`：`STAGE CWvsApp::Init END` / `CWvsApp::Init returned 256`，无 CRASH。
