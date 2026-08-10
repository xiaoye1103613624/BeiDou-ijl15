#pragma once

#include <windows.h>

// KA-style dual diagnostic logs for BeiDou-ijl15:
//   beidou-crash.log      - process-fatal AV / illegal insn / stack overflow (VEH, once)
//   beidou-com-error.log  - E_POINTER via _com_raise_error (max 16, still throws)
//
// Hook targets (IDA BeiDou_GMS_083, imagebase 0x400000):
//   _com_raise_error          VA 0x00A605C3  RVA 0x6605C3  (__cdecl, noreturn)
//   IWzResMan::GetObjectA     VA 0x00403A93  RVA 0x003A93  (__thiscall / fastcall)
//   CWndMan::TranslateMessage VA 0x009E7D77  RVA 0x5E7D77  (msg context only)

void CrashDiag_Init(HMODULE hModule);
void CrashDiag_AttachHooks();

// WndProc / TranslateMessage outer context (InterlockedExchange; clear after original).
void CrashDiag_SetMsgContext(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
void CrashDiag_ClearMsgContext();

// Optional: record a WZ path from other NameSpace hooks (e.g. rs_ns_hook).
void CrashDiag_NoteWzPath(const wchar_t* path);
