#pragma once

#include <windows.h>

// KA 两类诊断日志（对应「KA插件两类闪退日志实现-prompt」；不吞异常、不改业务逻辑）：
//   beidou-crash.log      - 进程级崩溃 AV / illegal insn / stack overflow（VEH，每进程一次）
//   beidou-com-error.log  - 运行中 E_POINTER（_com_raise_error，最多16条，仍原样抛出）
//
// Hook 目标（IDA BeiDou_GMS_083，imagebase 0x400000；此前会话已核对）：
//   _com_raise_error              VA 0x00A605C3  RVA 0x6605C3  (__cdecl, noreturn)
//   IWzResMan::GetObjectA         VA 0x00403A93  RVA 0x003A93  (__thiscall / fastcall)
//   CWndMan::TranslateMessageImpl VA 0x009E7D77  RVA 0x5E7D77  (消息上下文)
//   IWzGr2D*                      VA 0x00BF14EC
//
// 注意：不要在本模块里改刷新率 / 显卡 / IWzGr2D 写字段；仅只读取 Gr2D 指针记日志。

void CrashDiag_Init(HMODULE hModule);
void CrashDiag_AttachHooks();

void CrashDiag_SetMsgContext(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
void CrashDiag_ClearMsgContext();
void CrashDiag_NoteWzPath(const wchar_t* path);

/** Largest free VA region in bytes (VirtualQuery walk). Safe anytime. */
unsigned long long CrashDiag_GetMaxFreeVa();

// 仅开发自测（config.ini 打开）；会进入原异常流程 / 抛出 E_POINTER。
void CrashDiag_DebugTriggerAccessViolation();
void CrashDiag_DebugTriggerEPointer();
